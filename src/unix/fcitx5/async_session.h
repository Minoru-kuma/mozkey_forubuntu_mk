// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_ASYNC_SESSION_H_
#define MOZKEY_UNIX_FCITX5_ASYNC_SESSION_H_
#include <deque>
#include <vector>
#include "protocol/commands.pb.h"
#include "unix/fcitx5/async_rpc.h"
#include "unix/fcitx5/preparation_worker.h"
#include "ipc/linux_deadline.h"

namespace mozc::fcitx5 {
// One IC/session. Queued commands are sent once, in order. Failure seals the
// session: no restart/replay, implicit launcher, other-product fallback or I/O
// in destructors. Application requests never precede verified FD handoff.
class AsyncSession {
 public:
  using Done = std::function<void(bool, commands::Output)>;
  AsyncSession(fcitx::EventLoop& loop, LinuxDeadlineEndpoint endpoint = {},
               PreparationWorker::Job prepare = {});
  ~AsyncSession();
  bool Submit(commands::Input input, Done done);
  void Cancel();
  bool Close(Done done); // Cancel app effects; DELETE_SESSION only, no create/replay.
  bool failed() const { return failed_; }
  bool ready() const { return id_ != 0 && initialized_ && !failed_ && !closing_; }
 private:
  struct Pending { commands::Input input; Done done; };
  enum class Stage { Create, Request, Application };
  void Next();
  void Exchange(commands::Input input, Stage stage);
  void Prepare();
  void Prepared(int fd);
  void Received(AsyncRpc::Status status, std::string data);
  void Fail();
  fcitx::EventLoop& loop_;
  PreparationWorker::Job prepare_;
  std::deque<Pending> queue_;
  uint64_t id_ = 0, deadline_ = 0;
  int peer_pid_ = 0;
  bool initialized_ = false, failed_ = false, active_ = false, closing_ = false;
  Stage stage_ = Stage::Create;
  std::string request_;
  std::unique_ptr<PreparationWorker> worker_;
  std::unique_ptr<fcitx::EventSourceIO> notification_;
  std::unique_ptr<fcitx::EventSourceTime> preparation_timer_;
  std::unique_ptr<AsyncRpc> rpc_;
  std::unique_ptr<fcitx::EventSource> continuation_;
};
// Addon-owned bounded cleanup. Its teardown cancels all IO; it never retains
// an IC, input callback or event-loop reference beyond a sealed owner.
class SessionCloseQueue : public std::enable_shared_from_this<SessionCloseQueue> {
 public:
  ~SessionCloseQueue() { Cancel(); }
  void Retire(std::shared_ptr<AsyncSession> session);
  void Cancel();
  size_t size() const { return sessions_.size(); }
 private:
  bool sealed_ = false;
  std::vector<std::shared_ptr<AsyncSession>> sessions_;
};
}  // namespace mozc::fcitx5
#endif
