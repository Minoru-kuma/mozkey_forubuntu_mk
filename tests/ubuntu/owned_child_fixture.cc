// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
// Synthetic ELF child only: no IME, engine, dictionary, settings or DBus.
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#include "base/owned_child.h"

int main(int argc, char* argv[]) {
  std::string profile;
  if (!mozc::InitializeOwnedChild(&argc, argv, &profile) || profile.empty()) return 10;
  bool socket_mode = false;
  bool stall_shutdown = false;
  bool delay_shutdown = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg.starts_with("--mozkey-owned-")) return 15;
    if (arg == "--ignore-term") signal(SIGTERM, SIG_IGN);
    if (arg == "--delay350") std::this_thread::sleep_for(std::chrono::milliseconds(350));
    if (arg == "--socket") socket_mode = true;
    if (arg == "--stall-shutdown") { socket_mode = true; stall_shutdown = true; signal(SIGTERM, SIG_IGN); }
    if (arg == "--shutdown-delay150") { socket_mode = true; delay_shutdown = true; }
  }
  int listener = -1;
  if (socket_mode) {
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string name = "tmp/.mozkey.fixture_"+std::to_string(getpid());
    memcpy(address.sun_path+1, name.data(), name.size());
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address),
                            offsetof(sockaddr_un, sun_path)+1+name.size()) ||
        listen(listener, 4)) return 11;
  }
  const int ready = open((profile+"/.fixture-ready").c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (ready < 0 || write(ready, "ready", 5) != 5) return 12;
  close(ready);
  if (!socket_mode) {
    for (;;) pause();
  }
  for (;;) {
    const int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return 13;
    char request[64]{};
    const auto size = read(fd, request, sizeof(request));
    if (size == 5 && memcmp(request, "quit\n", 5) == 0) {
      if (stall_shutdown) { for (;;) pause(); }
      if (delay_shutdown) std::this_thread::sleep_for(std::chrono::milliseconds(150));
      const int marker = open((profile+"/.fixture-shutdown").c_str(),
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      if (marker < 0) return 14;
      close(marker);
      close(fd);
      close(listener);
      return 0;
    }
    close(fd);  // The verification connection carries no administrative bytes.
  }
}
