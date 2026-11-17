// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Synthetic protocol fixture only; no MozcServer/SessionServer/engine/IME.
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#include "base/owned_child.h"
#include "base/system_util.h"
#include "ipc/ipc_path_manager.h"
#include "protocol/commands.pb.h"

int main(int argc, char* argv[]) {
  std::string profile;
  if (!mozc::InitializeOwnedChild(&argc, argv, &profile)) return 10;
  bool delay = false;
  int fixture_parent = -1;
  for (int i=1; i<argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg.starts_with("--fixture-profile=") && profile.empty()) profile = arg.substr(18);
    if (arg == "--pause350") delay = true;
    if (arg.starts_with("--fixture-parent=")) fixture_parent = std::atoi(argv[i]+17);
  }
  if (profile.empty()) return 11;
  if (fixture_parent != -1 && (fixture_parent <= 1 || getppid() != fixture_parent ||
      prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != fixture_parent)) return 21;
  mozc::SystemUtil::SetUserProfileDirectory(profile);
  auto* manager = mozc::IPCPathManager::GetIPCPathManager("session");
  std::string address;
  if (!manager->CreateNewPathName() || !manager->GetPathName(&address)) return 12;
  const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un name{}; name.sun_family = AF_UNIX;
  if (address.size() >= sizeof(name.sun_path)) return 13;
  memcpy(name.sun_path, address.data(), address.size());
  if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&name),
      sizeof(name.sun_family)+address.size()) || listen(listener, 8)) return 14;
  if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(350));
  if (!manager->SavePathName()) return 15;
  const int ready = open((profile+"/.fixture-ready").c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (ready < 0) return 16;
  close(ready);
  for (;;) {
    const int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return 17;
    std::string bytes; char buffer[1024]; ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
      bytes.append(buffer, count); if (bytes.size() > 4096) return 18;
    }
    mozc::commands::Input input;
    if (!bytes.empty() && input.ParseFromString(bytes)) {
      const int trace = open((profile+"/.fixture-requests").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
      const std::string type = std::to_string(static_cast<int>(input.type()))+"\n";
      if (trace < 0 || write(trace, type.data(), type.size()) != static_cast<ssize_t>(type.size())) return 22;
      close(trace);
      if (input.type() == mozc::commands::Input::SHUTDOWN) {
        const int marker = open((profile+"/.fixture-shutdown").c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (marker < 0) return 19;
        close(marker); close(fd); close(listener); return 0;
      }
      mozc::commands::Output output;
      output.set_id(7); output.set_consumed(true);
      if (input.type() == mozc::commands::Input::GET_CONFIG) output.mutable_config();
      std::string response;
      if (!output.SerializeToString(&response) || send(fd, response.data(), response.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(response.size())) return 20;
    }
    close(fd);
  }
}
