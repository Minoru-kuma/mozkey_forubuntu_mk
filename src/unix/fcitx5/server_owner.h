// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_SERVER_OWNER_H_
#define MOZKEY_UNIX_FCITX5_SERVER_OWNER_H_
#include <memory>
#include <string>
#include <vector>
#include "ipc/linux_deadline.h"
#include "unix/fcitx5/preparation_worker.h"

namespace mozc::fcitx5 {
// Addon-scoped ownership, with value-only jobs that may outlive the addon.
// Connects to a fully verified existing server without adopting/killing it.
// Only a missing key BEFORE any verified connection permits ONE fresh launch;
// existing invalid/stale keys
// fail closed without migration, repair, restart or automatic input replay.
class ServerOwner {
 public:
  explicit ServerOwner(LinuxDeadlineEndpoint endpoint = {},
                       std::vector<std::string> child_arguments = {});
  ~ServerOwner();
  // Administrative save jobs must set allow_launch=false: saving Fcitx config
  // must not cold-start a conversion server that has never been used.
  PreparationWorker::Job PreparationJob(bool allow_launch = true) const;
  void Cancel();  // Never waits. Focus/IC cancellation must not call this.
  bool launch_attempted() const;
  bool owns_process() const;
  bool graceful_shutdown_ready() const;
 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace mozc::fcitx5
#endif
