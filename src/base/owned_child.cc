// Copyright 2026 Mozkey contributors
// SPDX-License-Identifier: BSD-3-Clause
#include "base/owned_child.h"
#include <elf.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/eventfd.h>
// Some supported glibc headers omit __BEGIN_DECLS for these C entry points.
extern "C" {
#include <sys/pidfd.h>
}
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include "base/mozkey_profile.h"

namespace mozc {
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
struct Fd {
  explicit Fd(int value = -1) : value(value) {}
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { const int error = errno; if (value >= 0) close(value); errno = error; }
  int Release() { return std::exchange(value, -1); }
  int value;
};
int Image(const std::string& path) {
  if (path.size() < 2 || path.front() != '/' || path.back() == '/' || path.find('\0') != path.npos) { errno = EINVAL; return -1; }
  Fd directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (directory.value < 0) return -1;
  size_t at = 1;
  while (at < path.size()) {
    auto slash = path.find('/', at); const auto name = path.substr(at, slash-at);
    if (name.empty() || name == "." || name == "..") { errno = EINVAL; return -1; }
    const int next = openat(directory.value, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
        (slash == path.npos ? 0 : O_DIRECTORY));
    if (next < 0) return -1;
    close(directory.value); directory.value = next;
    if (slash == path.npos) break;
    at = slash+1;
  }
  struct stat info{}; Elf64_Ehdr header{};
  if (fstat(directory.value, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
      (info.st_uid != 0 && info.st_uid != geteuid()) || (info.st_mode & 06022) || !(info.st_mode & 0111) ||
      pread(directory.value, &header, sizeof(header), 0) != sizeof(header) ||
      memcmp(header.e_ident, ELFMAG, SELFMAG) || header.e_ident[EI_CLASS] != ELFCLASS64 ||
      header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_machine != EM_X86_64 || !header.e_entry ||
      (header.e_type != ET_EXEC && header.e_type != ET_DYN)) { errno = EPERM; return -1; }
  return fcntl(directory.value, F_DUPFD_CLOEXEC, 5);
}
void Wake(int fd) {
  if (fd < 0) return;
  uint64_t one = 1; ssize_t result;
  do { result = write(fd, &one, sizeof(one)); } while (result < 0 && errno == EINTR);
}
bool EnvironmentEntry(const std::string& value) {
  for (const char* name : {"HOME=", "LANG=", "LC_ALL=", "LC_CTYPE=", "TZ=", "XDG_CACHE_HOME=", "XDG_DATA_HOME="})
    if (value.starts_with(name) && value.find('\0') == value.npos) return true;
  return false;
}
bool SamePeerProcess(int socket, int owned_identity) {
#ifdef SO_PEERPIDFD
  Fd peer;
  socklen_t size = sizeof(peer.value);
  struct statfs owned_fs{}, peer_fs{};
  struct stat owned_info{}, peer_info{};
  // Older anon_inode pidfds share an inode: equality alone is NOT identity.
  // pidfs has a process-specific inode retained by the owned descriptor.
  constexpr long kPidFsMagic = 0x50494446;
  if (owned_identity < 0 || fstatfs(owned_identity, &owned_fs) ||
      owned_fs.f_type != kPidFsMagic ||
      getsockopt(socket, SOL_SOCKET, SO_PEERPIDFD, &peer.value, &size) ||
      peer.value < 0 || size != sizeof(peer.value) ||
      fstatfs(peer.value, &peer_fs) || peer_fs.f_type != kPidFsMagic ||
      fstat(owned_identity, &owned_info) || fstat(peer.value, &peer_info)) return false;
  pollfd exited{owned_identity, POLLIN, 0};
  return poll(&exited, 1, 0) == 0 && owned_info.st_dev == peer_info.st_dev &&
      owned_info.st_ino == peer_info.st_ino;
#else
  (void)socket; (void)owned_identity;
  return false;  // No PID-number-only administrative-request fallback.
#endif
}
}
struct OwnedChild::State {
  explicit State(std::shared_ptr<std::atomic<bool>> flag) : cancelled(std::move(flag)) {}
  ~State() { if (identity_fd >= 0) close(identity_fd); }
  std::shared_ptr<std::atomic<bool>> cancelled;
  std::atomic<bool> registered{false}, published{false}, finished{false}, collected{false};
  std::atomic<int> error{0}, pid{-1};
  int pidfd = -1; // Native supervisor owns this after publication.
  int identity_fd = -1; // Immutable after publication; State pins process identity.
  std::mutex address_mutex;
  sockaddr_un address{}; socklen_t address_length = 0;
  std::array<char, 64> shutdown{}; size_t shutdown_length = 0;
  // Supervisor only from here.
  int socket = -1;
  bool stopping = false, term_sent = false, kill_sent = false, writing = false;
  size_t sent = 0, received = 0;
  Clock::time_point graceful_end{}, term_end{};
};
struct OwnedChild::Root {
  std::mutex mutex;
  std::shared_ptr<State> slot;
  std::atomic<bool> sealed{false}, started{false};
  std::atomic<unsigned> active{0};
  std::atomic<int> control{-1}; // Process-lived, including after join; no wake/close reuse race.
  pthread_t thread{};
  bool EnsureThread() {
    if (started.load()) return true;
    if (control < 0) {
      Fd event(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
      if (event.value >= 0) control.store(fcntl(event.value, F_DUPFD_CLOEXEC, 5));
    }
    if (control < 0) return false;
    if (pthread_create(&thread, nullptr, [](void* pointer) -> void* {
          static_cast<Root*>(pointer)->Run(); return nullptr;
        }, this)) return false;
    started.store(true); return true;
  }
  void Cleanup(const std::shared_ptr<State>& state) {
    if (state->socket >= 0) close(std::exchange(state->socket, -1));
    if (state->pidfd >= 0) close(std::exchange(state->pidfd, -1));
    state->finished.store(true);
    std::lock_guard lock(mutex);
    if (slot == state) { slot.reset(); active.store(0); }
  }
  void Terminate(const std::shared_ptr<State>& state) {
    if (!state->term_sent) {
      if (pidfd_send_signal(state->pidfd, SIGTERM, nullptr, 0) && errno != ESRCH) state->error.store(errno);
      state->term_sent = true; state->term_end = Clock::now()+50ms;
      if (state->socket >= 0) close(std::exchange(state->socket, -1));
    }
  }
  void Stop(const std::shared_ptr<State>& state) {
    if (!state->stopping) {
      state->stopping = true; state->graceful_end = Clock::now()+300ms;
      std::lock_guard lock(state->address_mutex);
      if (state->address_length && state->shutdown_length) {
        state->socket = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (state->socket >= 0 && (connect(state->socket, reinterpret_cast<const sockaddr*>(&state->address),
            state->address_length) == 0 || errno == EINPROGRESS)) state->writing = true;
        else Terminate(state);
      } else Terminate(state);
    }
    if (state->writing && state->socket >= 0) {
      int error = 0; socklen_t length = sizeof(error);
      ucred peer{}; socklen_t size = sizeof(peer);
      if (getsockopt(state->socket, SOL_SOCKET, SO_ERROR, &error, &length) || error ||
          getsockopt(state->socket, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
          peer.uid != geteuid() || peer.pid != state->pid.load() ||
          !SamePeerProcess(state->socket, state->identity_fd)) { Terminate(state); }
      else {
        const auto count = send(state->socket, state->shutdown.data()+state->sent,
            state->shutdown_length-state->sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (count > 0) state->sent += count;
        else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) Terminate(state);
        if (state->socket >= 0 && state->sent == state->shutdown_length) {
          if (shutdown(state->socket, SHUT_WR)) Terminate(state);
          state->writing = false;
        }
      }
    } else if (state->socket >= 0) {
      char bytes[4096]; const auto count = recv(state->socket, bytes, sizeof(bytes), MSG_DONTWAIT);
      if (count > 0) { state->received += count; if (state->received > 65536) Terminate(state); }
      else if (!count) close(std::exchange(state->socket, -1));
      else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Terminate(state);
    }
    if (!state->term_sent && Clock::now() >= state->graceful_end) Terminate(state);
    if (state->term_sent && !state->kill_sent && Clock::now() >= state->term_end) {
      if (pidfd_send_signal(state->pidfd, SIGKILL, nullptr, 0) && errno != ESRCH) state->error.store(errno);
      state->kill_sent = true;
    }
  }
  void Run() {
    for (;;) {
      std::shared_ptr<State> state;
      { std::lock_guard lock(mutex); state = slot; }
      if (!state && sealed.load()) return;
      if (state && state->published.load()) {
        if (state->pidfd < 0) { Cleanup(state); continue; }
        pollfd exited{state->pidfd, POLLIN, 0};
        if (poll(&exited, 1, 0) > 0 && (exited.revents & (POLLIN | POLLHUP))) {
          siginfo_t info{};
          const int result = waitid(P_PIDFD, state->pidfd, &info, WEXITED | WNOHANG);
          if (result == 0 && info.si_pid) {
            state->collected.store(true); Cleanup(state); continue;
          }
          if (result < 0 && errno == ECHILD) { Cleanup(state); continue; } // Already reaped elsewhere; never waitpid(-1).
        }
        if (sealed.load() || state->cancelled->load()) Stop(state);
      }
      pollfd events[3]{{control, POLLIN, 0}, {-1, POLLIN, 0}, {-1, POLLIN, 0}};
      if (state && state->published.load()) {
        events[1].fd = state->pidfd; events[2].fd = state->socket;
        if (state->writing) events[2].events = POLLOUT;
      }
      const int timeout = state && (!state->published.load() || state->stopping) ? 10 : -1;
      if (poll(events, 3, timeout) > 0 && events[0].revents) { uint64_t one; const auto read_bytes = read(control, &one, sizeof(one)); (void)read_bytes; }
    }
  }
};
OwnedChild::Root& OwnedChild::ProcessRoot() { static auto* root = new Root; return *root; }
std::shared_ptr<OwnedChild> OwnedChild::SpawnOnWorker(Spec spec, std::shared_ptr<std::atomic<bool>> cancelled) {
  if (!cancelled || cancelled->load() || getuid() != geteuid() || spec.profile.size() > 4096 || spec.arguments.size() > 16 || spec.environment.size() > 32) return {};
  auto state = std::make_shared<State>(std::move(cancelled));
  auto handle = std::shared_ptr<OwnedChild>(new OwnedChild(state));
  auto& root = ProcessRoot();
  {
    std::lock_guard lock(root.mutex);
    if (root.sealed.load() || root.slot || !root.EnsureThread()) return {};
    root.slot = state; root.active.store(1); state->registered.store(true);
  }
  auto fail = [&](int error) { state->error.store(error ? error : EIO); state->published.store(true); Wake(root.control); return handle; };
  try {
    Fd image(Image(spec.executable)); if (image.value < 0) return fail(errno);
    Fd launch(OpenMozkeyLockFile(spec.profile+"/.launch.lock")); if (launch.value < 0) return fail(errno);
    struct flock lock{}; lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
    if (fcntl(launch.value, F_OFD_SETLK, &lock)) return fail(errno);
    const int high_launch = fcntl(launch.value, F_DUPFD_CLOEXEC, 5); if (high_launch < 0) return fail(errno);
    Fd inherited_launch(high_launch);
    Fd null(open("/dev/null", O_RDWR | O_NOFOLLOW | O_CLOEXEC)); struct stat info{};
    if (null.value < 0 || fstat(null.value, &info) || !S_ISCHR(info.st_mode) || info.st_rdev != makedev(1,3)) return fail(EPERM);
    Fd high_null(fcntl(null.value, F_DUPFD_CLOEXEC, 5)); if (high_null.value < 0) return fail(errno);
    std::vector<std::string> argv{spec.executable, "--mozkey-owned-parent="+std::to_string(getpid()), "--mozkey-owned-profile="+spec.profile};
    for (auto& arg : spec.arguments) { if (arg.size() > 4096 || arg.find('\0') != arg.npos || arg.starts_with("--mozkey-owned-")) return fail(EINVAL); argv.push_back(std::move(arg)); }
    std::vector<char*> args; for (auto& arg : argv) args.push_back(arg.data()); args.push_back(nullptr);
    std::vector<std::string> environment;
    for (auto& item : spec.environment) { if (item.size() > 4096 || !EnvironmentEntry(item)) return fail(EINVAL); environment.push_back(std::move(item)); }
    std::vector<char*> env; for (auto& item : environment) env.push_back(item.data()); env.push_back(nullptr);
    posix_spawn_file_actions_t actions; int error = posix_spawn_file_actions_init(&actions); if (error) return fail(error);
    struct Actions { posix_spawn_file_actions_t* value; ~Actions() { posix_spawn_file_actions_destroy(value); } } action_guard{&actions};
    for (int target : {0,1,2}) if (!error) error = posix_spawn_file_actions_adddup2(&actions, high_null.value, target);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, image.value, 3);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, inherited_launch.value, 4);
    if (!error) error = posix_spawn_file_actions_addclosefrom_np(&actions, 5);
    if (error) return fail(error);
    posix_spawnattr_t attributes; error = posix_spawnattr_init(&attributes); if (error) return fail(error);
    struct Attributes { posix_spawnattr_t* value; ~Attributes() { posix_spawnattr_destroy(value); } } attribute_guard{&attributes};
    sigset_t empty, defaults; sigemptyset(&empty); sigemptyset(&defaults);
    for (int sig : {SIGTERM,SIGINT,SIGHUP,SIGPIPE,SIGCHLD}) sigaddset(&defaults, sig);
    error = posix_spawnattr_setsigmask(&attributes, &empty);
    if (!error) error = posix_spawnattr_setsigdefault(&attributes, &defaults);
    if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_RESETIDS);
    if (error || state->cancelled->load() || root.sealed.load()) return fail(error ? error : ECANCELED);
    int pidfd = -1;
    error = pidfd_spawn(&pidfd, "/proc/self/fd/3", &actions, &attributes, args.data(), env.data());
    if (error) return fail(error);
    state->identity_fd = fcntl(pidfd, F_DUPFD_CLOEXEC, 5);
    const int identity_error = errno;
    state->pidfd = pidfd;
    state->published.store(true); Wake(root.control);
    if (state->identity_fd < 0) { state->error.store(identity_error); state->cancelled->store(true); Wake(root.control); return handle; }
    const int child_pid = pidfd_getpid(state->identity_fd);
    if (child_pid < 1) { state->error.store(errno ? errno : ESRCH); state->cancelled->store(true); Wake(root.control); return handle; }
    state->pid.store(child_pid);
    return handle;
  } catch (...) { state->cancelled->store(true); return fail(ENOMEM); }
}
OwnedChild::~OwnedChild() { if (state_->registered.load()) Cancel(); }
void OwnedChild::Cancel() { state_->cancelled->store(true); NotifyCancellation(); }
void OwnedChild::NotifyCancellation() { Wake(ProcessRoot().control); }
bool OwnedChild::finished() const { return state_->finished.load(); }
int OwnedChild::error() const { return state_->error.load(); }
int OwnedChild::pid() const { return state_->pid.load(); }
bool OwnedChild::reaped_by_owner() const { return state_->collected.load(); }
bool OwnedChild::Busy() { return ProcessRoot().active.load() != 0; }
bool OwnedChild::ConfigureShutdownFromVerifiedConnection(int fd, const std::string& request) {
  if (request.empty() || request.size() > 64 || finished() || state_->cancelled->load()) return false;
  ucred peer{}; socklen_t size = sizeof(peer); sockaddr_un address{}; socklen_t length = sizeof(address);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != geteuid() || peer.pid <= 0 ||
      peer.pid != state_->pid.load() || getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length) || length > sizeof(address) ||
      address.sun_family != AF_UNIX || length <= sizeof(address.sun_family)+12 ||
      address.sun_path[0] != '\0' || memcmp(address.sun_path+1, "tmp/.mozkey.", 11) ||
      !SamePeerProcess(fd, state_->identity_fd)) return false;
  std::lock_guard lock(state_->address_mutex);
  if (state_->shutdown_length) return state_->address_length == length &&
      memcmp(&state_->address, &address, length) == 0 && state_->shutdown_length == request.size() &&
      memcmp(state_->shutdown.data(), request.data(), request.size()) == 0;
  state_->address = address; state_->address_length = length;
  memcpy(state_->shutdown.data(), request.data(), request.size()); state_->shutdown_length = request.size();
  return true;
}
bool OwnedChild::WaitForProcessExit(std::chrono::milliseconds budget) {
  auto& root = ProcessRoot(); root.sealed.store(true); Wake(root.control);
  const auto end = Clock::now()+std::clamp(budget, 0ms, 390ms);
  std::unique_lock lock(root.mutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  lock.unlock();
  if (!root.started.load()) return !root.active.load();
  for (;;) {
    const int error = pthread_tryjoin_np(root.thread, nullptr);
    if (!error) { root.started.store(false); return !root.active.load(); }
    if (error != EBUSY || Clock::now() >= end) return false;
    std::this_thread::sleep_for(std::min(end-Clock::now(), std::chrono::duration_cast<Clock::duration>(1ms)));
  }
}
bool InitializeOwnedChild(int* argc, char* argv[], std::string* profile) {
  if (!profile || !argc || *argc < 1 || !argv) return false;
  profile->clear();
  std::string parent, root; bool seen_parent = false, seen_root = false;
  for (int i=1; i<*argc; ++i) {
    if (!argv[i]) return false;
    const std::string_view arg(argv[i]);
    if (arg.starts_with("--mozkey-owned-parent=")) { if (seen_parent) return false; seen_parent = true; parent = arg.substr(22); }
    else if (arg.starts_with("--mozkey-owned-profile=")) { if (seen_root) return false; seen_root = true; root = arg.substr(23); }
    else if (arg.starts_with("--mozkey-owned-")) return false;
  }
  if (!seen_parent && !seen_root) return true;
  int pid = 0; const auto parsed = std::from_chars(parent.data(), parent.data()+parent.size(), pid);
  if (!seen_parent || !seen_root || parent.empty() || root.empty() || parsed.ec != std::errc{} || parsed.ptr != parent.data()+parent.size() || pid <= 1 ||
      prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != pid) return false;
  Fd directory(OpenMozkeyProfileDirectory(root)); if (directory.value < 0) return false;
  Fd launch(OpenMozkeyLockFile(root+"/.launch.lock")); if (launch.value < 0) return false;
  struct stat inherited{}, named{}, image{}, self{};
  struct flock named_lock{}, inherited_lock{}; named_lock.l_type = inherited_lock.l_type = F_WRLCK;
  named_lock.l_whence = inherited_lock.l_whence = SEEK_SET;
  if (fstat(4, &inherited) || fstat(launch.value, &named) || inherited.st_dev != named.st_dev || inherited.st_ino != named.st_ino ||
      fcntl(launch.value, F_OFD_GETLK, &named_lock) || named_lock.l_type != F_WRLCK || named_lock.l_pid != -1 ||
      named_lock.l_start != 0 || named_lock.l_len != 0 || fcntl(4, F_OFD_GETLK, &inherited_lock) || inherited_lock.l_type != F_UNLCK ||
      fstat(3, &image) || stat("/proc/self/exe", &self) || image.st_dev != self.st_dev || image.st_ino != self.st_ino ||
      fcntl(4, F_SETFD, FD_CLOEXEC) || getppid() != pid) return false;
  close(3);
  int output = 1;
  for (int i=1; i<*argc; ++i) {
    if (!std::string_view(argv[i]).starts_with("--mozkey-owned-")) argv[output++] = argv[i];
  }
  argv[output] = nullptr; *argc = output;
  *profile = std::move(root); return true;
}
}  // namespace mozc
