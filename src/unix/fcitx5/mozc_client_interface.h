// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef UNIX_FCITX5_MOZC_CLIENT_INTERFACE_H_
#define UNIX_FCITX5_MOZC_CLIENT_INTERFACE_H_
#include <fcitx-utils/event.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
namespace mozc::fcitx5 { class SessionCloseQueue; }
namespace fcitx {
class MozcClientInterface {
 public:
  using Done = std::function<void(bool, mozc::commands::Output)>;
  virtual ~MozcClientInterface() = default;
  virtual bool Submit(mozc::commands::Input input, Done done) = 0;
  virtual void Cancel() = 0;
  virtual bool EnsureConnection() = 0; // Cached readiness query only; never waits.
  virtual bool GetConfig(mozc::config::Config* config) = 0; // Cache only.
  virtual void set_client_capability(const mozc::commands::Capability&) {}
  virtual bool SyncData() {
    mozc::commands::Input in; in.set_type(mozc::commands::Input::SYNC_DATA);
    return Submit(std::move(in), {});
  }
  virtual bool LaunchTool(const std::string&, std::string_view) { return false; }
  virtual bool LaunchToolWithProtoBuf(const mozc::commands::Output&) { return false; }
};
std::unique_ptr<MozcClientInterface> createClient(EventLoop& loop, std::shared_ptr<mozc::fcitx5::SessionCloseQueue> cleanup = {});
}
#endif
