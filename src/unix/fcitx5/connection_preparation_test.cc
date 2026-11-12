// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/connection_preparation.h"
#include <fcitx-utils/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include "base/system_util.h"
#include "base/file_util.h"
#include "ipc/ipc_path_manager.h"
#include "ipc/ipc.pb.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::fcitx5 {
namespace {
class PreparationTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    TestWithTempUserProfile::SetUp();
    char path[4096];
    auto n = readlink("/proc/self/exe", path, sizeof(path));
    ASSERT_GT(n, 0);
    static unsigned sequence = 0;
    endpoint = {SystemUtil::GetUserProfileDirectory(), std::string(path, n),
                "prep_test_" + std::to_string(++sequence)};
    auto* manager = IPCPathManager::GetIPCPathManager(endpoint.channel);
    ASSERT_TRUE(manager->SavePathName());
    std::string address;
    ASSERT_TRUE(manager->GetPathName(&address));
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ASSERT_GE(listener, 0);
    sockaddr_un socket_address{};
    socket_address.sun_family = AF_UNIX;
    memcpy(socket_address.sun_path, address.data(), address.size());
    ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&socket_address),
                   sizeof(socket_address.sun_family) + address.size()), 0);
    ASSERT_EQ(listen(listener, 4), 0);
  }
  void TearDown() override {
    if (listener >= 0) close(listener);
    TestWithTempUserProfile::TearDown();
  }
  int Prepare() {
    fcitx::EventLoop loop;
    using namespace std::chrono_literals;
    auto worker = StartConnectionPreparation(endpoint, 1000ms);
    EXPECT_NE(worker, nullptr);
    if (!worker) return -1;
    int fd = -1;
    bool notified = false;
    auto io = loop.addIOEvent(worker->notification_fd(), fcitx::IOEventFlag::In,
        [&](fcitx::EventSourceIO*, int notification, fcitx::IOEventFlags) {
          uint64_t value;
          EXPECT_EQ(read(notification, &value, sizeof(value)), sizeof(value));
          notified = true; fd = worker->TakeFd(); loop.exit(); return true;
        });
    auto end = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 1500000, 1,
        [&](fcitx::EventSourceTime*, uint64_t) { loop.exit(); return true; });
    end->setOneShot();
    EXPECT_TRUE(loop.exec());
    EXPECT_TRUE(notified);
    io.reset();
    worker.reset();
    return fd;
  }
  LinuxDeadlineEndpoint endpoint;
  int listener = -1;
};
TEST_F(PreparationTest, ValidatedSameFdAndNoRequestBeforeHandoff) {
  const int fd = Prepare();
  ASSERT_GE(fd, 0);
  EXPECT_NE(fcntl(fd, F_GETFL) & O_NONBLOCK, 0);
  EXPECT_NE(fcntl(fd, F_GETFD) & FD_CLOEXEC, 0);
  const int peer = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
  ASSERT_GE(peer, 0);
  char buffer[16];
  EXPECT_EQ(recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT), -1);
  EXPECT_EQ(errno, EAGAIN); // Preparation has not sent an application request.
  EXPECT_EQ(send(fd, "fixture", 7, MSG_NOSIGNAL), 7);
  EXPECT_EQ(recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT), 7);
  EXPECT_EQ(std::string(buffer, 7), "fixture");
  EXPECT_EQ(accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC), -1);
  close(peer); close(fd);
}
TEST_F(PreparationTest, WrongPeerExecutableRejected) {
  endpoint.server_path = "/not/the/expected/server";
  EXPECT_EQ(Prepare(), -1);
}
TEST_F(PreparationTest, WrongProductVersionRejected) {
  const auto key_path = endpoint.profile + "/." + endpoint.channel + ".ipc";
  auto data = FileUtil::GetContents(key_path);
  ASSERT_TRUE(data.ok());
  ipc::IPCPathInfo info;
  ASSERT_TRUE(info.ParseFromString(*data));
  info.set_product_version("different-product-version");
  ASSERT_EQ(chmod(key_path.c_str(), 0600), 0);
  ASSERT_TRUE(FileUtil::SetContents(key_path, info.SerializeAsString()).ok());
  ASSERT_EQ(chmod(key_path.c_str(), 0400), 0);
  EXPECT_EQ(Prepare(), -1);
}
TEST_F(PreparationTest, ForeignNamespaceNeverConnected) {
  close(listener);
  listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  ASSERT_GE(listener, 0);
  auto* manager = IPCPathManager::GetIPCPathManager(endpoint.channel);
  std::string address;
  ASSERT_TRUE(manager->GetPathName(&address));
  ASSERT_EQ(address.substr(1, 12), "tmp/.mozkey.");
  address.replace(5, 8, ".mozc.");
  sockaddr_un foreign{};
  foreign.sun_family = AF_UNIX;
  memcpy(foreign.sun_path, address.data(), address.size());
  ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&foreign),
                 sizeof(foreign.sun_family) + address.size()), 0);
  ASSERT_EQ(listen(listener, 4), 0);
  EXPECT_EQ(Prepare(), -1);
  EXPECT_EQ(accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC), -1);
  EXPECT_EQ(errno, EAGAIN);
}
TEST_F(PreparationTest, PublicKeyRejected) {
  const auto key_path = endpoint.profile + "/." + endpoint.channel + ".ipc";
  ASSERT_EQ(chmod(key_path.c_str(), 0644), 0);
  EXPECT_EQ(Prepare(), -1);
}
}  // namespace
}  // namespace mozc::fcitx5
