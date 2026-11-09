// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "ipc/linux_deadline.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base/system_util.h"
#include "base/version.h"
#include "client/client.h"
#include "ipc/ipc_path_manager.h"
#include "protocol/commands.pb.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {
using namespace std::chrono_literals;
using Time = LinuxDeadline::Time;
using Type = commands::Input::CommandType;
using client::Client;
using client::LinuxFcitxOptions;

std::string Self() {
  char path[4096];
  const auto size = readlink("/proc/self/exe", path, sizeof(path));
  return size > 0 ? std::string(path, size) : std::string();
}
double Elapsed(Time start) {
  return std::chrono::duration<double, std::milli>(LinuxDeadline::Now() - start).count();
}
void Metric(const std::string& name, double elapsed) {
  ::testing::Test::RecordProperty(name, elapsed);
  std::cout << name << "=" << elapsed << "ms\n";
}
std::string Reply(uint64_t id = 42) {
  commands::Output output;
  output.set_id(id);
  output.set_consumed(true);
  output.set_error_code(commands::Output::SESSION_SUCCESS);
  return output.SerializeAsString();
}

struct FakeState {
  Time now{};
  std::vector<Type> calls;
  std::vector<Time> deadlines;
  std::chrono::milliseconds delay{0};
  bool late = false;
  bool disconnect_key = false;
  bool malformed = false;
  bool wrong_id = false;
  bool wrong_set_request = false;
  bool oversized = false;
  uint32_t version = IPC_PROTOCOL_VERSION;
  uint32_t pid = 123;
};
class FakeIPC final : public IPCClientInterface {
 public:
  FakeIPC(FakeState& state, LinuxDeadline deadline) : state_(state), deadline_(std::move(deadline)) {}
  bool Connected() const override { return true; }
  uint32_t GetServerProtocolVersion() const override { return state_.version; }
  absl::string_view GetServerProductVersion() const override { return version_; }
  uint32_t GetServerProcessId() const override { return state_.pid; }
  IPCErrorType GetLastIPCError() const override { return IPC_TIMEOUT_ERROR; }
  bool Call(absl::string_view request, std::string* output, absl::Duration) override {
    commands::Input input;
    if (!input.ParseFromArray(request.data(), request.size())) return false;
    state_.calls.push_back(input.type());
    const auto finish = state_.now + state_.delay;
    state_.now = state_.late ? finish : std::min(finish, deadline_.end());
    if (!state_.late && deadline_.Expired()) return false;
    if (state_.disconnect_key && input.type() == commands::Input::SEND_KEY) return false;
    *output = state_.oversized ? std::string(1024 * 1024 + 1, 'x') :
              state_.malformed ? "\xff" : Reply(
                  state_.wrong_id || (state_.wrong_set_request &&
                  input.type() == commands::Input::SET_REQUEST) ? 99 : 42);
    return true;
  }
 private:
  FakeState& state_;
  LinuxDeadline deadline_;
  std::string version_ = Version::GetMozcVersion();
};
LinuxFcitxOptions FakeOptions(FakeState& state) {
  LinuxFcitxOptions options;
  options.close_group = std::make_shared<LinuxFcitxCloseGroup>([&] { return state.now; });
  options.factory = [&](const LinuxDeadline& deadline) {
    state.deadlines.push_back(deadline.end());
    return std::make_unique<FakeIPC>(state, deadline);
  };
  return options;
}

TEST(LinuxDeadlineTest, WholeOperationIncludesSessionRequestAndKey) {
  FakeState state;
  auto options = FakeOptions(state);
  Client client(options);
  state.delay = 40ms;
  commands::Output output;
  commands::KeyEvent key;
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(state.now, Time{} + 100ms);
  ASSERT_EQ(state.calls.size(), 3);
  EXPECT_EQ(state.calls[0], commands::Input::CREATE_SESSION);
  EXPECT_EQ(state.calls[1], commands::Input::SET_REQUEST);
  EXPECT_EQ(state.calls[2], commands::Input::SEND_KEY);
  for (const auto end : state.deadlines) EXPECT_EQ(end, Time{} + 100ms);
  EXPECT_FALSE(output.has_result());
  const auto count = state.calls.size();
  EXPECT_FALSE(client.SendKey(key, &output));
  client.Reset();
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(state.calls.size(), count);
}

