// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/fcitx_key_event_handler.h"
#include "unix/fcitx5/surrounding_text_util.h"
#include "unix/fcitx5/mozc_client_pool.h"
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
struct SessionContext : InputContext {
  explicit SessionContext(InputContextManager& manager) : InputContext(manager, "same-fixture-program") { created(); }
  ~SessionContext() override { destroy(); }
  const char* frontend() const override { return "mozkey-pool-fixture"; }
  void commitStringImpl(const std::string&) override {}
  void deleteSurroundingTextImpl(int, unsigned) override {}
  void updatePreeditImpl() override {}
  void forwardKeyImpl(const ForwardKeyEvent&) override {}
};
TEST(FrontendBoundaryTest, SessionsStayPerContextAndExplicitRetryUsesFreshClient) {
  EventLoop loop;
  InputContextManager manager;
  ASSERT_EQ(manager.instance(), nullptr);
  SessionContext first(manager), second(manager);
  for (const auto policy : {PropertyPropagatePolicy::No, PropertyPropagatePolicy::Program,
                            PropertyPropagatePolicy::All}) {
    MozcClientPool pool(policy, loop, {}, [](const auto&) { ADD_FAILURE() << "no server preparation expected"; return -1; });
    auto old = pool.requestClient(&first);
    auto other = pool.requestClient(&second);
    ASSERT_TRUE(old); ASSERT_TRUE(other); EXPECT_NE(old, other);
    EXPECT_EQ(pool.requestClient(&first), old); EXPECT_EQ(pool.policy(), policy);
    old->Cancel(); EXPECT_TRUE(old->IsCancelled()); EXPECT_FALSE(other->IsCancelled());
    auto fresh = pool.requestClient(&first);
    ASSERT_TRUE(fresh); EXPECT_NE(fresh, old); EXPECT_FALSE(fresh->IsCancelled());
    old.reset(); // An old deleter must not unregister its fresh replacement.
    EXPECT_EQ(pool.requestClient(&first), fresh);
    EXPECT_EQ(pool.requestClient(&second), other);
  }
}
}  // namespace
}  // namespace fcitx
