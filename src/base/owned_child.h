// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_BASE_OWNED_CHILD_H_
#define MOZC_BASE_OWNED_CHILD_H_
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace mozc {
// Linux/glibc process ownership, without an InputContext, event loop, callbacks
// or process-wide SIGCHLD handler. The embedding addon must pin its code.
class OwnedChild {
 public:
  struct Spec {
    std::string executable, profile;
    std::vector<std::string> arguments, environment;
    // Initial launch only: the child must atomically reserve a missing session
    // key before any product I/O. An existing regular/symlink key fails closed.
    // Reservation failure never repairs, truncates or removes existing assets.
    bool fresh_key_only = false;
  };
  // Worker only: validation and pidfd_spawn may block on filesystem/exec I/O.
  // ONE process-root reservation, plus an inherited private OFD launch lease,
  // prevents duplicate launches across wrapper destruction and host restarts.
  static std::shared_ptr<OwnedChild> SpawnOnWorker(
      Spec spec, std::shared_ptr<std::atomic<bool>> cancelled);
  ~OwnedChild();
  void Cancel(); // Logical cancellation + eventfd wake, never waits.
  static void NotifyCancellation();
  bool finished() const;
  int error() const;
  int pid() const;
  bool reaped_by_owner() const;
  // Worker only; fd must already have passed the product/profile/exec/version
  // checks. Cache only an owned child's address and an administrative request.
  // The reaper rechecks UID/PID before sending; it never performs filesystem I/O.
  bool ConfigureShutdownFromVerifiedConnection(int fd, const std::string& request);
  static bool Busy();
  // Actual process termination only. Seal admission and join the native
  // supervisor within the caller's REMAINING budget, clamped to 390ms.
  // False means resources remain owned here; never claim they were reclaimed.
  static bool WaitForProcessExit(std::chrono::milliseconds budget);
 private:
  struct State;
  struct Root;
  static Root& ProcessRoot();
  explicit OwnedChild(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};

// Called by the dedicated child before InitMozc/profile/log I/O. An ordinary
// invocation returns true with an empty profile. An owned invocation verifies
// inherited image/launch descriptors and parent identity, installs PDEATHSIG,
// and returns the explicitly validated private profile. Validated bootstrap
// arguments are removed before the ordinary product flags parser runs.
// No parent policy changes. A false result must terminate before product I/O.
bool InitializeOwnedChild(int* argc, char* argv[], std::string* profile);
}  // namespace mozc
#endif
