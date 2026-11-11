// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Standalone real-library API probe. No Instance/addon/frontend/server startup.
#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <sys/socket.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void Check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

int OpenFDs() {
  auto *directory = opendir("/proc/self/fd");
  Check(directory != nullptr, "fd inventory");
  int count = 0;
  while (readdir(directory)) ++count;
  closedir(directory);
  return count;
}

struct SyntheticIC : fcitx::InputContext {
  explicit SyntheticIC(fcitx::InputContextManager &m) : InputContext(m) { created(); }
  ~SyntheticIC() override { destroy(); }
  const char *frontend() const override { return "a5-api-fixture"; }
  std::vector<std::string> delivered;
  void commitStringImpl(const std::string &s) override { delivered.push_back(s); }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &k) override {
    delivered.push_back(std::to_string(k.time()) + (k.isRelease() ? "up" : "down"));
  }
  void updatePreeditImpl() override {}
};

void ContextContracts() {
  fcitx::InputContextManager manager;
  Check(manager.instance() == nullptr, "unexpected running Instance");
  auto ic = std::make_unique<SyntheticIC>(manager);
  auto weak = ic->watch();
  auto uuid = ic->uuid();
  Check(manager.findByUUID(uuid) == ic.get(), "UUID lookup");
  Check(!ic->hasFocus(), "initial focus");
  ic->focusIn();
  Check(ic->hasFocus(), "focusIn");
  fcitx::KeyEvent key(ic.get(), fcitx::Key(FcitxKey_a), false, 123);
  Check(!key.filtered() && !key.accepted(), "initial key state");
  key.filterAndAccept();
  Check(key.filtered() && key.accepted(), "filter and accept");
  Check(key.rawKey() == fcitx::Key(FcitxKey_a) && key.time() == 123, "key identity");
  // This blocker is a FRONTEND-only ordering API, not an IME async-key solution.
  ic->setBlockEventToClient(true);
  ic->forwardKey(key.rawKey(), false, 123);
  ic->forwardKey(key.rawKey(), true, 124);
  ic->commitString("fixture");
  Check(ic->delivered.empty() && ic->hasPendingEventsStrictOrder(), "blocked queue");
  ic->setBlockEventToClient(false);
  Check(ic->delivered == std::vector<std::string>({"123down", "124up", "fixture"}), "delivery order");
  ic->focusOut();
  Check(!ic->hasFocus(), "focusOut");
  fcitx::KeyEvent unfocused(ic.get(), fcitx::Key(FcitxKey_a));
  Check(!ic->keyEvent(unfocused), "unfocused key rejected");
  ic.reset();
  Check(!weak.isValid() && weak.get() == nullptr, "watch after destruction");
  Check(manager.findByUUID(uuid) == nullptr, "UUID removed");
  std::cout << "IC_CONTRACTS PASS (synthetic frontend, no engine)\n";
}

