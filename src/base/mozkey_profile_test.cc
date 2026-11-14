// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "base/mozkey_profile.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include "base/system_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {
class MozkeyProfileTest : public testing::TestWithTempUserProfile {
 protected:
  std::string Path(const char* name) { return SystemUtil::GetUserProfileDirectory()+"/"+name; }
  void Sentinel(const std::string& path) {
    std::ofstream file(path); file << "other-product-sentinel"; file.close(); ASSERT_EQ(chmod(path.c_str(), 0600), 0);
  }
  std::string Read(const std::string& path) { std::ifstream file(path); return {std::istreambuf_iterator<char>(file), {}}; }
};
TEST_F(MozkeyProfileTest, CreatesOnlyPrivateLeafAndReturnsOwnedCloexecFd) {
  const int fd = OpenMozkeyProfileDirectory(Path("mozkey"), true); ASSERT_GE(fd, 0);
  struct stat info{}; ASSERT_EQ(fstat(fd, &info), 0);
  EXPECT_EQ(info.st_uid, geteuid()); EXPECT_EQ(info.st_mode & 0777, 0700);
  EXPECT_NE(fcntl(fd, F_GETFD) & FD_CLOEXEC, 0); close(fd);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("missing/mozkey"), true), 0);
  EXPECT_EQ(access(Path("missing").c_str(), F_OK), -1);
}
TEST_F(MozkeyProfileTest, PublicDirectoryIsRejectedWithoutChmod) {
  ASSERT_EQ(mkdir(Path("public").c_str(), 0755), 0);
  ASSERT_EQ(chmod(Path("public").c_str(), 0755), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("public"), true), 0);
  struct stat info{}; ASSERT_EQ(stat(Path("public").c_str(), &info), 0); EXPECT_EQ(info.st_mode & 0777, 0755);
}
TEST_F(MozkeyProfileTest, SymlinkLeafAndAncestorNeverWriteIntoOtherDirectory) {
  ASSERT_EQ(mkdir(Path("other-product").c_str(), 0700), 0);
  Sentinel(Path("other-product/sentinel"));
  ASSERT_EQ(symlink(Path("other-product").c_str(), Path("link").c_str()), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("link"), true), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("link/mozkey"), true), 0);
  EXPECT_EQ(access(Path("other-product/mozkey").c_str(), F_OK), -1);
  EXPECT_EQ(Read(Path("other-product/sentinel")), "other-product-sentinel");
}
TEST_F(MozkeyProfileTest, AmbiguousAndRelativeNamesFailClosed) {
  EXPECT_LT(OpenMozkeyProfileDirectory("relative", true), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("../escape"), true), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("./escape"), true), 0);
  EXPECT_LT(OpenMozkeyProfileDirectory(Path("/escape"), true), 0);
  const auto nul = Path("mozkey") + std::string("\0suffix", 7);
  EXPECT_LT(OpenMozkeyProfileDirectory(nul, true), 0);
  EXPECT_LT(OpenMozkeyLockFile(Path("..")), 0);
}
TEST_F(MozkeyProfileTest, FileOpenNeverTruncatesExistingContents) {
  Sentinel(Path("lock")); const int fd = OpenMozkeyLockFile(Path("lock")); ASSERT_GE(fd, 0);
  EXPECT_NE(fcntl(fd, F_GETFD) & FD_CLOEXEC, 0); EXPECT_NE(fcntl(fd, F_GETFL) & O_NONBLOCK, 0);
  EXPECT_EQ(Read(Path("lock")), "other-product-sentinel"); close(fd);
}
TEST_F(MozkeyProfileTest, SymlinkHardlinkAndFifoAreRejectedWithoutChangingSentinel) {
  Sentinel(Path("sentinel"));
  ASSERT_EQ(symlink(Path("sentinel").c_str(), Path("symlink").c_str()), 0);
  EXPECT_LT(OpenMozkeyLockFile(Path("symlink")), 0);
  ASSERT_EQ(link(Path("sentinel").c_str(), Path("hardlink").c_str()), 0);
  EXPECT_LT(OpenMozkeyLockFile(Path("hardlink")), 0);
  ASSERT_EQ(mkfifo(Path("fifo").c_str(), 0600), 0); EXPECT_LT(OpenMozkeyLockFile(Path("fifo")), 0);
  EXPECT_EQ(Read(Path("sentinel")), "other-product-sentinel");
}
TEST_F(MozkeyProfileTest, LegacyReadonlyAndPublicFilesFailWithoutChangingModes) {
  for (auto mode : {0400, 0644}) {
    Sentinel(Path("lock")); ASSERT_EQ(chmod(Path("lock").c_str(), mode), 0);
    EXPECT_LT(OpenMozkeyLockFile(Path("lock")), 0);
    struct stat info{}; ASSERT_EQ(stat(Path("lock").c_str(), &info), 0); EXPECT_EQ(info.st_mode & 0777, mode);
    EXPECT_EQ(Read(Path("lock")), "other-product-sentinel");
    ASSERT_EQ(chmod(Path("lock").c_str(), 0600), 0);
  }
}
}  // namespace
}  // namespace mozc
