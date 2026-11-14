// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/frame_translator.h"
#include <fcitx-utils/utf8.h>
namespace mozc::fcitx5 {
void TranslateInputFrame(InputFrame& frame, fcitx::KeyEventHandler& handler,
                         config::Config::PreeditMethod method, const Compose& compose) {
  if (!frame.physical) return;
  frame.request.reset();
  commands::KeyEvent key;
  bool composed = false;
  if (!frame.release && !frame.raw.states().testAny(
          fcitx::KeyStates{fcitx::KeyState::Ctrl, fcitx::KeyState::Super}) && compose) {
    const auto result = compose(frame.raw.sym());
    if (result.kind == ComposeResult::Pending) {
      frame.compose = InputFrame::Compose::Pending; return;
    }
    if (result.kind == ComposeResult::Cancelled) {
      // Escape is explicit cancellation. Other invalid sequences are retained
      // for inspection, not silently discarded or forwarded out of order.
      frame.compose = frame.raw.sym() == FcitxKey_Escape ? InputFrame::Compose::Cancelled : InputFrame::Compose::Error;
      return;
    }
    if (result.kind == ComposeResult::Complete) {
      if (result.text.empty() || result.text.size() > 65536 ||
          !fcitx::utf8::validate(result.text)) { frame.compose = InputFrame::Compose::Error; return; }
      frame.composed_text = result.text;
      frame.compose = InputFrame::Compose::Complete;
      composed = handler.GetKeyEvent(result.text, method, frame.japanese_layout, &key);
      if (!composed) { frame.compose = InputFrame::Compose::Error; return; }
    }
  }
  if (!composed && !handler.GetKeyEvent(frame.raw.sym(), frame.raw.code(), frame.raw.states(),
                                      method, frame.japanese_layout, frame.release, &key)) return;
  frame.request.emplace(); frame.request->set_type(commands::Input::SEND_KEY);
  *frame.request->mutable_key() = std::move(key);
}
}  // namespace mozc::fcitx5
