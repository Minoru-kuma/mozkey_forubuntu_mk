// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_config.h"
#include <fcntl.h>
#include <linux/fs.h>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <utility>

namespace mozc::fcitx5 {
namespace {
struct Fd {
  int fd = -1;
  explicit Fd(int value) : fd(value) {}
  ~Fd() { if (fd >= 0) close(fd); }
  void Reset(int value) { if (fd >= 0) close(fd); fd = value; }
};
int Directory(const std::string& root, const std::atomic<bool>& cancelled) {
  if (cancelled.load() || root.empty() || root[0] != '/') return -1;
  Fd current(open("/", O_RDONLY|O_DIRECTORY|O_CLOEXEC));
  size_t at = 1;
  while (at < root.size()) {
    if (cancelled.load()) return -1;
    auto slash = root.find('/', at); auto part = root.substr(at, slash-at);
    if (part.empty() || part == "." || part == "..") return -1;
    current.Reset(openat(current.fd, part.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if (current.fd < 0) return -1;
    if (slash == std::string::npos) break;
    at = slash + 1;
  }
  struct stat st{};
  if (fstat(current.fd, &st) || st.st_uid != geteuid()) return -1;
  return std::exchange(current.fd, -1);
}
bool Transfer(const std::string& root, const std::optional<std::string>& save,
              const std::atomic<bool>& cancelled, std::string& text,
              const AsyncConfigFile::Options& options) {
  auto component = [](const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
  };
  if (!component(options.directory) || !component(options.filename) ||
      options.limit == 0 || options.limit > 1024 * 1024) return false;
  if (cancelled.load()) return false;
  Fd directory(Directory(root, cancelled));
  if (directory.fd < 0 || cancelled.load()) return false;
  if (save && mkdirat(directory.fd, options.directory.c_str(), 0700) && errno != EEXIST) return false;
  Fd conf(openat(directory.fd, options.directory.c_str(), O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  if (conf.fd < 0) return !save && errno == ENOENT;
  struct stat parent{}, target{};
  if (fstat(conf.fd, &parent) || parent.st_uid != geteuid() || (parent.st_mode & (options.private_directory ? 0077 : 0022))) return false;
  const int exists = fstatat(conf.fd, options.filename.c_str(), &target, AT_SYMLINK_NOFOLLOW);
  if ((exists && errno != ENOENT) || (!exists &&
      (!S_ISREG(target.st_mode) || target.st_uid != geteuid() || target.st_nlink != 1 || (options.private_directory && (target.st_mode & 0077))))) return false;
  if (cancelled.load()) return false;
  if (!save) {
    if (exists) return true;
    Fd file(openat(conf.fd, options.filename.c_str(), O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
    struct stat opened{};
    if (file.fd < 0 || fstat(file.fd, &opened) || opened.st_dev != target.st_dev ||
        opened.st_ino != target.st_ino || opened.st_size < 0 || static_cast<uint64_t>(opened.st_size) > options.limit) return false;
    char bytes[4096];
    while (!cancelled.load()) {
      auto n = read(file.fd, bytes, sizeof(bytes));
      if (n == 0) return true;
      if (n < 0) { if (errno == EINTR) continue; return false; }
      if (text.size() + n > options.limit) return false;
      text.append(bytes, n);
    }
    return false;
  }
  if (save->size() > options.limit) return false;
  static unsigned serial = 0; // Single preparation worker.
  auto name = "." + options.filename + "." + std::to_string(getpid()) + "." + std::to_string(++serial);
  Fd temporary(openat(conf.fd, name.c_str(), O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600));
  if (temporary.fd < 0) return false;
  size_t offset = 0;
  while (offset < save->size() && !cancelled.load()) {
    auto n = write(temporary.fd, save->data()+offset, save->size()-offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    offset += n;
  }
  // Rename replaces only our directory entry, never follows a target symlink.
  const bool synced = offset == save->size() && !cancelled.load() &&
      (!options.durable || fsync(temporary.fd) == 0);
  const bool ok = offset == save->size() && synced && !cancelled.load() &&
      renameat2(conf.fd, name.c_str(), conf.fd, options.filename.c_str(),
                options.no_replace ? RENAME_NOREPLACE : 0) == 0;
  if (!ok) unlinkat(conf.fd, name.c_str(), 0);
  // A durable success includes the rename and directory entry. Failure after
  // rename is uncertain; preserve the file and report failure, never replay it.
  return ok && !cancelled.load() && (!options.durable ||
      (fsync(conf.fd) == 0 && !cancelled.load() && fsync(directory.fd) == 0 && !cancelled.load()));
}
}
bool AsyncConfigFile::TransferForWorker(const std::string& root, const std::optional<std::string>& save,
    const std::atomic<bool>& cancelled, std::string& text, const Options& options) {
  return Transfer(root, save, cancelled, text, options);
}
AsyncConfigFile::AsyncConfigFile(fcitx::EventLoop& loop, std::string root,
    std::optional<std::string> save, Done done)
    : AsyncConfigFile(loop, std::move(root), std::move(save), std::move(done), Options{}) {}
AsyncConfigFile::AsyncConfigFile(fcitx::EventLoop& loop, std::string root,
    std::optional<std::string> save, Done done, Options options)
    : loop_(loop), root_(std::move(root)), save_(std::move(save)), done_(std::move(done)),
      options_(std::move(options)), deadline_(fcitx::now(CLOCK_MONOTONIC)+2000000) {
  defer_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Start(); return true; });
  defer_->setOneShot();
}
AsyncConfigFile::~AsyncConfigFile() { done_ = {}; defer_.reset(); io_.reset(); timer_.reset(); worker_.reset(); }
void AsyncConfigFile::Start() {
  if (fcitx::now(CLOCK_MONOTONIC) >= deadline_) { Finish(); return; }
  worker_ = PreparationWorker::Start([root=root_, save=save_, data=data_, options=options_](const auto& cancelled) {
    data->ok = Transfer(root, save, cancelled, data->text, options); return -1;
  });
  if (!worker_) {
    timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+5000, 1,
        [this](fcitx::EventSourceTime*, uint64_t) { Start(); return true; });
  } else {
    io_ = loop_.addIOEvent(worker_->notification_fd(), fcitx::IOEventFlag::In,
        [this](fcitx::EventSourceIO*, int fd, fcitx::IOEventFlags) {
          uint64_t value; const auto n=read(fd, &value, sizeof(value));
          if (n==sizeof(value) && worker_->finished()) Finish(); return true;
        });
    timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, deadline_, 1,
        [this](fcitx::EventSourceTime*, uint64_t) { Finish(); return true; });
  }
  timer_->setOneShot();
}
void AsyncConfigFile::Finish() {
  const bool completed = worker_ && worker_->finished();
  io_.reset(); timer_.reset(); worker_.reset();
  auto done = std::move(done_);
  auto data = data_;
  // Read shared data only after the worker's release/acquire completion flag.
  if (done) done(completed && data->ok, completed ? data->text : std::string{});
}
}
