# Copyright 2026 Mozkey contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Single-threaded selector/socketpair model; no Fcitx, worker, or server.

The fake peer's delay is scheduled, not a blocked filesystem syscall. Transport
uses EOF framing as the existing Unix protocol does. UI is a synthetic ledger.
"""
import json
import selectors
import socket
import time
import unittest


def scenario(mode):
    sel = selectors.DefaultSelector()
    client, peer = socket.socketpair()
    client.setblocking(False)
    peer.setblocking(False)
    sel.register(client, selectors.EVENT_READ)
    start = time.monotonic()
    delay = .350 if mode != 'normal' else .010
    due, end = start + delay, start + .550
    beat = start + .005
    callbacks, lags = [], []
    ticks = 0
    epoch = 1
    expected = (epoch, 7)
    applied = set()
    commits = []
    preedit = 'fixture'
    buf = bytearray()
    wire = json.dumps({'epoch': 1, 'request': 7, 'commit': 'fixture'}).encode()
    if mode == 'malformed':
        wire = b'bad'
    if mode == 'oversized':
        wire = b'x' * 5000
    offset = 0
    response_ms = None
    ticks_at_response = None
    cancelled = False
    live = True

    def apply(reply):
        nonlocal preedit, response_ms, ticks_at_response
        identity = (reply['epoch'], reply['request'])
        if identity != expected or epoch != expected[0] or identity in applied:
            return
        applied.add(identity)
        commits.append(reply['commit'])
        preedit = ''
        response_ms = (time.monotonic() - start) * 1000
        ticks_at_response = ticks

    def cancel():
        nonlocal epoch, live, cancelled
        epoch += 1
        cancelled = True
        if live:
            sel.unregister(client)
            client.close()
            live = False
        buf.clear()  # Keep preedit; no replay or uncertain commit.

    try:
        while time.monotonic() < end:
            now = time.monotonic()
            if now >= beat:
                lags.append(max(0, now - beat) * 1000)
                ticks += 1
                beat = now + .005
            if mode == 'cancel' and now >= start + .050 and not cancelled:
                cancel()
            if now >= due and mode != 'silent' and offset < len(wire):
                # One bounded send per dispatch; no thread and no sleep in callback.
                try:
                    offset += peer.send(wire[offset:offset + 1024])
                except (BlockingIOError, BrokenPipeError):
                    pass
                if offset == len(wire):
                    peer.shutdown(socket.SHUT_WR)
            if mode == 'silent' and now >= start + .450 and not cancelled:
                cancel()
            # The wait is the idle reactor wait, never a synchronous RPC wait.
            for key, _ in sel.select(max(0, min(.005, beat - time.monotonic()))):
                cb_start = time.monotonic()
                try:
                    chunk = key.fileobj.recv(4096)
                except BlockingIOError:
                    continue
                if chunk:
                    buf.extend(chunk)
                    if len(buf) > 4096:
                        cancel()
                else:
                    sel.unregister(client)
                    client.close()
                    live = False
                    try:
                        reply = json.loads(buf)
                        apply(reply)
                    except (ValueError, KeyError, TypeError):
                        pass
                callbacks.append((time.monotonic() - cb_start) * 1000)
        # Simulate an already-posted duplicate/late completion after transport exit.
        if mode == 'cancel' or response_ms is not None:
            for _ in range(2):
                apply({'epoch': 1, 'request': 7, 'commit': 'fixture'})
        return dict(ticks=ticks, lag_max_ms=max(lags, default=0),
                    callback_max_ms=max(callbacks, default=0),
                    response_ms=response_ms, commits=commits, preedit=preedit,
                    ticks_at_response=ticks_at_response,
                    live=live, cancelled=cancelled)
    finally:
        client.close()
        peer.close()
        sel.close()


class AsyncIPCPoC(unittest.TestCase):
    def check(self, mode):
        result = scenario(mode)
        print('ASYNC_METRIC ' + mode + ' ' + json.dumps(result), flush=True)
        # Progress and semantic assertions, not the former 100/250ms RPC gate.
        self.assertGreater(result['ticks'], 20)
        self.assertFalse(result['live'])
        return result

    def test_normal(self):
        r = self.check('normal')
        self.assertEqual(r['commits'], ['fixture'])
        self.assertEqual(r['preedit'], '')

    def test_350ms_response(self):
        r = self.check('delay')
        self.assertGreaterEqual(r['response_ms'], 350)
        self.assertGreater(r['ticks_at_response'], 20)
        self.assertEqual(r['commits'], ['fixture'])

    def test_cancel_rejects_late_response_and_keeps_preedit(self):
        r = self.check('cancel')
        self.assertTrue(r['cancelled'])
        self.assertEqual(r['commits'], [])
        self.assertEqual(r['preedit'], 'fixture')

    def test_silent_peer_does_not_stop_reactor(self):
        r = self.check('silent')
        self.assertEqual(r['commits'], [])
        self.assertEqual(r['preedit'], 'fixture')

    def test_malformed_and_oversized(self):
        for mode in ('malformed', 'oversized'):
            with self.subTest(mode=mode):
                r = self.check(mode)
                self.assertEqual(r['commits'], [])
                self.assertEqual(r['preedit'], 'fixture')


if __name__ == '__main__':
    unittest.main(verbosity=2)
