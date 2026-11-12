// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/callback_queue.h"
#include <limits>
#include <utility>

namespace mozc::fcitx5 {
void CallbackQueue::Invalidate() {
  ++serial_;
  timer_.reset();
  chain_ = 0;
}
bool CallbackQueue::Schedule(const commands::Output& output, Valid valid, Send send) {
  if (!output.has_callback() || !output.callback().has_session_command() ||
      !output.callback().session_command().has_type() || chain_ >= 16) return false;
  const auto& callback = output.callback();
  const auto& command = callback.session_command();
  switch (command.type()) {
    case commands::SessionCommand::UNDO:
    case commands::SessionCommand::CONVERT_REVERSE:
    case commands::SessionCommand::APPLY_LIVE_CONVERSION:
    case commands::SessionCommand::RECONVERT_SELECTION_OR_INSERT_SPACE:
    case commands::SessionCommand::RESUME_COMMAND_SEQUENCE: break;
    default: return false;
  }
  const uint64_t now = fcitx::now(CLOCK_MONOTONIC);
  const uint64_t delay = uint64_t(callback.delay_millisec()) * 1000;
  if (delay > std::numeric_limits<uint64_t>::max() - now) return false;
  ++chain_;
  const auto token = ++serial_;
  // Copy the complete protobuf, including all generation/reading fields.
  timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, now + delay, 1,
      [this, token, command, valid = std::move(valid), send = std::move(send)]
      (fcitx::EventSourceTime*, uint64_t) {
        if (token == serial_ && valid()) {
          ++serial_;  // Mark complete before send can schedule a continuation.
          // Scheduling a continuation replaces this timer and its closure.
          // Keep dispatch data on the stack across such replacement.
          auto dispatch = send;
          auto request = command;
          dispatch(request);
        }
        return true;
      });
  timer_->setOneShot();
  return true;  // consumed=false does not suppress a valid callback.
}
}  // namespace mozc::fcitx5