void Probe(const std::string &mode) {
  fcitx::EventLoop loop;
  fcitx::InputContextManager manager;
  auto context = std::make_unique<SyntheticIC>(manager);
  context->focusIn();
  auto watch = context->watch();
  int sockets[2];
  Check(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0, "socketpair");
  int client = sockets[0], peer = sockets[1];
  const auto start = fcitx::now(CLOCK_MONOTONIC);
  int ticks = 0, ticks_at_reply = 0, commits = 0, defers = 0, cancelled_callback = 0;
  int epoch = 1;
  bool closed = false, applied = false, fixture_ok = true;
  std::string buffer, preedit = "fixture";
  double lag_max = 0, callback_max = 0, response_ms = -1, blocking_defer_ms = 0;
  uint64_t next_tick = start + 5000;
  std::unique_ptr<fcitx::EventSourceIO> io;
  std::unique_ptr<fcitx::EventSource> deferred, duplicate, blocker;
  std::vector<std::unique_ptr<fcitx::EventSourceTime>> timers;
  auto timer = [&](uint64_t at, fcitx::TimeCallback cb) {
    auto source = loop.addTimeEvent(CLOCK_MONOTONIC, at, 1, std::move(cb));
    source->setOneShot();
    timers.push_back(std::move(source));
  };
  auto close_request = [&] {
    ++epoch;
    io->setEnabled(false);
    if (client >= 0) close(std::exchange(client, -1));
    closed = true;  // preedit deliberately preserved.
  };
  auto apply = [&] {
    ++defers;
    auto *ic = watch.get();
    if (!ic || !ic->hasFocus() || epoch != 1 || applied || buffer != "OK") return;
    applied = true;
    ++commits;
    ic->commitString("fixture");
    preedit.clear();
    response_ms = (fcitx::now(CLOCK_MONOTONIC) - start) / 1000.0;
    ticks_at_reply = ticks;
  };
  io = loop.addIOEvent(client, fcitx::IOEventFlag::In,
      [&](fcitx::EventSourceIO *source, int fd, fcitx::IOEventFlags) {
        const auto begin = fcitx::now(CLOCK_MONOTONIC);
        char bytes[4096];
        const auto n = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (n > 0) buffer.append(bytes, n);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
          source->setEnabled(false);
          close(std::exchange(client, -1));
          closed = true;
          deferred = loop.addDeferEvent([&](fcitx::EventSource *) { apply(); return true; });
          deferred->setOneShot();
          duplicate = loop.addDeferEvent([&](fcitx::EventSource *) { apply(); return true; });
          duplicate->setOneShot();
        }
        callback_max = std::max(callback_max, (fcitx::now(CLOCK_MONOTONIC) - begin) / 1000.0);
        return true;
      });
  auto heartbeat = loop.addTimeEvent(CLOCK_MONOTONIC, next_tick, 1,
      [&](fcitx::EventSourceTime *source, uint64_t) {
        const auto now = fcitx::now(CLOCK_MONOTONIC);
        lag_max = std::max(lag_max, now > next_tick ? (now - next_tick) / 1000.0 : 0.0);
        ++ticks;
        next_tick = now + 5000;
        source->setTime(next_tick);
        source->setEnabled(true);
        return true;
      });
  heartbeat->setOneShot();
  auto poison = loop.addTimeEvent(CLOCK_MONOTONIC, start + 100000, 1,
      [&](fcitx::EventSourceTime *, uint64_t) { ++cancelled_callback; return true; });
  poison->setOneShot();
  timer(start + 10000, [&](fcitx::EventSourceTime *, uint64_t) { poison.reset(); return true; });
  if (mode == "focus" || mode == "destroy") {
    timer(start + 50000, [&](fcitx::EventSourceTime *, uint64_t) {
      ++epoch;
      if (mode == "focus") context->focusOut();
      else context.reset();
      return true;
    });
  }
  if (mode == "blocking-defer-control") {
    timer(start + 25000, [&](fcitx::EventSourceTime *, uint64_t) {
      blocker = loop.addDeferEvent([&](fcitx::EventSource *) {
        auto begin = fcitx::now(CLOCK_MONOTONIC);
        timespec duration{0, 350000000};
        while (nanosleep(&duration, &duration) < 0 && errno == EINTR) {}
        blocking_defer_ms = (fcitx::now(CLOCK_MONOTONIC) - begin) / 1000.0;
        return true;
      });
      blocker->setOneShot();
      return true;
    });
  }
  if (mode == "cancel") {
    timer(start + 50000, [&](fcitx::EventSourceTime *, uint64_t) { close_request(); return true; });
    timer(start + 350000, [&](fcitx::EventSourceTime *, uint64_t) {
      buffer = "OK";  // already-posted old completion; epoch must reject it.
      deferred = loop.addDeferEvent([&](fcitx::EventSource *) { apply(); return true; });
      deferred->setOneShot();
      return true;
    });
  }
  if (mode != "silent") {
    timer(start + (mode == "normal" ? 10000 : 350000),
        [&](fcitx::EventSourceTime *, uint64_t) {
          if (mode == "disconnect") {
            close(std::exchange(peer, -1));
          } else {
            const char *payload = mode == "fragment" ? "O" : "OK";
            auto n = send(peer, payload, mode == "fragment" ? 1 : 2, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (mode != "cancel") fixture_ok = fixture_ok && n > 0;
            if (mode != "fragment") shutdown(peer, SHUT_WR);
          }
          return true;
        });
  }
  if (mode == "fragment") {
    timer(start + 410000, [&](fcitx::EventSourceTime *, uint64_t) {
      fixture_ok = fixture_ok && send(peer, "K", 1, MSG_DONTWAIT | MSG_NOSIGNAL) == 1;
      shutdown(peer, SHUT_WR);
      return true;
    });
  }
  timer(start + 450000, [&](fcitx::EventSourceTime *, uint64_t) {
    if (!closed) close_request();
    return true;
  });
  auto exit_event = loop.addExitEvent([&](fcitx::EventSource *) {
    io->setEnabled(false);
    heartbeat->setEnabled(false);
    return true;
  });
  timer(start + 550000, [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return true; });
  try {
    Check(loop.exec(), "event loop exit");
    Check(fixture_ok, "fixture send");
    Check(ticks > (mode == "blocking-defer-control" ? 20 : 40) && cancelled_callback == 0 && closed, "continuity or cancellation");
    const bool success = mode == "normal" || mode == "delay" || mode == "fragment" || mode == "blocking-defer-control";
    if (mode == "blocking-defer-control") Check(lag_max >= 300, "negative control not detected");
    if (context) Check(context->delivered.size() == static_cast<size_t>(commits), "actual IC commit count");
    else Check(!watch.isValid(), "destroyed context watch");
    Check(commits == (success ? 1 : 0), "commit count");
    Check(preedit == (success ? "" : "fixture"), "preedit state");
    if (mode == "delay" || mode == "fragment") Check(ticks_at_reply > 20, "blocked response wait");
    if (mode == "cancel") Check(defers == 1, "late defer executed for epoch check");
    std::cout << "API_METRIC mode=" << mode << " backend=" << loop.implementation()
              << " ticks=" << ticks << " ticks_at_reply=" << ticks_at_reply
              << " response_ms=" << response_ms << " callback_max_ms=" << callback_max
              << " heartbeat_lag_max_ms=" << lag_max << " blocking_defer_ms=" << blocking_defer_ms << " commits=" << commits << "\n";
  } catch (...) {
    io.reset(); timers.clear(); heartbeat.reset(); poison.reset();
    deferred.reset(); duplicate.reset(); blocker.reset(); exit_event.reset();
    if (client >= 0) close(client);
    if (peer >= 0) close(peer);
    throw;
  }
  io.reset(); timers.clear(); heartbeat.reset(); poison.reset();
  deferred.reset(); duplicate.reset(); blocker.reset(); exit_event.reset();
  if (client >= 0) close(client);
  if (peer >= 0) close(peer);
}

int main() {
  try {
    ContextContracts();
    const auto fds = OpenFDs();
    for (const auto *mode : {"normal", "delay", "fragment", "disconnect", "silent", "cancel", "focus", "destroy", "blocking-defer-control"}) Probe(mode);
    Check(OpenFDs() == fds, "FD leak after loops destroyed");
    std::cout << "FCITX5_API_PROBE PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FCITX5_API_PROBE FAIL: " << e.what() << "\n";
    return 1;
  }
}