TEST(LinuxDeadlineTest, SetRequestFailureStopsBeforeKey) {
  FakeState state; Client client(FakeOptions(state));
  state.delay = 60ms;
  commands::Output output; commands::KeyEvent key;
  EXPECT_FALSE(client.SendKey(key, &output));
  ASSERT_EQ(state.calls.size(), 2);
  EXPECT_EQ(state.calls.back(), commands::Input::SET_REQUEST);
}

TEST(LinuxDeadlineTest, LateValidResponseIsNeverPublished) {
  FakeState state; Client client(FakeOptions(state));
  ASSERT_TRUE(client.EnsureSession());
  state.delay = 120ms; state.late = true;
  commands::Output output; commands::KeyEvent key;
  output.mutable_result()->set_value("old");
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(output.ByteSizeLong(), 0);
}

TEST(LinuxDeadlineTest, InvalidSetRequestIdentityStopsBeforeKey) {
  FakeState state; state.wrong_set_request = true;
  Client client(FakeOptions(state));
  commands::Output output; commands::KeyEvent key;
  EXPECT_FALSE(client.SendKey(key, &output));
  ASSERT_EQ(state.calls.size(), 2);
  EXPECT_EQ(state.calls.back(), commands::Input::SET_REQUEST);
}

TEST(LinuxDeadlineTest, OversizedResponseIsNeverParsedOrPublished) {
  FakeState state; Client client(FakeOptions(state));
  ASSERT_TRUE(client.EnsureSession()); state.oversized = true;
  commands::Output output; commands::KeyEvent key;
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(output.ByteSizeLong(), 0);
}

TEST(LinuxDeadlineTest, NoReplayOrResendAfterDisconnect) {
  FakeState state; Client client(FakeOptions(state));
  commands::Output output; commands::KeyEvent key;
  ASSERT_TRUE(client.SendKey(key, &output));
  state.disconnect_key = true;
  const auto count = state.calls.size();
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(state.calls.size(), count + 1);
  EXPECT_FALSE(client.SendKey(key, &output));
  EXPECT_EQ(state.calls.size(), count + 1);
}

TEST(LinuxDeadlineTest, MalformedAndWrongSessionFailClosed) {
  for (bool malformed : {false, true}) {
    FakeState state; Client client(FakeOptions(state));
    ASSERT_TRUE(client.EnsureSession());
    state.malformed = malformed; state.wrong_id = !malformed;
    commands::Output output; commands::KeyEvent key;
    EXPECT_FALSE(client.SendKey(key, &output));
    EXPECT_EQ(output.ByteSizeLong(), 0);
  }
}

TEST(LinuxDeadlineTest, VersionMismatchDoesNotLaunchOrSend) {
  FakeState state; state.version++;
  Client client(FakeOptions(state));
  EXPECT_FALSE(client.CheckVersionOrRestartServer());
  EXPECT_FALSE(client.EnsureSession());
  EXPECT_FALSE(client.Shutdown());
  EXPECT_FALSE(client.LaunchTool("config_dialog", ""));
  EXPECT_FALSE(client.OpenBrowser("https://example.invalid"));
  EXPECT_TRUE(state.calls.empty());
}

TEST(LinuxDeadlineTest, ChangedServerPidDoesNotReceiveDelete) {
  FakeState state;
  auto client = std::make_unique<Client>(FakeOptions(state));
  ASSERT_TRUE(client->EnsureSession());
  const auto count = state.calls.size();
  state.pid++;
  EXPECT_FALSE(client->CloseLinuxFcitxSession());
  client.reset();
  EXPECT_EQ(state.calls.size(), count);
}

TEST(LinuxDeadlineTest, CloseIsIdempotentAndHealthySessionIsDeleted) {
  FakeState state;
  auto client = std::make_unique<Client>(FakeOptions(state));
  ASSERT_TRUE(client->EnsureSession());
  ASSERT_TRUE(client->CloseLinuxFcitxSession());
  const auto count = state.calls.size();
  EXPECT_EQ(state.calls.back(), commands::Input::DELETE_SESSION);
  EXPECT_TRUE(client->CloseLinuxFcitxSession());
  client.reset();
  EXPECT_EQ(state.calls.size(), count);
}

