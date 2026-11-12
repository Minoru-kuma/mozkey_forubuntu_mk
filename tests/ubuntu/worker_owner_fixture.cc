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
