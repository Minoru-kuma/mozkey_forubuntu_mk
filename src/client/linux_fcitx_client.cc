// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "client/client.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <algorithm>
#include <chrono>
#include <utility>

#include "base/version.h"
#include "session/key_info_util.h"

namespace mozc::client {
namespace {
// Nested session/config/key RPCs share a single outer operation budget.
class Operation {
 public:
  Operation(std::optional<LinuxDeadline>& active, const LinuxFcitxCloseGroup& group,
            absl::Duration timeout, bool closing)
      : active_(active), owner_(!active.has_value()) {
    if (!owner_) return;
    const auto budget = std::chrono::milliseconds(
        std::clamp<int64_t>(absl::ToInt64Milliseconds(timeout), 0, 100));
    active = LinuxDeadline::After(group.clock(), budget);
    if (closing && group.deadline() && group.deadline()->end() < active->end()) {
      active = *group.deadline();
    }
  }
  ~Operation() { if (owner_) active_.reset(); }
 private:
  std::optional<LinuxDeadline>& active_;
  bool owner_;
};
}  // namespace

Client::Client(LinuxFcitxOptions options)
    : id_(0), client_factory_(nullptr), timeout_(absl::Milliseconds(100)),
      server_status_(SERVER_UNKNOWN), server_protocol_version_(0),
      server_process_id_(0), last_mode_(commands::DIRECT) {
  // No legacy ConfigHandler/profile I/O or default launcher construction.
  direct_mode_keys_ = KeyInfoUtil::ExtractSortedDirectModeKeys(config::Config::default_instance());
  if (!options.close_group) options.close_group = std::make_shared<LinuxFcitxCloseGroup>();
  if (!options.factory) {
    options.factory = [endpoint = options.endpoint](const LinuxDeadline& deadline) {
      return NewLinuxDeadlineIPCClient(endpoint, deadline);
    };
  }
  linux_fcitx_ = std::make_unique<LinuxFcitxState>();
  linux_fcitx_->options = std::move(options);
  InitRequestForSvsJapanese(true);
}

bool Client::LinuxExchange(const commands::Input& input, commands::Output* output) {
  auto& state = *linux_fcitx_;
  const auto& deadline = *state.operation;
  output->Clear();
  if (deadline.Expired() || input.ByteSizeLong() > 1024 * 1024) return false;
  std::string request;
  if (!input.SerializeToString(&request) || deadline.Expired()) return false;
  auto peer = state.options.factory(deadline);
  if (deadline.Expired() || !peer || !peer->Connected()) return false;
  // Fail closed before any application request on version/identity mismatch.
  if (peer->GetServerProtocolVersion() != IPC_PROTOCOL_VERSION ||
      peer->GetServerProductVersion() != Version::GetMozcVersion() ||
      peer->GetServerProcessId() == 0 ||
      (state.peer_pid != 0 && state.peer_pid != peer->GetServerProcessId())) return false;
  state.peer_pid = peer->GetServerProcessId();
  std::string response;
  if (deadline.Expired() || !peer->Call(request, &response, absl::ZeroDuration()) ||
      deadline.Expired() || response.empty() || response.size() > 1024 * 1024) return false;
  commands::Output parsed;
  if (!parsed.ParseFromString(response) || deadline.Expired()) return false;
  output->Swap(&parsed);
  return true;
}

bool Client::LinuxCall(const commands::Input& input, commands::Output* output, bool closing) {
  auto& state = *linux_fcitx_;
  output->Clear();
  if (!closing && (state.failed || state.closed || state.options.close_group->closing())) return false;
  Operation operation(state.operation, *state.options.close_group, timeout_, closing);
  if (!LinuxExchange(input, output)) {
    state.failed = true;
    server_status_ = SERVER_TIMEOUT;
    // Do not expose partial/late results; never dump inputs, replay or resend.
    output->Clear();
    return false;
  }
  return true;
}

bool Client::LinuxEnsureSession() {
  auto& state = *linux_fcitx_;
  if (state.failed || state.closed || state.options.close_group->closing()) return false;
  Operation operation(state.operation, *state.options.close_group, timeout_, false);
  if (state.operation->Expired()) return false;
  if (id_ != 0) return true;
  server_status_ = SERVER_INVALID_SESSION;
  if (!CreateSession() || id_ == 0) { state.failed = true; return false; }
  if (request_) {
    commands::Input input;
    InitInput(&input);
    input.set_type(commands::Input::SET_REQUEST);
    *input.mutable_request() = *request_;
    commands::Output output;
    if (!LinuxCall(input, &output) || output.id() != id_ ||
        output.error_code() != commands::Output::SESSION_SUCCESS) {
      state.failed = true;
      return false;
    }
  }
  server_status_ = SERVER_OK;
  return true;
}

bool Client::LinuxEnsureCallCommand(commands::Input* input, commands::Output* output) {
  auto& state = *linux_fcitx_;
  Operation operation(state.operation, *state.options.close_group, timeout_, false);
  output->Clear();
  if (!LinuxEnsureSession()) return false;
  InitInput(input);
  if (!LinuxCall(*input, output)) return false;
  if (output->id() != id_ || output->error_code() != commands::Output::SESSION_SUCCESS) {
    output->Clear(); state.failed = true; return false;
  }
  return true;  // No history journal and no second trial in this opt-in path.
}

bool Client::CloseLinuxFcitxSession() {
  auto& state = *linux_fcitx_;
  if (state.closed) return true;
  state.closed = true;  // Mark before I/O; destructor never starts a second RPC.
  const uint64_t session = id_;
  id_ = 0;
  if (session == 0) return true;
  Operation operation(state.operation, *state.options.close_group, timeout_, true);
  if (state.operation->Expired()) return false;
  commands::Input input;
  input.set_type(commands::Input::DELETE_SESSION);
  input.set_id(session);
  commands::Output output;
  return LinuxCall(input, &output, true) &&
         output.id() == session &&
         output.error_code() == commands::Output::SESSION_SUCCESS;
}
}  // namespace mozc::client
#endif
