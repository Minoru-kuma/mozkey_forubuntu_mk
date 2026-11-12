// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <fcitx-utils/library.h>
#include <dlfcn.h>

// Tests that loader code pinning does not make worker destruction asynchronous.
// A PASS here means that this UNSUITABLE owner design was correctly rejected.
void OwnerLifetimeGate(const char *path) {
  fcitx::Library library(path);
  Check(library.load(), "owner fixture library load");
  auto create = fcitx::Library::toFunction<void *()>(library.resolve("a5_owner_create"));
  auto close_owner = fcitx::Library::toFunction<void(void *)>(library.resolve("a5_owner_close"));
  Check(create && close_owner, "owner fixture symbols");
  std::unique_ptr<void, decltype(close_owner)> owner(create(), close_owner);
  Check(owner != nullptr, "owner fixture creation");
  Check(library.unload(), "owner fixture wrapper unload");
  void *pinned = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
  // Do not call code after dlclose unless a new loader handle protects it.
  // Default PreventUnloadHint is expected on this Linux Fcitx5 build.
  if (!pinned) {
    // This would be an unsafe platform for this fixture's ownership contract.
    std::cerr << "OWNER_GATE unsupported: library not retained\n";
    std::_Exit(2);
  }
  fcitx::EventLoop loop;
  auto start = fcitx::now(CLOCK_MONOTONIC);
  auto next = start + 5000;
  int ticks = 0;
  double lag = 0, destruction_ms = 0;
  auto heartbeat = loop.addTimeEvent(CLOCK_MONOTONIC, next, 1,
      [&](fcitx::EventSourceTime *source, uint64_t) {
        auto now = fcitx::now(CLOCK_MONOTONIC);
        lag = std::max(lag, now > next ? (now - next) / 1000.0 : 0.0);
        ++ticks; next = now + 5000;
        source->setTime(next); source->setEnabled(true); return true;
      });
  heartbeat->setOneShot();
  auto unload = loop.addTimeEvent(CLOCK_MONOTONIC, start + 10000, 1,
      [&](fcitx::EventSourceTime *, uint64_t) {
        auto begin = fcitx::now(CLOCK_MONOTONIC);
        owner.reset();
        destruction_ms = (fcitx::now(CLOCK_MONOTONIC) - begin) / 1000.0;
        return true;
      });
  unload->setOneShot();
  auto end = loop.addTimeEvent(CLOCK_MONOTONIC, start + 550000, 1,
      [&](fcitx::EventSourceTime *, uint64_t) { loop.exit(); return true; });
  end->setOneShot();
  bool loop_ok = loop.exec();
  heartbeat.reset(); unload.reset(); end.reset();
  dlclose(pinned);
  Check(loop_ok && destruction_ms >= 250 && lag >= 250, "negative owner gate not exercised");
  std::cout << "OWNER_GATE code_pinned=1 destruction_ms=" << destruction_ms
            << " heartbeat_lag_max_ms=" << lag << " ticks=" << ticks
            << " result=REJECTED_FOR_PRODUCT\n";
}
