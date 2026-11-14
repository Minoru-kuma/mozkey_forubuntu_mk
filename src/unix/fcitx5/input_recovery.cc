// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/input_recovery.h"
#include <cstdint>
#include <algorithm>
#include <fcitx-utils/utf8.h>
#include <string_view>
#include <utility>
#include "unix/fcitx5/input_recovery.pb.h"

namespace mozc::fcitx5 {
namespace {
constexpr size_t kLimit = 1024 * 1024;
constexpr std::string_view kMagic = "MOZKEY-RECOVERY-1\n";
// Accidental corruption detection, not authentication. Ownership/permissions
// protect the file. The snapshot is never interpreted as executable input.
uint64_t Checksum(std::string_view bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
  return hash;
}
void FrameToProto(const InputFrame& frame, recovery::Frame* saved) {
  saved->set_surrounding_valid(frame.surrounding_valid);
  saved->set_symbol(frame.raw.sym()); saved->set_code(frame.raw.code());
  saved->set_modifiers(frame.raw.states().toInteger()); saved->set_release(frame.release);
  saved->set_compose(static_cast<unsigned>(frame.compose)); saved->set_composed_text(frame.composed_text);
  saved->set_time(frame.time); saved->set_physical(frame.physical);
  saved->set_translated(frame.translated); saved->set_japanese_layout(frame.japanese_layout);
  saved->set_generation(frame.generation); saved->set_selection_snapshot(frame.selection_snapshot);
  saved->set_cursor(frame.cursor); saved->set_anchor(frame.anchor);
  saved->set_selected_length(frame.selected_length);
  if (frame.request) *saved->mutable_request() = *frame.request;
}
bool FrameFromProto(const recovery::Frame& saved, InputFrame* frame) {
  if (saved.selection_snapshot().size() > 65536 || saved.composed_text().size() > 65536 ||
      saved.compose() > static_cast<unsigned>(InputFrame::Compose::Error)) return false;
  frame->compose = static_cast<InputFrame::Compose>(saved.compose()); frame->composed_text = saved.composed_text();
  frame->surrounding_valid = saved.surrounding_valid();
  frame->raw = fcitx::Key(static_cast<fcitx::KeySym>(saved.symbol()),
                         fcitx::KeyStates(saved.modifiers()), saved.code());
  frame->release = saved.release(); frame->time = saved.time(); frame->physical = saved.physical();
  frame->translated = saved.translated(); frame->japanese_layout = saved.japanese_layout();
  frame->generation = saved.generation(); frame->selection_snapshot = saved.selection_snapshot();
  frame->cursor = saved.cursor(); frame->anchor = saved.anchor(); frame->selected_length = saved.selected_length();
  if (saved.has_request()) frame->request = saved.request();
  return true;
}
}
bool InputRecovery::Encode(const Records& records, std::string* bytes) {
  if (!bytes || records.size() > 32) return false;
  recovery::Snapshot snapshot; snapshot.set_version(1);
  for (const auto& record : records) {
    if (!record || record->pending.size() > 16 || record->composing.size() > 16 || record->preedit.size() > 65536 ||
        record->reason.size() > 1024) return false;
    auto* saved = snapshot.add_records();
    for (const auto& frame : record->pending) {
      if (frame.selection_snapshot.size() > 65536 ||
          (frame.request && frame.request->ByteSizeLong() > kLimit)) return false;
      FrameToProto(frame, saved->add_pending());
    }
    for (const auto& frame : record->composing) FrameToProto(frame, saved->add_composing());
    if (record->delivering) FrameToProto(*record->delivering, saved->mutable_delivering());
    if (record->outcome) *saved->mutable_outcome() = *record->outcome;
    saved->set_preedit(record->preedit); saved->set_reason(record->reason);
    if (snapshot.ByteSizeLong() > kLimit - kMagic.size() - 8) return false;
  }
  std::string payload;
  if (!snapshot.SerializeToString(&payload)) return false;
  std::string result(kMagic); const auto checksum = Checksum(payload);
  for (unsigned i = 0; i < 8; ++i) result.push_back(static_cast<char>(checksum >> (i * 8)));
  result += payload; *bytes = std::move(result); return true;
}
bool InputRecovery::Decode(const std::string& bytes, Records* records) {
  if (!records || bytes.size() > kLimit || bytes.size() < kMagic.size() + 8 ||
      !std::string_view(bytes).starts_with(kMagic)) return false;
  uint64_t checksum = 0;
  for (unsigned i = 0; i < 8; ++i) checksum |= uint64_t(static_cast<unsigned char>(bytes[kMagic.size()+i])) << (i*8);
  const auto payload = std::string_view(bytes).substr(kMagic.size()+8);
  if (Checksum(payload) != checksum) return false;
  recovery::Snapshot snapshot;
  if (!snapshot.ParseFromArray(payload.data(), payload.size()) || snapshot.version() != 1 ||
      snapshot.records_size() > 32) return false;
  Records result;
  for (const auto& saved : snapshot.records()) {
    if (saved.pending_size() > 16 || saved.composing_size() > 16 || saved.preedit().size() > 65536 || saved.reason().size() > 1024) return false;
    auto record = std::make_shared<RetainedInput>();
    record->preedit = saved.preedit(); record->reason = saved.reason();
    // Inspect only. Never put recovered data into the active request pipeline.
    record->suspended = true;
    for (const auto& entry : saved.pending()) {
      InputFrame frame; if (!FrameFromProto(entry, &frame)) return false;
      record->pending.push_back(std::move(frame));
    }
    for (const auto& entry : saved.composing()) {
      InputFrame frame; if (!FrameFromProto(entry, &frame)) return false;
      record->composing.push_back(std::move(frame));
    }
    if (saved.has_delivering()) {
      InputFrame frame; if (!FrameFromProto(saved.delivering(), &frame)) return false;
      record->delivering = std::move(frame);
    }
    if (saved.has_outcome()) record->outcome = saved.outcome();
    result.push_back(std::move(record));
  }
  *records = std::move(result); return true;
}
std::string InputRecovery::Inspect(const Records& records, size_t limit) {
  std::string text;
  auto append = [&](std::string_view value) {
    size_t count = std::min(value.size(), limit-text.size());
    // Keep the UI slice bounded and on a UTF-8 boundary; never alter the saved data.
    while (count && count < value.size() && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80) --count;
    const auto part = value.substr(0, count);
    if (fcitx::utf8::validate(part)) text.append(part);
  };
  auto frame = [&](const InputFrame& item) {
    append(" ["); append(item.raw.toString()); append("]");
    if (!item.composed_text.empty()) { append(" "); append(item.composed_text); }
  };
  for (const auto& record : records) {
    if (!record || text.size() >= limit) break;
    append("\n"); append(record->preedit);
    if (record->outcome && record->outcome->has_result()) {
      append("\n"); append(record->outcome->result().value());
    }
    if (record->delivering) frame(*record->delivering);
    for (const auto& item : record->composing) { if (text.size() >= limit) break; frame(item); }
    for (const auto& item : record->pending) { if (text.size() >= limit) break; frame(item); }
  }
  return text;
}
std::shared_ptr<InputRecovery::ExitSave> InputRecovery::QueueForExit(
    std::string root, std::string filename, const Records& records) {
  auto result = std::make_shared<ExitSave>();
  std::string bytes;
  if (!ValidFilename(filename) || !Encode(records, &bytes)) return result;
  result->queued = PreparationWorker::SetFinalJob(
      [root=std::move(root), filename=std::move(filename), bytes=std::move(bytes), result](const auto& cancel) {
        std::string ignored;
        if (!AsyncConfigFile::TransferForWorker(root, bytes, cancel, ignored, FileOptions(filename))) return -1;
        auto index = FileOptions("held-index.snapshot");
        index.filename = "latest"; index.limit = 100; index.no_replace = false;
        result->durable.store(AsyncConfigFile::TransferForWorker(root, filename, cancel, ignored, index));
        return -1;
      });
  return result;
}
bool InputRecovery::ValidFilename(const std::string& name) {
  if (!name.starts_with("held-") || !name.ends_with(".snapshot") || name.size() > 100) return false;
  for (const unsigned char ch : name)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '.')) return false;
  return true;
}
AsyncConfigFile::Options InputRecovery::FileOptions(std::string filename) {
  AsyncConfigFile::Options options;
  options.directory = "mozkey-recovery";
  // An invalid caller name fails component validation, never falls back.
  options.filename = ValidFilename(filename) ? std::move(filename) : std::string{};
  options.limit = kLimit; options.durable = true; options.private_directory = true; options.no_replace = true;
  return options;
}
}  // namespace mozc::fcitx5