TEST(LinuxDeadlineTest, BatchDestructionSharesOneDeadline) {
  FakeState state;
  auto options = FakeOptions(state);
  std::vector<std::unique_ptr<Client>> clients;
  for (int i = 0; i < 64; ++i) {
    clients.push_back(std::make_unique<Client>(options));
    ASSERT_TRUE(clients.back()->EnsureSession());
  }
  state.calls.clear(); state.deadlines.clear(); state.delay = 60ms;
  options.close_group->BeginClose();
  const auto end = options.close_group->deadline()->end();
  options.close_group->BeginClose();
  EXPECT_EQ(options.close_group->deadline()->end(), end);
  clients.clear();
  EXPECT_EQ(state.now, Time{} + 100ms);
  ASSERT_EQ(state.calls.size(), 2);
  for (const auto deadline : state.deadlines) EXPECT_EQ(deadline, end);
}

class LinuxPeerTest : public testing::TestWithTempUserProfile {};
class Peer {
 public:
  enum Mode { Normal, Silence, Trickle, SlowDelete, Close, Malformed, NoRead, QueueFull, SlowAll, Oversized };
  explicit Peer(Mode mode = Normal) : mode_(mode) {
    static int sequence = 0;
    endpoint = {SystemUtil::GetUserProfileDirectory(), Self(), "a5_" + std::to_string(++sequence)};
    auto* manager = IPCPathManager::GetIPCPathManager(endpoint.channel);
    EXPECT_TRUE(manager->SavePathName());
    std::string address; EXPECT_TRUE(manager->GetPathName(&address));
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un sock{}; sock.sun_family = AF_UNIX;
    memcpy(sock.sun_path, address.data(), address.size());
    EXPECT_EQ(bind(socket_, reinterpret_cast<sockaddr*>(&sock), sizeof(sock.sun_family) + address.size()), 0);
    EXPECT_EQ(listen(socket_, 4), 0);
    thread_ = std::thread([this] { Run(); });
  }
  ~Peer() { stop_ = true; thread_.join(); close(socket_); }
  LinuxDeadlineEndpoint endpoint;
  std::atomic<int> requests{0};
 private:
  void Run() {
    while (!stop_) {
      if (mode_ == QueueFull) { std::this_thread::sleep_for(10ms); continue; }
      pollfd p{socket_, POLLIN, 0};
      if (poll(&p, 1, 10) <= 0) continue;
      const int fd = accept4(socket_, nullptr, nullptr, SOCK_CLOEXEC);
      if (fd < 0) continue;
      if (mode_ == NoRead) { std::this_thread::sleep_for(200ms); close(fd); continue; }
      std::string request;
      for (;;) {
        pollfd readable{fd, POLLIN, 0};
        if (poll(&readable, 1, 200) <= 0) break;
        char buffer[8192]; const auto count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        request.append(buffer, count);
      }
      if (request.empty()) { close(fd); continue; }
      requests++;
      if (mode_ == Silence) std::this_thread::sleep_for(350ms);
      else if (mode_ != Close) {
        commands::Input input;
        const bool parsed = input.ParseFromString(request);
        if (mode_ == SlowAll) std::this_thread::sleep_for(40ms);
        if (mode_ == SlowDelete && input.type() == commands::Input::DELETE_SESSION) std::this_thread::sleep_for(350ms);
        const std::string response = mode_ == Oversized ? std::string(1024 * 1024 + 1, 'x') :
                                     mode_ == Malformed ? "\xff" : Reply();
        (void)parsed; // Raw transport probes deliberately use a non-protobuf request.
        if (mode_ == Trickle) {
          for (int i = 0; i < 6; ++i) {
            std::this_thread::sleep_for(60ms);
            if (send(fd, "x", 1, MSG_NOSIGNAL) != 1) break;
          }
        } else {
          // EPIPE is expected when the client has correctly timed out.
          send(fd, response.data(), response.size(), MSG_NOSIGNAL);
        }
      }
      close(fd);
    }
  }
  Mode mode_;
  int socket_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

TEST_F(LinuxPeerTest, RealFragmentResponseHasWholeDeadline) {
  Peer peer(Peer::Trickle);
  const auto start = LinuxDeadline::Now();
  auto ipc = NewLinuxDeadlineIPCClient(peer.endpoint, LinuxDeadline::After(LinuxDeadline::Now, 100ms));
  ASSERT_TRUE(ipc->Connected());
  std::string output = "old";
  EXPECT_FALSE(ipc->Call("fixture", &output, absl::Seconds(30)));
  const auto elapsed = Elapsed(start); Metric("trickle_whole_ms", elapsed);
  EXPECT_LE(elapsed, 250); EXPECT_TRUE(output.empty());
}

TEST_F(LinuxPeerTest, RealDestructorIsBounded) {
  Peer peer(Peer::SlowDelete);
  LinuxFcitxOptions options; options.endpoint = peer.endpoint;
  auto client = std::make_unique<Client>(options);
  ASSERT_TRUE(client->EnsureSession());
  const auto start = LinuxDeadline::Now();
  client.reset();
  const auto elapsed = Elapsed(start); Metric("destructor_whole_ms", elapsed);
  EXPECT_LE(elapsed, 250);
}

TEST_F(LinuxPeerTest, RealBatchDestructionHasOneBudget) {
  Peer peer(Peer::SlowDelete);
  LinuxFcitxOptions options; options.endpoint = peer.endpoint;
  options.close_group = std::make_shared<LinuxFcitxCloseGroup>();
  std::vector<std::unique_ptr<Client>> clients;
  for (int i = 0; i < 20; ++i) {
    clients.push_back(std::make_unique<Client>(options));
    ASSERT_TRUE(clients.back()->EnsureSession());
  }
  const auto start = LinuxDeadline::Now();
  options.close_group->BeginClose();
  clients.clear();
  const auto elapsed = Elapsed(start); Metric("batch_20_destructors_ms", elapsed);
  EXPECT_LE(elapsed, 250);
}

TEST_F(LinuxPeerTest, RealSessionAndRequestShareBudget) {
  Peer peer(Peer::SlowAll);
  LinuxFcitxOptions options; options.endpoint = peer.endpoint;
  Client client(options); commands::KeyEvent key; commands::Output output;
  const auto start = LinuxDeadline::Now();
  EXPECT_FALSE(client.SendKey(key, &output));
  const auto elapsed = Elapsed(start); Metric("session_request_key_ms", elapsed);
  EXPECT_LE(elapsed, 250); EXPECT_EQ(output.ByteSizeLong(), 0);
}

TEST_F(LinuxPeerTest, HealthyWholeOperationTwentySamples) {
  Peer peer;
  double maximum = 0;
  for (int i = 0; i < 20; ++i) {
    LinuxFcitxOptions options; options.endpoint = peer.endpoint;
    auto client = std::make_unique<Client>(options);
    commands::Output output; commands::KeyEvent key;
    const auto start = LinuxDeadline::Now();
    ASSERT_TRUE(client->SendKey(key, &output));
    maximum = std::max(maximum, Elapsed(start));
    ASSERT_TRUE(client->CloseLinuxFcitxSession());
  }
  Metric("healthy_whole_max_ms", maximum); EXPECT_LE(maximum, 100);
  EXPECT_EQ(peer.requests.load(), 80); // CREATE / SET_REQUEST / KEY / DELETE.
}

TEST_F(LinuxPeerTest, FaultsClearOutputsAndReturnToDispatcher) {
  for (const auto mode : {Peer::Silence, Peer::Close, Peer::Malformed}) {
    Peer peer(mode); LinuxFcitxOptions options; options.endpoint = peer.endpoint;
    Client client(options); commands::Output output; commands::KeyEvent key;
    output.mutable_result()->set_value("old");
    const auto start = LinuxDeadline::Now();
    EXPECT_FALSE(client.SendKey(key, &output));
    // This is a fake dispatch boundary, not an actual Fcitx5 event loop.
    const auto elapsed = Elapsed(start); Metric("fault_" + std::to_string(mode) + "_dispatch_ms", elapsed);
    EXPECT_LE(elapsed, 250); EXPECT_EQ(output.ByteSizeLong(), 0);
    const auto requests = peer.requests.load();
    EXPECT_FALSE(client.SendKey(key, &output));
    EXPECT_EQ(peer.requests.load(), requests);
  }
}

TEST_F(LinuxPeerTest, BackpressureSendIsBounded) {
  Peer peer(Peer::NoRead);
  const auto start = LinuxDeadline::Now();
  auto ipc = NewLinuxDeadlineIPCClient(peer.endpoint, LinuxDeadline::After(LinuxDeadline::Now, 100ms));
  ASSERT_TRUE(ipc->Connected());
  std::string output;
  EXPECT_FALSE(ipc->Call(std::string(1024 * 1024, 'x'), &output, absl::Seconds(30)));
  const auto elapsed = Elapsed(start); Metric("backpressure_send_ms", elapsed);
  EXPECT_LE(elapsed, 250);
}

TEST_F(LinuxPeerTest, WrongExecutableAndExpiredConnectDoNotSend) {
  Peer peer; auto endpoint = peer.endpoint; endpoint.server_path += ".other-product";
  auto wrong = NewLinuxDeadlineIPCClient(endpoint, LinuxDeadline::After(LinuxDeadline::Now, 100ms));
  EXPECT_FALSE(wrong->Connected());
  auto expired = NewLinuxDeadlineIPCClient(peer.endpoint, LinuxDeadline::After(LinuxDeadline::Now, 0ms));
  EXPECT_FALSE(expired->Connected()); EXPECT_EQ(peer.requests.load(), 0);
}

TEST_F(LinuxPeerTest, FullConnectQueueCannotBlockOrSend) {
  Peer peer(Peer::QueueFull);
  std::string address;
  ASSERT_TRUE(IPCPathManager::GetIPCPathManager(peer.endpoint.channel)->GetPathName(&address));
  sockaddr_un sock{}; sock.sun_family = AF_UNIX;
  memcpy(sock.sun_path, address.data(), address.size());
  std::vector<int> fillers;
  for (int i = 0; i < 6; ++i) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ASSERT_GE(fd, 0);
    if (connect(fd, reinterpret_cast<sockaddr*>(&sock), sizeof(sock.sun_family) + address.size()) != 0) {
      close(fd); break;
    }
    fillers.push_back(fd);
  }
  const auto start = LinuxDeadline::Now();
  auto ipc = NewLinuxDeadlineIPCClient(peer.endpoint, LinuxDeadline::After(LinuxDeadline::Now, 100ms));
  const auto elapsed = Elapsed(start);
  EXPECT_FALSE(ipc->Connected()); EXPECT_LE(elapsed, 250);
  Metric("queue_full_connect_ms", elapsed);
  EXPECT_EQ(peer.requests.load(), 0);
  for (int fd : fillers) close(fd);
}

