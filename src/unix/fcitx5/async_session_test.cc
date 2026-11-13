// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_session.h"
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>
#include "testing/gunit.h"

namespace mozc::fcitx5 {
namespace {
using namespace std::chrono_literals;
class Peer {
 public:
  ~Peer() { for (auto& thread : threads) thread.join(); }
  int Connect() {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets)) return -1;
    threads.emplace_back([this, fd = sockets[1]] {
      std::string data;
      for (;;) {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 2500) <= 0) break;
        char bytes[8192]; auto n = recv(fd, bytes, sizeof(bytes), 0);
        if (!n) break;
        if (n > 0) data.append(bytes, n);
        if (data.size() > 2 * 1024 * 1024) break;
      }
      commands::Input input;
      if (input.ParseFromString(data)) {
        { std::lock_guard lock(mutex); received.push_back(input); }
        if (input.type() == commands::Input::SEND_KEY) std::this_thread::sleep_for(delay);
        if (!silent) {
          commands::Output out;
          out.set_id(wrong_id && input.type() == commands::Input::SEND_KEY ? 999 : 77);
          out.set_error_code(commands::Output::SESSION_SUCCESS);
          out.set_consumed(true);
          out.mutable_preedit()->set_cursor(1);
          auto* segment = out.mutable_preedit()->add_segment();
          segment->set_annotation(commands::Preedit::Segment::UNDERLINE);
          segment->set_value("あ"); segment->set_value_length(1);
          auto bytes = out.SerializeAsString();
          const auto sent = send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
          (void)sent;
        }
      }
      close(fd);
    });
    return sockets[0];
  }
  std::vector<commands::Input> Snapshot() { std::lock_guard lock(mutex); return received; }
  std::chrono::milliseconds delay{0};
  bool wrong_id = false, silent = false;
 private:
  std::mutex mutex;
  std::vector<commands::Input> received;
  std::vector<std::thread> threads; // Modified only by the single preparation worker.
};
commands::Input KeyInput(char key) {
  commands::Input in; in.set_type(commands::Input::SEND_KEY); in.mutable_key()->set_key_code(key); return in;
}
void RunLoop(fcitx::EventLoop& loop, unsigned ms) {
  auto stop = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + ms * 1000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) { loop.exit(); return true; });
  stop->setOneShot(); EXPECT_TRUE(loop.exec());
}
TEST(AsyncSessionTest, InitializationOrderAndTwoKeys) {
  fcitx::EventLoop loop; auto peer = std::make_shared<Peer>();
  AsyncSession session(loop, {}, [peer](const auto&) { return peer->Connect(); });
  int calls = 0;
  for (char key : {'a', 'b'}) ASSERT_TRUE(session.Submit(KeyInput(key), [&](bool ok, auto out) {
    EXPECT_TRUE(ok); EXPECT_EQ(out.id(), 77); ++calls;
  }));
  EXPECT_EQ(calls, 0); RunLoop(loop, 150);
  EXPECT_EQ(calls, 2);
  auto received = peer->Snapshot(); ASSERT_EQ(received.size(), 4);
  EXPECT_EQ(received[0].type(), commands::Input::CREATE_SESSION);
  EXPECT_EQ(received[1].type(), commands::Input::SET_REQUEST);
  EXPECT_EQ(received[2].key().key_code(), 'a'); EXPECT_EQ(received[3].key().key_code(), 'b');
}
TEST(AsyncSessionTest, SlowReplyKeepsLoopAlive) {
  for (auto delay : {0ms, 250ms, 500ms, 1000ms}) {
    fcitx::EventLoop loop; auto peer = std::make_shared<Peer>(); peer->delay = delay;
    AsyncSession session(loop, {}, [peer](const auto&) { return peer->Connect(); });
    int calls = 0, ticks = 0;
    const auto start = fcitx::now(CLOCK_MONOTONIC);
    auto previous = start; uint64_t max_gap = 0, latency = 0;
    auto tick = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 5000, 1,
        [&](fcitx::EventSourceTime* s, uint64_t) { ++ticks; auto now = fcitx::now(CLOCK_MONOTONIC); max_gap = std::max(max_gap, now-previous); previous = now; s->setTime(now+5000); s->setEnabled(true); return true; });
    tick->setOneShot();
    ASSERT_TRUE(session.Submit(KeyInput('a'), [&](bool ok, auto) { EXPECT_TRUE(ok); ++calls; latency = fcitx::now(CLOCK_MONOTONIC)-start; }));
    RunLoop(loop, delay.count() + 150);
    EXPECT_EQ(calls, 1); EXPECT_GT(ticks, delay.count()/10);
    std::cout << "ASYNC_SESSION_METRIC injected_ms=" << delay.count() << " reply_ms=" << latency/1000.0 << " heartbeat_max_gap_ms=" << max_gap/1000.0 << " ticks=" << ticks << std::endl;
  }
}
TEST(AsyncSessionTest, WrongSessionSealsWithoutReplay) {
  fcitx::EventLoop loop; auto peer = std::make_shared<Peer>(); peer->wrong_id = true;
  AsyncSession session(loop, {}, [peer](const auto&) { return peer->Connect(); });
  int calls = 0;
  for (char key : {'a','b'}) ASSERT_TRUE(session.Submit(KeyInput(key), [&](bool ok, auto) { EXPECT_FALSE(ok); ++calls; }));
  RunLoop(loop, 100);
  EXPECT_EQ(calls, 1); EXPECT_TRUE(session.failed());
  EXPECT_EQ(peer->Snapshot().size(), 3);
  EXPECT_FALSE(session.Submit(KeyInput('c'), {}));
}
TEST(AsyncSessionTest, DestroyInsideCompletion) {
  fcitx::EventLoop loop; auto peer = std::make_shared<Peer>();
  auto session = std::make_unique<AsyncSession>(loop, LinuxDeadlineEndpoint{}, [peer](const auto&) { return peer->Connect(); });
  int calls = 0;
  ASSERT_TRUE(session->Submit(KeyInput('a'), [&](bool ok, auto) { EXPECT_TRUE(ok); ++calls; session.reset(); }));
  RunLoop(loop, 100); EXPECT_EQ(calls, 1); EXPECT_EQ(session, nullptr);
}
TEST(AsyncSessionTest, CancelBlockedPreparationRetainsNoUICallback) {
  fcitx::EventLoop loop;
  AsyncSession session(loop, {}, [](const auto&) { std::this_thread::sleep_for(350ms); return -1; });
  int calls = 0;
  ASSERT_TRUE(session.Submit(KeyInput('a'), [&](bool, auto) { ++calls; }));
  auto cancel = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC)+10000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) { session.Cancel(); return true; });
  cancel->setOneShot(); RunLoop(loop, 400); EXPECT_EQ(calls, 0);
}
TEST(AsyncRpcTest, TimeoutDisconnectOversizeAndCancel) {
  for (int mode = 0; mode < 4; ++mode) {
    fcitx::EventLoop loop; int fds[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, fds), 0);
    int calls = 0; AsyncRpc::Status status = AsyncRpc::Status::Success;
    auto rpc = std::make_unique<AsyncRpc>(loop, fds[0], mode == 2 ? std::string(1024*1024+1,'x') : "request",
        fcitx::now(CLOCK_MONOTONIC)+20000, [&](auto s, auto bytes) { ++calls; status=s; EXPECT_TRUE(bytes.empty()); });
    if (mode == 1) { close(fds[1]); fds[1] = -1; }
    if (mode == 3) rpc.reset();
    RunLoop(loop, 50);
    EXPECT_EQ(calls, mode == 3 ? 0 : 1);
    if (mode != 3) EXPECT_NE(status, AsyncRpc::Status::Success);
    if (fds[1] >= 0) close(fds[1]);
  }
}
}  // namespace
}  // namespace mozc::fcitx5
