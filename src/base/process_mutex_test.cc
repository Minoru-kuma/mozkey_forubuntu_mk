// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "base/process_mutex.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "base/file_util.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

#ifndef _WIN32
#include <unistd.h>

#include <cstdlib>
#endif  // _WIN32
#ifdef MOZKEY_LINUX_BUILD
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <fstream>
#endif

namespace mozc {
namespace {
static constexpr char kName[] = "process_mutex_test";

class ProcessMutexTest : public testing::TestWithTempUserProfile {
 protected:
  void TearDown() override {
    ProcessMutex mutex(kName);
#ifdef MOZKEY_LINUX_BUILD
    // Production preserves the inode across unlock. Remove only this test's
    // private fixture after checking its identity/type/mode.
    struct stat info{};
    if (lstat(mutex.lock_filename().c_str(), &info) == 0) {
      EXPECT_TRUE(S_ISREG(info.st_mode)); EXPECT_EQ(info.st_uid, geteuid());
      EXPECT_EQ(info.st_nlink, 1); EXPECT_EQ(info.st_mode & 0777, 0600);
      EXPECT_TRUE(FileUtil::Unlink(mutex.lock_filename()).ok());
    } else EXPECT_EQ(errno, ENOENT);
#else
    if (absl::Status s = FileUtil::FileExists(mutex.lock_filename());
        !absl::IsNotFound(s)) {
      LOG(FATAL)
          << "Lock file unexpectedly remains or cannot check the existence: "
          << mutex.lock_filename() << ": " << s;
    }
#endif
  }
};
#ifdef MOZKEY_LINUX_BUILD
void ExpectChildSuccess(pid_t pid) {
  int status = 0; pid_t reaped = 0;
  const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(2);
  do { reaped = waitpid(pid, &status, WNOHANG); if (reaped < 0 && errno == EINTR) reaped = 0;
    if (!reaped) usleep(1000);
  } while (!reaped && std::chrono::steady_clock::now() < end);
  if (!reaped) { const auto killed = kill(pid, SIGKILL); (void)killed;
    for (int i=0; i<1000 && !reaped; ++i) {
      reaped = waitpid(pid, &status, WNOHANG); if (reaped < 0 && errno == EINTR) reaped = 0;
      usleep(1000);
    }
  }
  EXPECT_EQ(reaped, pid); if (reaped == pid) { EXPECT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status), 0); }
}
#endif

#if !defined(_WIN32)
TEST_F(ProcessMutexTest, ForkProcessMutexTest) {
  const pid_t pid = ::fork();
  if (pid == 0) {  // child process
    ProcessMutex m(kName);
    EXPECT_TRUE(m.Lock());
    absl::SleepFor(absl::Seconds(3));
    EXPECT_TRUE(m.UnLock());
    ::exit(0);
  } else if (pid > 0) {  // parent process
    ProcessMutex m(kName);
    absl::SleepFor(absl::Seconds(1));

    // kName should be locked by child
    EXPECT_FALSE(m.Lock());

    absl::SleepFor(absl::Seconds(5));

    // child process already finished, so now we can lock
    EXPECT_TRUE(m.Lock());
    EXPECT_TRUE(m.UnLock());
  } else {
    LOG(FATAL) << "fork() failed";
  }
}
#endif  // !_WIN32

TEST_F(ProcessMutexTest, BasicTest) {
  ProcessMutex m1(kName);
  EXPECT_TRUE(m1.Lock());
  EXPECT_TRUE(m1.locked());

  ProcessMutex m2(kName);
  EXPECT_FALSE(m2.Lock());
  EXPECT_FALSE(m2.locked());

  ProcessMutex m3(kName);
  EXPECT_FALSE(m3.Lock());
  EXPECT_FALSE(m3.locked());

  EXPECT_TRUE(m1.UnLock());
  EXPECT_FALSE(m1.locked());

  EXPECT_TRUE(m2.Lock());
  EXPECT_TRUE(m2.locked());

  EXPECT_FALSE(m3.Lock());
  EXPECT_FALSE(m3.locked());
}

