/*
 * Copyright (C) 2017~2017 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
 */

#include "unix/fcitx5/mozc_engine.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/semver.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>

#include <cstddef>
#include <memory>
#include <string>
#include <sstream>
#include <fcitx-utils/standardpath.h>


#include "protocol/commands.pb.h"
#include "unix/fcitx5/i18nwrapper.h"
#include "unix/fcitx5/mozc_client_interface.h"
#include "unix/fcitx5/mozc_client_pool.h"
#include "unix/fcitx5/mozc_response_parser.h"
#include "unix/fcitx5/mozc_state.h"

namespace fcitx {

const struct CompositionModeInfo {
  const char* name;
  const char* icon;
  const char* label;
  const char* description;
  mozc::commands::CompositionMode mode;
} kPropCompositionModes[] = {
    {
        "mozkey-mode-direct",
        "fcitx_mozkey_direct",
        "A",
        N_("Direct"),
        mozc::commands::DIRECT,
    },
    {
        "mozkey-mode-hiragana",
        "fcitx_mozkey_hiragana",
        "\xe3\x81\x82",  // Hiragana letter A in UTF-8.
        N_("Hiragana"),
        mozc::commands::HIRAGANA,
    },
    {
        "mozkey-mode-katakana_full",
        "fcitx_mozkey_katakana_full",
        "\xe3\x82\xa2",  // Katakana letter A.
        N_("Full Katakana"),
        mozc::commands::FULL_KATAKANA,
    },
    {

        "mozkey-mode-alpha_half",
        "fcitx_mozkey_alpha_half",
        "A",
        N_("Half ASCII"),
        mozc::commands::HALF_ASCII,
    },
    {

        "mozkey-mode-alpha_full",
        "fcitx_mozkey_alpha_full",
        "\xef\xbc\xa1",  // Full width ASCII letter A.
        N_("Full ASCII"),
        mozc::commands::FULL_ASCII,
    },
    {
        "mozkey-mode-katakana_half",
        "fcitx_mozkey_katakana_half",
        "\xef\xbd\xb1",  // Half width Katakana letter A.
        N_("Half Katakana"),
        mozc::commands::HALF_KATAKANA,
    },
};
const size_t kNumCompositionModes = FCITX_ARRAY_SIZE(kPropCompositionModes);

MozcModeSubAction::MozcModeSubAction(MozcEngine* engine,
                                     mozc::commands::CompositionMode mode)
    : engine_(engine), mode_(mode) {
  setShortText(_(kPropCompositionModes[mode].description));
  setLongText(_(kPropCompositionModes[mode].description));
  setIcon(kPropCompositionModes[mode].icon);
  setCheckable(true);
}

bool MozcModeSubAction::isChecked(InputContext* ic) const {
  auto* mozc_state = engine_->mozcState(ic);
  return mozc_state->GetCompositionMode() == mode_;
}

void MozcModeSubAction::activate(InputContext* ic) {
  auto* mozc_state = engine_->mozcState(ic);
  mozc_state->SendCompositionMode(mode_);
}

// This array must correspond with the CompositionMode enum in the
// mozc/session/command.proto file.
static_assert(mozc::commands::NUM_OF_COMPOSITIONS == kNumCompositionModes,
              "number of modes must match");


MozcEngine::MozcEngine(Instance* instance)
    : instance_(instance),
      parser_(std::make_unique<MozcResponseParser>(this)),
      client_(createClient(instance->eventLoop())),
      factory_([this](InputContext& ic) { return new MozcState(&ic, this); }) {
  pool_ = std::make_unique<MozcClientPool>(GetSharedStatePolicy(), instance->eventLoop());
  for (auto command :
       {mozc::commands::DIRECT, mozc::commands::HIRAGANA,
        mozc::commands::FULL_KATAKANA, mozc::commands::FULL_ASCII,
        mozc::commands::HALF_ASCII, mozc::commands::HALF_KATAKANA}) {
    modeActions_.push_back(std::make_unique<MozcModeSubAction>(this, command));
  }

  instance_->inputContextManager().registerProperty("mozkeyState", &factory_);
  instance_->userInterfaceManager().registerAction("mozkey-tool", &toolAction_);
  toolAction_.setShortText(_("Mozkey Settings"));
  toolAction_.setLongText(_("Mozkey Settings"));
  toolAction_.setIcon("fcitx_mozkey_tool");

  int i = 0;
  for (auto& modeAction : modeActions_) {
    instance_->userInterfaceManager().registerAction(
        kPropCompositionModes[i].name, modeAction.get());
    toolMenu_.addAction(modeAction.get());
    i++;
  }

  separatorAction_.setSeparator(true);
  instance_->userInterfaceManager().registerAction("mozkey-separator",
                                                   &separatorAction_);

  SemanticVersion version;
  version.setMajor(5);
  version.setMinor(0);
  version.setPatch(22);
  // Where we fix the support for separator
  if (auto fcitxVersion = SemanticVersion::parse(Instance::version());
      fcitxVersion && *fcitxVersion >= version) {
    toolMenu_.addAction(&separatorAction_);
  }

  // Tool launching remains unavailable until its nonblocking ownership is verified.

  instance_->userInterfaceManager().registerAction("mozkey-retained-input", &recoveryAction_);
  recoveryAction_.setShortText(_("Show retained input"));
  recoveryAction_.connect<SimpleAction::Activated>([this](InputContext* ic) {
    if (instance_->inputMethod(ic) == "mozkey") mozcState(ic)->ShowRetained();
  });
  toolMenu_.addAction(&recoveryAction_);
  toolAction_.setMenu(&toolMenu_);

  globalConfigReloadHandle_ =
      instance_->watchEvent(EventType::GlobalConfigReloaded,
                            EventWatcherPhase::Default, [this](Event&) {
                              ResetClientPool();
                              return true;
                            });

  exitHandle_ = instance_->eventLoop().addExitEvent([this](EventSource*) {
    configIo_.reset();
    for (auto* state : states_) state->Shutdown();
    if (client_) client_->Cancel();
    mozc::fcitx5::PreparationWorker::WaitForProcessExit(std::chrono::milliseconds(400));
    return true;
  });
  reloadConfig();
}

MozcEngine::~MozcEngine() {
  lifetime_.reset();
  configIo_.reset();
  exitHandle_.reset();
  for (auto* state : states_) state->Shutdown();
  if (client_) client_->Cancel();
}

void MozcEngine::setConfig(const RawConfig& config) {
  config_.load(config, true); ResetClientPool();
  RawConfig copy; config_.save(copy);
  std::ostringstream stream; writeAsIni(copy, stream);
  configIo_.reset();
  configIo_ = std::make_unique<mozc::fcitx5::AsyncConfigFile>(instance_->eventLoop(),
      StandardPath::global().userDirectory(StandardPath::Type::PkgConfig), stream.str(),
      [](bool, std::string) {});
}
void MozcEngine::reloadConfig() {
  configIo_.reset();
  configIo_ = std::make_unique<mozc::fcitx5::AsyncConfigFile>(instance_->eventLoop(),
      StandardPath::global().userDirectory(StandardPath::Type::PkgConfig), std::nullopt,
      [this](bool ok, std::string data) {
        if (!ok) return;
        RawConfig raw; std::istringstream stream(std::move(data)); readFromIni(raw, stream);
        config_.load(raw, true); ResetClientPool();
      });
}

void MozcEngine::activate(const fcitx::InputMethodEntry& /*entry*/,
                          fcitx::InputContextEvent& event) {
  if (client_) {
    client_->EnsureConnection();
  }
  auto* ic = event.inputContext();
  const auto watch = ic->watch();
  const auto weak = lifetime();
  auto* mozc_state = mozcState(ic);
  mozc_state->FocusIn();
  if (weak.expired() || !watch.isValid()) return;
  ic->statusArea().addAction(StatusGroup::InputMethod, &toolAction_);
}
void MozcEngine::deactivate(const fcitx::InputMethodEntry& /*entry*/,
                            fcitx::InputContextEvent& event) {
  auto* ic = event.inputContext();
  deactivating_ = true;
  auto* mozc_state = mozcState(ic);
  mozc_state->FocusOut(event);
  deactivating_ = false;
}
void MozcEngine::keyEvent(const InputMethodEntry& entry, KeyEvent& event) {
  auto* mozc_state = mozcState(event.inputContext());

  const auto& group = instance_->inputMethodManager().currentGroup();
  std::string layout = group.layoutFor(entry.uniqueName());
  if (layout.empty()) {
    layout = group.defaultLayout();
  }

  const bool isJP = (layout == "jp" || layout.starts_with("jp-"));

  if (mozc_state->ProcessKeyEvent(event.rawKey().sym(), event.rawKey().code(),
                                  event.rawKey().states(), isJP,
                                  event.isRelease(), event.time())) {
    event.filterAndAccept();
  }
}

void MozcEngine::reset(const InputMethodEntry& /*entry*/,
                       InputContextEvent& event) {
  auto* mozc_state = mozcState(event.inputContext());
  mozc_state->Reset();
}

void MozcEngine::save() {
  if (client_ == nullptr) {
    return;
  }
  client_->SyncData();
}

std::string MozcEngine::subMode(const fcitx::InputMethodEntry& /*entry*/,
                                fcitx::InputContext& ic) {
  auto* mozc_state = mozcState(&ic);
  return _(kPropCompositionModes[mozc_state->GetCompositionMode()].description);
}

std::string MozcEngine::subModeIconImpl(
    const fcitx::InputMethodEntry& /*unused*/, fcitx::InputContext& ic) {
  auto* mozc_state = mozcState(&ic);
  return _(kPropCompositionModes[mozc_state->GetCompositionMode()].icon);
}

MozcState* MozcEngine::mozcState(InputContext* ic) {
  return ic->propertyFor(&factory_);
}

void MozcEngine::compositionModeUpdated(InputContext* ic) {
  auto valid = mozcState(ic)->DeliveryValid();
  for (const auto& modeAction : modeActions_) {
    modeAction->update(ic);
    if (!valid()) return;
  }
  ic->updateUserInterface(UserInterfaceComponent::StatusArea);
}

AddonInstance* MozcEngine::clipboardAddon() { return nullptr; }

void MozcEngine::ResetClientPool() {
  instance_->inputContextManager().foreach([this](InputContext* ic) {
    if (instance_->inputMethod(ic) == "mozkey") {
      if (auto* state = this->mozcState(ic)) state->CancelCallbacks();
    }
    return true;
  });
  if (pool_->policy() != GetSharedStatePolicy()) {
    instance_->inputContextManager().foreach ([this](InputContext* ic) {
      if (auto* state = this->mozcState(ic)) {
        state->ReleaseClient();
      }
      return true;
    });
    pool_->setPolicy(GetSharedStatePolicy());
  }
}

PropertyPropagatePolicy MozcEngine::GetSharedStatePolicy() {
  // Session state is always per-InputContext in the initial Mozkey product.
  return PropertyPropagatePolicy::No;
}

}  // namespace fcitx
