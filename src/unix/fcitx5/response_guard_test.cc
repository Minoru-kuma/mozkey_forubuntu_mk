// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/response_guard.h"
#include <limits>
#include "testing/gunit.h"
namespace mozc::fcitx5 {
namespace {
InputFrame Capture(const fcitx::SurroundingText& text) {
  InputFrame frame; frame.surrounding_valid = text.isValid(); frame.selection_snapshot = text.text();
  frame.cursor = text.cursor(); frame.anchor = text.anchor(); return frame;
}
commands::Output Reconverted() {
  commands::Output out; out.set_consumed(true); out.mutable_preedit()->set_cursor(1);
  auto* segment = out.mutable_preedit()->add_segment(); segment->set_value("い");
  segment->set_value_length(1); segment->set_annotation(commands::Preedit::Segment::UNDERLINE); return out;
}
TEST(ResponseGuardTest, RefusedReconversionNeverDeletesSelection) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 1);
  auto frame = Capture(text); frame.selected_length = 1; commands::Output out; out.set_consumed(false);
  EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold); EXPECT_FALSE(out.has_deletion_range());
}
TEST(ResponseGuardTest, UnicodeSelectionBothDirectionsUsesCharacterOffsets) {
  for (bool reverse : {false, true}) {
    fcitx::SurroundingText text; text.setText("あいう", reverse ? 1 : 2, reverse ? 2 : 1);
    auto frame = Capture(text); frame.selected_length = reverse ? -1 : 1; auto out = Reconverted();
    EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Apply);
    EXPECT_EQ(out.deletion_range().offset(), reverse ? 0 : -1); EXPECT_EQ(out.deletion_range().length(), 1);
  }
}
TEST(ResponseGuardTest, ChangedTextCursorOrAnchorHoldsWithoutModifyingReply) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 1); auto frame = Capture(text); frame.selected_length = 1;
  for (unsigned kind = 0; kind < 3; ++kind) {
    fcitx::SurroundingText changed; changed.setText(kind == 0 ? "あえう" : "あいう", kind == 1 ? 1 : 2, kind == 2 ? 0 : 1);
    auto out = Reconverted(); const auto original = out.SerializeAsString();
    EXPECT_EQ(PrepareResponse(frame, 0, changed, true, &out), ResponseDecision::Hold); EXPECT_EQ(out.SerializeAsString(), original);
  }
}
TEST(ResponseGuardTest, EmptyReplacementCannotRemoveSelectedText) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 1); auto frame = Capture(text); frame.selected_length = 1;
  commands::Output out; out.set_consumed(true);
  EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold); EXPECT_FALSE(out.has_deletion_range());
}
TEST(ResponseGuardTest, InvalidDeletionLengthsAndOverflowAreRejected) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 2); auto frame = Capture(text);
  for (auto [offset, size] : {std::pair{0, -1}, std::pair{1,1}, std::pair{-2,1},
      std::pair{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()},
      std::pair{0,std::numeric_limits<int>::max()}}) {
    commands::Output out; out.set_consumed(true); out.mutable_deletion_range()->set_offset(offset); out.mutable_deletion_range()->set_length(size);
    EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold);
  }
}
TEST(ResponseGuardTest, OrdinaryDeletionRequiresExactBoundedSurroundingSnapshot) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 2); auto frame = Capture(text);
  commands::Output out; out.set_consumed(true); out.mutable_deletion_range()->set_offset(-1); out.mutable_deletion_range()->set_length(1);
  EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Apply);
  frame.surrounding_valid = false; EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold);
}
TEST(ResponseGuardTest, InvalidModeAndInconsistentCommitCannotReachFrontend) {
  fcitx::SurroundingText text; InputFrame frame; commands::Output out;
  out.set_mode(commands::NUM_OF_COMPOSITIONS); EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold);
  out.clear_mode(); out.set_consumed(false); out.mutable_result()->set_type(commands::Result::STRING); out.mutable_result()->set_value("あ");
  EXPECT_EQ(PrepareResponse(frame, 0, text, true, &out), ResponseDecision::Hold);
}
TEST(ResponseGuardTest, StaleCommandsKeepEffectsButSkipOnlyPureUiSnapshots) {
  fcitx::SurroundingText text; InputFrame frame; frame.physical = false;
  auto out = Reconverted(); EXPECT_EQ(PrepareResponse(frame, 1, text, true, &out), ResponseDecision::SkipUI);
  out.mutable_deletion_range()->set_length(1); EXPECT_EQ(PrepareResponse(frame, 1, text, true, &out), ResponseDecision::Hold);
  out.clear_deletion_range(); frame.composed_text = " "; EXPECT_EQ(PrepareResponse(frame, 1, text, true, &out), ResponseDecision::Hold);
}
TEST(ResponseGuardTest, MissingFrontendCapabilityCannotDeleteEvenWithCachedText) {
  fcitx::SurroundingText text; text.setText("あいう", 2, 2); auto frame = Capture(text);
  commands::Output out; out.set_consumed(true); out.mutable_deletion_range()->set_offset(-1); out.mutable_deletion_range()->set_length(1);
  EXPECT_EQ(PrepareResponse(frame, 0, text, false, &out), ResponseDecision::Hold);
}
TEST(ResponseGuardTest, PhysicalKeysRemainOrderedAcrossNewKeyGenerations) {
  InputFrame frame; fcitx::SurroundingText text; commands::Output out; out.set_consumed(false);
  EXPECT_EQ(PrepareResponse(frame, 10, text, true, &out), ResponseDecision::Apply);
}
}  // namespace
}  // namespace mozc::fcitx5
