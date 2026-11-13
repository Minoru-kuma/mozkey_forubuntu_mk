// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_ASYNC_CONFIG_H_
#define MOZKEY_UNIX_FCITX5_ASYNC_CONFIG_H_
#include "unix/fcitx5/preparation_worker.h"
#include <fcitx-utils/event.h>
#include <optional>
#include <string>
namespace mozc::fcitx5 {
// Reads/writes only conf/mozkey.conf under an explicit cached Fcitx config
// root. All filesystem operations run on the owned preparation worker.
class AsyncConfigFile {
 public:
  using Done = std::function<void(bool, std::string)>;
  AsyncConfigFile(fcitx::EventLoop& loop, std::string root,
                  std::optional<std::string> save, Done done);
  ~AsyncConfigFile();
 private:
  struct Data { std::string text; bool ok = false; };
  void Start();
  void Finish();
  fcitx::EventLoop& loop_;
  std::string root_;
  std::optional<std::string> save_;
  Done done_;
  uint64_t deadline_;
  std::shared_ptr<Data> data_ = std::make_shared<Data>();
  std::unique_ptr<PreparationWorker> worker_;
  std::unique_ptr<fcitx::EventSourceIO> io_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  std::unique_ptr<fcitx::EventSource> defer_;
};
}
#endif
