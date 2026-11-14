#!/usr/bin/env python3
# Copyright 2026 Mozkey contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Copy an opt-in build to an empty work directory, without executing an IME.

This creates an INCOMPLETE staging snapshot, not an installable release.
The addon is disabled and its factory refuses loading until A5 is integrated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_elf(path, shared=False):
    header = subprocess.check_output(['readelf', '-h', str(path)], text=True)
    if 'ELF64' not in header or 'Advanced Micro Devices X86-64' not in header:
        raise ValueError(f'Unexpected ELF architecture: {path.name}')
    dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
    paths = re.findall(r'\((?:RPATH|RUNPATH)\).*\[(.*?)\]', dynamic)
    if any('/home/' in p or '/tmp/' in p or '.local/' in p or 'bazel-out' in p
           for p in paths):
        raise ValueError(f'Build-host runtime dependency: {path.name}: {paths}')
    result = {'needed': re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic),
              'runtime_search_paths': paths}
    if shared:
        symbols = subprocess.check_output(
            ['nm', '-D', '--defined-only', str(path)], text=True)
        exports = [line.split()[-1] for line in symbols.splitlines() if line.split()]
        if exports != ['fcitx_addon_factory_instance_mozkey']:
            raise ValueError(f'Unexpected addon exports: {exports}')
        result['exports'] = exports
    return result


def stage(checkout, binary_root, output):
    if output.exists() or output.is_symlink():
        raise ValueError('Output must not exist; existing assets are never overwritten')
    frontend = checkout / 'src/unix/fcitx5'
    mappings = {
        'usr/lib/x86_64-linux-gnu/fcitx5/fcitx5-mozkey.so': binary_root / 'unix/fcitx5/fcitx5-mozkey.so',
        'usr/lib/mozkey/mozkey_server': binary_root / 'server/mozkey_server',
        'usr/lib/mozkey/mozkey_tool': binary_root / 'gui/tool/mozkey_tool',
        'usr/share/fcitx5/addon/mozkey.conf': frontend / 'mozkey-addon.conf',
        'usr/share/fcitx5/inputmethod/mozkey.conf': frontend / 'mozkey.conf',
        'usr/share/doc/mozkey/LICENSE': checkout / 'LICENSE',
        'usr/share/doc/mozkey/THIRD_PARTY_NOTICES.md': checkout / 'THIRD_PARTY_NOTICES.md',
        'usr/share/doc/mozkey/COPYING.LGPL-2.0': frontend / 'COPYING.LGPL-2.0',
        'usr/share/doc/mozkey/COPYING.GPL-2.0': frontend / 'COPYING.GPL-2.0',
        'usr/share/doc/mozkey/ORIGIN.json': frontend / 'ORIGIN.json',
        'usr/share/doc/mozkey/UPSTREAM-LICENSE': frontend / 'UPSTREAM-LICENSE',
    }
    for target, source in {
        'fcitx_mozkey': 'icon', 'fcitx_mozkey_tool': 'icon',
        'fcitx_mozkey_direct': 'direct', 'fcitx_mozkey_hiragana': 'hiragana',
        'fcitx_mozkey_katakana_full': 'full_katakana',
        'fcitx_mozkey_katakana_half': 'half_katakana',
        'fcitx_mozkey_alpha_half': 'half_ascii',
        'fcitx_mozkey_alpha_full': 'full_ascii',
    }.items():
        mappings[f'usr/share/icons/hicolor/scalable/apps/{target}.svg'] = (
            checkout / f'src/data/images/{source}.svg')
    # Retain the frontend source and build inputs for review and relinking.
    for path in frontend.iterdir():
        if path.suffix in {'.cc', '.h', '.proto', '.bazel', '.map'}:
            mappings['usr/share/doc/mozkey/frontend-source/' + path.name] = path
    for source in mappings.values():
        if not source.is_file():
            raise ValueError(f'Missing build/source input: {source}')
    if 'Enabled=False' not in (frontend / 'mozkey-addon.conf').read_text():
        raise ValueError('Unreviewed addon must remain disabled')
    elf = {target: inspect_elf(source, target.endswith('.so'))
           for target, source in mappings.items()
           if target.endswith('.so') or target.startswith('usr/lib/mozkey/')}
    manifest = {
        'status': 'INCOMPLETE_NOT_FOR_IME_LOAD',
        'addon_factory': 'refuses loading; A5 integration pending',
        'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=checkout, text=True).strip(),
        'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=checkout)),
        'platform': platform.platform(),
        'build_options': ['--config=release_build', '--//:linux_mozkey=true', '--jobs=2'],
        'runtime_server': '/usr/lib/mozkey/mozkey_server',
        'runtime_tool': '/usr/lib/mozkey/mozkey_tool',
        'dictionary': 'embedded in dedicated server; reused Phase 1 generated inputs',
        'embedded_dataset_sha256': sha256(binary_root / 'data_manager/oss/mozc.data'),
        'elf': elf,
        'files': [],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='mozkey-stage-', dir=output.parent) as temporary:
        tree = Path(temporary) / 'tree'
        tree.mkdir()
        for relative, source in sorted(mappings.items()):
            destination = tree / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            destination.chmod(0o755 if relative.startswith('usr/lib/') else 0o644)
            manifest['files'].append({'path': relative, 'sha256': sha256(destination),
                                      'bytes': destination.stat().st_size})
        (tree / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        (tree / 'NOT_READY_FOR_IME_LOAD.txt').write_text(
            'Phase 2A incomplete. Do not install or register this snapshot.\n'
            'The addon is disabled and its factory refuses loading.\n')
        if output.exists():
            raise ValueError('Output appeared during staging; refusing overwrite')
        tree.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bazel-bin', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    checkout = Path(__file__).resolve().parents[2]
    manifest = stage(checkout, args.bazel_bin.resolve(), args.output.absolute())
    print(json.dumps({'output': str(args.output), 'files': len(manifest['files']),
                      'status': manifest['status']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
