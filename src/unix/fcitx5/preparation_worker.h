// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_PREPARATION_WORKER_H_
#define MOZKEY_UNIX_FCITX5_PREPARATION_WORKER_H_

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace mozc::fcitx5 {
// A process-wide, single-flight preparation gate. The job owns all its data;
// it must never capture an addon, InputContext, event loop or borrowed FD.
// Linux Fcitx's DefaultHint/PreventUnloadHint must pin this module's code.
// Cancellation is logical: a blocked syscall is not pthread_cancel'ed.
// The process root retains a blocked job and refuses new jobs across reloads.
// Normal process shutdown seals and tryjoins within a bounded budget. A false
// result means the blocked job/TLS has NOT been reclaimed; runtime shutdown
// safety is not established. No static destructor or unbounded join is used.
class PreparationWorker {
 public:
  // Return a freshly owned validated FD, or -1. No UI callbacks on the worker.
  using Job = std::function<int(const std::atomic<bool>& cancelled)>;
  static std::unique_ptr<PreparationWorker> Start(Job job);
  ~PreparationWorker();
  PreparationWorker(const PreparationWorker&) = delete;
  PreparationWorker& operator=(const PreparationWorker&) = delete;

  int notification_fd() const;
  bool finished() const;
  int TakeFd();  // Caller owns the result; still check IC/epoch before applying.
  void Cancel();  // Never waits; idempotent, also closes an unclaimed result.
  static bool Busy();
  // Actual process shutdown only. Seal admission and own ONE final value-only
  // job after any in-flight job. No UI/IC/loop capture; no new helper/thread pool.
  static bool SetFinalJob(Job job);
  // Only for actual process termination, never focus/switch/addon unload.
  // Uses at most 390ms of a requested 400ms, reserving 10ms for unwinding.
  // True means the native thread, including TLS destructors, was joined.
  // False leaves ownership at the process root; never waits beyond the budget.
  static bool WaitForProcessExit(std::chrono::milliseconds budget);

 private:
  struct State;
  struct Root;
  static Root& ProcessRoot();
  explicit PreparationWorker(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};
}  // namespace mozc::fcitx5
#endif
