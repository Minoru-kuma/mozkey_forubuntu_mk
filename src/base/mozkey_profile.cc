// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "base/mozkey_profile.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <utility>

namespace mozc {
namespace {
struct Fd {
  explicit Fd(int value) : value(value) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { Reset(-1); }
  void Reset(int next) {
    const int error = errno;
    if (value >= 0) close(value);
    value = next; errno = error;
  }
  int value;
};
bool Component(std::string_view name) {
  return !name.empty() && name != "." && name != ".." && name.find('\0') == name.npos;
}
}
int OpenMozkeyProfileDirectory(std::string_view path, bool create_leaf) {
  if (path.size() < 2 || path.front() != '/' || path.back() == '/' || path.find('\0') != path.npos) {
    errno = EINVAL; return -1;
  }
  Fd directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (directory.value < 0) return -1;
  size_t at = 1;
  while (at < path.size()) {
    const auto slash = path.find('/', at);
    const auto part = path.substr(at, slash == path.npos ? path.size()-at : slash-at);
    if (!Component(part)) { errno = EINVAL; return -1; }
    const std::string name(part);
    if (slash == path.npos && create_leaf && mkdirat(directory.value, name.c_str(), 0700) && errno != EEXIST) return -1;
    directory.Reset(openat(directory.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.value < 0) return -1;
    if (slash == path.npos) break;
    at = slash+1;
  }
  struct stat info{};
  if (fstat(directory.value, &info)) return -1;
  if (info.st_uid != geteuid() || !S_ISDIR(info.st_mode) || (info.st_mode & 0777) != 0700) {
    errno = EPERM; return -1;
  }
  return std::exchange(directory.value, -1);
}
int OpenMozkeyLockFile(std::string_view path) {
  const auto slash = path.rfind('/');
  if (slash == path.npos || !Component(path.substr(slash+1))) { errno = EINVAL; return -1; }
  Fd directory(OpenMozkeyProfileDirectory(path.substr(0, slash)));
  if (directory.value < 0) return -1;
  const std::string name(path.substr(slash+1));
  Fd file(openat(directory.value, name.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600));
  if (file.value < 0) return -1;
  struct stat info{};
  if (fstat(file.value, &info)) return -1;
  if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1 ||
      (info.st_mode & 07777) != 0600) { errno = EPERM; return -1; }
  return std::exchange(file.value, -1);
}
}  // namespace mozc
