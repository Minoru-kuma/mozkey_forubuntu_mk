// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZC_IPC_LINUX_DEADLINE_H_
#define MOZC_IPC_LINUX_DEADLINE_H_

#if defined(__linux__) && !defined(__ANDROID__)
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ipc/ipc.h"

namespace mozc {
// No wall clock, global timeout, thread, or implicit profile lookup.
class LinuxDeadline {
 public:
  using Time = std::chrono::steady_clock::time_point;
  using Clock = std::function<Time()>;
  static Time Now() { return std::chrono::steady_clock::now(); }
  LinuxDeadline(Clock clock, Time end) : clock_(std::move(clock)), end_(end) {}
  static LinuxDeadline After(Clock clock, std::chrono::milliseconds budget) {
    const auto start = clock();
    return LinuxDeadline(std::move(clock), start + budget);
  }
  bool Expired() const { return clock_() >= end_; }
  int PollMilliseconds() const {
    const auto remaining = end_ - clock_();
    if (remaining <= Time::duration::zero()) return 0;
    // Round up only the syscall wait. Always recheck the absolute deadline.
    return static_cast<int>(std::min<int64_t>(
        std::chrono::ceil<std::chrono::milliseconds>(remaining).count(), 100));
  }
  Time end() const { return end_; }
  const Clock& clock() const { return clock_; }
 private:
  Clock clock_;
  Time end_;
};

struct LinuxDeadlineEndpoint {
  // Explicit, private profile and canonical executable. No HOME/PATH fallback.
  std::string profile;
  std::string server_path;
  std::string channel = "session";
};

std::unique_ptr<IPCClientInterface> NewLinuxDeadlineIPCClient(
    const LinuxDeadlineEndpoint& endpoint, const LinuxDeadline& deadline);

// One shared instance per addon. BeginClose() MUST precede destruction of a
// batch of contexts; it seals the group and gives the entire batch 100ms.
// Calls are serialized on the owner event-loop thread, never concurrently.
class LinuxFcitxCloseGroup {
 public:
  explicit LinuxFcitxCloseGroup(LinuxDeadline::Clock clock = LinuxDeadline::Now)
      : clock_(std::move(clock)) {}
  void BeginClose() {
    if (!deadline_) deadline_ = LinuxDeadline::After(clock_, std::chrono::milliseconds(100));
  }
  bool closing() const { return deadline_.has_value(); }
  const std::optional<LinuxDeadline>& deadline() const { return deadline_; }
  const LinuxDeadline::Clock& clock() const { return clock_; }
 private:
  LinuxDeadline::Clock clock_;
  std::optional<LinuxDeadline> deadline_;
};
}  // namespace mozc
#endif
#endif  // MOZC_IPC_LINUX_DEADLINE_H_
