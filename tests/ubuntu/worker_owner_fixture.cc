// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Negative lifecycle fixture only. Never linked into an IME.
#include <atomic>
#include <chrono>
#include <thread>

struct Owner {
  std::atomic<bool> stopped{false};
  std::thread worker;
  Owner() : worker([this] {
    // A finite stand-in for a syscall that cannot observe cancellation yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    (void)stopped.load();
  }) {}
  ~Owner() {
    stopped.store(true);
    worker.join();
  }
};
extern "C" void *a5_owner_create() { return new Owner; }
extern "C" void a5_owner_close(void *owner) { delete static_cast<Owner *>(owner); }

// Positive candidate: process-owned, one worker across wrapper unload/reload.
#include <sys/eventfd.h>
#include "unix/fcitx5/preparation_worker.h"
using PreparationWorker = mozc::fcitx5::PreparationWorker;
static std::atomic<int> running{0};
static std::atomic<int> tls_exits{0};
struct ThreadExitMarker {
  int delay_ms = 0;
  ~ThreadExitMarker() {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    tls_exits.fetch_add(1);
  }
};
static std::atomic<int> tls_started{0};
extern "C" void *a5_worker_start(int delay_ms) {
  return PreparationWorker::Start([delay_ms](const std::atomic<bool>&) {
    thread_local ThreadExitMarker marker;
    (void)marker;
    running.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    running.fetch_sub(1);
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  }).release();
}
extern "C" void a5_worker_close(void *p) { delete static_cast<PreparationWorker*>(p); }
extern "C" bool a5_worker_busy() { return PreparationWorker::Busy(); }
extern "C" int a5_worker_running() { return running.load(); }
extern "C" bool a5_worker_wait_exit(int ms) {
  return PreparationWorker::WaitForProcessExit(std::chrono::milliseconds(ms));
}

extern "C" int a5_worker_tls_exits() { return tls_exits.load(); }

extern "C" void* a5_worker_start_tls(int delay_ms) {
  return PreparationWorker::Start([delay_ms](const std::atomic<bool>&) {
    thread_local ThreadExitMarker marker;
    marker.delay_ms = delay_ms; tls_started.store(1);
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  }).release();
}
extern "C" int a5_worker_tls_started() { return tls_started.load(); }
