// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "unix/fcitx5/async_rpc.h"
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <utility>

namespace mozc::fcitx5 {
namespace { constexpr size_t kLimit = 1024 * 1024, kDispatch = 64 * 1024; }
AsyncRpc::AsyncRpc(fcitx::EventLoop& loop, int fd, std::string request,
                   uint64_t deadline, Complete complete)
    : loop_(loop), fd_(fd), request_(std::move(request)), deadline_(deadline),
      complete_(std::move(complete)) {
  if (fd_ < 0) { Finish(Status::IoError); return; }
  if (request_.empty() || request_.size() > kLimit) { Finish(Status::TooLarge); return; }
  io_ = loop_.addIOEvent(fd_, fcitx::IOEventFlag::Out,
      [this](fcitx::EventSourceIO*, int, fcitx::IOEventFlags) { Pump(); return true; });
  timer_ = loop_.addTimeEvent(CLOCK_MONOTONIC, deadline_, 1,
      [this](fcitx::EventSourceTime*, uint64_t) { Finish(Status::Timeout); return true; });
  timer_->setOneShot();
}
AsyncRpc::~AsyncRpc() { Cancel(); }
void AsyncRpc::Cancel() {
  finished_ = true;
  complete_ = {};
  defer_.reset(); timer_.reset(); io_.reset();
  if (fd_ >= 0) close(std::exchange(fd_, -1));
}
void AsyncRpc::Finish(Status status) {
  if (finished_) return;
  finished_ = true;
  if (io_) io_->setEnabled(false);
  if (timer_) timer_->setEnabled(false);
  if (fd_ >= 0) close(std::exchange(fd_, -1));
  if (status != Status::Success) response_.clear();
  defer_ = loop_.addDeferEvent([this, status](fcitx::EventSource*) {
    auto callback = std::move(complete_);
    auto result = std::move(response_);
    // No member access after callback: it may destroy this RPC/session.
    if (callback) callback(status, std::move(result));
    return true;
  });
  defer_->setOneShot();
}
void AsyncRpc::Pump() {
  if (finished_) return;
  size_t budget = kDispatch;
  while (budget) {
    if (fcitx::now(CLOCK_MONOTONIC) >= deadline_) { Finish(Status::Timeout); return; }
    if (!receiving_) {
      const auto n = send(fd_, request_.data() + offset_,
          std::min(budget, request_.size() - offset_), MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        Finish(Status::IoError); return;
      }
      if (!n) { Finish(Status::IoError); return; }
      offset_ += n; budget -= n;
      if (offset_ != request_.size()) continue;
      if (shutdown(fd_, SHUT_WR)) { Finish(Status::IoError); return; }
      receiving_ = true;
      io_->setEvents(fcitx::IOEventFlag::In); // No idle POLLOUT busy loop.
    } else {
      char bytes[8192];
      const auto n = recv(fd_, bytes, std::min(budget, sizeof(bytes)), MSG_DONTWAIT);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        Finish(Status::IoError); return;
      }
      if (!n) { Finish(response_.empty() ? Status::EmptyReply : Status::Success); return; }
      if (response_.size() + n > kLimit) { Finish(Status::TooLarge); return; }
      response_.append(bytes, n); budget -= n;
    }
  }
}
}  // namespace mozc::fcitx5
