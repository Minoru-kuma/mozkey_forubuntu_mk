# Phase 2A Fcitx5 staging sources

This is an incomplete opt-in Linux build. **Do not install or register it.**
The addon definition has `Enabled=False` and the V2 factory always returns
`nullptr`. Editing the configuration does not bypass this gate.

The frontend now uses IO/Timer/Defer-driven RPC and a per-context asynchronous
session. Profile/key/peer and dedicated configuration I/O run on the single
owned preparation worker. Physical keys are recorded before consumption;
late/failing replies suspend delivery and retain input in process memory.
This is still **not input-safe enough to activate**: process-exit recovery,
actual frontend key release/repetition, session cleanup/server startup and
worker/runtime shutdown remain unverified. The recovery menu only inspects
retained data; it does not replay uncertain effects. Server-response launches,
GUI tool menu launching and clipboard fallback are unavailable. See the current
results in `docs/ubuntu/phase-02-results.md` and `a5-async-design.md`.

Build explicitly from `src/`, using the existing Phase 1 toolchain/cache and
an isolated build HOME/XDG environment as recorded in the Phase 2 results:

```sh
bazelisk build //unix/fcitx5:stage_inputs --//:linux_mozkey=true \
  --config=release_build --jobs=2 --repository_disable_download
bazelisk test //unix/fcitx5:callback_queue_test \
  //unix/fcitx5:connection_preparation_test \
  //unix/fcitx5:frontend_boundary_test //unix/fcitx5:async_session_test \
  //unix/fcitx5:input_pipeline_test //unix/fcitx5:async_config_test \
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
