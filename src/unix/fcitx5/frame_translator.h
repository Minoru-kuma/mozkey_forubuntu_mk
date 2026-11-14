// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_FRAME_TRANSLATOR_H_
#define MOZKEY_UNIX_FCITX5_FRAME_TRANSLATOR_H_
#include <functional>
#include "unix/fcitx5/fcitx_key_event_handler.h"
#include "unix/fcitx5/input_pipeline.h"
namespace mozc::fcitx5 {
struct ComposeResult {
  enum Kind { None, Pending, Complete, Cancelled } kind = None;
  std::string text;
};
using Compose = std::function<ComposeResult(fcitx::KeySym)>;
void TranslateInputFrame(InputFrame& frame, fcitx::KeyEventHandler& handler,
                         config::Config::PreeditMethod method, const Compose& compose);
}  // namespace mozc::fcitx5
#endif
