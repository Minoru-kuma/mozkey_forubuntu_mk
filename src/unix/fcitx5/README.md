# Phase 2A Fcitx5 staging sources

This is a Phase 2B candidate, **not permission to install, register or load it**.
The addon definition stays `Enabled=False`. The V2 factory constructs the
asynchronous engine after the frozen Phase 2A code safety checks. Full CI for
the final SHA and separate Phase 2B execution authorization remain required.

The frontend uses IO/Timer/Defer-driven RPC and one session per input context.
Filesystem preparation runs on the single owned worker. The 16-frame queue
records accepted keys before consumption. On overflow, accepted frames stay in
order for inspection; new frames are rejected before acceptance and pass
through without conversion. New pass-through input can appear before retained
input is manually resolved, as explicitly allowed by the user. Retained or
uncertain input is never automatically replayed. An explicit retry creates a
fresh pipeline/session only for future input, preserving the old journal.

Show/Next retained input pages expose the whole suspended journal in bounded
UTF-8 pages. Save and Read snapshot actions use private files (directory 0700,
files 0600) in `mozkey-recovery/` under the Fcitx PkgConfig directory. Snapshots
contain plaintext synthetic/user input and are not encrypted. Save failure is
reported while input remains in memory; process-exit retention during storage
failure is not guaranteed, an explicitly accepted future limitation.

Normal shutdown shares a 390ms budget across child and worker cleanup. Exceeding
it is recorded as uncollected, never cleanup success. Focus/IME changes never
use this wait. Permanently stuck kernel I/O and comprehensive same-UID hostile
manipulation proofs are outside Phase 2A. Known UAF, ordinary indefinite waits
and concrete serious vulnerabilities remain prohibited.

New owned launches record key provenance. A later scope reuses only a marked,
unlocked private key with the same profile/key inode and birth time. Unmarked
legacy keys are preserved and rejected; they are not silently migrated. Existing
verified peers remain unowned and are never stopped by this addon. Platforms
without key birth-time support fail closed and need separate evaluation.

Real Fcitx loading, Wayland/X11, real input/display latency and frontend/server
operation belong to Phase 2B. The frozen scope and evidence are in
`AGENTS.md` and `docs/ubuntu/phase-02-results.md`.

Build explicitly from `src/`, using the existing Phase 1 toolchain/cache and
an isolated build HOME/XDG environment as recorded in the Phase 2 results:

```sh
bazelisk build //unix/fcitx5:stage_inputs --//:linux_mozkey=true \
  --config=release_build --jobs=2 --repository_disable_download
bazelisk test //unix/fcitx5:callback_queue_test \
  //unix/fcitx5:connection_preparation_test \
  //unix/fcitx5:frontend_boundary_test //unix/fcitx5:async_session_test \
  //unix/fcitx5:input_pipeline_test //unix/fcitx5:async_config_test \
  //unix/fcitx5:input_recovery_test //unix/fcitx5:frame_translator_test \
  //unix/fcitx5:response_guard_test \
  --//:linux_mozkey=true --config=release_build --jobs=2
```

The existing targets and non-Linux platforms retain their default identity.
New targets are manual and require the Linux opt-in configuration. Runtime
server/tool paths are `/usr/lib/mozkey/mozkey_server` and `mozkey_tool`; staging
paths are never compiled into them. The earlier `mozkey_server_dir` flag was a
proposal and is not implemented: this snapshot uses the single fixed constant
in `config.bzl`. Changing that prefix requires a consistent rebuild.

From the repository root, create a **new** staging directory:

```sh
/usr/bin/python3 tools/ubuntu/stage_phase2a.py \
  --bazel-bin src/bazel-bin --output /path/to/workspace/new-staging
```

This copies build outputs and notices, checks ELF architecture/exports/runtime
search paths, and records hashes/NEEDED libraries in `manifest.json`. It never
executes the outputs and refuses an existing destination. The frontend and
tools use the dedicated profile/IPC namespace, but a complete private-path and
lifetime audit is still required before activation. The raw generated Phase 1
dictionary inputs are reused; the dedicated server embeds the resulting
dataset. No external renderer or AI service is included in this snapshot.

Source provenance and licenses are in `ORIGIN.json`, `UPSTREAM-LICENSE`,
`COPYING.LGPL-2.0` and `COPYING.GPL-2.0`; original notices remain in the files.
Translations are not shipped yet. The gettext domain is `fcitx5-mozkey` and
source strings are the fallback. Future catalog extraction must use this
domain (including `_`/`N_` markers) and install under `share/locale`; catalogs
for `fcitx5-mozc` must not be reused under the old domain.
