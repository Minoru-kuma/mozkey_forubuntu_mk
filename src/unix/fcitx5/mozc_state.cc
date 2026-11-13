// Copyright 2012~2013, Weng Xuetian <wengxt@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "unix/fcitx5/mozc_state.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/vlog.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "unix/fcitx5/fcitx_key_event_handler.h"
#include "unix/fcitx5/i18nwrapper.h"
#include "unix/fcitx5/mozc_engine.h"
#include "unix/fcitx5/mozc_response_parser.h"
#include "unix/fcitx5/surrounding_text_util.h"

namespace fcitx {

MozcState::MozcState(InputContext* ic, MozcEngine* engine)
    : ic_(ic), engine_(engine), callbacks_(engine->instance()->eventLoop()),
      handler_(std::make_unique<KeyEventHandler>()) {
  composition_mode_ = *engine_->config().initialMode;
  engine_->registerState(this);
}
MozcState::~MozcState() {
  lifetime_.reset(); Shutdown(); pipeline_.reset(); engine_->unregisterState(this);
}
void MozcState::Shutdown() {
  callbacks_.Invalidate(); ++epoch_;
  if (pipeline_) pipeline_->Hold("owner-shutdown");
  if (client_) client_->Cancel();
}
void MozcState::CancelCallbacks() { ++epoch_; callbacks_.Invalidate(); }

bool MozcState::EnsurePipeline() {
  if (pipeline_) return !pipeline_->suspended();
  retained_ = mozc::fcitx5::InputRetention::Reserve();
  if (!retained_) { SetAuxString(_("Retained input limit reached")); return false; }
  auto watch = ic_->watch();
  auto* client = GetClient();
  pipeline_ = std::make_unique<mozc::fcitx5::InputPipeline>(engine_->instance()->eventLoop(), retained_,
      [this, client](auto input, auto done) {
        input.mutable_config()->set_use_live_conversion(*engine_->config().liveConversion);
        input.mutable_config()->set_live_conversion_delay_msec(std::clamp(*engine_->config().liveDelay, 0, 5000));
        return client->Submit(std::move(input), std::move(done));
      },
      [client] { client->Cancel(); },
      [this, watch] {
        auto* ic = watch.get();
        return ic && ic->hasFocus() && engine_->instance()->inputMethod(ic) == "mozkey" &&
            !ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive);
      },
      [this, watch](auto output, auto forward) {
        auto* ic = watch.get();
        if (!ic || !ic->hasFocus() || engine_->instance()->inputMethod(ic) != "mozkey") return false;
        const auto frame = retained_->delivering;
        if (frame && !frame->physical && frame->generation != epoch_) {
          if (output.has_result()) { pipeline_->Hold("stale-command-result"); return false; }
          return true; // Superseded UI-only live response; no commit is dropped.
        }
        if (frame && frame->selected_length != 0) {
          const auto& surrounding = ic->surroundingText();
          if (!surrounding.isValid() || surrounding.text() != frame->selection_snapshot ||
              surrounding.cursor() != frame->cursor || surrounding.anchor() != frame->anchor) {
            pipeline_->Hold("selection-changed"); return false;
          }
          auto* range = output.mutable_deletion_range();
          range->set_offset(frame->selected_length > 0 ? -frame->selected_length : 0);
          range->set_length(frame->selected_length > 0 ? frame->selected_length : -frame->selected_length);
        }
        auto valid = DeliveryValid();
        if (frame && frame->request && !ParseResponse(output)) return false;
        // A commit/UI callback may destroy the IC; do not forward to another one.
        ic = watch.get();
        if (!ic || !ic->hasFocus()) return false;
        if (!valid()) return false;
        if (forward) ic->forwardKey(forward->raw, forward->release, forward->time);
        return watch.isValid();
      });
  pipeline_->SetReady(false);
  pipeline_->SetTranslator([this](auto& frame) { TranslateFrame(frame); });
  return true;
}
void MozcState::TranslateFrame(mozc::fcitx5::InputFrame& frame) {
  if (!frame.physical) return;
  mozc::commands::KeyEvent key;
  if (!handler_->GetKeyEvent(frame.raw.sym(), frame.raw.code(), frame.raw.states(),
                            preedit_method_, frame.japanese_layout, frame.release, &key)) {
    frame.request.reset(); return;
  }
  mozc::commands::Input input; input.set_type(mozc::commands::Input::SEND_KEY);
  *input.mutable_key() = std::move(key);
  SurroundingTextInfo text;
  if (ic_->surroundingText().text().size() <= 65536 && GetSurroundingText(ic_, &text, nullptr)) {
    input.mutable_context()->set_preceding_text(text.preceding_text);
    input.mutable_context()->set_following_text(text.following_text);
  }
  frame.request = std::move(input);
}
void MozcState::UpdatePreeditMethod() {
  if (!EnsurePipeline()) return;
  mozc::commands::Input input; input.set_type(mozc::commands::Input::GET_CONFIG);
  const auto watch = ic_->watch();
  const auto weak = std::weak_ptr<int>(lifetime_);
  GetClient()->Submit(std::move(input), [this, watch, weak](bool ok, auto output) {
    if (weak.expired() || !watch.isValid()) return;
    if (!ok || !output.has_config()) { pipeline_->Hold("configuration-failed"); return; }
    preedit_method_ = output.config().preedit_method();
    pipeline_->SetReady(true);
  });
}
void MozcState::ScheduleCallback(const mozc::commands::Output& output) {
  if (retained_ && retained_->delivering && retained_->delivering->generation != epoch_) return;
  if (ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) return;
  const auto expected = epoch_; const auto watch = ic_->watch();
  callbacks_.Schedule(output,
      [this, expected, watch] { return watch.isValid() && ic_->hasFocus() && expected == epoch_ &&
          engine_->instance()->inputMethod(ic_) == "mozkey"; },
      [this](const auto& command) { SubmitCommand(command); });
}
bool MozcState::ProcessKeyEvent(KeySym sym, uint32_t code, KeyStates states,
                                bool jp, bool release, uint32_t time) {
  if (ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) return false;
  if (!release) CancelCallbacks();
  if (!EnsurePipeline()) return false;
  if (pipeline_->suspended()) { ShowRetained(); return false; }
  mozc::fcitx5::InputFrame frame;
  frame.raw = Key(sym, states, code); frame.release = release; frame.time = time;
  frame.japanese_layout = jp; frame.generation = epoch_;
  // Reserve the frame before claiming the physical event. Translation happens
  // after asynchronous config initialization, preserving first-key ordering.
  const bool consumed = pipeline_->Submit(std::move(frame));
  if (!consumed) ShowRetained();
  return consumed;
}
bool MozcState::SubmitCommand(mozc::commands::SessionCommand command) {
  if (!EnsurePipeline() || pipeline_->suspended()) return false;
  mozc::fcitx5::InputFrame frame; frame.physical = false; frame.translated = true;
  frame.generation = epoch_;
  if (command.type() == mozc::commands::SessionCommand::CONVERT_REVERSE ||
      command.type() == mozc::commands::SessionCommand::RECONVERT_SELECTION_OR_INSERT_SPACE) {
    SurroundingTextInfo text;
    if (ic_->surroundingText().text().size() > 65536) return false;
    if (GetSurroundingText(ic_, &text, nullptr)) {
      frame.selection_snapshot = ic_->surroundingText().text();
      frame.cursor = ic_->surroundingText().cursor(); frame.anchor = ic_->surroundingText().anchor();
      frame.selected_length = text.relative_selected_length;
      command.set_text(text.selection_text);
    } else if (command.type() == mozc::commands::SessionCommand::CONVERT_REVERSE) return false;
    if (command.type() == mozc::commands::SessionCommand::RECONVERT_SELECTION_OR_INSERT_SPACE) {
      if (frame.selected_length == 0) {
        mozc::commands::Input input; input.set_type(mozc::commands::Input::SEND_KEY);
        input.mutable_key()->set_special_key(mozc::commands::KeyEvent::SPACE);
        frame.request = std::move(input); return pipeline_->Submit(std::move(frame));
      }
      command.set_type(mozc::commands::SessionCommand::CONVERT_REVERSE);
    }
  }
  mozc::commands::Input input; input.set_type(mozc::commands::Input::SEND_COMMAND);
  *input.mutable_command() = std::move(command); frame.request = std::move(input);
  return pipeline_->Submit(std::move(frame));
}
void MozcState::SelectCandidate(int id) {
  if (id == kBadCandidateId) return;
  CancelCallbacks();
  mozc::commands::SessionCommand command;
  command.set_type(mozc::commands::SessionCommand::SELECT_CANDIDATE); command.set_id(id);
  SubmitCommand(std::move(command));
}
void MozcState::Reset() {
  CancelCallbacks();
  if (pipeline_) pipeline_->Hold("context-reset");
  // Old input remains process-owned; reset never replays or silently clears it.
  pipeline_.reset(); retained_.reset(); client_.reset(); handler_->Clear();
  preedit_ = Text(); ic_->inputPanel().reset();
  engine_->instance()->resetCompose(ic_);
}
bool MozcState::Paging(bool prev) {
  mozc::commands::SessionCommand command;
  command.set_type(prev ? mozc::commands::SessionCommand::CONVERT_PREV_PAGE :
                         mozc::commands::SessionCommand::CONVERT_NEXT_PAGE);
  return SubmitCommand(std::move(command));
}
void MozcState::FocusIn() {
  CancelCallbacks();
  if (!EnsurePipeline()) return;
  if (pipeline_->suspended()) { ShowRetained(); return; }
  UpdatePreeditMethod();
  SendCompositionMode(composition_mode_);
  DrawAll();
}
void MozcState::FocusOut(const InputContextEvent&) {
  CancelCallbacks();
  if (pipeline_) pipeline_->Hold("focus-or-ime-switch");
  handler_->Clear();
  const auto watch = ic_->watch();
  const auto weak = std::weak_ptr<int>(lifetime_);
  ic_->inputPanel().reset();
  engine_->instance()->resetCompose(ic_);
  if (weak.expired() || !watch.isValid()) return;
  watch.get()->updatePreedit();
  if (weak.expired() || !watch.isValid()) return;
  watch.get()->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void MozcState::ShowRetained() {
  if (ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) return;
  // Explicit inspection only: never replay uncertain commits/raw keys.
  std::string text = _("Input retained; automatic delivery is suspended");
  for (const auto& record : mozc::fcitx5::InputRetention::Snapshot()) {
    text += "\n" + record->preedit;
    if (record->delivering) text += " [" + record->delivering->raw.toString() + "]";
    for (const auto& frame : record->pending) if (frame.physical) text += " [" + frame.raw.toString() + "]";
  }
  ic_->inputPanel().setAuxUp(Text(text));
  ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

std::function<bool()> MozcState::DeliveryValid() const {
  const auto watch = ic_->watch();
  const auto weak = std::weak_ptr<int>(lifetime_);
  const auto epoch = epoch_;
  const auto engineLifetime = engine_->lifetime();
  return [this, watch, weak, epoch, engineLifetime] {
    return !engineLifetime.expired() && !weak.expired() && watch.isValid() && epoch == epoch_ &&
        ic_->hasFocus() && engine_->instance()->inputMethod(ic_) == "mozkey" &&
        !ic_->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive);
  };
}
bool MozcState::ParseResponse(const mozc::commands::Output& raw_response) {
  auto valid = DeliveryValid();
  const auto watch = ic_->watch();
  const auto oldMode = composition_mode_;
  result_.reset(); deletion_.reset();
  // Build the complete local model before any frontend delivery can reenter.
  if (raw_response.consumed()) ClearAll();
  engine_->parser()->ParseResponse(raw_response, ic_);
  OpenUrl();
  auto result = std::move(result_);
  auto deletion = std::move(deletion_);
  result_.reset(); deletion_.reset();
  const bool changedMode = oldMode != composition_mode_;
  if (!valid()) return false;
  if (deletion) {
    watch.get()->deleteSurroundingText(deletion->first, deletion->second);
    if (!valid()) return false;
  }
  if (result) {
    watch.get()->commitString(*result);
    if (!valid()) return false;
  }
  if (!DrawAll() || !valid()) return false;
  if (changedMode) {
    // Last external effect: no state access after notification.
    engine_->compositionModeUpdated(watch.get());
    return valid();
  }
  return true;
}
void MozcState::SetResultString(const std::string& result_string) {
  result_ = result_string;
}
void MozcState::SetDeletionRange(int offset, unsigned length) {
  deletion_ = std::make_pair(offset, length);
}

void MozcState::SetPreeditInfo(Text preedit_info) {
  preedit_ = std::move(preedit_info);
  if (retained_) retained_->preedit = preedit_.toString();
}

void MozcState::SetAuxString(const std::string& str) { aux_ = str; }

void MozcState::SetCompositionMode(mozc::commands::CompositionMode mode,
                                   bool updateUI) {
  composition_mode_ = mode;
  DCHECK(composition_mode_ < mozc::commands::NUM_OF_COMPOSITIONS);
  if (updateUI) {
    engine_->compositionModeUpdated(ic_);
  }
}

void MozcState::SendCompositionMode(mozc::commands::CompositionMode mode) {
  CancelCallbacks();
  mozc::commands::SessionCommand command;
  command.set_type(mode == mozc::commands::DIRECT ? mozc::commands::SessionCommand::TURN_OFF_IME :
                                                   mozc::commands::SessionCommand::SWITCH_COMPOSITION_MODE);
  command.set_composition_mode(mode); SubmitCommand(std::move(command));
}

void MozcState::SetUrl(const std::string& url) { url_ = url; }

void MozcState::ClearAll() {
  SetPreeditInfo(Text());
  SetAuxString("");
  ic_->inputPanel().reset();
  url_.clear();
}

bool MozcState::DrawAll() {
  auto valid = DeliveryValid();
  const auto watch = ic_->watch();
  std::string aux;
  if (!aux_.empty()) {
    aux += "[";
    aux += aux_;
    aux += "]";
  }
  if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
    Text preedit = preedit_;
    if (*engine_->config().preeditCursorPositionAtBeginning) {
      preedit.setCursor(0);
    }
    ic_->inputPanel().setClientPreedit(preedit);
    if (!aux_.empty()) {
      ic_->inputPanel().setAuxUp(Text(aux));
    }
  } else {
    Text preedit = preedit_;
    if (!preedit.empty()) {
      preedit.append(" ");
      preedit.append(aux);
      ic_->inputPanel().setPreedit(preedit);
    } else if (!aux_.empty()) {
      ic_->inputPanel().setAuxUp(Text(aux));
    }
  }
  if (!valid()) return false;
  watch.get()->updatePreedit();
  if (!valid()) return false;
  watch.get()->updateUserInterface(UserInterfaceComponent::InputPanel);
  return valid();
}

void MozcState::OpenUrl() {
  if (url_.empty()) {
    return;
  }
  // No automatic process/browser launch from an IPC response.
  url_.clear();
}

bool MozcState::SendCommand(const mozc::commands::SessionCommand& command) {
  return SubmitCommand(command);
}

void MozcState::SetUsage(const std::string& title,
                         const std::string& description) {
  title_ = title;
  description_ = description;
}

void MozcState::DisplayUsage() {
  displayUsage_ = true;

  ic_->inputPanel().reset();
  auto candidateList = std::make_unique<DisplayOnlyCandidateList>();

  auto lines = stringutils::split(description_, "\n");
  candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
  candidateList->setContent(lines);
  ic_->inputPanel().setCandidateList(std::move(candidateList));
  auto str = title_ + " [" + _("Press Escape to go back") + "]";
  ic_->inputPanel().setAuxUp(Text(str));
  ic_->updatePreedit();
  ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

MozcClientInterface* MozcState::GetClient() const {
  if (!client_) {
    client_ = engine_->pool()->requestClient(ic_);
  }
  return client_.get();
}

void MozcState::ReleaseClient() { Reset(); }

}  // namespace fcitx
