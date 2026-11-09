// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause

// Diagnostic only, not an alternative frontend or a production worker.
// Injects finite stand-ins for non-cancellable operations at the existing
// factory boundary. No profile access, socket, server, or IME is used.
// Exit 0: all measured bounds met; 1: fixture error; 2: A5 stop condition.
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "base/version.h"
#include "client/client.h"
#include "ipc/linux_deadline.h"
#include "protocol/commands.pb.h"

namespace {
using namespace std::chrono_literals;
using mozc::LinuxDeadline;
using mozc::client::Client;
using mozc::client::LinuxFcitxOptions;

class FixturePeer final : public mozc::IPCClientInterface {
 public:
  bool Connected() const override { return true; }
  uint32_t GetServerProtocolVersion() const override {
    return mozc::IPC_PROTOCOL_VERSION;
  }
  absl::string_view GetServerProductVersion() const override { return version_; }
  uint32_t GetServerProcessId() const override { return 123; }
  mozc::IPCErrorType GetLastIPCError() const override { return mozc::IPC_NO_ERROR; }
  bool Call(absl::string_view request, std::string* response,
            absl::Duration) override {
    mozc::commands::Input input;
    if (!input.ParseFromArray(request.data(), request.size())) return false;
    mozc::commands::Output output;
    output.set_id(42);
    output.set_error_code(mozc::commands::Output::SESSION_SUCCESS);
    return output.SerializeToString(response);
  }
 private:
  std::string version_ = mozc::Version::GetMozcVersion();
};

double Elapsed(LinuxDeadline::Time start) {
  return std::chrono::duration<double, std::milli>(LinuxDeadline::Now() - start)
      .count();
}

bool Report(const char* name, double elapsed, bool result, bool empty_output) {
  const bool gate = elapsed <= 250;
  std::cout << "{\"case\":\"" << name << "\",\"elapsed_ms\":" << elapsed
            << ",\"call_ok\":" << (result ? "true" : "false")
            << ",\"empty_output\":" << (empty_output ? "true" : "false")
            << ",\"gate_ok\":" << (gate ? "true" : "false") << "}\n";
  return gate;
}

bool BlockAtFactory(const char* name) {
  LinuxFcitxOptions options;
  options.factory = [](const LinuxDeadline&) {
    // Models one syscall that does not return to a deadline check for 350ms.
    std::this_thread::sleep_for(350ms);
    return std::make_unique<FixturePeer>();
  };
  Client client(options);
  mozc::commands::KeyEvent key;
  mozc::commands::Output output;
  output.mutable_result()->set_value("stale fixture");
  const auto start = LinuxDeadline::Now();
  const bool result = client.SendKey(key, &output);
  const double elapsed = Elapsed(start);
  return Report(name, elapsed, result, output.ByteSizeLong() == 0);
}

bool BlockDuringClose() {
  bool closing = false;
  LinuxFcitxOptions options;
  options.factory = [&](const LinuxDeadline&) {
    if (closing) std::this_thread::sleep_for(350ms);
    return std::make_unique<FixturePeer>();
  };
  auto client = std::make_unique<Client>(options);
  if (!client->EnsureSession()) throw std::runtime_error("session fixture failed");
  closing = true;
  const auto start = LinuxDeadline::Now();
  const bool result = client->CloseLinuxFcitxSession();
  const double elapsed = Elapsed(start);
  client.reset();  // Already closed: no repeated discovery or delete.
  return Report("blocking_discovery_during_close", elapsed, result, true);
}

bool JoiningValidator() {
  const auto start = LinuxDeadline::Now();
  {
    // The naive limited-worker proposal: cancellation cannot interrupt the
    // blocked operation, so even jthread destruction exceeds the UI budget.
    std::jthread validator([] { std::this_thread::sleep_for(350ms); });
    validator.request_stop();
  }
  return Report("naive_validator_join_after_cancel", Elapsed(start), false, true);
}
}  // namespace

int main() {
  try {
    bool passed = BlockAtFactory("blocking_profile_key_discovery");
    passed = BlockAtFactory("blocking_peer_executable_inspection") && passed;
    passed = BlockDuringClose() && passed;
    passed = JoiningValidator() && passed;
    return passed ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "fixture error: " << error.what() << '\n';
    return 1;
  }
}
