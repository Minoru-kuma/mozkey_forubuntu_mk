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

#ifndef MOZC_UNIX_FCITX_FCITX_MOZC_H_
#define MOZC_UNIX_FCITX_FCITX_MOZC_H_

#include <fcitx-utils/key.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/text.h>

#include <cstdint>
#include <memory>
#include <string>

#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "unix/fcitx5/mozc_client_interface.h"
#include "unix/fcitx5/callback_queue.h"
#include "unix/fcitx5/input_pipeline.h"

namespace fcitx {
inline const int32_t kBadCandidateId = -12345;
class MozcConnectionInterface;
class MozcResponseParser;
class KeyTranslator;
class KeyEventHandler;
class MozcEngine;

class MozcState : public InputContextProperty {
 public:
  // This constructor is used by unittests.
  MozcState(InputContext* ic, MozcEngine* engine);
  MozcState(const MozcState&) = delete;
  virtual ~MozcState();

  void UpdatePreeditMethod();
  void ScheduleCallback(const mozc::commands::Output& output);
  void CancelCallbacks();

  bool ProcessKeyEvent(KeySym sym, uint32_t keycode, KeyStates state,
                       bool layout_is_jp, bool is_key_up, uint32_t time);
  void SelectCandidate(int idx);
  void Reset();
  void FocusIn();
  void FocusOut(const InputContextEvent& event);
  bool Paging(bool prev);

  // Functions called by the MozcResponseParser class to update UI.

  // Displays a 'result' (aka 'commit string') on FCITX UI.
  void SetResultString(const std::string& result_string);
  void SetDeletionRange(int offset, unsigned length);
  std::function<bool()> DeliveryValid() const;
  // Displays a 'preedit' string on FCITX UI. This function takes ownership
  // of preedit_info. If the parameter is NULL, hides the string currently
  // displayed.
  void SetPreeditInfo(Text preedit_info);
  // Displays an auxiliary message (e.g., an error message, a title of
  // candidate window). If the string is empty (""), hides the message
  // currently being displayed.
  void SetAuxString(const std::string& str);
  // Sets a current composition mode (e.g., Hankaku Katakana).
  void SetCompositionMode(mozc::commands::CompositionMode mode,
                          bool updateUI = true);

  void SendCompositionMode(mozc::commands::CompositionMode mode);

  // Sets the url to be opened by the default browser.
  void SetUrl(const std::string& url);

  const std::string& GetIconFile(std::string key);

  mozc::commands::CompositionMode GetCompositionMode() {
    return composition_mode_;
  }

  MozcClientInterface* GetClient() const;
  void ReleaseClient();

  bool SendCommand(const mozc::commands::SessionCommand& session_command);
  void ShowRetained();
  void Shutdown();

  void SetUsage(const std::string& title, const std::string& description);

  bool DrawAll();

 private:
  void DisplayUsage();
  bool EnsurePipeline();
  void TranslateFrame(mozc::fcitx5::InputFrame& frame);
  bool SubmitCommand(mozc::commands::SessionCommand command);
  // Parses the response from mozc_server. Returns whether the server consumes
  // the input or not (true means 'consumed').
  bool ParseResponse(const mozc::commands::Output& raw_response);

  void ClearAll();
  void DrawPreeditInfo();
  void DrawAux();

  // Open url_ with a default browser.
  void OpenUrl();

  InputContext* ic_;
  MozcEngine* engine_;
  mutable std::shared_ptr<MozcClientInterface> client_;
  mozc::fcitx5::CallbackQueue callbacks_;
  uint64_t epoch_ = 0;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::optional<std::string> result_;
  std::optional<std::pair<int, unsigned>> deletion_;
  std::shared_ptr<mozc::fcitx5::RetainedInput> retained_;
  std::unique_ptr<mozc::fcitx5::InputPipeline> pipeline_;

  mozc::commands::CompositionMode composition_mode_ = mozc::commands::HIRAGANA;
  mozc::config::Config::PreeditMethod preedit_method_ =
      mozc::config::Config::ROMAN;
  const std::unique_ptr<KeyEventHandler> handler_;

  bool displayUsage_ = false;
  Text preedit_;
  std::string aux_;  // error tooltip, or candidate window title.
  std::string url_;  // URL to be opened by a browser.
  std::string description_;
  std::string title_;
};

}  // namespace fcitx

#endif  // MOZC_UNIX_FCITX_FCITX_MOZC_H_
