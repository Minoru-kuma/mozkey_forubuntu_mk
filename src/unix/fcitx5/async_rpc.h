// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_ASYNC_RPC_H_
#define MOZKEY_UNIX_FCITX5_ASYNC_RPC_H_
#include <fcitx-utils/event.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mozc::fcitx5 {
// One verified AF_UNIX FD, one request, SHUT_WR/EOF framing. Loop thread only.
// No filesystem/discovery/poll/wait/reconnect. Takes FD ownership immediately.
class AsyncRpc {
 public:
  enum class Status { Success, IoError, Timeout, TooLarge, EmptyReply };
  using Complete = std::function<void(Status, std::string)>;
  AsyncRpc(fcitx::EventLoop& loop, int fd, std::string request,
           uint64_t absolute_deadline_usec, Complete complete);
  ~AsyncRpc();
  void Cancel();  // Suppresses completion and closes all owned resources.
  AsyncRpc(const AsyncRpc&) = delete;
  AsyncRpc& operator=(const AsyncRpc&) = delete;
 private:
  void Pump();
  void Finish(Status status);
  fcitx::EventLoop& loop_;
  int fd_;
  std::string request_, response_;
  size_t offset_ = 0;
  bool receiving_ = false, finished_ = false;
  uint64_t deadline_;
  Complete complete_;
  std::unique_ptr<fcitx::EventSourceIO> io_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  std::unique_ptr<fcitx::EventSource> defer_;
};
}  // namespace mozc::fcitx5
#endif
