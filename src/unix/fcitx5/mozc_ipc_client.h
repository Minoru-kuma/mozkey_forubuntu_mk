// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef UNIX_FCITX5_MOZC_IPC_CLIENT_H_
#define UNIX_FCITX5_MOZC_IPC_CLIENT_H_
#include "unix/fcitx5/mozc_client_interface.h"
#include "unix/fcitx5/async_session.h"
namespace fcitx {
class MozcIPCClient final : public MozcClientInterface {
 public:
  explicit MozcIPCClient(EventLoop& loop) : session_(loop) {}
  ~MozcIPCClient() override { session_.Cancel(); }
  bool Submit(mozc::commands::Input input, Done done) override;
  void Cancel() override { session_.Cancel(); }
  bool EnsureConnection() override;
  bool GetConfig(mozc::config::Config* config) override { *config = config_; return have_config_; }
 private:
  mozc::fcitx5::AsyncSession session_;
  mozc::config::Config config_;
  bool have_config_ = false;
};
}
#endif
