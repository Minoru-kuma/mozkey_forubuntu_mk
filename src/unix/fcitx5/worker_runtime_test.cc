// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Test process only: no Instance, addon, product IME/server or host settings.
#include "unix/fcitx5/preparation_worker.h"

#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "base/system_util.h"
#include "ipc/ipc_path_manager.h"
#include "ipc/linux_deadline.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

extern char** environ;
namespace mozc::fcitx5 {
namespace {
using namespace std::chrono_literals;
struct ExitState {
  std::atomic<bool> started{false}, host_destroyed{false}, prepared{false};
  std::atomic<int> returned_fd{-1};
  int listener = -1, delay = 0;
};
ExitState* exit_state = nullptr; // Child fixture owns this until process exit.
struct StaticExitProbe {
  ~StaticExitProbe() {
    // Model an already-destroyed owner. The job captures only endpoint values
    // and heap-owned state, never an addon/IC or a borrowed listener descriptor.
    exit_state->host_destroyed.store(true);
    const bool bounded = PreparationWorker::WaitForProcessExit(390ms);
    // Diagnostic ONLY: keep this test's static destructor alive to observe a
    // late return after the production wait budget. This is not a 400ms pass.
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (PreparationWorker::Busy() && std::chrono::steady_clock::now() < end) usleep(1000);
    const bool joined = PreparationWorker::WaitForProcessExit(390ms);
    const bool prepared = exit_state->prepared.load();
    const int fd = exit_state->returned_fd.load();
    const bool closed = fd >= 0 && fcntl(fd, F_GETFD) == -1 && errno == EBADF;
    auto rejected = PreparationWorker::Start([](const auto&) { return -1; });
    const bool sealed = !rejected;
    char text[240];
    const auto count = snprintf(text, sizeof(text),
        "WORKER_STATIC_TEARDOWN_METRIC delay_ms=%d bounded_join=%d late_join=%d validated_fd=%d fd_closed=%d sealed=%d\n",
        exit_state->delay, bounded, joined, prepared, closed, sealed);
    if (count > 0 && count < int(sizeof(text))) {
      const auto written = write(STDOUT_FILENO, text, count); (void)written;
    }
    close(exit_state->listener);
    if (!joined || !prepared || !closed || !sealed || bounded != (exit_state->delay == 350)) _exit(74);
  }
};
TEST(WorkerRuntimeTest, ChildOperation) {
  const char* root = std::getenv("MOZKEY_WORKER_RUNTIME_ROOT");
  const char* mode = std::getenv("MOZKEY_WORKER_RUNTIME_DELAY");
  if (!root || !mode) GTEST_SKIP();
  const int delay = std::string(mode) == "350" ? 350 : 3500;
  SystemUtil::SetUserProfileDirectory(root);
  char executable[4096]; const auto bytes = readlink("/proc/self/exe", executable, sizeof(executable));
  ASSERT_GT(bytes, 0); ASSERT_LT(bytes, sizeof(executable));
  LinuxDeadlineEndpoint endpoint{root, std::string(executable, bytes), "runtime_exit_fixture"};
  auto* manager = IPCPathManager::GetIPCPathManager(endpoint.channel);
  ASSERT_TRUE(manager->SavePathName());
  std::string address; ASSERT_TRUE(manager->GetPathName(&address));
  exit_state = new ExitState; exit_state->delay = delay;
  exit_state->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  ASSERT_GE(exit_state->listener, 0);
  sockaddr_un name{}; name.sun_family = AF_UNIX;
  ASSERT_LT(address.size(), sizeof(name.sun_path)); memcpy(name.sun_path, address.data(), address.size());
  ASSERT_EQ(bind(exit_state->listener, reinterpret_cast<sockaddr*>(&name), sizeof(name.sun_family)+address.size()), 0);
  ASSERT_EQ(listen(exit_state->listener, 1), 0);
  static StaticExitProbe probe; (void)probe;
  auto* state = exit_state;
  auto worker = PreparationWorker::Start([endpoint, state, delay](const auto&) {
    state->started.store(true);
    // Cancellation cannot interrupt a syscall/preemption that is already in
    // progress. Deliberately exercise post-cancellation support-library use.
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (!state->host_destroyed.load() || SystemUtil::GetUserProfileDirectory() != endpoint.profile ||
        SystemUtil::GetServerPath().empty()) return -1;
    auto deadline = LinuxDeadline::After(LinuxDeadline::Now, 1000ms);
    const int fd = PrepareLinuxDeadlineSocket(endpoint, deadline);
    state->returned_fd.store(fd); state->prepared.store(fd >= 0); return fd;
  });
  ASSERT_TRUE(worker);
  const auto end = std::chrono::steady_clock::now() + 1s;
  while (!state->started.load() && std::chrono::steady_clock::now() < end) usleep(1000);
  ASSERT_TRUE(state->started.load());
  worker.reset(); EXPECT_FALSE(PreparationWorker::WaitForProcessExit(10ms));
  // The actual profile/key parse, peer/exec/version validation and FD disposal
  // happen after this test returns, inside the controlled static-exit window.
}

class WorkerRuntimeParent : public testing::TestWithTempUserProfile {};
void RunExitChild(const std::string& root, const char* delay) {
  std::vector<std::string> environment;
  for (char** at = environ; *at; ++at) {
    std::string entry(*at);
    if (entry.starts_with("TEST_PREMATURE_EXIT_FILE=") || entry.starts_with("XML_OUTPUT_FILE=") ||
        entry.starts_with("GTEST_OUTPUT=") || entry.starts_with("GTEST_SHARD_") ||
        entry.starts_with("GTEST_TOTAL_SHARDS=") || entry.starts_with("MOZKEY_WORKER_RUNTIME_")) continue;
    environment.push_back(std::move(entry));
  }
  environment.push_back("MOZKEY_WORKER_RUNTIME_ROOT="+root);
  environment.push_back(std::string("MOZKEY_WORKER_RUNTIME_DELAY=")+delay);
  std::vector<char*> values; for (auto& entry : environment) values.push_back(entry.data()); values.push_back(nullptr);
  char executable[] = "/proc/self/exe", filter[] = "--gtest_filter=WorkerRuntimeTest.ChildOperation";
  char* args[] = {executable, filter, nullptr}; pid_t pid = -1;
  ASSERT_EQ(posix_spawn(&pid, executable, nullptr, nullptr, args, values.data()), 0);
  const auto end = std::chrono::steady_clock::now()+8s;
  int status = 0; pid_t result;
  do {
    result = waitpid(pid, &status, WNOHANG);
    if (result != 0) break;
    usleep(1000);
  } while (std::chrono::steady_clock::now() < end);
  if (!result) {
    (void)kill(pid, SIGKILL); // Still our unreaped child, never an arbitrary PID.
    for (int i=0; i<1000 && !result; ++i) { result = waitpid(pid, &status, WNOHANG); usleep(1000); }
  }
  ASSERT_EQ(result, pid); ASSERT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(WorkerRuntimeParent, SupportLibrariesAndVerifiedFdOutliveOwnerAtStaticExit) {
  RunExitChild(SystemUtil::GetUserProfileDirectory(), "350");
}
TEST_F(WorkerRuntimeParent, LateReturnAfterBudgetIsObservedWithoutClaimingBoundedReclamation) {
  RunExitChild(SystemUtil::GetUserProfileDirectory(), "3500");
}
}  // namespace
}  // namespace mozc::fcitx5
