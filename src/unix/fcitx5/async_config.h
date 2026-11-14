// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_ASYNC_CONFIG_H_
#define MOZKEY_UNIX_FCITX5_ASYNC_CONFIG_H_
#include "unix/fcitx5/preparation_worker.h"
#include <fcitx-utils/event.h>
#include <optional>
#include <string>
namespace mozc::fcitx5 {
// Defaults to conf/mozkey.conf under an explicit cached Fcitx config root.
// Private snapshot options use only validated single-component names. All
// filesystem operations run on the owned preparation worker.
class AsyncConfigFile {
 public:
  using Done = std::function<void(bool, std::string)>;
  struct Options {
    std::string directory = "conf", filename = "mozkey.conf";
    size_t limit = 65536;
    bool durable = false, private_directory = false, no_replace = false;
  };
  AsyncConfigFile(fcitx::EventLoop& loop, std::string root,
                  std::optional<std::string> save, Done done);
  AsyncConfigFile(fcitx::EventLoop& loop, std::string root,
                  std::optional<std::string> save, Done done, Options options);
  ~AsyncConfigFile();
  // Worker-only shared operation for the bounded final shutdown task.
  static bool TransferForWorker(const std::string& root, const std::optional<std::string>& save,
                               const std::atomic<bool>& cancelled, std::string& text, const Options& options);
 private:
  struct Data { std::string text; bool ok = false; };
  void Start();
  void Finish();
  fcitx::EventLoop& loop_;
  std::string root_;
  std::optional<std::string> save_;
  Done done_;
  Options options_;
  uint64_t deadline_;
  std::shared_ptr<Data> data_ = std::make_shared<Data>();
  std::unique_ptr<PreparationWorker> worker_;
  std::unique_ptr<fcitx::EventSourceIO> io_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  std::unique_ptr<fcitx::EventSource> defer_;
};
}
#endif
