// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_config.h"
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include "testing/gunit.h"

namespace mozc::fcitx5 {
namespace {
struct ConfigFixture : ::testing::Test {
  std::string root;
  fcitx::EventLoop loop;
  std::unique_ptr<AsyncConfigFile> request;
  void SetUp() override {
    char name[] = "/tmp/mozkey-async-config-XXXXXX";
    auto* directory = mkdtemp(name); ASSERT_NE(directory, nullptr); root = directory;
  }
  void TearDown() override {
    request.reset();
    // Completion/cancellation tests wait until this fixture's worker is idle.
    while (PreparationWorker::Busy()) usleep(1000);
    std::filesystem::remove_all(root);
  }
  void Run() {
    auto stop = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+3000000, 1,
        [&](auto*, auto) { ADD_FAILURE() << "config completion timeout"; loop.exit(); return true; });
    stop->setOneShot(); EXPECT_TRUE(loop.exec());
  }
};
TEST_F(ConfigFixture, AtomicWriteAndReadDedicatedFile) {
  request = std::make_unique<AsyncConfigFile>(loop, root, "[Behavior]\nLiveConversion=True\n",
      [&](bool ok, auto) {
        EXPECT_TRUE(ok);
        request = std::make_unique<AsyncConfigFile>(loop, root, std::nullopt,
            [&](bool read, auto text) { EXPECT_TRUE(read); EXPECT_EQ(text, "[Behavior]\nLiveConversion=True\n"); request.reset(); loop.exit(); });
      });
  Run();
  EXPECT_FALSE(std::filesystem::exists(root+"/conf/mozc.conf"));
  for (const auto& entry : std::filesystem::directory_iterator(root+"/conf"))
    EXPECT_EQ(entry.path().filename(), "mozkey.conf");
}
TEST_F(ConfigFixture, SymlinkTargetCannotOverwriteSentinel) {
  std::filesystem::create_directory(root+"/conf");
  { std::ofstream out(root+"/sentinel"); out << "unchanged"; }
  ASSERT_EQ(symlink("../sentinel", (root+"/conf/mozkey.conf").c_str()), 0);
  request = std::make_unique<AsyncConfigFile>(loop, root, "wrong", [&](bool ok, auto) { EXPECT_FALSE(ok); loop.exit(); });
  Run();
  std::ifstream in(root+"/sentinel"); std::string text; in >> text; EXPECT_EQ(text, "unchanged");
}
TEST_F(ConfigFixture, HardlinkAndParentSymlinkAreRejected) {
  std::filesystem::create_directory(root+"/conf");
  { std::ofstream out(root+"/sentinel"); out << "unchanged"; }
  ASSERT_EQ(link((root+"/sentinel").c_str(), (root+"/conf/mozkey.conf").c_str()), 0);
  request = std::make_unique<AsyncConfigFile>(loop, root, std::nullopt, [&](bool ok, auto) {
    EXPECT_FALSE(ok);
    std::filesystem::remove(root+"/conf/mozkey.conf"); std::filesystem::remove(root+"/conf");
    ASSERT_EQ(symlink(".", (root+"/conf").c_str()), 0);
    request = std::make_unique<AsyncConfigFile>(loop, root, "wrong", [&](bool saved, auto) { EXPECT_FALSE(saved); loop.exit(); });
  });
  Run(); EXPECT_FALSE(std::filesystem::exists(root+"/mozkey.conf"));
}
TEST_F(ConfigFixture, CancellationBeforeDeferredStartDoesNotWrite) {
  request = std::make_unique<AsyncConfigFile>(loop, root, "wrong", [&](bool, auto) { ADD_FAILURE(); });
  request.reset();
  auto stop = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+10000, 1,
      [&](auto*, auto) { loop.exit(); return true; });
  stop->setOneShot(); EXPECT_TRUE(loop.exec()); EXPECT_FALSE(std::filesystem::exists(root+"/conf"));
}
}
}
