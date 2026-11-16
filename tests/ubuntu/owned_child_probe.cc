// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Isolated synthetic process ownership probe; never starts a product binary.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
extern "C" {
#include <sys/pidfd.h>
}
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "base/owned_child.h"
#include "unix/fcitx5/preparation_worker.h"

// Link-time fault injection only in this probe, never in the product/library.
std::atomic<bool> deny_peer_pidfd{false};
std::atomic<int> spoof_peer_pid{0};
extern "C" int __real_getsockopt(int, int, int, void*, socklen_t*);
extern "C" int __wrap_getsockopt(int fd, int level, int option, void* value, socklen_t* size) {
#ifdef SO_PEERPIDFD
  if (option == SO_PEERPIDFD && deny_peer_pidfd.load()) { errno = ENOPROTOOPT; return -1; }
#endif
  const int result = __real_getsockopt(fd, level, option, value, size);
  if (!result && level == SOL_SOCKET && option == SO_PEERCRED && spoof_peer_pid.load() > 0)
    static_cast<ucred*>(value)->pid = spoof_peer_pid.load();
  return result;
}

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using mozc::OwnedChild;
using mozc::fcitx5::PreparationWorker;
struct Result {
  std::atomic<std::shared_ptr<OwnedChild>> child;
  std::atomic<bool> attempted{false}, duplicate_rejected{false};
};
bool PumpUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout,
               int* ticks = nullptr) {
  const auto end = Clock::now()+timeout;
  while (!condition()) {
    if (Clock::now() >= end) return false;
    if (ticks) ++*ticks;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}
int Connect(int pid) {
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  const std::string name = "tmp/.mozkey.fixture_"+std::to_string(pid);
  memcpy(address.sun_path+1, name.data(), name.size());
  if (connect(fd, reinterpret_cast<sockaddr*>(&address),
              offsetof(sockaddr_un, sun_path)+1+name.size())) { close(fd); return -1; }
  return fd;
}
bool PeerIdentityAvailable() {
#ifdef SO_PEERPIDFD
  const int owned = pidfd_open(getpid(), 0);
  int pair[2];
  if (owned < 0) return false;
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) { close(owned); return false; }
  int peer = -1; socklen_t size = sizeof(peer); struct statfs fs{};
  const bool available = !fstatfs(owned, &fs) && fs.f_type == 0x50494446 &&
      !getsockopt(pair[0], SOL_SOCKET, SO_PEERPIDFD, &peer, &size);
  if (peer >= 0) close(peer);
  close(pair[0]); close(pair[1]); close(owned);
  return available;
#else
  return false;
#endif
}
bool RejectSpoofedPeer(const std::shared_ptr<OwnedChild>& child) {
  const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) return false;
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  const std::string name = "tmp/.mozkey.fixture_"+std::to_string(getpid());
  memcpy(address.sun_path+1, name.data(), name.size());
  const bool listening = !bind(listener, reinterpret_cast<sockaddr*>(&address),
      offsetof(sockaddr_un, sun_path)+1+name.size()) && !listen(listener, 1);
  const int client = listening ? Connect(getpid()) : -1;
  const int accepted = client >= 0 ? accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK) : -1;
  bool rejected = false;
  if (client >= 0 && accepted >= 0) {
    // Simulate a reused PID number. Kernel SO_PEERPIDFD still identifies this
    // probe rather than the owned child; no shutdown bytes may cross it.
    spoof_peer_pid.store(child->pid());
    rejected = !child->ConfigureShutdownFromVerifiedConnection(client, "quit\n");
    spoof_peer_pid.store(0);
    char byte;
    rejected = recv(accepted, &byte, 1, MSG_DONTWAIT) < 0 && rejected;
  }
  if (client >= 0) close(client);
  if (accepted >= 0) close(accepted);
  close(listener);
  return rejected;
}
}

