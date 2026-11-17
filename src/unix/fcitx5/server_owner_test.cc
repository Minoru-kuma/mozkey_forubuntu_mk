// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Synthetic ELF + real profile/key/peer checks. Never starts a product server.
#include "unix/fcitx5/server_owner.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
extern "C" {
#include <sys/pidfd.h>
}
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "base/owned_child.h"
#include "base/system_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::fcitx5 {
namespace {
using namespace std::chrono_literals;
bool Until(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 2s) {
  const auto end = std::chrono::steady_clock::now()+timeout;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= end) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}
class IndependentFixture {
 public:
  ~IndependentFixture() {
    if (fd_ < 0) return;
    pidfd_send_signal(fd_, SIGTERM, nullptr, 0);
    pollfd event{fd_, POLLIN, 0};
    if (poll(&event, 1, 1000) <= 0) {
      pidfd_send_signal(fd_, SIGKILL, nullptr, 0);
      EXPECT_GT(poll(&event, 1, 1000), 0);
    }
    siginfo_t info{};
    EXPECT_EQ(waitid(P_PIDFD, fd_, &info, WEXITED | WNOHANG), 0);
    EXPECT_GT(info.si_pid, 0);
    close(fd_);
  }
  bool Start(std::string image, const std::string& profile) {
    auto root = "--fixture-profile="+profile;
    auto parent = "--fixture-parent="+std::to_string(getpid());
    char language[] = "LANG=C";
    char* args[]{image.data(), root.data(), parent.data(), nullptr};
    char* environment[]{language, nullptr};
    return pidfd_spawn(&fd_, image.c_str(), nullptr, nullptr, args, environment) == 0;
  }
  bool Alive() const { pollfd event{fd_, POLLIN, 0}; return fd_ >= 0 && poll(&event, 1, 0) == 0; }
 private:
  int fd_ = -1;
};
class ServerOwnerTest : public mozc::testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    TestWithTempUserProfile::SetUp();
    profile_ = mozc::SystemUtil::GetUserProfileDirectory();
    const char* source = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    ASSERT_NE(source, nullptr); ASSERT_NE(workspace, nullptr);
    const auto input = std::filesystem::path(source)/workspace/"unix/fcitx5/server_owner_fixture";
    executable_ = profile_+"/fixture";
    ASSERT_TRUE(std::filesystem::copy_file(input, executable_));
    ASSERT_EQ(chmod(executable_.c_str(), 0700), 0);
    endpoint_ = {profile_, executable_, "session"};
  }
  void TearDown() override {
    owner_.reset();
    EXPECT_TRUE(Until([] { return !OwnedChild::Busy() && !PreparationWorker::Busy(); }));
    TestWithTempUserProfile::TearDown();
  }
  std::unique_ptr<PreparationWorker> Begin(std::vector<std::string> arguments = {}) {
    owner_ = std::make_unique<ServerOwner>(endpoint_, std::move(arguments));
    return PreparationWorker::Start(owner_->PreparationJob());
  }
  int Complete(const std::unique_ptr<PreparationWorker>& worker) {
    if (!worker || !Until([&] { return worker->finished(); })) return -1;
    return worker->TakeFd();
  }
  std::string Read(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  }
  std::string profile_, executable_;
  LinuxDeadlineEndpoint endpoint_;
  std::unique_ptr<ServerOwner> owner_;
};
TEST_F(ServerOwnerTest, FreshLaunchTransfersValidatedFdAndCancelsOwnedChild) {
  auto worker = Begin(); ASSERT_TRUE(worker);
  const int fd = Complete(worker); ASSERT_GE(fd, 0);
  const int status_flags = fcntl(fd, F_GETFL), descriptor_flags = fcntl(fd, F_GETFD);
  EXPECT_GE(status_flags, 0); EXPECT_NE(status_flags & O_NONBLOCK, 0);
  EXPECT_GE(descriptor_flags, 0); EXPECT_NE(descriptor_flags & FD_CLOEXEC, 0);
  EXPECT_TRUE(owner_->launch_attempted()); EXPECT_TRUE(owner_->owns_process());
  const bool graceful = owner_->graceful_shutdown_ready();
  close(fd); owner_->Cancel();
  ASSERT_TRUE(Until([] { return !OwnedChild::Busy(); }));
  EXPECT_EQ(std::filesystem::exists(profile_+"/.fixture-shutdown"), graceful);
}
TEST_F(ServerOwnerTest, FiniteStartupDelayAndOwnerCancellationDoNotHandOffLateFd) {
  auto worker = Begin({"--pause350"}); ASSERT_TRUE(worker);
  ASSERT_TRUE(Until([&] { return owner_->owns_process() && std::filesystem::exists(profile_+"/.session.ipc"); }));
  owner_->Cancel();
  EXPECT_EQ(Complete(worker), -1);
  EXPECT_TRUE(Until([] { return !OwnedChild::Busy(); }));
}
TEST_F(ServerOwnerTest, JobRetainsOnlyValuesAfterAddonOwnerDestruction) {
  auto worker = Begin({"--pause350"}); ASSERT_TRUE(worker);
  ASSERT_TRUE(Until([&] { return owner_->owns_process() && std::filesystem::exists(profile_+"/.session.ipc"); }));
  owner_.reset();
  EXPECT_EQ(Complete(worker), -1);
  EXPECT_TRUE(Until([] { return !OwnedChild::Busy(); }));
}
TEST_F(ServerOwnerTest, MalformedExistingKeyIsNeverOverwrittenOrLaunchedOver) {
  const auto key = profile_+"/.session.ipc";
  { std::ofstream file(key); file << "foreign-sentinel"; }
  ASSERT_EQ(chmod(key.c_str(), 0600), 0);
  auto worker = Begin(); ASSERT_TRUE(worker);
  EXPECT_EQ(Complete(worker), -1);
  EXPECT_FALSE(owner_->launch_attempted()); EXPECT_FALSE(owner_->owns_process());
  EXPECT_EQ(Read(key), "foreign-sentinel");
}
TEST_F(ServerOwnerTest, SymlinkKeyTargetAndModeSurvive) {
  const auto target = profile_+"/sentinel";
  { std::ofstream file(target); file << "other-product"; }
  ASSERT_EQ(chmod(target.c_str(), 0400), 0);
  ASSERT_EQ(symlink(target.c_str(), (profile_+"/.session.ipc").c_str()), 0);
  auto worker = Begin(); ASSERT_TRUE(worker);
  EXPECT_EQ(Complete(worker), -1); EXPECT_FALSE(owner_->launch_attempted());
  struct stat info{}; ASSERT_EQ(stat(target.c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0400); EXPECT_EQ(Read(target), "other-product");
  EXPECT_TRUE(std::filesystem::is_symlink(profile_+"/.session.ipc"));
}
TEST_F(ServerOwnerTest, CancelledOwnerJobCannotStartAfterNewFocus) {
  owner_ = std::make_unique<ServerOwner>(endpoint_);
  auto job = owner_->PreparationJob(); owner_->Cancel();
  auto worker = PreparationWorker::Start(std::move(job)); ASSERT_TRUE(worker);
  EXPECT_EQ(Complete(worker), -1); EXPECT_FALSE(owner_->launch_attempted());
}
TEST_F(ServerOwnerTest, OperationCancellationKeepsAddonOwnedServerForNextContext) {
  auto worker = Begin({"--pause350"}); ASSERT_TRUE(worker);
  ASSERT_TRUE(Until([&] { return owner_->owns_process() && std::filesystem::exists(profile_+"/.session.ipc"); }));
  worker->Cancel();
  EXPECT_EQ(Complete(worker), -1);
  ASSERT_TRUE(Until([] { return !PreparationWorker::Busy(); }));
  EXPECT_TRUE(OwnedChild::Busy());
  auto next = PreparationWorker::Start(owner_->PreparationJob()); ASSERT_TRUE(next);
  const int fd = Complete(next); ASSERT_GE(fd, 0); close(fd);
  EXPECT_TRUE(owner_->owns_process());
}
TEST_F(ServerOwnerTest, VerifiedExistingPeerIsNotAdoptedOrStopped) {
  IndependentFixture independent;
  ASSERT_TRUE(independent.Start(executable_, profile_));
  ASSERT_TRUE(Until([&] { return std::filesystem::exists(profile_+"/.fixture-ready"); }));
  auto worker = Begin(); ASSERT_TRUE(worker);
  const int fd = Complete(worker); ASSERT_GE(fd, 0); close(fd);
  EXPECT_FALSE(owner_->launch_attempted()); EXPECT_FALSE(owner_->owns_process());
  owner_->Cancel();
  EXPECT_TRUE(independent.Alive());
  EXPECT_FALSE(std::filesystem::exists(profile_+"/.fixture-shutdown"));
}
TEST_F(ServerOwnerTest, ValidButStaleUnownedKeyIsPreservedWithoutRestart) {
  {
    IndependentFixture independent;
    ASSERT_TRUE(independent.Start(executable_, profile_));
    ASSERT_TRUE(Until([&] { return std::filesystem::exists(profile_+"/.fixture-ready"); }));
  }
  const auto before = Read(profile_+"/.session.ipc"); ASSERT_FALSE(before.empty());
  auto worker = Begin(); ASSERT_TRUE(worker);
  EXPECT_EQ(Complete(worker), -1);
  EXPECT_FALSE(owner_->launch_attempted()); EXPECT_FALSE(owner_->owns_process());
  EXPECT_EQ(Read(profile_+"/.session.ipc"), before);
}
TEST_F(ServerOwnerTest, AdministrativeSaveCannotColdStartButInputPreparationCan) {
  owner_ = std::make_unique<ServerOwner>(endpoint_);
  auto save = PreparationWorker::Start(owner_->PreparationJob(false)); ASSERT_TRUE(save);
  EXPECT_EQ(Complete(save), -1); EXPECT_FALSE(owner_->launch_attempted());
  EXPECT_FALSE(std::filesystem::exists(profile_+"/.session.ipc"));
  ASSERT_TRUE(Until([] { return !PreparationWorker::Busy(); }));
  auto input = PreparationWorker::Start(owner_->PreparationJob()); ASSERT_TRUE(input);
  const int fd = Complete(input); ASSERT_GE(fd, 0); close(fd);
  EXPECT_TRUE(owner_->owns_process());
}
TEST_F(ServerOwnerTest, LosingAnExistingPeerAndItsKeyCannotImplicitlyLaunchDuringCleanup) {
  {
    IndependentFixture independent;
    ASSERT_TRUE(independent.Start(executable_, profile_));
    ASSERT_TRUE(Until([&] { return std::filesystem::exists(profile_+"/.fixture-ready"); }));
    auto worker = Begin(); ASSERT_TRUE(worker);
    const int fd = Complete(worker); ASSERT_GE(fd, 0); close(fd);
    EXPECT_FALSE(owner_->owns_process());
  }
  // Model an older peer removing its own key on exit. Only this test's private
  // synthetic metadata is removed; no product/user profile is touched.
  ASSERT_EQ(unlink((profile_+"/.session.ipc").c_str()), 0);
  ASSERT_TRUE(Until([] { return !PreparationWorker::Busy(); }));
  auto cleanup = PreparationWorker::Start(owner_->PreparationJob()); ASSERT_TRUE(cleanup);
  EXPECT_EQ(Complete(cleanup), -1); EXPECT_FALSE(owner_->launch_attempted());
  EXPECT_FALSE(std::filesystem::exists(profile_+"/.session.ipc"));
}
}  // namespace
}  // namespace mozc::fcitx5
