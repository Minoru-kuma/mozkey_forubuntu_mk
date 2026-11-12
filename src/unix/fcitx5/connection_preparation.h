// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_CONNECTION_PREPARATION_H_
#define MOZKEY_UNIX_FCITX5_CONNECTION_PREPARATION_H_
#include <chrono>
#include <memory>
#include "ipc/linux_deadline.h"
#include "unix/fcitx5/preparation_worker.h"

namespace mozc::fcitx5 {
// Explicit endpoint values only. Discovering the profile/launching the server
// on the input loop is not part of this boundary. No implicit restart/replay.
std::unique_ptr<PreparationWorker> StartConnectionPreparation(
    LinuxDeadlineEndpoint endpoint, std::chrono::milliseconds budget);
}  // namespace mozc::fcitx5
#endif