int main(int argc, char* argv[]) {
  if (argc != 4) return 2;
  const std::string mode = argv[1], executable = argv[2], profile = argv[3];
  const bool peer_identity = PeerIdentityAvailable();
  if (mode == "auto-reap") signal(SIGCHLD, SIG_IGN);  // Disposable probe only.
  const bool rejected = mode == "bad-env" || mode == "symlink" || mode == "public-image" || mode == "lease-busy";
  auto flag = std::make_shared<std::atomic<bool>>(false);
  auto result = std::make_shared<Result>();
  OwnedChild::Spec spec{executable, profile, {}, {"LANG=C"}};
  if (mode == "bad-env") spec.environment.push_back("LD_PRELOAD=/must-not-load");
  if (mode == "ignore-term" || mode == "retained-worker") spec.arguments.push_back("--ignore-term");
  if (mode == "startup350") spec.arguments.push_back("--delay350");
  if (mode == "graceful" || mode == "legacy-peer") spec.arguments.push_back("--socket");
  if (mode == "stall-shutdown") spec.arguments.push_back("--stall-shutdown");
  const bool exit_order = mode == "exit-hold" || mode == "exit-no-hold";
  if (exit_order) spec.arguments.push_back("--shutdown-delay150");
  const auto beginning = Clock::now();
  auto worker = PreparationWorker::Start([spec, flag, result, mode](const std::atomic<bool>&) {
    if (mode == "cancel-before-spawn350") std::this_thread::sleep_for(350ms);
    auto child = OwnedChild::SpawnOnWorker(spec, flag);
    if (mode == "duplicate" && child) {
      auto other_flag = std::make_shared<std::atomic<bool>>(false);
      auto other = OwnedChild::SpawnOnWorker(spec, other_flag);
      result->duplicate_rejected.store(!other && !other_flag->load() && !flag->load());
    }
    result->child.store(child);
    result->attempted.store(true);
    if (mode == "retained-worker") std::this_thread::sleep_for(3500ms);
    return -1;
  });
  if (!worker) return 3;
  const auto enqueue_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-beginning).count();
  int ticks = 0;
  if (mode == "cancel-before-spawn350") { flag->store(true); OwnedChild::NotifyCancellation(); }
  bool ok = PumpUntil([&] { return result->attempted.load(); }, 2s, &ticks);
  auto child = result->child.load();
  if (child) fprintf(stderr, "owned child admission mode=%s pid=%d error=%d finished=%d\n",
                     mode.c_str(), child->pid(), child->error(), child->finished());
  if (!child && mode != "cancel-before-spawn350") ok = false;
  if (mode == "cancel-before-spawn350") ok = !child && ticks > 10 && ok;
  bool owner_collected = false, busy_during_reap = false;
  long cancel_us = -1;
  if (child && rejected) {
    ok = PumpUntil([&] { return child->finished(); }, 500ms) && ok;
    ok = child->error() != 0 && child->pid() < 0 && !child->reaped_by_owner() && ok;
  } else if (child) {
    ok = PumpUntil([&] { return access((profile+"/.fixture-ready").c_str(), F_OK) == 0; }, 2s, &ticks) && ok;
    if (mode == "hold" || mode == "parent-death") {
      const int output = open((profile+"/.fixture-child-pid").c_str(),
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      const std::string pid = std::to_string(child->pid());
      if (output < 0 || write(output, pid.data(), pid.size()) != static_cast<ssize_t>(pid.size())) ok = false;
      if (output >= 0) close(output);
      if (mode == "parent-death") {
        // Test-only abrupt host exit; deliberately bypass root destructors.
        // The driver is a subreaper and collects only this fixture child.
        _exit(ok ? 0 : 1);
      }
      ok = PumpUntil([&] { return access((profile+"/.fixture-release").c_str(), F_OK) == 0; }, 5s, &ticks) && ok;
    }
    if (mode == "graceful" || mode == "stall-shutdown" || mode == "legacy-peer" || exit_order) {
      const int fd = Connect(child->pid());
      if (mode == "legacy-peer") deny_peer_pidfd.store(true);
      const bool configured = fd >= 0 && child->ConfigureShutdownFromVerifiedConnection(fd, "quit\n");
      ok = fd >= 0 && configured == (peer_identity && mode != "legacy-peer") && ok;
      deny_peer_pidfd.store(false);
      if (!peer_identity) printf("::notice::Kernel peer pidfd/pidfs identity unavailable; graceful request NOT EXECUTED, PID-only fallback rejected\n");
      if (fd >= 0) close(fd);
    }
    if (exit_order) {
      const auto begin = Clock::now();
      std::unique_ptr<PreparationWorker::ExitHold> hold;
      if (mode == "exit-hold") {
        hold = PreparationWorker::HoldProcessExit();
        ok = bool(hold) && !PreparationWorker::HoldProcessExit() && ok;
      }
      auto final_done = std::make_shared<std::atomic<bool>>(false);
      ok = PreparationWorker::SetFinalJob([final_done](const auto&) {
        final_done->store(true); return -1;
      }) && ok;
      ok = PumpUntil([&] { return final_done->load(); }, 50ms) && ok;
      bool creator_joined_early = false;
      if (mode == "exit-no-hold") creator_joined_early = PreparationWorker::WaitForProcessExit(50ms);
      child->Cancel();
      const auto remaining = [&] {
        return 390ms-std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin);
      };
      const bool collected = OwnedChild::WaitForProcessExit(remaining());
      hold.reset();
      const bool creator_joined = PreparationWorker::WaitForProcessExit(remaining());
      const bool marker = access((profile+"/.fixture-shutdown").c_str(), F_OK) == 0;
      ok = collected && creator_joined && final_done->load() && child->reaped_by_owner() && ok;
      ok = marker == (peer_identity && mode == "exit-hold") && ok;
      if (mode == "exit-no-hold") ok = creator_joined_early && ok;
      const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();
      printf("OWNED_CHILD_EXIT_ORDER mode=%s pass=%d final_done=%d child_join=%d creator_join=%d early_creator_join=%d graceful_marker=%d peer_identity=%d total_us=%ld\n",
             mode.c_str(), ok, final_done->load(), collected, creator_joined,
             creator_joined_early, marker, peer_identity, duration);
      return ok ? 0 : 1;
    }
    if (mode == "spoof-peer") ok = RejectSpoofedPeer(child) && ok;
    if (mode == "wrong-peer") {
      int pair[2];
      if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair)) ok = false;
      else {
        ok = !child->ConfigureShutdownFromVerifiedConnection(pair[0], "quit\n") && ok;
        char byte;
        ok = recv(pair[1], &byte, 1, MSG_DONTWAIT) < 0 && ok;
        close(pair[0]); close(pair[1]);
      }
    }
    const auto cancel = Clock::now();
    if (mode == "drop-handle") {
      // Observe via our own duplicated process identity, without keeping an
      // OwnedChild wrapper alive and thereby masking destructor cancellation.
      const int observed = pidfd_open(child->pid(), 0);
      ok = observed >= 0 && ok;
      ok = PumpUntil([] { return !PreparationWorker::Busy(); }, 500ms) && ok;
      result->child.store({}); child.reset();
      ok = PumpUntil([] { return !OwnedChild::Busy(); }, 390ms, &ticks) && ok;
      if (observed >= 0) {
        pollfd event{observed, POLLIN, 0}; siginfo_t info{};
        const bool signalled = poll(&event, 1, 0) == 1 && (event.revents & POLLIN);
        const int collected = waitid(P_PIDFD, observed, &info, WEXITED | WNOHANG);
        ok = signalled && collected < 0 && errno == ECHILD && ok;
        close(observed);
      }
      owner_collected = ok;
    } else if (mode == "retained-worker") {
      // The preparation job retains the handle after the addon-side scope has
      // been logically cancelled. Its sleeping worker cannot drive collection.
      flag->store(true); OwnedChild::NotifyCancellation(); worker->Cancel();
    } else child->Cancel();
    const bool completed = !child || PumpUntil([&] { return child->finished(); }, 390ms, &ticks);
    cancel_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-cancel).count();
    if (child) owner_collected = child->reaped_by_owner();
    busy_during_reap = PreparationWorker::Busy();
    ok = completed && (mode == "auto-reap" ? !owner_collected : owner_collected) && ok;
    if (mode == "retained-worker") ok = busy_during_reap && ok;
    if (mode == "graceful") ok = (access((profile+"/.fixture-shutdown").c_str(), F_OK) == 0) == peer_identity && ok;
    if (mode == "legacy-peer") ok = access((profile+"/.fixture-shutdown").c_str(), F_OK) != 0 && ok;
    if (mode == "duplicate") ok = result->duplicate_rejected.load() && ok;
  }
  flag->store(true); OwnedChild::NotifyCancellation();
  // Allow the deliberately sleeping fixture job to return before testing the
  // actual bounded process-exit join. This is not a 400ms total-exit proof.
  ok = PumpUntil([] { return !PreparationWorker::Busy() && !OwnedChild::Busy(); }, 4s, &ticks) && ok;
  result->child.store({}); child.reset(); worker.reset();
  const auto end = Clock::now()+390ms;
  const bool preparation_joined = PreparationWorker::WaitForProcessExit(390ms);
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now());
  const bool child_joined = OwnedChild::WaitForProcessExit(remaining);
  ok = preparation_joined && child_joined && ok;
  printf("OWNED_CHILD_PROBE mode=%s pass=%d enqueue_us=%ld cancel_reap_us=%ld ticks=%d owned_reap=%d worker_still_busy=%d prep_join=%d child_join=%d peer_identity=%d\n",
         mode.c_str(), ok, enqueue_us, cancel_us, ticks, owner_collected, busy_during_reap,
         preparation_joined, child_joined, peer_identity);
  return ok ? 0 : 1;
}
