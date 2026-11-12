// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/fcitx_key_event_handler.h"
#include "unix/fcitx5/surrounding_text_util.h"
#include <limits>
#include "testing/gunit.h"

namespace fcitx {
namespace {
using mozc::commands::KeyEvent;
constexpr auto kRoman = mozc::config::Config::ROMAN;
// These are protocol-translation decisions, not Fcitx consume/pass-through.
TEST(FrontendBoundaryTest, FirstLetterAndSpace) {
  KeyEventHandler handler;
  KeyEvent key;
  ASSERT_TRUE(handler.GetKeyEvent(FcitxKey_a, 0, {}, kRoman, false, false, &key));
  EXPECT_EQ(key.key_code(), 'a');
  ASSERT_TRUE(handler.GetKeyEvent(FcitxKey_space, 0, {}, kRoman, false, false, &key));
  EXPECT_EQ(key.special_key(), KeyEvent::SPACE);
}
TEST(FrontendBoundaryTest, ControlChordAndKeyupAreNotDuplicateRequests) {
  KeyEventHandler handler;
  KeyEvent key;
  EXPECT_FALSE(handler.GetKeyEvent(FcitxKey_Control_L, 0, {}, kRoman, false, false, &key));
  ASSERT_TRUE(handler.GetKeyEvent(FcitxKey_a, 0, KeyState::Ctrl, kRoman, false, false, &key));
  EXPECT_EQ(key.key_code(), 'a');
  bool control = false;
  for (auto modifier : key.modifier_keys()) control |= modifier == KeyEvent::CTRL;
  EXPECT_TRUE(control);
  EXPECT_FALSE(handler.GetKeyEvent(FcitxKey_a, 0, KeyState::Ctrl, kRoman, false, true, &key));
  EXPECT_FALSE(handler.GetKeyEvent(FcitxKey_Control_L, 0, {}, kRoman, false, true, &key));
}
TEST(FrontendBoundaryTest, ResetModifierHistory) {
  KeyEventHandler handler;
  KeyEvent key;
  EXPECT_FALSE(handler.GetKeyEvent(FcitxKey_Control_L, 0, {}, kRoman, false, false, &key));
  handler.Clear();
  ASSERT_TRUE(handler.GetKeyEvent(FcitxKey_a, 0, {}, kRoman, false, false, &key));
  EXPECT_EQ(key.modifier_keys_size(), 0);
}
TEST(FrontendBoundaryTest, UnicodeComposeIsPreserved) {
  KeyEventHandler handler;
  KeyEvent key;
  ASSERT_TRUE(handler.GetKeyEvent("あ", kRoman, false, &key));
  EXPECT_EQ(key.key_string(), "あ");
}
TEST(FrontendBoundaryTest, SelectionCountsUnicodeCharacters) {
  unsigned anchor = 0;
  ASSERT_TRUE(SurroundingTextUtil::GetAnchorPosFromSelection("あいう", "い", 1, &anchor));
  EXPECT_EQ(anchor, 2);
  ASSERT_TRUE(SurroundingTextUtil::GetAnchorPosFromSelection("あいう", "い", 2, &anchor));
  EXPECT_EQ(anchor, 1);
  EXPECT_FALSE(SurroundingTextUtil::GetAnchorPosFromSelection("あいう", "お", 1, &anchor));
}
TEST(FrontendBoundaryTest, DeletionRangeExcludesSignedOverflow) {
  int32_t delta = 0;
  ASSERT_TRUE(SurroundingTextUtil::GetSafeDelta(3, 1, &delta));
  EXPECT_EQ(delta, 2);
  EXPECT_FALSE(SurroundingTextUtil::GetSafeDelta(0, 2147483648u, &delta));
  EXPECT_FALSE(SurroundingTextUtil::GetSafeDelta(std::numeric_limits<unsigned>::max(), 0, &delta));
}
}  // namespace
}  // namespace fcitx
