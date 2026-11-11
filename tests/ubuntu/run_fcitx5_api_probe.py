# Copyright 2026 Mozkey contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Build one standalone probe with installed Fcitx5; never install dependencies."""
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mozkey-fcitx-api-') as directory:
    subprocess.run(['pkg-config', '--atleast-version=5.1.19',
                    'Fcitx5Core', 'Fcitx5Utils'], check=True)
    flags = subprocess.check_output(
        ['pkg-config', '--cflags', '--libs', 'Fcitx5Core', 'Fcitx5Utils'], text=True)
    print('Fcitx5 version:', subprocess.check_output(
        ['pkg-config', '--modversion', 'Fcitx5Core'], text=True).strip(), flush=True)
    binary = str(pathlib.Path(directory) / 'api-probe')
    subprocess.run(['g++', '-std=c++20', '-pthread', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(root / 'fcitx5_api_probe.cc'), '-o', binary,
                    *shlex.split(flags)], check=True, timeout=60)
    subprocess.run([binary], check=True, timeout=15)