TEST_F(ProcessMutexTest, RecursiveLockTest) {
  ProcessMutex mutex(kName);
  EXPECT_TRUE(mutex.Lock());
  EXPECT_TRUE(mutex.locked());
  EXPECT_FALSE(mutex.Lock()) << "Recursive lock should fail.";
  EXPECT_TRUE(mutex.locked());
  EXPECT_TRUE(mutex.UnLock());
  EXPECT_FALSE(mutex.locked());
}
#ifdef MOZKEY_LINUX_BUILD
TEST_F(ProcessMutexTest, UnlockPreservesTheSamePrivateInodeForNextOwner) {
  ProcessMutex first(kName); ASSERT_TRUE(first.LockAndWrite("first"));
  struct stat before{}, after{};
  ASSERT_EQ(stat(first.lock_filename().c_str(), &before), 0); ASSERT_TRUE(first.UnLock());
  ASSERT_EQ(stat(first.lock_filename().c_str(), &after), 0);
  EXPECT_EQ(after.st_ino, before.st_ino); EXPECT_EQ(after.st_dev, before.st_dev);
  ProcessMutex next(kName); ASSERT_TRUE(next.LockAndWrite("second"));
  ASSERT_EQ(stat(next.lock_filename().c_str(), &after), 0); EXPECT_EQ(after.st_ino, before.st_ino);
  EXPECT_EQ(after.st_mode & 0777, 0600);
  auto text = FileUtil::GetContents(next.lock_filename()); ASSERT_TRUE(text.ok()); EXPECT_EQ(*text, "second");
}
TEST_F(ProcessMutexTest, RejectedSymlinkDoesNotChangeOtherProductContentsOrMode) {
  ProcessMutex mutex(kName); const auto sentinel = mutex.lock_filename()+".sentinel";
  ASSERT_TRUE(FileUtil::SetContents(sentinel, "other-product-sentinel").ok());
  ASSERT_EQ(chmod(sentinel.c_str(), 0400), 0);
  ASSERT_EQ(symlink(sentinel.c_str(), mutex.lock_filename().c_str()), 0);
  EXPECT_FALSE(mutex.LockAndWrite("replace"));
  auto text = FileUtil::GetContents(sentinel); ASSERT_TRUE(text.ok()); EXPECT_EQ(*text, "other-product-sentinel");
  struct stat info{}; ASSERT_EQ(stat(sentinel.c_str(), &info), 0); EXPECT_EQ(info.st_mode & 0777, 0400);
  EXPECT_EQ(unlink(mutex.lock_filename().c_str()), 0); EXPECT_EQ(unlink(sentinel.c_str()), 0);
}
TEST_F(ProcessMutexTest, FailedCrossProcessLockNeverTruncatesTheCurrentKey) {
  ProcessMutex contender(kName); const auto filename = contender.lock_filename();
  ASSERT_TRUE(FileUtil::SetContents(filename, "current-key-sentinel").ok()); ASSERT_EQ(chmod(filename.c_str(), 0600), 0);
  int pipefd[2]; ASSERT_EQ(pipe(pipefd), 0);
  const char* name = filename.c_str(); const pid_t pid = fork(); ASSERT_GE(pid, 0);
  if (!pid) {
    // This fixture is single-threaded; the child uses only async-safe syscalls.
    close(pipefd[0]); const int fd = open(name, O_RDWR | O_CLOEXEC);
    struct flock command{}; command.l_type = F_WRLCK; command.l_whence = SEEK_SET;
    const char ready = fd >= 0 && fcntl(fd, F_SETLK, &command) == 0 ? 1 : 0;
    const auto written = write(pipefd[1], &ready, 1); (void)written;
    usleep(200000); if (fd >= 0) close(fd); close(pipefd[1]); _exit(ready ? 0 : 75);
  }
  close(pipefd[1]); pollfd item{pipefd[0], POLLIN, 0};
  const int notified = poll(&item, 1, 1000); EXPECT_EQ(notified, 1);
  char ready = 0;
  if (notified == 1) { EXPECT_EQ(read(pipefd[0], &ready, 1), 1); EXPECT_EQ(ready, 1); }
  if (ready) {
    EXPECT_FALSE(contender.LockAndWrite("replace"));
    auto text = FileUtil::GetContents(filename); EXPECT_TRUE(text.ok());
    if (text.ok()) EXPECT_EQ(*text, "current-key-sentinel");
  }
  close(pipefd[0]); ExpectChildSuccess(pid);
}
TEST_F(ProcessMutexTest, ReadingAndClosingTheKeyCannotReleaseItsOwnerLock) {
  ProcessMutex owner(kName); ASSERT_TRUE(owner.LockAndWrite("held-key"));
  auto text = FileUtil::GetContents(owner.lock_filename()); ASSERT_TRUE(text.ok()); EXPECT_EQ(*text, "held-key");
  const char* name = owner.lock_filename().c_str(); const pid_t pid = fork(); ASSERT_GE(pid, 0);
  if (!pid) {
    const int fd = open(name, O_RDWR | O_CLOEXEC); struct flock command{};
    command.l_type = F_WRLCK; command.l_whence = SEEK_SET;
    const int locked = fd < 0 ? -2 : fcntl(fd, F_SETLK, &command);
    const int error = errno; if (fd >= 0) close(fd);
    _exit(locked == -1 && (error == EAGAIN || error == EACCES) ? 0 : 76);
  }
  ExpectChildSuccess(pid);
}
#endif

}  // namespace
}  // namespace mozc
