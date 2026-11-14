// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/mozc_ipc_client.h"
#include <utility>
namespace fcitx {
bool MozcIPCClient::Submit(mozc::commands::Input input, Done done) {
  if (!session_) return false;
  return session_->Submit(std::move(input), [this, done = std::move(done)](bool ok, auto out) {
    if (ok && out.has_config()) { config_ = out.config(); have_config_ = true; }
    // No member access after delivery; the owner may release this client.
    if (done) done(ok, std::move(out));
  });
}
bool MozcIPCClient::EnsureConnection() {
  return session_ && session_->ready();
}
void MozcIPCClient::Cancel() {
  auto session = std::exchange(session_, {});
  if (cleanup_) cleanup_->Retire(std::move(session));
  else if (session) session->Cancel();
}
std::unique_ptr<MozcClientInterface> createClient(EventLoop& loop,
    std::shared_ptr<mozc::fcitx5::SessionCloseQueue> cleanup) {
  return std::make_unique<MozcIPCClient>(loop, std::move(cleanup));
}
}
