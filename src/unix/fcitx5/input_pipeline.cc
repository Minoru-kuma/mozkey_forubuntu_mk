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
uint64_t PhysicalKey(const InputFrame& frame) {
  return frame.raw.code() ? (uint64_t(frame.raw.code()) << 1) | 1 : uint64_t(frame.raw.sym()) << 1;
}
}
std::shared_ptr<RetainedInput> InputRetention::Reserve() {
  if (Records().size() >= 32) return {};
  auto record = std::make_shared<RetainedInput>();
  Records().push_back(record);
  return record;
}
void InputRetention::ReleaseEmpty(const std::shared_ptr<RetainedInput>& record) {
  if (record && record->pending.empty() && record->composing.empty() && record->preedit.empty() && !record->delivering && !record->outcome) {
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
bool InputPipeline::ForwardRelease(const InputFrame& frame) const {
  const auto down = forwarded_down_.find(PhysicalKey(frame));
  return down == forwarded_down_.end() || down->second;
}
void InputPipeline::RecordPhysicalDelivery(const InputFrame& frame, bool raw_forwarded) {
  if (!frame.physical) return;
  const auto key = PhysicalKey(frame);
  if (frame.release) forwarded_down_.erase(key);
  else forwarded_down_[key] = forwarded_down_[key] || raw_forwarded;
}
void InputPipeline::Next() {
  if (!ready_) { active_ = false; return; }
  if (record_->suspended || record_->pending.empty()) { active_ = false; return; }
  auto weak = std::weak_ptr<int>(lifetime_);
  auto valid = valid_;
  const bool eligible = valid();
  if (weak.expired() || record_->suspended) return;
  if (!eligible) { Hold("focus-or-epoch-changed"); return; }
  const auto& physical = record_->pending.front();
  if (physical.physical && !physical.release && forwarded_down_.size() >= 256 &&
      !forwarded_down_.contains(PhysicalKey(physical))) { Hold("physical-key-ledger-full"); return; }
  if (!record_->pending.front().translated && translate_) {
    auto translate = translate_;
    auto translated = record_->pending.front();
    translate(translated);
    if (weak.expired()) return;
    if (record_->suspended) return;
    record_->pending.front() = std::move(translated);
    record_->pending.front().translated = true;
  }
  const auto compose = record_->pending.front().compose;
  if (compose == InputFrame::Compose::Error) { Hold("compose-invalid"); return; }
  if (compose == InputFrame::Compose::Pending) {
    if (record_->composing.size() >= 16) { Hold("compose-prefix-full"); return; }
    RecordPhysicalDelivery(record_->pending.front(), false);
    record_->composing.push_back(std::move(record_->pending.front()));
    record_->pending.pop_front(); Schedule(); return;
  }
  const auto token = ++token_;
  auto complete = [this, token, weak = std::weak_ptr<int>(lifetime_)](bool ok, commands::Output output) {
    if (weak.expired()) return;
    if (token != token_ || record_->suspended) return;
    if (!ok) { Hold("rpc-failed"); return; }
    auto valid = valid_;
    const bool eligible = valid();
    if (weak.expired() || token != token_ || record_->suspended) return;
    if (!eligible) {
      // Preserve a validated response even when the original destination is no
      // longer eligible. It is inspection data, never a reason to replay.
      record_->delivering = std::move(record_->pending.front());
      record_->pending.pop_front(); record_->outcome = std::move(output);
      Hold("response-after-focus-change"); return;
    }
    ++token_; // Duplicate callbacks cannot deliver/commit twice.
    auto frame = std::move(record_->pending.front()); record_->pending.pop_front();
    const bool forward = frame.physical && frame.release ? ForwardRelease(frame) :
        frame.compose != InputFrame::Compose::Cancelled &&
        (frame.physical || !frame.composed_text.empty()) && (!frame.request || !output.consumed());
    const bool raw_forwarded = forward && frame.composed_text.empty();
    record_->delivering = frame;
    record_->outcome = output;
    auto record = record_;
    auto deliver = deliver_;
    // Do not arm the next frame while the frontend is applying this one.
    // A delivery hook may run a nested loop, submit more keys or destroy us.
    const bool accepted = deliver(std::move(output), forward ? std::optional<InputFrame>(std::move(frame)) : std::nullopt);
    if (weak.expired()) return;
    if (!accepted) { Hold("delivery-interrupted"); return; }
    if (record->suspended) return;
    RecordPhysicalDelivery(*record->delivering, raw_forwarded);
    if (record->delivering->compose == InputFrame::Compose::Complete ||
        record->delivering->compose == InputFrame::Compose::Cancelled) record->composing.clear();
    record->delivering.reset(); record->outcome.reset();
    Schedule();
  };
  if (record_->pending.front().request) {
    auto send = send_;
    auto input = *record_->pending.front().request;
    const bool accepted = send(std::move(input), std::move(complete));
    if (!weak.expired() && !accepted && token_ == token) Hold("submission-rejected");
  } else {
    commands::Output output;
    complete(true, std::move(output));
  }
}
}  // namespace mozc::fcitx5
