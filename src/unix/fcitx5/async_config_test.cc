// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_config.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "testing/gunit.h"

namespace {
enum class SaveFault { None, PartialWrite, FileSync, Rename, DirectorySync, RootSync };
struct FaultControl {
  SaveFault fault = SaveFault::None;
  unsigned syncs = 0, writes = 0;
  bool injected = false;
};
// Only the fixture's worker operation arms injection; eventfd notification and
// other threads' runtime I/O continue to call the real syscalls.
thread_local FaultControl fault_control;
struct FaultScope {
  explicit FaultScope(SaveFault fault) { fault_control = {fault}; }
  ~FaultScope() { fault_control = {}; }
};
struct UmaskScope {
  explicit UmaskScope(mode_t mask) : prior(umask(mask)) {}
  ~UmaskScope() { umask(prior); }
  mode_t prior;
};
}
extern "C" int __real_fsync(int fd);
extern "C" int __wrap_fsync(int fd) {
  const auto fault = fault_control.fault;
  const unsigned call = ++fault_control.syncs;
  if ((fault == SaveFault::FileSync && call == 1) ||
      (fault == SaveFault::DirectorySync && call == 2) ||
      (fault == SaveFault::RootSync && call == 3)) {
    fault_control.injected = true; errno = EIO; return -1;
  }
  return __real_fsync(fd);
}
extern "C" int __real_renameat2(int oldfd, const char* oldname, int newfd, const char* newname, unsigned flags);
extern "C" int __wrap_renameat2(int oldfd, const char* oldname, int newfd, const char* newname, unsigned flags) {
  if (fault_control.fault == SaveFault::Rename) {
    fault_control.injected = true; errno = ENOSPC; return -1;
  }
  return __real_renameat2(oldfd, oldname, newfd, newname, flags);
}
extern "C" ssize_t __real_write(int fd, const void* data, size_t size);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
  if (fault_control.fault == SaveFault::PartialWrite) {
    if (++fault_control.writes == 1) return __real_write(fd, data, size > 3 ? 3 : size);
    fault_control.injected = true; errno = ENOSPC; return -1;
  }
  return __real_write(fd, data, size);
}

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
  bool WaitForWorker(const PreparationWorker& worker) {
    const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!worker.finished() && std::chrono::steady_clock::now() < end) usleep(1000);
    return worker.finished();
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
TEST_F(ConfigFixture, PrivateSaveRejectsUmaskThatRemovesOwnerFilePermissions) {
  ASSERT_EQ(mkdir((root+"/recovery").c_str(), 0700), 0);
  for (const mode_t mask : {0777, 0400, 0200}) {
    AsyncConfigFile::Options options;
    options.directory = "recovery"; options.filename = "held.snapshot";
    options.private_directory = options.durable = options.no_replace = true;
    auto result = std::make_shared<bool>(true);
    auto worker = PreparationWorker::Start([root=root, options, mask, result](const auto& cancelled) {
      // Change the mask only around this single worker operation, and restore
      // it before publication. No product process or user config is involved.
      UmaskScope mask_scope(mask);
      std::string ignored;
      *result = AsyncConfigFile::TransferForWorker(root, "retained-fixture", cancelled, ignored, options);
      return -1;
    });
    ASSERT_TRUE(worker);
    ASSERT_TRUE(WaitForWorker(*worker));
    EXPECT_FALSE(*result) << mask;
    worker.reset();
    EXPECT_FALSE(std::filesystem::exists(root+"/recovery/held.snapshot"));
    EXPECT_TRUE(std::filesystem::is_empty(root+"/recovery"));
  }
}
TEST_F(ConfigFixture, PrivateSaveDoesNotRepairDirectoryOwnerPermissions) {
  ASSERT_EQ(mkdir((root+"/recovery").c_str(), 0700), 0);
  ASSERT_EQ(chmod((root+"/recovery").c_str(), 0500), 0);
  AsyncConfigFile::Options options;
  options.directory = "recovery"; options.filename = "held.snapshot";
  options.private_directory = options.durable = options.no_replace = true;
  request = std::make_unique<AsyncConfigFile>(loop, root, "retained-fixture",
      [&](bool ok, auto) { EXPECT_FALSE(ok); loop.exit(); }, options);
  Run();
  struct stat directory{};
  ASSERT_EQ(stat((root+"/recovery").c_str(), &directory), 0);
  EXPECT_EQ(directory.st_mode & 0777, 0500);
  EXPECT_FALSE(std::filesystem::exists(root+"/recovery/held.snapshot"));
  // Fixture teardown alone restores its own permissions.
  ASSERT_EQ(chmod((root+"/recovery").c_str(), 0700), 0);
}
TEST_F(ConfigFixture, PrivateSaveRejectsUmaskThatMakesNewDirectoryInaccessible) {
  AsyncConfigFile::Options options;
  options.directory = "recovery"; options.filename = "held.snapshot";
  options.private_directory = options.durable = options.no_replace = true;
  auto result = std::make_shared<bool>(true);
  auto worker = PreparationWorker::Start([root=root, options, result](const auto& cancelled) {
    UmaskScope mask_scope(0777);
    std::string ignored;
    *result = AsyncConfigFile::TransferForWorker(root, "retained-fixture", cancelled, ignored, options);
    return -1;
  });
  ASSERT_TRUE(worker); ASSERT_TRUE(WaitForWorker(*worker)); EXPECT_FALSE(*result);
  worker.reset();
  struct stat directory{};
  ASSERT_EQ(stat((root+"/recovery").c_str(), &directory), 0);
  EXPECT_EQ(directory.st_mode & 0777, 0000);
  // Only this disposable fixture restores access so it can inspect/clean up.
  ASSERT_EQ(chmod((root+"/recovery").c_str(), 0700), 0);
  EXPECT_TRUE(std::filesystem::is_empty(root+"/recovery"));
}
TEST_F(ConfigFixture, StorageFailuresNeverClaimDurabilityOrRemovePublishedSnapshots) {
  ASSERT_EQ(mkdir((root+"/recovery").c_str(), 0700), 0);
  const std::string prior_name = root+"/recovery/prior.snapshot";
  { std::ofstream out(prior_name); out << "prior-snapshot"; }
  ASSERT_EQ(chmod(prior_name.c_str(), 0600), 0);
  struct stat original{}; ASSERT_EQ(stat(prior_name.c_str(), &original), 0);
  for (const auto fault : {SaveFault::PartialWrite, SaveFault::FileSync, SaveFault::Rename,
                           SaveFault::DirectorySync, SaveFault::RootSync}) {
    SCOPED_TRACE(static_cast<int>(fault));
    AsyncConfigFile::Options options;
    options.directory = "recovery"; options.filename = "current.snapshot";
    options.private_directory = options.durable = options.no_replace = true;
    auto result = std::make_shared<bool>(true);
    auto injected = std::make_shared<bool>(false);
    auto worker = PreparationWorker::Start([root=root, options, fault, result, injected](const auto& cancelled) {
      FaultScope injection(fault);
      std::string ignored;
      *result = AsyncConfigFile::TransferForWorker(root, "new-snapshot-fixture", cancelled, ignored, options);
      *injected = fault_control.injected;
      return -1;
    });
    ASSERT_TRUE(worker);
    ASSERT_TRUE(WaitForWorker(*worker));
    EXPECT_FALSE(*result); EXPECT_TRUE(*injected);
    worker.reset();
    struct stat prior{}; ASSERT_EQ(stat(prior_name.c_str(), &prior), 0);
    EXPECT_EQ(prior.st_ino, original.st_ino); EXPECT_EQ(prior.st_mode & 0777, 0600);
    std::ifstream prior_input(prior_name); std::string prior_text; prior_input >> prior_text;
    EXPECT_EQ(prior_text, "prior-snapshot");
    const bool published = fault == SaveFault::DirectorySync || fault == SaveFault::RootSync;
    const auto current = root+"/recovery/current.snapshot";
    EXPECT_EQ(std::filesystem::exists(current), published);
    if (published) {
      // Rename completed, durability is uncertain. Preserve the complete new
      // file for manual inspection even though the save result is false.
      std::ifstream input(current); std::string text; input >> text;
      EXPECT_EQ(text, "new-snapshot-fixture");
      struct stat file{}; ASSERT_EQ(stat(current.c_str(), &file), 0);
      EXPECT_EQ(file.st_mode & 0777, 0600);
      ASSERT_TRUE(std::filesystem::remove(current));
    }
    // No partially written temporary survives a failure before publication.
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root+"/recovery"),
                            std::filesystem::directory_iterator()), 1);
  }
}
}
}
