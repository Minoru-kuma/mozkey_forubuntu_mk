// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/input_pipeline.h"
#include "testing/gunit.h"

namespace mozc::fcitx5 {
namespace {
InputFrame Frame(char key, bool release = false) {
  InputFrame frame; frame.raw = fcitx::Key(static_cast<fcitx::KeySym>(key)); frame.release = release; frame.time = 123;
  if (!release) { frame.request.emplace(); frame.request->set_type(commands::Input::SEND_KEY); frame.request->mutable_key()->set_key_code(key); }
  return frame;
}
void RunLoop(fcitx::EventLoop& loop) {
  auto end = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+10000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) { loop.exit(); return true; });
  end->setOneShot(); EXPECT_TRUE(loop.exec());
}
struct Fixture {
  fcitx::EventLoop loop;
  std::shared_ptr<RetainedInput> record = InputRetention::Reserve();
  AsyncSession::Done complete;
  int sent = 0, applied = 0, forwarded = 0, cancelled = 0;
  bool focused = true;
  std::vector<unsigned> keys;
  std::unique_ptr<InputPipeline> pipeline;
  std::vector<std::unique_ptr<fcitx::EventSourceTime>> timers;
  void At(unsigned ms, std::function<void()> fn) {
    auto event = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+ms*1000, 1,
        [fn=std::move(fn)](fcitx::EventSourceTime*, uint64_t) { fn(); return true; });
    event->setOneShot(); timers.push_back(std::move(event));
  }
  Fixture() {
    pipeline = std::make_unique<InputPipeline>(loop, record,
        [&](auto, auto done) { ++sent; complete = std::move(done); return true; },
        [&] { ++cancelled; }, [&] { return focused; },
        [&](auto out, auto forward) {
          if (out.has_consumed()) ++applied;
          if (forward) { ++forwarded; keys.push_back(forward->raw.sym()); }
          return true;
        });
  }
  ~Fixture() {
    pipeline.reset();
    // Test-owned records are explicitly discarded after assertions only.
    record->pending.clear(); record->preedit.clear(); record->delivering.reset();
    InputRetention::ReleaseEmpty(record);
  }
  void Reply(bool consumed, bool ok = true) {
    commands::Output output; output.set_consumed(consumed);
    auto callback = complete; callback(ok, output);
  }
};
TEST(InputPipelineTest, ConsumeDecisionBeforeDeferredSendAndExactlyOnceForward) {
  Fixture f;
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  ASSERT_TRUE(f.pipeline->Submit(Frame('b')));
  ASSERT_TRUE(f.pipeline->Submit(Frame('b', true)));
  EXPECT_EQ(f.sent, 0);
  f.At(2, [&] { EXPECT_EQ(f.sent, 1); f.Reply(true); f.Reply(true); });
  f.At(5, [&] { EXPECT_EQ(f.sent, 2); f.Reply(false); f.Reply(false); });
  RunLoop(f.loop);
  EXPECT_EQ(f.applied, 2); EXPECT_EQ(f.forwarded, 2);
  EXPECT_EQ(f.keys, (std::vector<unsigned>{'b','b'})); EXPECT_TRUE(f.record->pending.empty());
}
TEST(InputPipelineTest, LateFocusCompletionIsRetainedAndNeverReplayed) {
  Fixture f; f.record->preedit = "未確定";
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] { f.focused = false; f.Reply(true); });
  f.At(5, [&] { f.focused = true; f.Reply(true); });
  RunLoop(f.loop);
  EXPECT_TRUE(f.pipeline->suspended()); EXPECT_EQ(f.applied, 0); EXPECT_EQ(f.forwarded, 0);
  EXPECT_EQ(f.record->pending.size(), 1); EXPECT_EQ(f.record->preedit, "未確定");
}
TEST(InputPipelineTest, DestroyedContextKeepsRawInputAndRejectsLateCallback) {
  Fixture f; ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] { f.pipeline.reset(); });
  f.At(5, [&] { auto late = f.complete; late(true, {}); });
  RunLoop(f.loop);
  EXPECT_EQ(f.record->pending.size(), 1); EXPECT_EQ(f.applied, 0); EXPECT_EQ(f.forwarded, 0);
}
TEST(InputPipelineTest, BoundedQueueProtectsPreviouslyConsumedInput) {
  Fixture f; for (int i=0; i<16; ++i) ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  EXPECT_FALSE(f.pipeline->Submit(Frame('b')));
  EXPECT_EQ(f.record->pending.size(), 16); EXPECT_TRUE(f.record->suspended);
  EXPECT_EQ(f.sent, 0); EXPECT_EQ(f.forwarded, 0);
}
TEST(InputPipelineTest, ConfigurationGatePreservesFirstKeyUntilReady) {
  Fixture f; f.pipeline->SetReady(false);
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] { EXPECT_EQ(f.sent, 0); f.pipeline->SetReady(true); });
  RunLoop(f.loop); EXPECT_EQ(f.sent, 1);
}
TEST(InputPipelineTest, FailedRpcRetainsCurrentAndFollowingKeys) {
  Fixture f; ASSERT_TRUE(f.pipeline->Submit(Frame('a'))); ASSERT_TRUE(f.pipeline->Submit(Frame('b')));
  f.At(2, [&] { f.Reply(false, false); }); RunLoop(f.loop);
  EXPECT_TRUE(f.record->suspended); EXPECT_EQ(f.record->pending.size(), 2);
  EXPECT_EQ(f.sent, 1); EXPECT_EQ(f.applied, 0);
}
TEST(InputPipelineTest, InterruptedDeliveryDoesNotOverwriteUncertainOutcome) {
  Fixture f;
  f.pipeline.reset(); f.record->suspended = false;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto, auto done) { ++f.sent; f.complete = std::move(done); return true; }, [] {}, [] { return true; },
      [&](auto, auto) { return false; });
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  ASSERT_TRUE(f.pipeline->Submit(Frame('b')));
  f.At(2, [&] { f.Reply(true); }); RunLoop(f.loop);
  EXPECT_TRUE(f.record->suspended); EXPECT_EQ(f.sent, 1);
  ASSERT_TRUE(f.record->delivering.has_value()); EXPECT_EQ(f.record->delivering->raw.sym(), 'a');
  ASSERT_EQ(f.record->pending.size(), 1); EXPECT_EQ(f.record->pending.front().raw.sym(), 'b');
}
TEST(InputPipelineTest, DestructionDuringDeliveryKeepsUncertainOutcome) {
  Fixture f;
  f.pipeline.reset(); f.record->suspended = false;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto, auto done) { f.complete = std::move(done); return true; }, [] {}, [] { return true; },
      [&](auto, auto) { f.pipeline.reset(); return false; });
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] { f.Reply(true); }); RunLoop(f.loop);
  EXPECT_TRUE(f.record->delivering.has_value()); EXPECT_TRUE(f.record->outcome.has_value());
}
}  // namespace
}  // namespace mozc::fcitx5
