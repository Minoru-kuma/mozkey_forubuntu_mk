# Copyright 2026 Mozkey contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Compile synthetic ELF fixtures only; no product server/IME or tool download."""
import ctypes
import os
import pathlib
import select
import shutil
import signal
import subprocess
import tempfile
import time

root = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='mozkey-owned-child-') as directory:
    work = pathlib.Path(directory)
    common = ['g++', '-std=c++20', '-pthread', '-O2', '-Wall', '-Wextra',
              '-Werror', '-I' + str(root / 'src')]
    sources = [str(root / 'src/base/owned_child.cc'),
               str(root / 'src/base/mozkey_profile.cc')]
    fixture = work / 'fixture'
    probe = work / 'probe'
    subprocess.run([*common, str(root / 'tests/ubuntu/owned_child_fixture.cc'),
                    *sources, '-o', str(fixture)], check=True, timeout=60)
    fixture.chmod(0o700)
    subprocess.run([*common, str(root / 'tests/ubuntu/owned_child_probe.cc'),
                    *sources, str(root / 'src/unix/fcitx5/preparation_worker.cc'),
                    '-Wl,--wrap=getsockopt', '-o', str(probe)], check=True, timeout=60)
    public = work / 'public-fixture'
    shutil.copyfile(fixture, public)
    public.chmod(0o777)
    link = work / 'symlink-fixture'
    link.symlink_to(fixture)
    for mode in ('normal', 'startup350', 'cancel-before-spawn350', 'ignore-term',
                 'graceful', 'stall-shutdown', 'wrong-peer', 'spoof-peer', 'legacy-peer',
                 'duplicate', 'bad-env', 'symlink',
                 'public-image', 'drop-handle', 'auto-reap', 'retained-worker',
                 'exit-no-hold', 'exit-hold'):
        profile = work / mode
        profile.mkdir(mode=0o700)
        executable = link if mode == 'symlink' else public if mode == 'public-image' else fixture
        subprocess.run([str(probe), mode, str(executable), str(profile)],
                       check=True, timeout=12)

    # Distinct host roots must contend on the same inherited OFD lease. No
    # product process is involved; the second root must not start another child.
    profile = work / 'cross-root'
    profile.mkdir(mode=0o700)
    owner = subprocess.Popen([str(probe), 'hold', str(fixture), str(profile)])
    try:
        deadline = time.monotonic() + 3
        while not (profile / '.fixture-child-pid').exists():
            if owner.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('cross-root fixture did not become ready')
            time.sleep(0.001)
        subprocess.run([str(probe), 'lease-busy', str(fixture), str(profile)],
                       check=True, timeout=5)
    finally:
        (profile / '.fixture-release').touch(mode=0o600)
        owner.wait(timeout=8)
    if owner.returncode:
        raise RuntimeError('cross-root owner cleanup failed')

    # Only this disposable driver changes its own subreaper policy. The addon
    # never changes SIGCHLD/subreaper policy in Fcitx5. Adoption makes the death
    # test collectable without treating kill(pid, 0) as proof of reclamation.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'test-only subreaper setup')
    profile = work / 'parent-death'
    profile.mkdir(mode=0o700)
    parent = subprocess.run([str(probe), 'parent-death', str(fixture), str(profile)],
                            check=True, timeout=5)
    pid = int((profile / '.fixture-child-pid').read_text())
    pidfd = os.pidfd_open(pid)
    try:
        poller = select.poll()
        poller.register(pidfd, select.POLLIN)
        if not poller.poll(1000):
            # Cleanup uses the exact descriptor, never a possibly reused PID.
            signal.pidfd_send_signal(pidfd, signal.SIGKILL)
            os.waitid(os.P_PIDFD, pidfd, os.WEXITED)
            raise RuntimeError('owned fixture survived abrupt parent exit')
        result = os.waitid(os.P_PIDFD, pidfd, os.WEXITED)
        if result.si_code != os.CLD_KILLED or result.si_status != signal.SIGKILL:
            raise RuntimeError('unexpected parent-death termination: ' + str(result))
        print('OWNED_CHILD_PROBE parent-death: SIGKILL observed and adopted child reaped', flush=True)
    finally:
        os.close(pidfd)
    print('OWNED_CHILD_PROBE PASS (synthetic children; no actual server/IME)', flush=True)
