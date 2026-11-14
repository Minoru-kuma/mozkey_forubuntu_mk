// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/frame_translator.h"
#include "testing/gunit.h"
namespace mozc::fcitx5 {
namespace {
TEST(FrameTranslatorTest, PendingAndCompletedComposeKeepRawAndUnicode) {
  fcitx::KeyEventHandler handler;
  InputFrame dead; dead.raw = fcitx::Key(FcitxKey_dead_acute);
  TranslateInputFrame(dead, handler, config::Config::ROMAN, [](auto) { return ComposeResult{ComposeResult::Pending, {}}; });
  EXPECT_EQ(dead.compose, InputFrame::Compose::Pending); EXPECT_FALSE(dead.request);
  InputFrame letter; letter.raw = fcitx::Key(FcitxKey_e);
  TranslateInputFrame(letter, handler, config::Config::ROMAN, [](auto) { return ComposeResult{ComposeResult::Complete, "é"}; });
  ASSERT_TRUE(letter.request); EXPECT_EQ(letter.request->key().key_string(), "é");
  EXPECT_EQ(letter.raw.sym(), FcitxKey_e); EXPECT_EQ(letter.composed_text, "é");
}
TEST(FrameTranslatorTest, ModifierChordAndReleaseNeverFeedCompose) {
  fcitx::KeyEventHandler handler; int calls = 0;
  auto compose = [&](auto) { ++calls; return ComposeResult{}; };
  InputFrame frame; frame.raw = fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl);
  TranslateInputFrame(frame, handler, config::Config::ROMAN, compose); ASSERT_TRUE(frame.request);
  frame.release = true; TranslateInputFrame(frame, handler, config::Config::ROMAN, compose);
  EXPECT_FALSE(frame.request); EXPECT_EQ(calls, 0);
}
TEST(FrameTranslatorTest, InvalidComposeIsRetainedAndOnlyEscapeCancels) {
  fcitx::KeyEventHandler handler; InputFrame frame; frame.raw = fcitx::Key(FcitxKey_e);
  TranslateInputFrame(frame, handler, config::Config::ROMAN, [](auto) { return ComposeResult{ComposeResult::Cancelled, {}}; });
  EXPECT_EQ(frame.compose, InputFrame::Compose::Error); EXPECT_FALSE(frame.request);
  InputFrame escape; escape.raw = fcitx::Key(FcitxKey_Escape);
  TranslateInputFrame(escape, handler, config::Config::ROMAN, [](auto) { return ComposeResult{ComposeResult::Cancelled, {}}; });
  EXPECT_EQ(escape.compose, InputFrame::Compose::Cancelled); EXPECT_FALSE(escape.request);
}
TEST(FrameTranslatorTest, MalformedUtf8DoesNotBecomeCommittedText) {
  fcitx::KeyEventHandler handler; InputFrame frame; frame.raw = fcitx::Key(FcitxKey_e);
  TranslateInputFrame(frame, handler, config::Config::ROMAN, [](auto) { return ComposeResult{ComposeResult::Complete, "\xff"}; });
  EXPECT_EQ(frame.compose, InputFrame::Compose::Error); EXPECT_FALSE(frame.request);
}
}  // namespace
}  // namespace mozc::fcitx5
