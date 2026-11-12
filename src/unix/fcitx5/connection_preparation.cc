// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/connection_preparation.h"
#include <algorithm>
#include <utility>

namespace mozc::fcitx5 {
std::unique_ptr<PreparationWorker> StartConnectionPreparation(
    LinuxDeadlineEndpoint endpoint, std::chrono::milliseconds budget) {
  using namespace std::chrono_literals;
  budget = std::clamp(budget, 0ms, 2000ms);
  return PreparationWorker::Start(
      [endpoint = std::move(endpoint), budget](const std::atomic<bool>& cancelled) {
        if (cancelled.load()) return -1;
        auto deadline = LinuxDeadline::After(LinuxDeadline::Now, budget);
        return PrepareLinuxDeadlineSocket(endpoint, deadline);
      });
}
}  // namespace mozc::fcitx5
