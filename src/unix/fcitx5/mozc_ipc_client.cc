// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/mozc_ipc_client.h"
#include <utility>
namespace fcitx {
bool MozcIPCClient::Submit(mozc::commands::Input input, Done done) {
  return session_.Submit(std::move(input), [this, done = std::move(done)](bool ok, auto out) {
    if (ok && out.has_config()) { config_ = out.config(); have_config_ = true; }
    // No member access after delivery; the owner may release this client.
    if (done) done(ok, std::move(out));
  });
}
bool MozcIPCClient::EnsureConnection() {
  return session_.ready();
}
std::unique_ptr<MozcClientInterface> createClient(EventLoop& loop) {
  return std::make_unique<MozcIPCClient>(loop);
}
}
