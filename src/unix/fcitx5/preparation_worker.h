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
// At process exit the kernel reclaims it; there is no static destructor/join.
// This candidate is not yet connected to the product's client/launcher.
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
  // Only for actual process termination, never focus/switch/addon unload.
  // Uses at most 390ms of a requested 400ms, reserving 10ms for unwinding.
  // A false result leaves ownership at the process root; never joins a syscall.
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
