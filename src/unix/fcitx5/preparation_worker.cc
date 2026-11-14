// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/preparation_worker.h"

#include <sys/eventfd.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <condition_variable>
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <thread>
#include <utility>

namespace mozc::fcitx5 {
struct PreparationWorker::State {
  explicit State(Job work) : job(std::move(work)) {}
  ~State() {
    const int result = fd.exchange(-1);
    if (result >= 0) close(result);
    if (notify >= 0) close(notify);
  }
  Job job;
  int notify = -1;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> done{false};
  std::atomic<int> fd{-1};
};
struct PreparationWorker::Root {
  std::mutex mutex;
  std::condition_variable ready;
  std::shared_ptr<State> slot;
  bool pending = false;
  std::atomic<bool> sealed{false};
  pthread_t worker{};  // Root owns this joinable Linux thread until tryjoin succeeds.
  std::atomic<bool> started{false};
  Job final_job;
  std::atomic<bool> final_cancelled{false};
  bool EnsureThread() {
    if (started.load()) return true;
    const int error = pthread_create(&worker, nullptr, [](void* pointer) -> void* {
      static_cast<Root*>(pointer)->Run(); return nullptr;
    }, this);
    if (error) return false;
    started.store(true); return true;
  }
  void Run() {
      for (;;) {
        std::shared_ptr<State> state;
        Job final;
        {
          std::unique_lock lock(mutex);
          ready.wait(lock, [this] { return pending || sealed.load(); });
          if (!pending) {
            if (!final_job) return;  // Sealed and drained: includes TLS exit.
            final = std::move(final_job); final_job = {};
          } else { pending = false; state = slot; }
        }
        if (final) {
          try { if (!final_cancelled.load()) { const int fd = final(final_cancelled); if (fd >= 0) close(fd); } }
          catch (...) {}  // Result owner remains false on failure.
          continue;
        }
        int fd = -1;
        try {
          if (!state->cancelled.load()) fd = state->job(state->cancelled);
        } catch (...) {
          // Never unwind through the host's runtime or Fcitx callbacks.
        }
        state->fd.store(fd);
        if (state->cancelled.load()) {
          const int unused = state->fd.exchange(-1);
          if (unused >= 0) close(unused);
        }
        state->job = {};
        {
          std::lock_guard lock(mutex);
          state->done.store(true);
          // A live client owns its completed result. If it was cancelled or
          // unloaded, the worker's local reference performs final reclamation.
          slot.reset();
        }
        uint64_t one = 1;
        ssize_t bytes;
        do { bytes = write(state->notify, &one, sizeof(one)); }
        while (bytes < 0 && errno == EINTR);
      }
  }
};
PreparationWorker::Root& PreparationWorker::ProcessRoot() {
  // Intentionally process-lived, including across addon unload/reload. A
  // bounded single pending job is owned here, not abandoned on detach.
  static Root* root = new Root;
  return *root;
}
PreparationWorker::PreparationWorker(std::shared_ptr<State> state)
    : state_(std::move(state)) {}
std::unique_ptr<PreparationWorker> PreparationWorker::Start(Job job) {
  auto& root = ProcessRoot();
  std::unique_lock lock(root.mutex, std::try_to_lock);
  if (!lock.owns_lock() || root.sealed.load()) return nullptr;
  if (root.slot && !root.slot->done.load()) return nullptr;
  root.slot.reset();
  auto state = std::make_shared<State>(std::move(job));
  state->notify = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (state->notify < 0) return nullptr;
  // Allocate the client before starting: exceptions must not orphan a job.
  auto client = std::unique_ptr<PreparationWorker>(new PreparationWorker(state));
  root.slot = state;
  if (!root.EnsureThread()) { root.slot.reset(); return nullptr; }
  root.pending = true;
  root.ready.notify_one();
  return client;
}
PreparationWorker::~PreparationWorker() { Cancel(); }
int PreparationWorker::notification_fd() const { return state_->notify; }
bool PreparationWorker::finished() const { return state_->done.load(); }
int PreparationWorker::TakeFd() {
  if (!finished() || state_->cancelled.load()) return -1;
  return state_->fd.exchange(-1);
}
void PreparationWorker::Cancel() {
  state_->cancelled.store(true);
  const int unused = state_->fd.exchange(-1);
  if (unused >= 0) close(unused);
}
bool PreparationWorker::Busy() {
  auto& root = ProcessRoot();
  std::unique_lock lock(root.mutex, std::try_to_lock);
  if (!lock.owns_lock()) return true;
  if (root.slot && !root.slot->done.load()) return true;
  root.slot.reset();
  return false;
}
bool PreparationWorker::SetFinalJob(Job job) {
  auto& root = ProcessRoot();
  std::unique_lock lock(root.mutex, std::try_to_lock);
  if (!lock.owns_lock() || root.sealed.load() || !job) return false;
  root.sealed.store(true);
  if (root.slot) root.slot->cancelled.store(true);
  root.final_job = std::move(job);
  if (!root.EnsureThread()) return false;
  root.ready.notify_one(); return true;
}
bool PreparationWorker::WaitForProcessExit(std::chrono::milliseconds budget) {
  using namespace std::chrono_literals;
  // Keep 10ms of the user's 400ms allowance for dispatch/unwinding.
  budget = std::clamp(budget, 0ms, 390ms);
  const auto end = std::chrono::steady_clock::now() + budget;
  auto& root = ProcessRoot();
  root.sealed.store(true);
  std::unique_lock lock(root.mutex, std::try_to_lock);
  if (!lock.owns_lock()) { root.final_cancelled.store(true); return false; }
  if (root.slot) root.slot->cancelled.store(true);
  lock.unlock();
  root.ready.notify_one();
  if (!root.started.load()) return true;
  // Job completion is not thread completion: include callback-local destruction
  // and TLS destructors. No unbounded std::thread::join after an early signal.
  for (;;) {
    const int error = pthread_tryjoin_np(root.worker, nullptr);
    if (!error) { root.started.store(false); return true; }
    if (error != EBUSY || std::chrono::steady_clock::now() >= end) {
      root.final_cancelled.store(true); return false;
    }
    const auto remaining = end - std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::min(remaining,
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(1ms)));
  }
}
}  // namespace mozc::fcitx5
