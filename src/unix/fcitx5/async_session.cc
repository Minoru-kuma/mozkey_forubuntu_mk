// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_session.h"
#include <unistd.h>
#include <sys/socket.h>
#include <chrono>
#include <utility>
#include <algorithm>
#include "base/system_util.h"

namespace mozc::fcitx5 {
AsyncSession::AsyncSession(fcitx::EventLoop& loop, LinuxDeadlineEndpoint endpoint,
                           PreparationWorker::Job prepare)
    : loop_(loop), prepare_(std::move(prepare)) {
  if (!prepare_) prepare_ = [endpoint = std::move(endpoint)](const std::atomic<bool>& cancel) mutable {
    if (cancel.load()) return -1;
    // The profile cache/mutex/filesystem/NSS stay on the preparation worker.
    if (endpoint.profile.empty()) endpoint.profile = SystemUtil::GetUserProfileDirectory();
    if (endpoint.server_path.empty()) endpoint.server_path = SystemUtil::GetServerPath();
    if (cancel.load() || endpoint.profile.empty()) return -1;
    const auto end = LinuxDeadline::Now() + std::chrono::milliseconds(2000);
    LinuxDeadline deadline([&cancel, end] { return cancel.load() ? end : LinuxDeadline::Now(); }, end);
    return PrepareLinuxDeadlineSocket(endpoint, deadline);
  };
}
AsyncSession::~AsyncSession() { Cancel(); }
void AsyncSession::Cancel() {
  failed_ = true; id_ = 0;
  continuation_.reset(); notification_.reset(); preparation_timer_.reset();
  rpc_.reset(); worker_.reset(); queue_.clear();
}
bool AsyncSession::Close(Done done) {
  if (!id_ || closing_) return false;
  const auto id = id_; Cancel();
  id_ = id; initialized_ = true; failed_ = false; closing_ = true;
  deadline_ = 0; active_ = true;
  commands::Input input; input.set_type(commands::Input::DELETE_SESSION);
  queue_.push_back({std::move(input), std::move(done)});
  continuation_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Next(); return true; });
  continuation_->setOneShot(); return true;
}
bool AsyncSession::Submit(commands::Input input, Done done) {
  if (failed_ || closing_ || queue_.size() >= 16 || input.ByteSizeLong() > 1024 * 1024) return false;
  queue_.push_back({std::move(input), std::move(done)});
  if (!active_) {
    active_ = true;
    continuation_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Next(); return true; });
    continuation_->setOneShot();
  }
  return true;
}
void AsyncSession::Next() {
  rpc_.reset();
  if (failed_ || queue_.empty()) { active_ = false; return; }
  if (!deadline_) deadline_ = fcitx::now(CLOCK_MONOTONIC) + 2000000;
  commands::Input input;
  if (!id_) {
    input.set_type(commands::Input::CREATE_SESSION);
    input.mutable_application_info()->set_process_id(getpid());
    input.mutable_capability()->set_text_deletion(commands::Capability::DELETE_PRECEDING_TEXT);
    Exchange(std::move(input), Stage::Create);
  } else if (!initialized_) {
    input.set_type(commands::Input::SET_REQUEST); input.set_id(id_);
    auto* params = input.mutable_request()->mutable_decoder_experiment_params();
    params->set_variation_character_types(params->variation_character_types() |
        commands::DecoderExperimentParams::SVS_JAPANESE);
    Exchange(std::move(input), Stage::Request);
  } else {
    input = queue_.front().input; input.set_id(id_);
    input.mutable_config()->set_use_cascading_window(false);
    Exchange(std::move(input), Stage::Application);
  }
}
void AsyncSession::Exchange(commands::Input input, Stage stage) {
  stage_ = stage;
  if (!input.SerializeToString(&request_) || request_.empty()) { Fail(); return; }
  Prepare();
}
void AsyncSession::Prepare() {
  if (failed_) return;
  if (fcitx::now(CLOCK_MONOTONIC) >= deadline_) { Fail(); return; }
  worker_ = PreparationWorker::Start(prepare_);
  if (!worker_) {
    preparation_timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + 5000, 1,
        [this](fcitx::EventSourceTime*, uint64_t) { Prepare(); return true; });
    preparation_timer_->setOneShot();
    return;
  }
  preparation_timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, deadline_, 1,
      [this](fcitx::EventSourceTime*, uint64_t) { Fail(); return true; });
  preparation_timer_->setOneShot();
  notification_ = loop_.addIOEvent(worker_->notification_fd(), fcitx::IOEventFlag::In,
      [this](fcitx::EventSourceIO*, int fd, fcitx::IOEventFlags) {
        uint64_t one; const auto count = read(fd, &one, sizeof(one));
        if (count == sizeof(one) && worker_->finished()) Prepared(worker_->TakeFd());
        return true;
      });
}
void AsyncSession::Prepared(int fd) {
  notification_.reset(); preparation_timer_.reset(); worker_.reset();
  if (fd < 0) { Fail(); return; }
  ucred peer{}; socklen_t length = sizeof(peer);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 ||
      peer.uid != geteuid() || peer.pid <= 0 || (peer_pid_ && peer_pid_ != peer.pid)) {
    close(fd); Fail(); return;
  }
  peer_pid_ = peer.pid;
  rpc_ = std::make_unique<AsyncRpc>(loop_, fd, std::move(request_), deadline_,
      [this](AsyncRpc::Status status, std::string data) { Received(status, std::move(data)); });
}
void AsyncSession::Received(AsyncRpc::Status status, std::string data) {
  commands::Output output;
  if (status != AsyncRpc::Status::Success || !output.ParseFromString(data) ||
      output.error_code() != commands::Output::SESSION_SUCCESS || output.id() == 0 ||
      (stage_ != Stage::Create && output.id() != id_)) { Fail(); return; }
  if (stage_ == Stage::Create) id_ = output.id();
  else if (stage_ == Stage::Request) initialized_ = true;
  else {
    auto callback = std::move(queue_.front().done); queue_.pop_front();
    deadline_ = 0;
    if (closing_) {
      id_ = 0; failed_ = true; active_ = false;
      if (callback) callback(true, std::move(output));
      return;
    }
    // Defer the next RPC before the callback; the callback may delete us.
    continuation_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Next(); return true; });
    continuation_->setOneShot();
    if (callback) callback(true, std::move(output));
    return;
  }
  continuation_ = loop_.addDeferEvent([this](fcitx::EventSource*) { Next(); return true; });
  continuation_->setOneShot();
}
void AsyncSession::Fail() {
  failed_ = true; notification_.reset(); preparation_timer_.reset(); worker_.reset();
  auto pending = std::move(queue_);
  continuation_ = loop_.addDeferEvent([pending = std::move(pending)](fcitx::EventSource*) mutable {
    // Only the first completion is necessary to tell the owner to protect the
    // whole input journal. Never invoke later callbacks after owner teardown.
    auto callback = pending.empty() ? Done{} : std::move(pending.front().done);
    pending.clear();
    if (callback) callback(false, {});
    return true;
  });
  continuation_->setOneShot();
}
void SessionCloseQueue::Retire(std::shared_ptr<AsyncSession> session) {
  if (!session) return;
  if (sealed_ || sessions_.size() >= 32) { session->Cancel(); return; }
  auto weak = weak_from_this();
  if (weak.expired()) { session->Cancel(); return; }
  auto* identity = session.get();
  sessions_.push_back(session);
  if (!session->Close([weak, identity](bool, auto) {
        if (auto owner = weak.lock()) {
          auto& entries = owner->sessions_;
          entries.erase(std::remove_if(entries.begin(), entries.end(),
              [identity](const auto& entry) { return entry.get() == identity; }), entries.end());
        }
      })) {
    session->Cancel(); sessions_.pop_back();
  }
}
void SessionCloseQueue::Cancel() {
  sealed_ = true;
  for (auto& session : sessions_) session->Cancel();
  sessions_.clear();
}
}  // namespace mozc::fcitx5
