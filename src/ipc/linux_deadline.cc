// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "ipc/linux_deadline.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include "base/version.h"
#include "ipc/ipc.pb.h"

namespace mozc {
namespace {
constexpr size_t kMaximumMessage = 1024 * 1024;
class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  ~Fd() { Reset(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return fd_; }
  int Release() { return std::exchange(fd_, -1); }
  void Reset(int fd = -1) { if (fd_ >= 0) close(fd_); fd_ = fd; }
 private:
  int fd_;
};

bool IsPrivate(int fd, bool directory) {
  struct stat st{};
  return fstat(fd, &st) == 0 && st.st_uid == geteuid() &&
         (st.st_mode & 077) == 0 &&
         (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

bool ReadKey(const LinuxDeadlineEndpoint& endpoint, const LinuxDeadline& deadline,
             ipc::IPCPathInfo* info) {
  if (endpoint.profile.empty() || endpoint.profile[0] != '/' ||
      endpoint.server_path.empty() || endpoint.server_path[0] != '/' ||
      endpoint.channel.empty() || endpoint.channel.size() > 32) return false;
  for (char c : endpoint.channel) {
    if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  // Traverse with directory fds, rejecting symlinks in every component.
  Fd dir(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  size_t pos = 1;
  while (pos < endpoint.profile.size()) {
    if (deadline.Expired()) return false;
    const size_t slash = endpoint.profile.find('/', pos);
    const std::string part = endpoint.profile.substr(pos, slash - pos);
    if (part.empty() || part == "." || part == "..") return false;
    const int next = openat(dir.get(), part.c_str(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0) return false;
    dir.Reset(next);
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }
  if (deadline.Expired() || !IsPrivate(dir.get(), true)) return false;
  const std::string name = "." + endpoint.channel + ".ipc";
  Fd key(openat(dir.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (key.get() < 0 || !IsPrivate(key.get(), false)) return false;
  struct stat st{};
  if (fstat(key.get(), &st) != 0 || st.st_size <= 0 || st.st_size > 2096) return false;
  std::string data(st.st_size, '\0');
  size_t offset = 0;
  while (offset < data.size()) {
    if (deadline.Expired()) return false;
    const auto count = read(key.get(), data.data() + offset, data.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    offset += count;
  }
  if (deadline.Expired() || !info->ParseFromString(data) || info->key().size() != 32) return false;
  for (char c : info->key()) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return !deadline.Expired();
}

class DeadlineClient final : public IPCClientInterface {
 public:
  DeadlineClient(const LinuxDeadlineEndpoint& endpoint, const LinuxDeadline& deadline)
      : deadline_(deadline) {
    // Safe to retry discovery/connect: no application request has been sent.
    // Both attempts share the original operation deadline and close old fds.
    for (int trial = 0; trial < 2 && !deadline_.Expired(); ++trial) {
      fd_.Reset();
      if (!ReadKey(endpoint, deadline_, &info_)) break;
#if defined(MOZKEY_LINUX_BUILD)
      std::string address = "/tmp/.mozkey." + info_.key() + "." + endpoint.channel;
#else
      std::string address = "/tmp/.mozc." + info_.key() + "." + endpoint.channel;
#endif
      address[0] = '\0';
      sockaddr_un sock{};
      sock.sun_family = AF_UNIX;
      if (address.size() >= sizeof(sock.sun_path)) break;
      memcpy(sock.sun_path, address.data(), address.size());
      fd_.Reset(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
      if (fd_.get() < 0) break;
      if (connect(fd_.get(), reinterpret_cast<sockaddr*>(&sock),
                  sizeof(sock.sun_family) + address.size()) != 0) {
        if (errno != EINPROGRESS) continue;  // Includes AF_UNIX queue-full EAGAIN.
        if (!Wait(POLLOUT)) break;
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(fd_.get(), SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error) continue;
      }
      ucred peer{}; socklen_t size = sizeof(peer);
      if (getsockopt(fd_.get(), SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 ||
          peer.uid != geteuid() || peer.pid <= 0 ||
          static_cast<uint32_t>(peer.pid) != info_.process_id()) break;
      char path[4096];
      const std::string proc = "/proc/" + std::to_string(peer.pid) + "/exe";
      const auto count = readlink(proc.c_str(), path, sizeof(path));
      if (count <= 0 || count == sizeof(path) ||
          std::string(path, count) != endpoint.server_path) break;
      if (deadline_.Expired()) break;
      connected_ = true; error_ = IPC_NO_ERROR; return;
    }
    error_ = deadline_.Expired() ? IPC_TIMEOUT_ERROR : IPC_INVALID_SERVER;
    fd_.Reset();
  }
  bool Connected() const override { return connected_; }
  int ReleaseValidatedFd() {
    if (!connected_ || deadline_.Expired() ||
        info_.protocol_version() != IPC_PROTOCOL_VERSION ||
        info_.product_version() != Version::GetMozcVersion()) return -1;
    connected_ = false;
    return fd_.Release();
  }
  uint32_t GetServerProtocolVersion() const override { return info_.protocol_version(); }
  absl::string_view GetServerProductVersion() const override { return info_.product_version(); }
  uint32_t GetServerProcessId() const override { return info_.process_id(); }
  IPCErrorType GetLastIPCError() const override { return error_; }
  bool Call(absl::string_view request, std::string* response, absl::Duration) override {
    response->clear();
    if (!connected_ || request.size() > kMaximumMessage || deadline_.Expired()) return Fail(IPC_TIMEOUT_ERROR);
    size_t offset = 0;
    while (offset < request.size()) {
      if (!Wait(POLLOUT)) return Fail(IPC_TIMEOUT_ERROR);
      const auto count = send(fd_.get(), request.data() + offset, request.size() - offset,
                              MSG_DONTWAIT | MSG_NOSIGNAL);
      if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      if (count <= 0) return Fail(IPC_WRITE_ERROR);
      offset += count;
    }
    if (deadline_.Expired() || shutdown(fd_.get(), SHUT_WR) != 0) return Fail(IPC_WRITE_ERROR);
    std::string result;
    for (;;) {
      if (!Wait(POLLIN)) return Fail(IPC_TIMEOUT_ERROR);
      char buffer[8192];
      const auto count = recv(fd_.get(), buffer, sizeof(buffer), MSG_DONTWAIT);
      if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      if (count < 0) return Fail(IPC_READ_ERROR);
      if (deadline_.Expired()) return Fail(IPC_TIMEOUT_ERROR);
      if (count == 0) {
        *response = std::move(result);
        connected_ = false; fd_.Reset(); return true;
      }
      if (result.size() + count > kMaximumMessage) return Fail(IPC_READ_ERROR);
      result.append(buffer, count);
    }
  }
 private:
  bool Wait(short events) {
    while (!deadline_.Expired()) {
      pollfd item{fd_.get(), events, 0};
      const int result = poll(&item, 1, deadline_.PollMilliseconds());
      if (deadline_.Expired()) return false;
      if (result < 0 && errno == EINTR) continue;
      if (result < 0 || (item.revents & POLLNVAL)) return false;
      if (result > 0) return true;  // Read/write reports HUP/ERR precisely.
    }
    return false;
  }
  bool Fail(IPCErrorType error) {
    error_ = error; connected_ = false; fd_.Reset(); return false;
  }
  LinuxDeadline deadline_;
  ipc::IPCPathInfo info_;
  Fd fd_;
  IPCErrorType error_ = IPC_NO_CONNECTION;
  bool connected_ = false;
};
}  // namespace

std::unique_ptr<IPCClientInterface> NewLinuxDeadlineIPCClient(
    const LinuxDeadlineEndpoint& endpoint, const LinuxDeadline& deadline) {
  return std::make_unique<DeadlineClient>(endpoint, deadline);
}
int PrepareLinuxDeadlineSocket(const LinuxDeadlineEndpoint& endpoint,
                               const LinuxDeadline& deadline) {
  DeadlineClient peer(endpoint, deadline);
  return peer.ReleaseValidatedFd();
}
}  // namespace mozc
#endif
