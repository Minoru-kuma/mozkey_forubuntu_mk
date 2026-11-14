// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_RESPONSE_GUARD_H_
#define MOZKEY_UNIX_FCITX5_RESPONSE_GUARD_H_
#include <fcitx/surroundingtext.h>
#include "unix/fcitx5/input_pipeline.h"
namespace mozc::fcitx5 {
enum class ResponseDecision { Apply, SkipUI, Hold };
// Validates effects before any frontend call. Hold leaves output unchanged.
ResponseDecision PrepareResponse(const InputFrame& frame, uint64_t generation,
                                 const fcitx::SurroundingText& current,
                                 bool supports_deletion, commands::Output* output);
}  // namespace mozc::fcitx5
#endif
