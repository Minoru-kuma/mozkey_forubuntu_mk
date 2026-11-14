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

#include "base/system_util.h"

#include <string>

#include "base/environ_mock.h"
#include "base/const.h"
#include "base/file_util.h"
#include "base/file_util_mock.h"
#include "testing/gmock.h"
#include "testing/gunit.h"
#ifdef MOZKEY_LINUX_BUILD
#include <sys/stat.h>
#include <unistd.h>
#include "testing/mozctest.h"
#endif

namespace mozc {

class SystemUtilTest : public ::testing::Test {};

#ifdef MOZKEY_LINUX_BUILD
TEST_F(SystemUtilTest, DedicatedProductPaths) {
  EXPECT_STREQ(kProductNameInEnglish, "Mozkey");
  EXPECT_EQ(SystemUtil::GetServerPath(), "/usr/lib/mozkey/mozkey_server");
  EXPECT_EQ(SystemUtil::GetToolPath(), "/usr/lib/mozkey/mozkey_tool");
}
#endif

TEST_F(SystemUtilTest, GetUserProfileDirectory) {
#if defined(OS_CHROMEOS)
  EXPECT_EQ(SystemUtil::GetUserProfileDirectory(), "/mutable");

#elif defined(__wasm__)
  EXPECT_EQ(SystemUtil::GetUserProfileDirectory(), "/tmp");

#elif defined(__ANDROID__)
  EXPECT_TRUE(SystemUtil::GetUserProfileDirectory().empty());

#elif defined(_WIN32)
#elif defined(__APPLE__)
  // TODO(komatsu): write a test.

#elif defined(MOZKEY_LINUX_BUILD)
  EnvironMock environ_mock;
  auto root = testing::MakeTempDirectoryOrDie();
  const auto home = root.path();
  environ_mock.SetEnv("HOME", home);
  environ_mock.SetEnv("XDG_CONFIG_HOME", "");
  ASSERT_OK(FileUtil::CreateDirectory(home+"/.config"));
  SystemUtil::SetUserProfileDirectory("");
  EXPECT_EQ(home+"/.config/mozkey", SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::CreateDirectory(home+"/.mozc"));
  ASSERT_OK(FileUtil::CreateDirectory(home+"/config"));
  environ_mock.SetEnv("XDG_CONFIG_HOME", home+"/config");
  SystemUtil::SetUserProfileDirectory("");
  EXPECT_EQ(home+"/config/mozkey", SystemUtil::GetUserProfileDirectory());
  // A legacy profile and an invalid relative XDG path never select .mozc.
  environ_mock.SetEnv("XDG_CONFIG_HOME", "relative");
  SystemUtil::SetUserProfileDirectory("");
  EXPECT_EQ(home+"/.config/mozkey", SystemUtil::GetUserProfileDirectory());
  SystemUtil::SetUserProfileDirectory("");
#elif defined(__linux__)
  EnvironMock environ_mock;
  FileUtilMock file_util_mock;
  SystemUtil::SetUserProfileDirectory("");

  // The default path is "$HOME/.config/mozc".
  EXPECT_FALSE(FileUtil::DirectoryExists("/home/mozcuser/.config/mozc").ok());
  EXPECT_EQ("/home/mozcuser/.config/mozc",
            SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.config/mozc"));

  environ_mock.SetEnv("XDG_CONFIG_HOME", "/tmp/config");

  // Once the dir is initialized, the value is not changed.
  EXPECT_EQ("/home/mozcuser/.config/mozc",
            SystemUtil::GetUserProfileDirectory());

  // Resets the cache by setting an empty string.
  SystemUtil::SetUserProfileDirectory("");

  // $XDG_CONFIG_HOME is already set with "/tmp/config" in above.
  // If $XDG_CONFIG_HOME is specified, "$XDG_CONFIG_HOME/mozc" is used.
  EXPECT_FALSE(FileUtil::DirectoryExists("/tmp/config/mozc").ok());
  EXPECT_EQ("/tmp/config/mozc", SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/tmp/config/mozc"));

  // Resets again.
  SystemUtil::SetUserProfileDirectory("");

  // If "$HOME/.mozc" exists, it is used for backward compatibility.
  EXPECT_OK(FileUtil::CreateDirectory("/home/mozcuser/.mozc"));
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.mozc"));
  EXPECT_EQ("/home/mozcuser/.mozc", SystemUtil::GetUserProfileDirectory());
  EXPECT_OK(FileUtil::DirectoryExists("/home/mozcuser/.mozc"));

  // Resets again to avoid side effects to other tests.
  SystemUtil::SetUserProfileDirectory("");

#else  // Platforms
#error Undefined target platform.

#endif  // Platforms
}

TEST_F(SystemUtilTest, GetTotalPhysicalMemoryTest) {
  EXPECT_GT(SystemUtil::GetTotalPhysicalMemory(), 0);
}
#ifdef MOZKEY_LINUX_BUILD
TEST_F(SystemUtilTest, UnsafeProfileDoesNotCreateThroughLinksOrFallback) {
  EnvironMock environ_mock; auto root = testing::MakeTempDirectoryOrDie();
  const auto home = root.path();
  environ_mock.SetEnv("HOME", home); environ_mock.SetEnv("XDG_CONFIG_HOME", home);
  ASSERT_EQ(mkdir((home+"/mozkey").c_str(), 0755), 0);
  ASSERT_EQ(chmod((home+"/mozkey").c_str(), 0755), 0);
  SystemUtil::SetUserProfileDirectory(""); EXPECT_TRUE(SystemUtil::GetUserProfileDirectory().empty());
  struct stat info{}; ASSERT_EQ(stat((home+"/mozkey").c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0755);
  ASSERT_EQ(rmdir((home+"/mozkey").c_str()), 0);
  ASSERT_EQ(mkdir((home+"/other-product").c_str(), 0700), 0);
  ASSERT_EQ(symlink((home+"/other-product").c_str(), (home+"/mozkey").c_str()), 0);
  EXPECT_TRUE(SystemUtil::GetUserProfileDirectory().empty());
  environ_mock.SetEnv("XDG_CONFIG_HOME", home+"/mozkey");
  EXPECT_TRUE(SystemUtil::GetUserProfileDirectory().empty());
  EXPECT_EQ(access((home+"/other-product/mozkey").c_str(), F_OK), -1);
  SystemUtil::SetUserProfileDirectory("");
}
#endif

#ifdef __ANDROID__
TEST_F(SystemUtilTest, GetOSVersionStringTestForAndroid) {
  std::string result = SystemUtil::GetOSVersionString();
  // |result| must start with "Android ".
  EXPECT_TRUE(result.starts_with("Android "));
}
#endif  // __ANDROID__

}  // namespace mozc
