# Phase 2A Fcitx5 staging sources

This is an incomplete opt-in Linux build. **Do not install or register it.**
The addon definition has `Enabled=False` and the V2 factory always returns
`nullptr`. Editing the configuration does not bypass this gate.

The frontend now uses IO/Timer/Defer-driven RPC and a per-context asynchronous
session. Profile/key/peer and dedicated configuration I/O run on the single
owned preparation worker. Physical keys are recorded before consumption;
late/failing replies suspend delivery and retain input in process memory.
This is still **not input-safe enough to activate**. Bounded normal shutdown now
seals admission, attempts a durable inspection snapshot and joins the native
worker including TLS destruction. A timeout is a failure, never proof of cleanup.
Compose prefixes and validated late responses are retained. Per-context clients
retire known sessions through an addon-owned DELETE_SESSION queue; owner teardown
cancels that queue without waiting. Empty contexts can prepare a fresh session on
focus return. Rejected reconversion never deletes the selection; deletions require
an exact surrounding snapshot and frontend capability.

Private snapshots are bounded to 1MiB, stored in `mozkey-recovery/` under the
Fcitx PkgConfig directory (directory 0700, files 0600). They contain input/context
in plaintext and are **not logs or encrypted storage**. Unique snapshots are never
overwritten. A separate `latest` pointer supports explicit inspection after
restart. Inspection never replays requests, keys or uncertain commits. A full
archive browser, explicit resolution and bounded retention policy remain pending.
Filesystem failure, shutdown budget expiry and snapshot-size refusal preserve
memory while alive but do not guarantee process-exit recovery; activation remains
blocked. Real frontend/IME/server startup and all six full CI suites are unverified.
See `docs/ubuntu/phase-02-results.md` and `a5-async-design.md`.

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