TEST_F(LinuxPeerTest, KeySymlinkFifoAndPublicDirectoryAreRejected) {
  const auto profile = SystemUtil::GetUserProfileDirectory();
  const auto sentinel = profile + "/sentinel";
  { std::ofstream file(sentinel); file << "unchanged"; }
  LinuxDeadlineEndpoint endpoint{profile, Self(), "unsafe"};
  const auto key = profile + "/.unsafe.ipc";
  ASSERT_EQ(symlink(sentinel.c_str(), key.c_str()), 0);
  auto rejected = [&] {
    auto ipc = NewLinuxDeadlineIPCClient(endpoint, LinuxDeadline::After(LinuxDeadline::Now, 100ms));
    EXPECT_FALSE(ipc->Connected());
  };
  rejected(); ASSERT_EQ(unlink(key.c_str()), 0);
  ASSERT_EQ(mkfifo(key.c_str(), 0600), 0); rejected();
  ASSERT_EQ(unlink(key.c_str()), 0);
  ASSERT_EQ(chmod(profile.c_str(), 0755), 0); rejected();
  ASSERT_EQ(chmod(profile.c_str(), 0700), 0);
  std::ifstream file(sentinel); std::string value; file >> value;
  EXPECT_EQ(value, "unchanged");
}
}  // namespace
}  // namespace mozc
