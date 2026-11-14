// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#ifndef MOZKEY_UNIX_FCITX5_INPUT_RECOVERY_H_
#define MOZKEY_UNIX_FCITX5_INPUT_RECOVERY_H_
#include <string>
#include <vector>
#include "unix/fcitx5/async_config.h"
#include "unix/fcitx5/input_pipeline.h"

namespace mozc::fcitx5 {
// Local, bounded inspection data. No recovery method sends keys/IPC or commits
// text. An uncertain outcome must never be automatically applied again.
class InputRecovery {
 public:
  using Records = std::vector<std::shared_ptr<const RetainedInput>>;
  static bool Encode(const Records& records, std::string* bytes);
  static bool Decode(const std::string& bytes, Records* records);
  static std::string Inspect(const Records& records, size_t limit = 16384);
  static AsyncConfigFile::Options FileOptions(std::string filename);
  static bool ValidFilename(const std::string& name);
  struct ExitSave { bool queued = false; std::atomic<bool> durable{false}; };
  static std::shared_ptr<ExitSave> QueueForExit(std::string root, std::string filename, const Records& records);
};
}  // namespace mozc::fcitx5
#endif
