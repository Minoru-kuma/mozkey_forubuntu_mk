// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/input_pipeline.h"
#include "testing/gunit.h"
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include "unix/fcitx5/fcitx_key_event_handler.h"

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
    record->pending.clear(); record->composing.clear(); record->preedit.clear(); record->delivering.reset(); record->outcome.reset();
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
  EXPECT_TRUE(f.record->pending.empty()); ASSERT_TRUE(f.record->delivering);
  EXPECT_EQ(f.record->delivering->raw.sym(), FcitxKey_a); ASSERT_TRUE(f.record->outcome);
  EXPECT_EQ(f.record->preedit, "未確定");
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
TEST(InputPipelineTest, ValidatedResultAfterFocusLossRemainsInspectable) {
  Fixture f; ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] {
    f.focused = false; commands::Output out; out.set_consumed(true);
    out.mutable_result()->set_type(commands::Result::STRING); out.mutable_result()->set_value("亜");
    auto done = f.complete; done(true, out);
  }); RunLoop(f.loop);
  ASSERT_TRUE(f.record->outcome); EXPECT_EQ(f.record->outcome->result().value(), "亜");
  EXPECT_EQ(f.applied, 0); EXPECT_EQ(f.forwarded, 0); EXPECT_TRUE(f.record->suspended);
}
TEST(InputPipelineTest, SynchronousSenderMayDestroyOwnerInsideCompletion) {
  Fixture f; f.pipeline.reset(); f.record->suspended = false;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [](auto, auto done) { commands::Output out; out.set_consumed(true); done(true, out); return false; },
      [] {}, [] { return true; }, [&](auto, auto) { f.pipeline.reset(); return false; });
  ASSERT_TRUE(f.pipeline->Submit(Frame('a'))); RunLoop(f.loop);
  EXPECT_TRUE(f.record->suspended); ASSERT_TRUE(f.record->delivering);
}
TEST(InputPipelineTest, TranslatorMayDestroyOwnerWithoutDanglingFrameAccess) {
  Fixture f;
  f.pipeline->SetTranslator([&](auto&) { f.pipeline.reset(); });
  auto input = Frame('a'); input.translated = false;
  ASSERT_TRUE(f.pipeline->Submit(input)); RunLoop(f.loop);
  EXPECT_EQ(f.sent, 0); EXPECT_EQ(f.record->pending.size(), 1); EXPECT_TRUE(f.record->suspended);
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
TEST(InputPipelineTest, IncompleteComposePrefixIsRetainedWithoutForwarding) {
  Fixture f; auto dead = Frame('a'); dead.compose = InputFrame::Compose::Pending; dead.translated = true;
  dead.raw = fcitx::Key(FcitxKey_dead_acute); dead.request.reset();
  ASSERT_TRUE(f.pipeline->Submit(dead)); RunLoop(f.loop);
  EXPECT_EQ(f.sent, 0); EXPECT_EQ(f.forwarded, 0); ASSERT_EQ(f.record->composing.size(), 1);
  EXPECT_EQ(f.record->composing[0].raw.sym(), FcitxKey_dead_acute); EXPECT_TRUE(f.record->pending.empty());
}
TEST(InputPipelineTest, FailedComposeRequestKeepsPrefixAndCompletedUnicode) {
  Fixture f; auto dead = Frame('a'); dead.compose = InputFrame::Compose::Pending; dead.translated = true;
  dead.request.reset(); ASSERT_TRUE(f.pipeline->Submit(dead));
  auto completed = Frame('e'); completed.compose = InputFrame::Compose::Complete; completed.composed_text = "é";
  ASSERT_TRUE(f.pipeline->Submit(completed)); f.At(2, [&] { f.Reply(false, false); }); RunLoop(f.loop);
  ASSERT_EQ(f.record->composing.size(), 1); ASSERT_EQ(f.record->pending.size(), 1);
  EXPECT_EQ(f.record->pending[0].composed_text, "é"); EXPECT_TRUE(f.record->suspended); EXPECT_EQ(f.forwarded, 0);
}
TEST(InputPipelineTest, ExplicitEscapeCancelsComposeWithoutDiscardingServerPreedit) {
  Fixture f; f.record->preedit = "未確定";
  auto dead = Frame('a'); dead.compose = InputFrame::Compose::Pending; dead.translated = true; dead.request.reset();
  ASSERT_TRUE(f.pipeline->Submit(dead));
  auto escape = Frame('b'); escape.compose = InputFrame::Compose::Cancelled; escape.translated = true; escape.request.reset();
  ASSERT_TRUE(f.pipeline->Submit(escape)); RunLoop(f.loop);
  EXPECT_TRUE(f.record->composing.empty()); EXPECT_EQ(f.record->preedit, "未確定");
  EXPECT_EQ(f.forwarded, 0); EXPECT_EQ(f.sent, 0);
}
TEST(InputPipelineTest, InvalidComposeIsRetainedWithoutDelivery) {
  Fixture f; auto frame = Frame('a'); frame.compose = InputFrame::Compose::Error; frame.translated = true;
  ASSERT_TRUE(f.pipeline->Submit(frame)); RunLoop(f.loop);
  EXPECT_TRUE(f.record->suspended); EXPECT_EQ(f.record->pending.size(), 1); EXPECT_EQ(f.sent, 0); EXPECT_EQ(f.forwarded, 0);
}
// Real Core event objects and product pipeline/translator, with a synthetic
// frontend and no Instance, addon load, DBus or product server.
struct DispatchContext : fcitx::InputContext {
  explicit DispatchContext(fcitx::InputContextManager& manager) : InputContext(manager) { created(); }
  ~DispatchContext() override { destroy(); }
  const char* frontend() const override { return "mozkey-pipeline-fixture"; }
  std::vector<fcitx::Key> forwarded;
  std::vector<uint32_t> stamps;
  std::vector<bool> releases;
  std::vector<std::string> commits;
  void commitStringImpl(const std::string& text) override { commits.push_back(text); }
  void deleteSurroundingTextImpl(int, unsigned) override {}
  void updatePreeditImpl() override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent& event) override {
    forwarded.push_back(event.rawKey()); stamps.push_back(event.time()); releases.push_back(event.isRelease());
  }
};
TEST(InputPipelineCoreTest, RealKeyEventsPreserveModifierReleaseAndTimestampOrder) {
  Fixture f; fcitx::InputContextManager manager; ASSERT_EQ(manager.instance(), nullptr);
  DispatchContext ic(manager); ic.focusIn(); fcitx::KeyEventHandler translator;
  f.pipeline.reset(); f.record->suspended = false; const auto watch = ic.watch();
  std::vector<std::unique_ptr<fcitx::EventSource>> replies;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto input, auto done) {
        ++f.sent; EXPECT_EQ(input.key().key_code(), 'a');
        bool control = false; for (auto value : input.key().modifier_keys()) control |= value == commands::KeyEvent::CTRL;
        EXPECT_TRUE(control);
        auto event = f.loop.addDeferEvent([done=std::move(done)](auto*) mutable {
          commands::Output output; output.set_consumed(false); done(true, output); return true;
        }); event->setOneShot(); replies.push_back(std::move(event)); return true;
      }, [] {}, [watch] { return watch.isValid() && watch.get()->hasFocus(); },
      [watch](auto, auto frame) { if (!watch.isValid()) return false;
        if (frame) watch.get()->forwardKey(frame->raw, frame->release, frame->time);
        return watch.isValid(); });
  f.pipeline->SetTranslator([&](auto& frame) {
    commands::KeyEvent key;
    if (translator.GetKeyEvent(frame.raw.sym(), frame.raw.code(), frame.raw.states(),
        config::Config::ROMAN, false, frame.release, &key)) {
      frame.request.emplace(); frame.request->set_type(commands::Input::SEND_KEY); *frame.request->mutable_key() = key;
    }
  });
  const std::vector<fcitx::Key> keys = {fcitx::Key(FcitxKey_Control_L),
      fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl, 38), fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl, 38),
      fcitx::Key(FcitxKey_Control_L)};
  for (unsigned i = 0; i < keys.size(); ++i) {
    fcitx::KeyEvent event(&ic, keys[i], i >= 2, 100+i);
    InputFrame frame; frame.raw = event.rawKey(); frame.release = event.isRelease(); frame.time = event.time();
    ASSERT_TRUE(f.pipeline->Submit(frame)); event.filterAndAccept();
    EXPECT_TRUE(event.accepted()); EXPECT_TRUE(event.filtered());
  }
  EXPECT_TRUE(ic.forwarded.empty()); RunLoop(f.loop);
  EXPECT_EQ(f.sent, 1); EXPECT_EQ(ic.forwarded, keys);
  EXPECT_EQ(ic.stamps, (std::vector<uint32_t>{100,101,102,103}));
  EXPECT_EQ(ic.releases, (std::vector<bool>{false,false,true,true})); EXPECT_TRUE(ic.commits.empty());
}
TEST(InputPipelineCoreTest, ComposedUnicodeCommitsOnceAndKeyupKeepsTimestamp) {
  Fixture f; fcitx::InputContextManager manager; DispatchContext ic(manager); ic.focusIn();
  f.pipeline.reset(); f.record->suspended = false; const auto watch = ic.watch();
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto, auto done) { f.complete = std::move(done); return true; }, [] {},
      [watch] { return watch.isValid() && watch.get()->hasFocus(); },
      [watch](auto, auto frame) {
        if (!watch.isValid() || !watch.get()->hasFocus()) return false;
        if (frame && !frame->composed_text.empty()) watch.get()->commitString(frame->composed_text);
        else if (frame) watch.get()->forwardKey(frame->raw, frame->release, frame->time);
        return watch.isValid();
      });
  auto dead = Frame('a'); dead.compose = InputFrame::Compose::Pending; dead.translated = true; dead.request.reset();
  ASSERT_TRUE(f.pipeline->Submit(dead));
  auto completed = Frame('e'); completed.compose = InputFrame::Compose::Complete; completed.composed_text = "é";
  ASSERT_TRUE(f.pipeline->Submit(completed)); auto up = Frame('e', true); up.time = 321;
  ASSERT_TRUE(f.pipeline->Submit(up));
  f.At(2, [&] { f.Reply(false); f.Reply(false); }); RunLoop(f.loop);
  EXPECT_EQ(ic.commits, (std::vector<std::string>{"é"})); ASSERT_EQ(ic.forwarded.size(), 1);
  EXPECT_EQ(ic.releases, (std::vector<bool>{true})); EXPECT_EQ(ic.stamps, (std::vector<uint32_t>{321}));
  EXPECT_TRUE(f.record->composing.empty()); EXPECT_TRUE(f.record->pending.empty());
}
TEST(InputPipelineCoreTest, UnconsumedCallbackSpaceCommitsExactlyOnceWithoutRawKey) {
  Fixture f; fcitx::InputContextManager manager; DispatchContext ic(manager); ic.focusIn();
  f.pipeline.reset(); f.record->suspended = false;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto, auto done) { f.complete = std::move(done); return true; }, [] {}, [] { return true; },
      [&](auto, auto frame) { if (frame) ic.commitString(frame->composed_text); return true; });
  auto frame = Frame(' '); frame.physical = false; frame.composed_text = " ";
  ASSERT_TRUE(f.pipeline->Submit(frame)); f.At(2, [&] { f.Reply(false); f.Reply(false); }); RunLoop(f.loop);
  EXPECT_EQ(ic.commits, (std::vector<std::string>{" "})); EXPECT_TRUE(ic.forwarded.empty());
}
TEST(InputPipelineCoreTest, FocusChangeDoesNotCommitOrForwardIntoEitherContext) {
  Fixture f; fcitx::InputContextManager manager; DispatchContext old(manager), other(manager);
  old.focusIn(); const auto watch = old.watch();
  f.pipeline.reset(); f.record->suspended = false;
  f.pipeline = std::make_unique<InputPipeline>(f.loop, f.record,
      [&](auto, auto done) { f.complete = std::move(done); return true; }, [] {},
      [watch] { return watch.isValid() && watch.get()->hasFocus(); },
      [watch](auto out, auto frame) {
        if (!watch.isValid() || !watch.get()->hasFocus()) return false;
        if (out.has_result()) watch.get()->commitString(out.result().value());
        if (frame) watch.get()->forwardKey(frame->raw, frame->release, frame->time);
        return true;
      });
  ASSERT_TRUE(f.pipeline->Submit(Frame('a')));
  f.At(2, [&] {
    old.focusOut(); other.focusIn(); commands::Output out; out.set_consumed(true);
    out.mutable_result()->set_type(commands::Result::STRING); out.mutable_result()->set_value("亜");
    auto done = f.complete; done(true, out);
  }); RunLoop(f.loop);
  EXPECT_TRUE(old.commits.empty()); EXPECT_TRUE(other.commits.empty());
  EXPECT_TRUE(old.forwarded.empty()); EXPECT_TRUE(other.forwarded.empty());
  ASSERT_TRUE(f.record->outcome); EXPECT_EQ(f.record->outcome->result().value(), "亜");
}
}  // namespace
}  // namespace mozc::fcitx5
