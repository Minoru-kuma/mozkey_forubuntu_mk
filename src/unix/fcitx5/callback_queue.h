// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_CALLBACK_QUEUE_H_
#define MOZKEY_UNIX_FCITX5_CALLBACK_QUEUE_H_
#include <fcitx-utils/event.h>
#include <cstdint>
#include <functional>
#include <memory>
#include "protocol/commands.pb.h"

namespace mozc::fcitx5 {
// Loop-thread-only scheduler. The sender must be nonblocking in the finished
// addon; this class only owns scheduling, full command copies and cancellation.
class CallbackQueue {
 public:
  explicit CallbackQueue(fcitx::EventLoop& loop) : loop_(loop) {}
  using Valid = std::function<bool()>;
  using Send = std::function<void(const commands::SessionCommand&)>;
  bool Schedule(const commands::Output& output, Valid valid, Send send);
  void Invalidate();  // New input/focus/reset/session/config/IME/owner change.
 private:
  fcitx::EventLoop& loop_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  uint64_t serial_ = 0;
  unsigned chain_ = 0;
};
}  // namespace mozc::fcitx5
#endif
