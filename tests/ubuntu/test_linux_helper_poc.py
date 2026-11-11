# Copyright 2026 Mozkey contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Linux-only, single-threaded diagnostic. Never used by a product or IME.

The helper transfers a synthetic socketpair, not a verified Mozkey connection.
The parent must stay alive until it has reaped its child; this is not an addon
unload solution. Python after fork is unsuitable for a multithreaded host.
"""
import array
import os
import select
import signal
import socket
import struct
import sys
import time
import unittest

BUDGET = 0.100
TOKEN = struct.Struct('!Q')


def child(control, mode, generation):
    s = socket.socket(fileno=control)
    if mode == 'delay':
        s.send(b'R')  # Prove the injected stop was reached before timeout.
        time.sleep(.350)  # Model a blocking verification syscall, not real I/O.
    if mode == 'exit':
        os._exit(23)
    a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
    b.sendall(b'P')
    if mode == 'bad-generation':
        generation += 1
    fds = array.array('i', [a.fileno()])
    if mode == 'extra-fd':
        fds.append(b.fileno())
    s.sendmsg([TOKEN.pack(generation)], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)])
    a.close()
    b.close()
    s.close()


class Owner:
    """Single owner, one child; no blocking join and no unowned child."""
    def __init__(self):
        self.pid = None
        self.pidfd = None
        self.channel = None
        self.generation = 0

    def start(self, mode='ok'):
        if self.pid is not None:
            return False
        parent, sub = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_CLOEXEC | socket.SOCK_NONBLOCK)
        self.generation += 1
        gen = self.generation
        sub.set_inheritable(True)
        try:
            pid = os.fork()
        except BaseException:
            parent.close(); sub.close()
            raise
        if pid == 0:
            try:
                parent.close()
                if mode == 'preexec-delay':
                    os.write(sub.fileno(), b'R')
                    time.sleep(.350)
                if mode == 'bad-exec':
                    os.execv('/nonexistent/mozkey-poc-helper', ['missing'])
                os.execv(sys.executable, [sys.executable, os.path.abspath(__file__), '--helper', str(sub.fileno()), mode, str(gen)])
            except BaseException:
                os._exit(127)
        sub.close()
        self.pid = pid
        self.channel = parent
        # Direct child is unreaped and cannot be reused. Failure is fail closed.
        try:
            self.pidfd = os.pidfd_open(pid, 0)
        except (OSError, AttributeError):
            self.cancel()
            raise
        return True

    def receive(self, deadline):
        if self.channel is None:
            return None
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                self.cancel()
                return None
            ready, _, _ = select.select([self.channel], [], [], left)
            if not ready:
                continue
            try:
                data, ancillary, flags, _ = self.channel.recvmsg(TOKEN.size, socket.CMSG_SPACE(8 * array.array('i').itemsize), socket.MSG_CMSG_CLOEXEC)
            except BlockingIOError:
                continue
            fds = []
            for level, kind, payload in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    values = array.array('i')
                    values.frombytes(payload[:len(payload) - len(payload) % values.itemsize])
                    fds.extend(values)
            good = (not flags & (socket.MSG_CTRUNC | socket.MSG_TRUNC) and len(data) == TOKEN.size
                    and TOKEN.unpack(data)[0] == self.generation and len(fds) == 1
                    and time.monotonic() < deadline)
            if not good:
                for fd in fds:
                    os.close(fd)
                self.cancel()
                return None
            fd = fds[0]
            if os.get_inheritable(fd):
                os.close(fd); self.cancel(); return None
            self.channel.close(); self.channel = None
            return fd

    def cancel(self):
        self.generation += 1
        if self.channel is not None:
            self.channel.close(); self.channel = None
        if self.pid is not None:
            try:
                if self.pidfd is not None:
                    signal.pidfd_send_signal(self.pidfd, signal.SIGKILL)
                else:
                    # Only direct, unreaped child while pidfd_open failed.
                    os.kill(self.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def reap(self):
        if self.pid is None:
            return True
        pid, _ = os.waitpid(self.pid, os.WNOHANG)
        if pid == 0:
            return False
        self.pid = None
        if self.pidfd is not None:
            os.close(self.pidfd); self.pidfd = None
        return True


class HelperPoC(unittest.TestCase):
    def setUp(self):
        self.owner = Owner()

    def tearDown(self):
        self.owner.cancel()
        deadline = time.monotonic() + 2
        while not self.owner.reap():
            if time.monotonic() >= deadline:
                self.fail('child not reaped; owner must remain alive')
            time.sleep(.005)

    def metric(self, name, start):
        ms = (time.monotonic() - start) * 1000
        print(f'METRIC {name} {ms:.3f}ms', flush=True)
        return ms

    def await_reap(self):
        end = time.monotonic() + 2
        while not self.owner.reap() and time.monotonic() < end:
            time.sleep(.005)
        self.assertIsNone(self.owner.pid)

    def test_normal_socket_transfer(self):
        samples = []
        for _ in range(20):
            start = time.monotonic()
            self.assertTrue(self.owner.start())
            fd = self.owner.receive(start + BUDGET)
            self.assertIsNotNone(fd)
            with socket.socket(fileno=fd) as received:
                self.assertEqual(received.recv(1), b'P')
            samples.append((time.monotonic() - start) * 1000)
            self.await_reap()
        print(f'METRIC normal_20_max {max(samples):.3f}ms', flush=True)
        self.assertLess(max(samples), 100)

    def test_350ms_verification_stop(self):
        start = time.monotonic(); self.assertTrue(self.owner.start('delay'))
        ready, _, _ = select.select([self.owner.channel], [], [], .080)
        self.assertTrue(ready)
        self.assertEqual(self.owner.channel.recv(1), b'R')
        self.assertIsNone(self.owner.receive(start + BUDGET))
        self.assertLess(self.metric('delay_dispatch', start), 250)
        self.assertFalse(self.owner.start())  # no replacement until reaped
        reap_start = time.monotonic()
        self.await_reap()
        self.metric('delay_reap_after_dispatch', reap_start)
        self.assertTrue(self.owner.start())
        fd = self.owner.receive(time.monotonic() + BUDGET)
        self.assertIsNotNone(fd); os.close(fd)

    def test_350ms_preexec_stop(self):
        start = time.monotonic(); self.assertTrue(self.owner.start('preexec-delay'))
        self.assertLess(self.metric('spawn_preexec', start), 100)
        ready, _, _ = select.select([self.owner.channel], [], [], .080)
        self.assertTrue(ready)
        self.assertEqual(self.owner.channel.recv(1), b'R')
        self.assertIsNone(self.owner.receive(start + BUDGET))
        self.assertLess(self.metric('preexec_dispatch', start), 250)
        self.assertFalse(self.owner.start())
        self.await_reap()

    def test_cancel_and_bounded_children(self):
        start = time.monotonic(); self.assertTrue(self.owner.start('delay'))
        self.owner.cancel()
        self.assertLess(self.metric('cancel', start), 250)
        for _ in range(100):
            self.assertFalse(self.owner.start())
        self.assertIsNone(self.owner.receive(start + BUDGET))
        self.await_reap()

    def test_wrong_generation_and_extra_fd_are_rejected(self):
        for mode in ('bad-generation', 'extra-fd'):
            self.assertTrue(self.owner.start(mode))
            self.assertIsNone(self.owner.receive(time.monotonic() + BUDGET))
            self.await_reap()

    def test_exec_failure_and_peer_exit(self):
        for mode in ('bad-exec', 'exit'):
            start = time.monotonic(); self.assertTrue(self.owner.start(mode))
            self.assertIsNone(self.owner.receive(start + BUDGET))
            self.assertLess(self.metric(mode, start), 250)
            self.await_reap()

    def test_no_parent_fd_leak_over_repeated_failures(self):
        baseline = len(os.listdir('/proc/self/fd'))
        for _ in range(20):
            self.assertTrue(self.owner.start('extra-fd'))
            self.assertIsNone(self.owner.receive(time.monotonic() + BUDGET))
            self.await_reap()
        self.assertEqual(len(os.listdir('/proc/self/fd')), baseline)

    def test_cancel_after_fd_queued_discards_it(self):
        baseline = len(os.listdir('/proc/self/fd'))
        self.assertTrue(self.owner.start())
        ready, _, _ = select.select([self.owner.channel], [], [], BUDGET)
        self.assertTrue(ready)
        self.owner.cancel()  # Closing the channel drops the queued SCM_RIGHTS.
        self.assertIsNone(self.owner.receive(time.monotonic() + BUDGET))
        self.await_reap()
        self.assertEqual(len(os.listdir('/proc/self/fd')), baseline)


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--helper':
        child(int(sys.argv[2]), sys.argv[3], int(sys.argv[4]))
    else:
        unittest.main(verbosity=2)
