// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/input_recovery.h"
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <fcitx-utils/utf8.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include "testing/gunit.h"
extern char** environ;
namespace mozc::fcitx5 {
namespace {
InputRecovery::Records Example() {
  auto record = std::make_shared<RetainedInput>();
  record->preedit = "あ"; record->reason = "delivery-interrupted";
  InputFrame frame; frame.raw = fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl, 38);
  frame.release = true; frame.time = 123; frame.generation = 7;
  frame.surrounding_valid = true; frame.selection_snapshot = "あいう"; frame.cursor = 2; frame.anchor = 1; frame.selected_length = 1;
  frame.request.emplace(); frame.request->set_type(commands::Input::SEND_KEY);
  frame.request->mutable_key()->set_key_code('a');
  InputFrame prefix; prefix.raw = fcitx::Key(FcitxKey_dead_acute); prefix.compose = InputFrame::Compose::Pending;
  record->composing.push_back(prefix); frame.composed_text = "é";
  record->pending.push_back(frame); record->delivering = frame;
  record->outcome.emplace(); record->outcome->set_consumed(true);
  record->outcome->mutable_result()->set_type(commands::Result::STRING);
  record->outcome->mutable_result()->set_value("亜");
  return {record};
}
TEST(InputRecoveryTest, FullInspectionRoundTripNeverCreatesActiveInput) {
  std::string bytes; ASSERT_TRUE(InputRecovery::Encode(Example(), &bytes));
  InputRecovery::Records restored; ASSERT_TRUE(InputRecovery::Decode(bytes, &restored));
  ASSERT_EQ(restored.size(), 1); const auto& record = *restored[0];
  EXPECT_TRUE(record.suspended); EXPECT_EQ(record.preedit, "あ");
  ASSERT_EQ(record.pending.size(), 1); EXPECT_EQ(record.pending[0].raw.code(), 38);
  EXPECT_TRUE(record.pending[0].raw.states().test(fcitx::KeyState::Ctrl));
  EXPECT_TRUE(record.pending[0].release); EXPECT_EQ(record.pending[0].time, 123);
  EXPECT_EQ(record.pending[0].generation, 7); EXPECT_EQ(record.pending[0].request->key().key_code(), 'a');
  EXPECT_TRUE(record.pending[0].surrounding_valid);
  EXPECT_EQ(record.pending[0].selection_snapshot, "あいう"); EXPECT_EQ(record.pending[0].cursor, 2);
  EXPECT_EQ(record.pending[0].anchor, 1); EXPECT_EQ(record.pending[0].selected_length, 1);
  ASSERT_TRUE(record.delivering); ASSERT_TRUE(record.outcome);
  EXPECT_EQ(record.outcome->result().value(), "亜");
  ASSERT_EQ(record.composing.size(), 1); EXPECT_EQ(record.composing[0].compose, InputFrame::Compose::Pending);
  EXPECT_EQ(record.pending[0].composed_text, "é");
}
TEST(InputRecoveryTest, CorruptionTruncationAndVersionFailWithoutChangingDestination) {
  std::string bytes; ASSERT_TRUE(InputRecovery::Encode(Example(), &bytes));
  auto restored = Example(); const auto original = restored[0];
  auto corrupt = bytes; corrupt.back() ^= 1;
  EXPECT_FALSE(InputRecovery::Decode(corrupt, &restored)); EXPECT_EQ(restored[0], original);
  EXPECT_FALSE(InputRecovery::Decode(bytes.substr(0, bytes.size()-1), &restored));
  EXPECT_FALSE(InputRecovery::Decode("MOZKEY-RECOVERY-2\n", &restored));
}
TEST(InputRecoveryTest, BoundariesAndFilenameCannotEscapePrivateDirectory) {
  std::string bytes = "unchanged";
  auto records = Example(); records.resize(33, records[0]);
  EXPECT_FALSE(InputRecovery::Encode(records, &bytes)); EXPECT_EQ(bytes, "unchanged");
  auto record = std::make_shared<RetainedInput>(); record->pending.resize(17);
  EXPECT_FALSE(InputRecovery::Encode({record}, &bytes));
  record->pending.clear(); record->preedit.assign(65537, 'x');
  EXPECT_FALSE(InputRecovery::Encode({record}, &bytes));
  EXPECT_FALSE(InputRecovery::ValidFilename("../mozkey.conf"));
  EXPECT_FALSE(InputRecovery::ValidFilename("held-a/b.snapshot"));
  EXPECT_FALSE(InputRecovery::ValidFilename(std::string("held-a\0x.snapshot", 17)));
  EXPECT_TRUE(InputRecovery::ValidFilename("held-123-456.snapshot"));
}
TEST(InputRecoveryTest, InspectionIsBoundedValidUtf8AndKeepsOriginalData) {
  auto record = std::make_shared<RetainedInput>();
  for (int i = 0; i < 20000; ++i) record->preedit += "あ";
  const auto original = record->preedit;
  const auto text = InputRecovery::Inspect({record}, 128);
  EXPECT_LE(text.size(), 128); EXPECT_TRUE(fcitx::utf8::validate(text)); EXPECT_EQ(record->preedit, original);
  EXPECT_TRUE(InputRecovery::Inspect({record}, 0).empty());
}
TEST(InputRecoveryTest, EncoderCannotReportSuccessForUnreadableFrameFields) {
  // Cover every location: pending, compose prefix and uncertain delivery.
  for (int location = 0; location < 3; ++location) {
    for (int invalid = 0; invalid < 4; ++invalid) {
      auto record = std::make_shared<RetainedInput>();
      InputFrame frame;
      if (invalid == 0) frame.composed_text.assign(65537, 'x');
      if (invalid == 1) frame.selection_snapshot.assign(65537, 'x');
      if (invalid == 2) frame.compose = static_cast<InputFrame::Compose>(999);
      if (invalid == 3) {
        frame.request.emplace(); frame.request->set_type(commands::Input::SEND_KEY);
        frame.request->mutable_key()->set_key_string(std::string(1024*1024, 'x'));
      }
      if (location == 0) record->pending.push_back(frame);
      if (location == 1) record->composing.push_back(frame);
      if (location == 2) record->delivering = frame;
      std::string bytes = "prior-snapshot";
      EXPECT_FALSE(InputRecovery::Encode({record}, &bytes)) << location << ':' << invalid;
      EXPECT_EQ(bytes, "prior-snapshot");
      EXPECT_EQ(record->pending.size() + record->composing.size() + bool(record->delivering), 1);
    }
  }
}
TEST(InputRecoveryTest, MaximumAllowedFieldsRoundTripInEveryFrameLocation) {
  auto record = std::make_shared<RetainedInput>();
  InputFrame frame; frame.composed_text.assign(65536, 'x'); frame.selection_snapshot.assign(65536, 'y');
  frame.compose = InputFrame::Compose::Complete;
  record->pending.push_back(frame); record->composing.push_back(frame); record->delivering = frame;
  std::string bytes; ASSERT_TRUE(InputRecovery::Encode({record}, &bytes));
  InputRecovery::Records restored; ASSERT_TRUE(InputRecovery::Decode(bytes, &restored));
  ASSERT_EQ(restored.size(), 1);
  EXPECT_EQ(restored[0]->pending[0].composed_text, frame.composed_text);
  EXPECT_EQ(restored[0]->composing[0].selection_snapshot, frame.selection_snapshot);
  ASSERT_TRUE(restored[0]->delivering);
  EXPECT_EQ(restored[0]->delivering->composed_text, frame.composed_text);
}
TEST(InputRecoveryTest, AggregateLimitFailsWithoutTruncatingOrReplacingInput) {
  auto record = std::make_shared<RetainedInput>();
  InputFrame frame; frame.selection_snapshot.assign(65536, 'x');
  record->pending.assign(16, frame);
  std::string bytes = "prior-snapshot";
  EXPECT_FALSE(InputRecovery::Encode({record}, &bytes)); EXPECT_EQ(bytes, "prior-snapshot");
  EXPECT_EQ(record->pending.size(), 16); EXPECT_EQ(record->pending.back().selection_snapshot, frame.selection_snapshot);
}
struct RecoveryFileTest : ::testing::Test {
  std::string root;
  fcitx::EventLoop loop;
  std::unique_ptr<AsyncConfigFile> request;
  void SetUp() override {
    char name[] = "/tmp/mozkey-recovery-test-XXXXXX";
    auto* directory = mkdtemp(name); ASSERT_NE(directory, nullptr); root = directory;
  }
  void TearDown() override {
    request.reset(); while (PreparationWorker::Busy()) usleep(1000);
    std::filesystem::remove_all(root);
  }
  void Run() {
    auto end = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+3000000, 1,
        [&](auto*, auto) { ADD_FAILURE() << "private snapshot timeout"; loop.exit(); return true; });
    end->setOneShot(); EXPECT_TRUE(loop.exec());
  }
};
TEST_F(RecoveryFileTest, DurablePrivateWriteReadAndNoReplacement) {
  std::string bytes; ASSERT_TRUE(InputRecovery::Encode(Example(), &bytes));
  auto options = InputRecovery::FileOptions("held-test.snapshot");
  request = std::make_unique<AsyncConfigFile>(loop, root, bytes, [&](bool ok, auto) {
    EXPECT_TRUE(ok);
    request = std::make_unique<AsyncConfigFile>(loop, root, "wrong", [&](bool replaced, auto) {
      EXPECT_FALSE(replaced);
      request = std::make_unique<AsyncConfigFile>(loop, root, std::nullopt, [&](bool read, auto data) {
        EXPECT_TRUE(read); EXPECT_EQ(data, bytes); loop.exit();
      }, options);
    }, options);
  }, options);
  Run();
  struct stat directory{}, file{};
  ASSERT_EQ(stat((root+"/mozkey-recovery").c_str(), &directory), 0);
  ASSERT_EQ(stat((root+"/mozkey-recovery/held-test.snapshot").c_str(), &file), 0);
  EXPECT_EQ(directory.st_mode & 0777, 0700); EXPECT_EQ(file.st_mode & 0777, 0600);
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root+"/mozkey-recovery"),
                          std::filesystem::directory_iterator()), 1);
}
TEST_F(RecoveryFileTest, PublicSnapshotAndTraversalFailClosed) {
  std::filesystem::create_directory(root+"/mozkey-recovery"); chmod((root+"/mozkey-recovery").c_str(), 0700);
  { std::ofstream out(root+"/mozkey-recovery/held-test.snapshot"); out << "sentinel"; }
  chmod((root+"/mozkey-recovery/held-test.snapshot").c_str(), 0644);
  request = std::make_unique<AsyncConfigFile>(loop, root, std::nullopt, [&](bool ok, auto) {
    EXPECT_FALSE(ok);
    request = std::make_unique<AsyncConfigFile>(loop, root, "wrong", [&](bool saved, auto) {
      EXPECT_FALSE(saved); loop.exit();
    }, InputRecovery::FileOptions("../mozkey.conf"));
  }, InputRecovery::FileOptions("held-test.snapshot"));
  Run(); EXPECT_FALSE(std::filesystem::exists(root+"/mozkey.conf"));
}
// Spawn only this test binary, never a product IME/server. A writer process
// exits normally; a fresh reader reconstructs the file without shared memory.
TEST(RecoveryProcessTest, ChildOperation) {
  const char* root = std::getenv("MOZKEY_RECOVERY_FIXTURE_ROOT");
  const char* operation = std::getenv("MOZKEY_RECOVERY_FIXTURE_OPERATION");
  if (!root || !operation) GTEST_SKIP();
  if (std::string(operation).starts_with("exit-")) {
    const bool blocked = std::string(operation) == "exit-blocked";
    const bool cancel_tail = std::string(operation) == "exit-tail-cancel";
    const bool bad_index = std::string(operation) == "exit-bad-index";
    auto started = std::make_shared<std::atomic<bool>>(false);
    auto worker = PreparationWorker::Start([started, blocked](const auto&) {
      started->store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(blocked ? 3500 : 350)); return -1;
    }); ASSERT_TRUE(worker);
    const auto startup = std::chrono::steady_clock::now();
    while (!started->load()) {
      ASSERT_LT(std::chrono::steady_clock::now()-startup, std::chrono::seconds(1)); usleep(1000);
    }
    worker.reset();
    auto save = InputRecovery::QueueForExit(root, "held-process.snapshot", Example());
    ASSERT_TRUE(save->queued);
    EXPECT_FALSE(InputRecovery::QueueForExit(root, "held-second.snapshot", Example())->queued);
    const auto begin = std::chrono::steady_clock::now();
    const bool joined = PreparationWorker::WaitForProcessExit(std::chrono::milliseconds(cancel_tail ? 10 : 400));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count();
    EXPECT_EQ(joined, !blocked && !cancel_tail); EXPECT_EQ(save->durable.load(), !blocked && !bad_index && !cancel_tail);
    if (cancel_tail) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      EXPECT_TRUE(PreparationWorker::WaitForProcessExit(std::chrono::milliseconds(0)));
      EXPECT_FALSE(save->durable.load());
      EXPECT_FALSE(std::filesystem::exists(std::string(root)+"/mozkey-recovery/held-process.snapshot"));
    }
    EXPECT_LT(ms, 500); // Scheduler-tolerant diagnostic; 400ms remains the goal.
    std::cout << "EXIT_ARCHIVE_METRIC blocked=" << blocked << " bad_index=" << bad_index
              << " wait_ms=" << ms << " joined=" << joined << " durable=" << save->durable.load() << std::endl;
    return;
  }
  fcitx::EventLoop loop; std::string bytes; ASSERT_TRUE(InputRecovery::Encode(Example(), &bytes));
  const bool writing = std::string(operation) == "write";
  AsyncConfigFile request(loop, root, writing ? std::optional<std::string>(bytes) : std::nullopt,
      [&](bool ok, auto data) { EXPECT_TRUE(ok); if (!writing) {
        EXPECT_EQ(data, bytes); InputRecovery::Records records;
        ASSERT_TRUE(InputRecovery::Decode(data, &records)); EXPECT_TRUE(records[0]->suspended);
      } loop.exit(); }, InputRecovery::FileOptions("held-process.snapshot"));
  auto stop = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+3000000, 1,
      [&](auto*, auto) { ADD_FAILURE(); loop.exit(); return true; });
  stop->setOneShot(); EXPECT_TRUE(loop.exec());
  EXPECT_TRUE(PreparationWorker::WaitForProcessExit(std::chrono::milliseconds(400)));
}
void RunChild(const std::string& root, const char* mode) {
  std::vector<std::string> env;
  for (char** at = environ; *at; ++at) {
    std::string entry(*at);
    if (entry.starts_with("TEST_PREMATURE_EXIT_FILE=") || entry.starts_with("XML_OUTPUT_FILE=") ||
        entry.starts_with("GTEST_OUTPUT=") || entry.starts_with("GTEST_SHARD_") ||
        entry.starts_with("GTEST_TOTAL_SHARDS=")) continue;
    env.push_back(std::move(entry));
  }
  env.push_back("MOZKEY_RECOVERY_FIXTURE_ROOT="+root);
  env.push_back(std::string("MOZKEY_RECOVERY_FIXTURE_OPERATION=")+mode);
  std::vector<char*> values; for (auto& entry : env) values.push_back(entry.data()); values.push_back(nullptr);
  char executable[] = "/proc/self/exe";
  char filter[] = "--gtest_filter=RecoveryProcessTest.ChildOperation";
  char* args[] = {executable, filter, nullptr}; pid_t pid;
  ASSERT_EQ(posix_spawn(&pid, executable, nullptr, nullptr, args, values.data()), 0);
  int status; ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)); ASSERT_EQ(WEXITSTATUS(status), 0);
}
TEST_F(RecoveryFileTest, FreshProcessCanInspectAfterWriterExit) {
  RunChild(root, "write"); RunChild(root, "read");
}
TEST_F(RecoveryFileTest, ShutdownTailSavesAfterBlockedPreparationThenJoins) {
  RunChild(root, "exit-write"); RunChild(root, "read");
  EXPECT_FALSE(std::filesystem::exists(root+"/mozkey-recovery/held-second.snapshot"));
}
TEST_F(RecoveryFileTest, LongPreparationReturnsFailureWithoutUnboundedExitWait) {
  RunChild(root, "exit-blocked");
  EXPECT_FALSE(std::filesystem::exists(root+"/mozkey-recovery/held-process.snapshot"));
}
TEST_F(RecoveryFileTest, TimedOutShutdownNeverStartsALateSnapshotWrite) {
  RunChild(root, "exit-tail-cancel");
  EXPECT_FALSE(std::filesystem::exists(root+"/mozkey-recovery/held-process.snapshot"));
}
TEST_F(RecoveryFileTest, IndexFailureIsNotReportedAsDurableShutdownSuccess) {
  std::filesystem::create_directory(root+"/mozkey-recovery"); chmod((root+"/mozkey-recovery").c_str(), 0700);
  { std::ofstream out(root+"/mozkey-recovery/latest"); out << "sentinel"; }
  chmod((root+"/mozkey-recovery/latest").c_str(), 0644);
  RunChild(root, "exit-bad-index"); RunChild(root, "read");
  std::ifstream in(root+"/mozkey-recovery/latest"); std::string text; in >> text;
  EXPECT_EQ(text, "sentinel");
}

}  // namespace
}  // namespace mozc::fcitx5
