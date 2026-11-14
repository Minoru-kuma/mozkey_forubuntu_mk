// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/response_guard.h"
#include <fcitx-utils/utf8.h>
#include <cstdint>
namespace mozc::fcitx5 {
namespace {
bool Matches(const InputFrame& frame, const fcitx::SurroundingText& current) {
  return frame.surrounding_valid && current.isValid() && current.text().size() <= 65536 &&
      current.text() == frame.selection_snapshot && current.cursor() == frame.cursor &&
      current.anchor() == frame.anchor;
}
bool Replacement(const commands::Output& output) {
  if (output.has_result() && output.result().type() == commands::Result::STRING &&
      !output.result().value().empty()) return true;
  for (const auto& segment : output.preedit().segment()) if (!segment.value().empty()) return true;
  return false;
}
}
ResponseDecision PrepareResponse(const InputFrame& frame, uint64_t generation,
                                 const fcitx::SurroundingText& current, bool supports_deletion, commands::Output* output) {
  if (!output) return ResponseDecision::Hold;
  if (output->has_mode() && (output->mode() < commands::DIRECT ||
      output->mode() >= commands::NUM_OF_COMPOSITIONS)) return ResponseDecision::Hold;
  if ((output->has_result() || output->has_deletion_range()) && !output->consumed()) return ResponseDecision::Hold;
  if (output->has_result() && !fcitx::utf8::validate(output->result().value())) return ResponseDecision::Hold;
  for (const auto& segment : output->preedit().segment())
    if (!fcitx::utf8::validate(segment.value())) return ResponseDecision::Hold;
  if (!frame.physical && frame.generation != generation) {
    return output->has_result() || output->has_deletion_range() || !frame.composed_text.empty()
        ? ResponseDecision::Hold : ResponseDecision::SkipUI;
  }
  auto prepared = *output;
  if (frame.selected_length != 0) {
    if (!supports_deletion || !output->consumed() || !Replacement(*output) || !Matches(frame, current) ||
        int64_t(frame.cursor)-frame.anchor != frame.selected_length) return ResponseDecision::Hold;
    const int64_t selected = frame.selected_length;
    const auto length = selected < 0 ? -selected : selected;
    if (length > 65536) return ResponseDecision::Hold;
    auto* range = prepared.mutable_deletion_range();
    range->set_offset(selected > 0 ? -selected : 0); range->set_length(length);
  }
  if (prepared.has_deletion_range()) {
    const auto& range = prepared.deletion_range();
    if (range.offset() > 0 || range.length() < 0 || int64_t(range.offset())+range.length() < 0)
      return ResponseDecision::Hold;
    if (range.length() == 0) {
      if (range.offset() != 0) return ResponseDecision::Hold;
      prepared.clear_deletion_range();
    } else {
      if (!supports_deletion || !Matches(frame, current)) return ResponseDecision::Hold;
      const auto length = fcitx::utf8::lengthValidated(current.text());
      const int64_t begin = int64_t(current.cursor())+range.offset();
      if (length == fcitx::utf8::INVALID_LENGTH || begin < 0 || begin+range.length() > int64_t(length))
        return ResponseDecision::Hold;
    }
  }
  *output = std::move(prepared); return ResponseDecision::Apply;
}
}  // namespace mozc::fcitx5
