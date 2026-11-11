// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
// Test-only root-owner model. Not an addon unload or unbounded-I/O solution.
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>

struct PrepFD {
  int fd = -1;
  explicit PrepFD(int value = -1) : fd(value) {}
  ~PrepFD() { if (fd >= 0) close(fd); }
  PrepFD(const PrepFD &) = delete;
};

bool VerifyFixture(const std::string &profile, const std::string &address,
                   const std::string &expected_exe, pid_t expected_pid,
                   int &result) {
  PrepFD dir(open(profile.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  struct stat st{};
  if (dir.fd < 0 || fstat(dir.fd, &st) || st.st_uid != geteuid() || (st.st_mode & 077)) return false;
  PrepFD key(openat(dir.fd, ".key", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (key.fd < 0 || fstat(key.fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) || st.st_size > 32) return false;
  char data[33]{};
  auto size = read(key.fd, data, 32);
  if (size <= 0 || std::string(data, size) != std::to_string(expected_pid)) return false;
  PrepFD connection(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (connection.fd < 0) return false;
  sockaddr_un endpoint{};
  endpoint.sun_family = AF_UNIX;
  memcpy(endpoint.sun_path + 1, address.data(), address.size());
  if (connect(connection.fd, reinterpret_cast<sockaddr *>(&endpoint),
              offsetof(sockaddr_un, sun_path) + address.size() + 1)) return false;
  ucred cred{}; socklen_t length = sizeof(cred);
  if (getsockopt(connection.fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) ||
      cred.uid != geteuid() || cred.pid != expected_pid) return false;
  char exe[4096];
  auto count = readlink(("/proc/" + std::to_string(cred.pid) + "/exe").c_str(), exe, sizeof(exe));
  if (count <= 0 || count == static_cast<ssize_t>(sizeof(exe)) || std::string(exe, count) != expected_exe) return false;
  result = std::exchange(connection.fd, -1);  // Same verified socket; no reconnect.
  return true;
}

void PreparationProbe(const std::string &mode) {
  char pattern[] = "/tmp/mozkey-preparation-XXXXXX";
  const char *created = mkdtemp(pattern);
  Check(created != nullptr, "fixture directory");
  const std::string profile(created);
  const std::string key = profile + "/.key";
  { std::ofstream stream(key); stream << (mode == "wrong-pid" ? getpid() + 1 : getpid()); Check(stream.good(), "key fixture write"); }
  Check(chmod(key.c_str(), 0600) == 0, "key permissions");
  if (mode == "public-key") Check(chmod(key.c_str(), 0644) == 0, "public fixture");
  if (mode == "symlink") {
    Check(rename(key.c_str(), (profile + "/target").c_str()) == 0, "rename fixture");
    Check(symlink("target", key.c_str()) == 0, "symlink fixture");
  }
  PrepFD listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  std::string address = "a5-" + profile.substr(profile.rfind('/') + 1);
  sockaddr_un endpoint{}; endpoint.sun_family = AF_UNIX;
  memcpy(endpoint.sun_path + 1, address.data(), address.size());
  Check(bind(listener.fd, reinterpret_cast<sockaddr *>(&endpoint),
             offsetof(sockaddr_un, sun_path) + address.size() + 1) == 0 && listen(listener.fd, 1) == 0, "fixture endpoint");
  char exe[4096]; auto n = readlink("/proc/self/exe", exe, sizeof(exe));
  Check(n > 0 && n < static_cast<ssize_t>(sizeof(exe)), "fixture identity");
  const std::string expected_exe = mode == "wrong-exe" ? "/invalid/fixture" : std::string(exe, n);
  const pid_t expected_pid = mode == "wrong-pid" ? getpid() + 1 : getpid();
  PrepFD notification(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
  Check(notification.fd >= 0, "eventfd");
  fcitx::EventLoop loop;
  bool addon_alive = true, accepted = false, got_notification = false;
  std::atomic<bool> cancel{false}, done{false};
  int verified = -1; bool valid = false;
  const auto start = fcitx::now(CLOCK_MONOTONIC);
  uint64_t next = start + 5000;
  double lag = 0, completion_ms = 0;
  int ticks = 0;
  auto heartbeat = loop.addTimeEvent(CLOCK_MONOTONIC, next, 1,
      [&](fcitx::EventSourceTime *source, uint64_t) {
        auto now = fcitx::now(CLOCK_MONOTONIC);
        lag = std::max(lag, now > next ? (now-next)/1000.0 : 0.0);
        ++ticks; next = now + 5000; source->setTime(next); source->setEnabled(true); return true;
      });
  heartbeat->setOneShot();
  auto io = loop.addIOEvent(notification.fd, fcitx::IOEventFlag::In,
      [&](fcitx::EventSourceIO *source, int fd, fcitx::IOEventFlags) {
        uint64_t value; auto ignored = read(fd, &value, sizeof(value)); (void)ignored;
        if (done.load(std::memory_order_acquire)) {
          got_notification = true;
          accepted = addon_alive && !cancel.load();
          completion_ms = (fcitx::now(CLOCK_MONOTONIC)-start)/1000.0;
          source->setEnabled(false); loop.exit();
        }
        return true;
      });
  auto cancellation = loop.addTimeEvent(CLOCK_MONOTONIC, start + 50000, 1,
      [&](fcitx::EventSourceTime *, uint64_t) {
        if (mode == "cancel" || mode == "cancel-after-verify") { addon_alive = false; cancel.store(true); }
        return true;
      });
  cancellation->setOneShot();
  auto watchdog = loop.addTimeEvent(CLOCK_MONOTONIC, start + 2000000, 1,
      [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return true; });
  watchdog->setOneShot();
  // The harness root outlives the simulated addon and joins AFTER completion.
  // Arbitrary stuck syscalls and real Fcitx process shutdown remain unproven.
  std::thread worker([&] {
    if (mode == "cancel-after-verify") valid = VerifyFixture(profile, address, expected_exe, expected_pid, verified);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    if (mode != "cancel-after-verify" && !cancel.load()) valid = VerifyFixture(profile, address, expected_exe, expected_pid, verified);
    done.store(true, std::memory_order_release);
    uint64_t value = 1; auto ignored = write(notification.fd, &value, sizeof(value)); (void)ignored;
  });
  const bool loop_ok = loop.exec();
  const bool completed_before_join = done.load(std::memory_order_acquire);
  worker.join();  // Finite fixture cleanup; not a product shutdown strategy.
  io.reset(); heartbeat.reset(); cancellation.reset(); watchdog.reset();
  PrepFD transferred(verified);
  unlink(key.c_str()); unlink((profile + "/target").c_str()); rmdir(profile.c_str());
  Check(loop_ok && completed_before_join && got_notification && ticks > 20, "preparation continuity/completion");
  Check(accepted == (mode != "cancel" && mode != "cancel-after-verify"), "cancelled result applied");
  Check(valid == (mode == "normal" || mode == "cancel-after-verify"), "identity/private-file rejection");
  std::cout << "PREP_METRIC mode=" << mode << " completion_ms=" << completion_ms
            << " ticks=" << ticks << " heartbeat_lag_max_ms=" << lag
            << " verified=" << valid << " applied=" << (valid && accepted) << "\n";
}
