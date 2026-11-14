// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_INPUT_PIPELINE_H_
#define MOZKEY_UNIX_FCITX5_INPUT_PIPELINE_H_
#include <fcitx-utils/key.h>
#include <deque>
#include <map>
#include <optional>
#include <vector>
#include "unix/fcitx5/async_session.h"

namespace mozc::fcitx5 {
struct InputFrame {
  enum class Compose { None, Pending, Complete, Cancelled, Error } compose = Compose::None;
  std::string composed_text;
  fcitx::Key raw;
  bool release = false;
  uint32_t time = 0;
  std::optional<commands::Input> request;
  bool physical = true, translated = false, japanese_layout = false;
  uint64_t generation = 0;
  bool surrounding_valid = false;
  std::string selection_snapshot;
  unsigned cursor = 0, anchor = 0;
  int32_t selected_length = 0;
};
struct RetainedInput {
  std::deque<InputFrame> pending, composing;
  std::string preedit, reason;
  std::optional<InputFrame> delivering;
  std::optional<commands::Output> outcome;
  bool suspended = false;
};
// Process-lived memory recovery only. No log, clipboard or cross-IME delivery.
// Survives addon/IC destruction; OS-process-exit durability remains unproven.
class InputRetention {
 public:
  static std::shared_ptr<RetainedInput> Reserve();
  static void ReleaseEmpty(const std::shared_ptr<RetainedInput>& record);
  static std::vector<std::shared_ptr<const RetainedInput>> Snapshot();
};
class InputPipeline {
 public:
  using Send = std::function<bool(commands::Input, AsyncSession::Done)>;
  // One delivery hook: it must check IC/watch/epoch between its UI effects.
  using Deliver = std::function<bool(commands::Output, std::optional<InputFrame>)>;
  InputPipeline(fcitx::EventLoop& loop, std::shared_ptr<RetainedInput> record,
                Send send, std::function<void()> cancel,
                std::function<bool()> valid, Deliver deliver);
  ~InputPipeline();
  bool Submit(InputFrame frame); // true means original KeyEvent must be consumed.
  void SetReady(bool ready);
  void SetTranslator(std::function<void(InputFrame&)> translate) { translate_ = std::move(translate); }
  void Hold(std::string reason);
  bool suspended() const { return record_->suspended; }
 private:
  void Schedule();
  void Next();
  bool ForwardRelease(const InputFrame& frame) const;
  void RecordPhysicalDelivery(const InputFrame& frame, bool raw_forwarded);
  fcitx::EventLoop& loop_;
  std::shared_ptr<RetainedInput> record_;
  Send send_;
  std::function<void()> cancel_;
  std::function<bool()> valid_;
  Deliver deliver_;
  std::function<void(InputFrame&)> translate_;
  std::unique_ptr<fcitx::EventSource> defer_;
  bool active_ = false, ready_ = true;
  uint64_t token_ = 0;
  // Per activation: a release balances any raw down already sent to the app.
  // A missing entry may have started before activation, so it is forwarded.
  std::map<uint64_t, bool> forwarded_down_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};
}  // namespace mozc::fcitx5
#endif
