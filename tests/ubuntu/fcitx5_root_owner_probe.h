// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <chrono>
#include <thread>

struct WorkerAPI {
  fcitx::Library library;
  explicit WorkerAPI(const char* path) : library(path) {
    Check(library.load(), "root worker fixture load");
  }
  template <typename T> auto symbol(const char* name) {
    auto fn = fcitx::Library::toFunction<T>(library.resolve(name));
    Check(fn != nullptr, "worker symbol");
    return fn;
  }
};
void RootOwnerProbe(const char* path) {
  WorkerAPI api(path);
  auto start_job = api.symbol<void*(int)>("a5_worker_start");
  auto close_job = api.symbol<void(void*)>("a5_worker_close");
  auto busy = api.symbol<bool()>("a5_worker_busy");
  auto* owner = start_job(350);
  Check(owner != nullptr, "root worker start");
  // Keep a separate handle to inspect the same pinned root after wrapper unload.
  WorkerAPI reloaded(path);
  Check(api.library.unload(), "root worker wrapper unload");
  fcitx::EventLoop loop;
  const auto begin = fcitx::now(CLOCK_MONOTONIC);
  auto next = begin + 5000;
  double lag = 0, cancel_ms = 0;
  int ticks = 0;
  bool refused = false, reclaimed = false;
  auto heartbeat = loop.addTimeEvent(CLOCK_MONOTONIC, next, 1,
      [&](fcitx::EventSourceTime* source, uint64_t) {
        auto now = fcitx::now(CLOCK_MONOTONIC);
        lag = std::max(lag, now > next ? (now - next) / 1000.0 : 0.0);
        ++ticks; next = now + 5000;
        source->setTime(next); source->setEnabled(true); return true;
      });
  heartbeat->setOneShot();
  auto cancel = loop.addTimeEvent(CLOCK_MONOTONIC, begin + 10000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) {
        const auto t = fcitx::now(CLOCK_MONOTONIC);
        close_job(owner); owner = nullptr;
        cancel_ms = (fcitx::now(CLOCK_MONOTONIC) - t) / 1000.0;
        refused = true;
        for (int i = 0; i < 1000; ++i) {
          auto* another = start_job(0);
          refused = refused && another == nullptr;
          if (another) close_job(another);
        }
        return true;
      });
  cancel->setOneShot();
  auto end = loop.addTimeEvent(CLOCK_MONOTONIC, begin + 550000, 1,
      [&](fcitx::EventSourceTime*, uint64_t) {
        reclaimed = !busy(); loop.exit(); return true;
      });
  end->setOneShot();
  Check(loop.exec(), "root worker event loop");
  Check(refused && reclaimed && ticks > 70 && lag < 50 && cancel_ms < 20,
        "root worker ownership/cancel/continuity");
  // Many cancel/reload attempts cannot allocate additional workers while busy.
  std::cout << "ROOT_OWNER cancel_ms=" << cancel_ms << " lag_max_ms=" << lag
            << " ticks=" << ticks << " busy_refused=" << refused
            << " reclaimed=" << reclaimed << "\n";
}
int ProcessExitProbe(const char* path, int delay_ms) {
  WorkerAPI api(path);
  auto start_job = api.symbol<void*(int)>(delay_ms < 0 ? "a5_worker_start_tls" : "a5_worker_start");
  auto close_job = api.symbol<void(void*)>("a5_worker_close");
  auto running = api.symbol<int()>(delay_ms < 0 ? "a5_worker_tls_started" : "a5_worker_running");
  auto wait_exit = api.symbol<bool(int)>("a5_worker_wait_exit");
  auto tls_exits = api.symbol<int()>("a5_worker_tls_exits");
  void* owner = start_job(delay_ms < 0 ? -delay_ms : delay_ms);
  Check(owner != nullptr, "exit worker start");
  const auto startup = std::chrono::steady_clock::now();
  while (delay_ms != 0 && running() == 0) {
    Check(std::chrono::steady_clock::now() - startup < std::chrono::seconds(1),
          "worker did not start");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  close_job(owner);
  const auto begin = std::chrono::steady_clock::now();
  bool completed = wait_exit(400);
  const double elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
  Check(completed == (std::abs(delay_ms) != 3500) && elapsed < 500,
        "bounded process-exit wait");
  Check(start_job(0) == nullptr, "shutdown seal rejects new jobs");
  if (completed) {
    Check(delay_ms == 0 || tls_exits() == 1, "successful shutdown includes TLS destruction");
    Check(wait_exit(0), "repeated shutdown of joined worker");
  }
  std::cout << "PROCESS_EXIT delay_ms=" << delay_ms << " wait_ms=" << elapsed
            << " completed=" << completed << " tls_exits=" << tls_exits() << "\n";
  // Normal C++ return, including Library destruction; no _Exit workaround.
  return 0;
}
