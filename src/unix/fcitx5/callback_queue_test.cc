// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/callback_queue.h"
#include <string>
#include "testing/gunit.h"

namespace mozc::fcitx5 {
namespace {
void RunLoop(fcitx::EventLoop& loop) {
  auto end = loop.addTimeEvent(CLOCK_MONOTONIC,
      fcitx::now(CLOCK_MONOTONIC) + 30000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) { loop.exit(); return true; });
  end->setOneShot();
  EXPECT_TRUE(loop.exec());
}
commands::Output Response() {
  commands::Output out;
  out.set_consumed(false);
  auto* c = out.mutable_callback()->mutable_session_command();
  c->set_type(commands::SessionCommand::APPLY_LIVE_CONVERSION);
  c->set_live_conversion_generation(42);
  c->set_live_conversion_key("fixture-reading");
  c->set_command_sequence_generation(123);
  c->set_text("fixture-text");
  out.mutable_callback()->set_delay_millisec(5);
  return out;
}
TEST(CallbackQueueTest, FullCopyUnconsumedAndOneDelivery) {
  fcitx::EventLoop loop;
  CallbackQueue queue(loop);
  auto out = Response();
  const auto expected = out.callback().session_command().SerializeAsString();
  int deliveries = 0;
  ASSERT_TRUE(queue.Schedule(out, [] { return true; }, [&](const auto& c) {
    ++deliveries; EXPECT_EQ(c.SerializeAsString(), expected);
  }));
  out.Clear();
  EXPECT_EQ(deliveries, 0);
  RunLoop(loop);
  EXPECT_EQ(deliveries, 1);
}
TEST(CallbackQueueTest, CancelAndInvalidContext) {
  for (bool cancel : {false, true}) {
    fcitx::EventLoop loop;
    CallbackQueue queue(loop);
    int deliveries = 0;
    ASSERT_TRUE(queue.Schedule(Response(), [cancel] { return cancel; },
                              [&](const auto&) { ++deliveries; }));
    if (cancel) queue.Invalidate();
    RunLoop(loop);
    EXPECT_EQ(deliveries, 0);
  }
}
TEST(CallbackQueueTest, ReplacedAndDestroyedTimer) {
  fcitx::EventLoop loop;
  CallbackQueue queue(loop);
  int deliveries = 0;
  ASSERT_TRUE(queue.Schedule(Response(), [] { return true; },
                            [&](const auto&) { deliveries += 100; }));
  ASSERT_TRUE(queue.Schedule(Response(), [] { return true; },
                            [&](const auto&) { ++deliveries; }));
  { CallbackQueue temporary(loop);
    ASSERT_TRUE(temporary.Schedule(Response(), [] { return true; },
                                  [&](const auto&) { deliveries += 100; })); }
  RunLoop(loop);
  EXPECT_EQ(deliveries, 1);
}
TEST(CallbackQueueTest, ZeroDelayContinuationsAreBounded) {
  fcitx::EventLoop loop;
  CallbackQueue queue(loop);
  auto out = Response();
  out.mutable_callback()->set_delay_millisec(0);
  int deliveries = 0;
  CallbackQueue::Send send;
  send = [&](const auto&) { ++deliveries; queue.Schedule(out, [] { return true; }, send); };
  ASSERT_TRUE(queue.Schedule(out, [] { return true; }, send));
  EXPECT_EQ(deliveries, 0);
  RunLoop(loop);
  EXPECT_EQ(deliveries, 16);
}
TEST(CallbackQueueTest, UnknownCommandRejected) {
  fcitx::EventLoop loop;
  CallbackQueue queue(loop);
  auto out = Response();
  out.mutable_callback()->mutable_session_command()->set_type(commands::SessionCommand::NONE);
  EXPECT_FALSE(queue.Schedule(out, [] { return true; }, [](const auto&) {}));
}
}  // namespace
}  // namespace mozc::fcitx5
