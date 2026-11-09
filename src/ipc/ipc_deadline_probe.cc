// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause

// A diagnostic for the Phase 2A IPC gate, deliberately outside the default
// test suite. It exercises the existing transport, not a reimplementation.
// Run only with a fresh private profile supplied by the caller. Exit codes:
// 0 = measured gates passed, 1 = fixture error, 2 = deadline gate not met.
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/system_util.h"
#include "client/client.h"
#include "client/client_interface.h"
#include "ipc/ipc.h"
#include "ipc/ipc_path_manager.h"
#include "protocol/commands.pb.h"

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class Fd {
 public:
  explicit Fd(int fd) : fd_(fd) { Require(fd >= 0, "invalid fd"); }
  ~Fd() { close(fd_); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return fd_; }
 private:
  int fd_;
};

double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void WaitReadable(int fd) {
  pollfd event{fd, POLLIN, 0};
  Require(poll(&event, 1, 2000) == 1, "fixture poll timeout");
}

std::string ReadRequest(int fd) {
  std::string request;
  for (;;) {
    WaitReadable(fd);
    char buffer[4096];
    const auto count = recv(fd, buffer, sizeof(buffer), 0);
    Require(count >= 0, "fixture recv failed");
    if (count == 0) return request;
    request.append(buffer, count);
    Require(request.size() <= 1024 * 1024, "oversized fixture request");
  }
}

// Keys are generated locally in the synthetic profile, with a random abstract
// socket address. The peer is this executable, never an installed IME/server.
class Peer {
 public:
  explicit Peer(const std::string& name)
      : listener_(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
    auto* manager = mozc::IPCPathManager::GetIPCPathManager(name);
    Require(manager->SavePathName(), "cannot save synthetic IPC key");
    std::string address;
    Require(manager->GetPathName(&address), "no synthetic address");
    Require(!address.empty() && address[0] == '\0', "expected abstract socket");
    sockaddr_un sock{};
    sock.sun_family = AF_UNIX;
    Require(address.size() < sizeof(sock.sun_path), "address too long");
    memcpy(sock.sun_path, address.data(), address.size());
    Require(bind(listener_.get(), reinterpret_cast<sockaddr*>(&sock),
                 sizeof(sock.sun_family) + address.size()) == 0, "bind failed");
    Require(listen(listener_.get(), 4) == 0, "listen failed");
  }
  int Accept() {
    WaitReadable(listener_.get());
    return accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC);
  }
 private:
  Fd listener_;
};

void Send(int fd, const std::string& value) {
  Require(send(fd, value.data(), value.size(), MSG_NOSIGNAL) == value.size(),
          "fixture send failed");
}

void Print(const char* name, double elapsed, bool call_ok, bool gate_ok) {
  std::cout << "{\"case\":\"" << name << "\",\"elapsed_ms\":" << elapsed
            << ",\"call_ok\":" << (call_ok ? "true" : "false")
            << ",\"gate_ok\":" << (gate_ok ? "true" : "false") << "}\n";
}

bool Transport(const std::string& executable, const char* name,
               bool trickle, bool silence) {
  Peer peer(name);
  auto worker = std::async(std::launch::async, [&] {
    Fd fd(peer.Accept());
    Require(ReadRequest(fd.get()) == "fixture", "unexpected request");
    if (silence) {
      std::this_thread::sleep_for(350ms);
    } else if (trickle) {
      for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(60ms);
        Send(fd.get(), "x");
      }
    } else {
      Send(fd.get(), "fixture-response");
    }
  });
  mozc::IPCClient client(name, executable);
  Require(client.Connected(), "fixture peer rejected");
  std::string result;
  const auto start = Clock::now();
  const bool ok = client.Call("fixture", &result, absl::Milliseconds(100));
  const double elapsed = Milliseconds(start);
  // Normal request target 100ms; these two fault cases must return within
  // 250ms and reject the incomplete/late result. Elapsed time is also printed
  // so the whole-request budget can be reviewed independently.
  const bool gate = (silence || trickle) ? (!ok && elapsed <= 250)
                                       : (ok && elapsed <= 100);
  worker.get();  // Outside measured caller return time.
  Print(name, elapsed, ok, gate);
  return gate;
}

// Launching, waiting, terminating and fatal dialogs are all intercepted.
// CREATE_SESSION / SET_REQUEST / DELETE_SESSION still use the real transport.
class NoLaunch final : public mozc::client::ServerLauncherInterface {
 public:
  explicit NoLaunch(std::string executable) : executable_(std::move(executable)) {}
  bool StartServer(mozc::client::ClientInterface*) override { return true; }
  bool ForceTerminateServer(absl::string_view) override {
    throw std::runtime_error("unexpected termination");
  }
  bool WaitServer(uint32_t) override {
    throw std::runtime_error("unexpected process wait");
  }
  void OnFatal(ServerErrorType) override {
    throw std::runtime_error("unexpected fatal dialog");
  }
  std::string server_program() const override { return executable_; }
  void set_suppress_error_dialog(bool) override {}
 private:
  std::string executable_;
};

bool Destruction(const std::string& executable) {
  Peer peer("session");
  auto worker = std::async(std::launch::async, [&] {
    for (;;) {
      Fd fd(peer.Accept());
      mozc::commands::Input input;
      Require(input.ParseFromString(ReadRequest(fd.get())), "invalid input");
      const bool deleting = input.type() == mozc::commands::Input::DELETE_SESSION;
      Require(deleting || input.type() == mozc::commands::Input::CREATE_SESSION ||
              input.type() == mozc::commands::Input::SET_REQUEST,
              "unexpected session operation");
      if (deleting) std::this_thread::sleep_for(350ms);
      mozc::commands::Output output;
      output.set_id(42);
      output.set_error_code(mozc::commands::Output::SESSION_SUCCESS);
      Send(fd.get(), output.SerializeAsString());
      if (deleting) return;
    }
  });
  auto client = std::make_unique<mozc::client::Client>();
  client->SetServerLauncher(std::make_unique<NoLaunch>(executable));
  client->set_timeout(absl::Milliseconds(100));
  Require(client->EnsureSession(), "cannot create synthetic session");
  const auto start = Clock::now();
  client.reset();
  const double elapsed = Milliseconds(start);
  worker.get();
  const bool gate = elapsed <= 250;
  Print("client_destruction_after_set_timeout_100ms", elapsed, true, gate);
  return gate;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "usage: ipc_deadline_probe FRESH_PRIVATE_PROFILE");
    struct stat st{};
    Require(lstat(argv[1], &st) == 0 && S_ISDIR(st.st_mode) &&
            st.st_uid == geteuid() && (st.st_mode & 077) == 0,
            "profile must be a private, owned directory (not a symlink)");
    Require(std::filesystem::is_empty(argv[1]), "profile must be fresh and empty");
    mozc::SystemUtil::SetUserProfileDirectory(argv[1]);
    char buffer[4096];
    const auto count = readlink("/proc/self/exe", buffer, sizeof(buffer));
    Require(count > 0 && count < sizeof(buffer), "cannot resolve self");
    const std::string executable(buffer, count);
    bool passed = Transport(executable, "phase2a_immediate", false, false);
    passed = Transport(executable, "phase2a_silence", false, true) && passed;
    passed = Transport(executable, "phase2a_trickle", true, false) && passed;
    passed = Destruction(executable) && passed;
    return passed ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "fixture error: " << error.what() << '\n';
    return 1;
  }
}
