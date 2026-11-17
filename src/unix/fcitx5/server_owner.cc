// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/server_owner.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

#include "base/mozkey_profile.h"
#include "base/owned_child.h"
#include "base/system_util.h"
#include "protocol/commands.pb.h"

namespace mozc::fcitx5 {
struct ServerOwner::State {
  LinuxDeadlineEndpoint endpoint;  // Accessed only by the single preparation worker.
  std::vector<std::string> arguments;
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
  std::atomic<std::shared_ptr<OwnedChild>> child;
  std::atomic<bool> attempted{false}, graceful{false}, connected_once{false};
};
ServerOwner::ServerOwner(LinuxDeadlineEndpoint endpoint, std::vector<std::string> arguments)
    : state_(std::make_shared<State>()) {
  state_->endpoint = std::move(endpoint); state_->arguments = std::move(arguments);
}
ServerOwner::~ServerOwner() { Cancel(); }
void ServerOwner::Cancel() {
  state_->cancelled->store(true);
  // Also wake before the worker has published its wrapper: pidfd_spawn/getpid
  // may still be returning while the native supervisor already owns the child.
  OwnedChild::NotifyCancellation();
  if (auto child = state_->child.load()) child->Cancel();
}
bool ServerOwner::launch_attempted() const { return state_->attempted.load(); }
bool ServerOwner::owns_process() const { return bool(state_->child.load()); }
bool ServerOwner::graceful_shutdown_ready() const { return state_->graceful.load(); }
PreparationWorker::Job ServerOwner::PreparationJob(bool allow_launch) const {
  return [state = state_, allow_launch](const std::atomic<bool>& cancelled) {
    using namespace std::chrono_literals;
    const auto end = LinuxDeadline::Now()+2000ms;
    LinuxDeadline deadline([&] {
      return cancelled.load() || state->cancelled->load() ? end : LinuxDeadline::Now();
    }, end);
    if (deadline.Expired() || (!state->child.load() && OwnedChild::Busy())) return -1;
    auto& endpoint = state->endpoint;
    if (endpoint.profile.empty()) endpoint.profile = SystemUtil::GetUserProfileDirectory();
    if (endpoint.server_path.empty()) endpoint.server_path = SystemUtil::GetServerPath();
    if (deadline.Expired() || endpoint.profile.empty() || endpoint.channel != "session") return -1;
    const auto connect = [&] {
      struct PreparedFd {
        int value;
        ~PreparedFd() { if (value >= 0) close(value); }
      } fd{PrepareLinuxDeadlineSocket(endpoint, deadline)};
      if (fd.value < 0) return -1;
      if (auto child = state->child.load()) {
        commands::Input input; input.set_type(commands::Input::SHUTDOWN);
        std::string bytes;
        if (input.SerializeToString(&bytes) && child->ConfigureShutdownFromVerifiedConnection(fd.value, bytes))
          state->graceful.store(true);
      }
      if (deadline.Expired()) return -1;
      state->connected_once.store(true);
      return std::exchange(fd.value, -1); // SAME verified socket; no request sent.
    };
    if (const int fd = connect(); fd >= 0) return fd;
    if (deadline.Expired()) return -1;
    auto child = state->child.load();
    if (!child) {
      if (!allow_launch || state->attempted.load() || state->connected_once.load()) return -1;
      // Do not infer permission to overwrite/restart from a failed connect.
      const int directory = OpenMozkeyProfileDirectory(endpoint.profile);
      if (directory < 0) return -1;
      struct stat info{};
      const int result = fstatat(directory, ".session.ipc", &info, AT_SYMLINK_NOFOLLOW);
      const int error = errno; close(directory);
      const bool reuse_owned = result == 0 && OwnedChild::ReusableSessionKeyOnWorker(endpoint.profile);
      if ((result == 0 && !reuse_owned) || (result != 0 && error != ENOENT) || deadline.Expired()) return -1;
      state->attempted.store(true);
      OwnedChild::Spec spec{endpoint.server_path, endpoint.profile, state->arguments, {}};
      spec.fresh_key_only = !reuse_owned;
      spec.reuse_owned_key = reuse_owned;
      for (const char* key : {"HOME", "LANG", "LC_ALL", "LC_CTYPE", "TZ", "XDG_CACHE_HOME", "XDG_DATA_HOME"})
        if (const char* value = std::getenv(key)) spec.environment.push_back(std::string(key)+"="+value);
      if (deadline.Expired()) return -1;
      child = OwnedChild::SpawnOnWorker(std::move(spec), state->cancelled);
      state->child.store(child);
    }
    while (child && !child->finished() && !deadline.Expired()) {
      if (const int fd = connect(); fd >= 0) return fd;
      std::this_thread::sleep_for(5ms);  // Worker only; cancellation checked each iteration.
    }
    return -1;
  };
}
}  // namespace mozc::fcitx5
