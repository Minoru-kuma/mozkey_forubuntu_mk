// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/input_pipeline.h"
#include <algorithm>
#include <utility>

namespace mozc::fcitx5 {
namespace {
auto& Records() {
  // Same loop-thread/process lifetime contract as the addon code pin.
  static auto* records = new std::vector<std::shared_ptr<RetainedInput>>;
  return *records;
}
}
std::shared_ptr<RetainedInput> InputRetention::Reserve() {
  if (Records().size() >= 32) return {};
  auto record = std::make_shared<RetainedInput>();
  Records().push_back(record);
  return record;
}
void InputRetention::ReleaseEmpty(const std::shared_ptr<RetainedInput>& record) {
  if (record && record->pending.empty() && record->preedit.empty() && !record->delivering) {
    auto& records = Records();
    records.erase(std::remove(records.begin(), records.end(), record), records.end());
  }
}
std::vector<std::shared_ptr<const RetainedInput>> InputRetention::Snapshot() {
  return {Records().begin(), Records().end()};
}
InputPipeline::InputPipeline(fcitx::EventLoop& loop, std::shared_ptr<RetainedInput> record,
                             Send send, std::function<void()> cancel,
                             std::function<bool()> valid, Deliver deliver)
    : loop_(loop), record_(std::move(record)), send_(std::move(send)),
      cancel_(std::move(cancel)), valid_(std::move(valid)), deliver_(std::move(deliver)) {}
InputPipeline::~InputPipeline() {
  lifetime_.reset();
  Hold("context-or-addon-destroyed");
  InputRetention::ReleaseEmpty(record_);
}
void InputPipeline::Hold(std::string reason) {
  ++token_; defer_.reset(); active_ = false;
  record_->suspended = true;
  record_->reason = std::move(reason);
  cancel_(); // AsyncSession cancel closes sources/FDs; never joins or sends.
}
bool InputPipeline::Submit(InputFrame frame) {
  if (record_->suspended) return false;
  if ((frame.request && frame.request->ByteSizeLong() > 1024 * 1024) ||
      frame.selection_snapshot.size() > 65536) { Hold("input-too-large"); return false; }
  if (record_->pending.size() >= 16) { Hold("input-queue-full"); return false; }
  record_->pending.push_back(std::move(frame));
  if (!active_) { active_ = true; Schedule(); }
  return true;
}
void InputPipeline::Schedule() {
  defer_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Next(); return true; });
  defer_->setOneShot();
}
void InputPipeline::SetReady(bool ready) {
  ready_ = ready;
  if (ready && !active_ && !record_->pending.empty() && !record_->suspended) {
    active_ = true; Schedule();
  }
}
void InputPipeline::Next() {
  if (!ready_) { active_ = false; return; }
  if (record_->suspended || record_->pending.empty()) { active_ = false; return; }
  if (!valid_()) { Hold("focus-or-epoch-changed"); return; }
  if (!record_->pending.front().translated && translate_) {
    translate_(record_->pending.front());
    record_->pending.front().translated = true;
  }
  const auto token = ++token_;
  auto complete = [this, token, weak = std::weak_ptr<int>(lifetime_)](bool ok, commands::Output output) {
    if (weak.expired()) return;
    if (token != token_ || record_->suspended) return;
    if (!ok || !valid_()) { Hold(ok ? "focus-or-epoch-changed" : "rpc-failed"); return; }
    ++token_; // Duplicate callbacks cannot deliver/commit twice.
    auto frame = std::move(record_->pending.front()); record_->pending.pop_front();
    const bool forward = frame.physical && (!frame.request || !output.consumed());
    record_->delivering = frame;
    record_->outcome = output;
    auto record = record_;
    auto deliver = deliver_;
    Schedule();
    // No member access after delivery: a frontend may destroy its IC.
    const bool accepted = deliver(std::move(output), forward ? std::optional<InputFrame>(std::move(frame)) : std::nullopt);
    if (!accepted && !weak.expired()) Hold("delivery-interrupted");
    if (accepted && !record->suspended) {
      record->delivering.reset(); record->outcome.reset();
    }
  };
  if (record_->pending.front().request) {
    if (!send_(*record_->pending.front().request, std::move(complete))) Hold("submission-rejected");
  } else {
    commands::Output output;
    complete(true, std::move(output));
  }
}
}  // namespace mozc::fcitx5
