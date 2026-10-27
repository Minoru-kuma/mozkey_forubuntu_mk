# Windows Zenz Runtime Contract

## Source identity

- llama.cpp repository: https://github.com/ggml-org/llama.cpp
- tag: `v0.7.0`
- commit: `620d2225cc436ab0b001e3200e590e3b286ef9ab`
- tokenizer compatibility patch SHA256: `6DB5C11B2DA8415D6B37200EC6AA4F3FDCDDE8EFB1E7A71375FE331A7B0E829A`
- patched `src/llama-vocab.cpp` SHA256: `2CEF3BDD758D9F2EB4CB80C1FC149C75461E9E32AF06E03D67D99E36FF69BBED`

## Model identity

- file: `models/zenz-v3.2-small-Q5_K_M.gguf`
- SHA256: `29C223D4C23327B80FD13EBB5AB2555057A46317997D5DA391584FFBEF0DB673`

## Runtime assets

### x64

- source-tree file: `x64/llama-server.exe`
- PE: x64 / 0x8664
- SHA256: `35B953C97F870CBD7426DC865783BE36090F841C273AE779BD098E9F080F3735`
- v0.7.0 x64: 119 cases x 4 paired rounds versus the tracked b10437 binary (not the historical security-backport build); 118/119 stable identical outputs; 116/119 exact reference matches for each build; mean latency 62.69 to 53.74 ms (-14.28%). Human review of one differing case and installed-IME testing remain pending.

### ARM64

- source-tree file: `arm64/llama-server.exe`
- PE: ARM64 / 0xAA64
- SHA256: `6BDE0FAF466261C8233298ECB2BDF01A78AAEBE9729C03BA4EA95F045F772737`
- build basis: GitHub Actions `windows-11-vs2026-arm`, ClangCL, `GGML_NATIVE=OFF`, `GGML_CPU_ARM_ARCH=armv8-a`
- ARM64 portable CI: native build, PE import audit, model load, authorization and 6/6 semantic smoke PASS (one runner)
- historical W1 candidate native GitHub ARM64 gate: PASS
  - this validated the exact ARM64 runtime candidate before MSI integration
  - workflow run: https://github.com/koyasi777/mozkey/actions/runs/31893874478
  - workflow commit: `daa13a4df3c4b2ce4da5a51dcb2a29aa3f7f248f`
- historical b10437 final-MSI native validation: PASS; v0.7.0 installed-MSI gate PENDING
  - CI job: `Native ARM64 installed MSI Zenz audit`
  - the exact same-run ARM64 MSI is installed on native `windows-11-arm`
  - installed `llama-server.exe` is ARM64 and matches the fixed runtime SHA256
  - installed `mozc_zenz_scorer.exe` is ARM64
  - the installed model matches the fixed model SHA256
  - native `llama-server --version`, the canonical 12-case semantic gate, and the scorer named-pipe context gate pass from installed files

## v0.7.0 candidate provenance and outstanding gates

- Windows x64 **rebuilt** isolated MSVC candidate SHA256: `35B953C97F870CBD7426DC865783BE36090F841C273AE779BD098E9F080F3735` (the original `E1E0ADE4...` candidate is unavailable; no claim of identical binary).
- Windows x64 source patches: tracked tokenizer compatibility patch and tracked dual-liveness patch (`SHA256 3267E978F61456C32DFB83B15BF0F3A75393C5F98C946EE7A2846F7121B36DDA`).
- Windows x64 quality/performance: new SHA tested with 119 cases x 4 rounds, 118/119 stable identical, 116/119 reference exact on both, mean latency -14.28%; differing case requires human review. Baseline was tracked `b10437`, **not** the historical security-backport executable.
- Windows ARM64 source: pinned upstream v0.7.0 plus tokenizer patch; no dual-liveness patch was applied to the ARM64 candidate.
- Windows ARM64 portable native CI: https://github.com/koyasi777/mozkey/actions/runs/40533764179 .
- Portable ARM64 candidate SHA256: `6BDE0FAF466261C8233298ECB2BDF01A78AAEBE9729C03BA4EA95F045F772737`.
- ARM64 PE normal and delay imports: audited; external dependencies are Windows OS/UCRT and Microsoft Visual C++ runtime DLLs.
- Model SHA256 and Zenz tokenizer patch SHA256 are unchanged.
- Source includes the verified upstream chat-PEG parser security fix.
- Tested on one Windows ARM64 CI runner. Portability across all ARM64 CPUs remains unverified.
- v0.7.0 MSI build, installed scorer-to-server test, firewall, normal upgrade, and uninstall audits are PENDING until the integration CI completes.
- Historical native ARM64 installed-MSI results above describe the prior runtime and must not be treated as evidence of a v0.7.0 installed MSI.

## Build/runtime policy

- CPU only
- `BUILD_SHARED_LIBS=OFF`
- `GGML_OPENMP=OFF`
- `GGML_NATIVE=OFF`
- `GGML_BLAS=OFF`
- `GGML_CPU_KLEIDIAI=OFF`
- `LLAMA_BUILD_UI=OFF`
- `LLAMA_OPENSSL=OFF`
- scorer launches `llama-server` with `--parallel 1`
- server binds to `127.0.0.1`
- scorer supplies a generated API key
- the Windows installer installs enabled outbound/block firewall rules for `mozc_zenz_scorer.exe` and `llama-server.exe`, in addition to the existing Mozc runtime rules
- the firewall rules must not break scorer-to-llama loopback inference and must be removed during uninstall
- no `ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`, or `llama.dll` is packaged
- `MSVCP140.dll` / `VCRUNTIME140*.dll` remain dynamic dependencies and are supplied through the existing architecture-specific MSI CRT payload.

## MSI architecture contract

- x64 MSI: x64 Mozkey + x64 scorer + x64 llama-server + x64 CRT
- ARM64 MSI: ARM64 Mozkey + ARM64 scorer + ARM64 llama-server + ARM64 CRT
- the Zenz model is architecture-independent and has one fixed SHA256.
- universal MSI is not treated as a dual-native whole-product package in this phase.

## Installed security gate

The final ARM64 MSI is also validated on native `windows-11-arm` with the
installed payload, not source-tree binaries:

- the seven Mozc outbound/block Windows Firewall rules are installed and point
  to the actual installed executables
- the Zenz scorer-to-llama path continues to pass with the firewall rules enabled
- scorer-owned `llama-server.exe` listens on loopback only
- scorer / llama have no non-loopback TCP connection during the audit
- uninstall removes the Zenz payload and the Mozc firewall rules

## Installer lifecycle gate

The native ARM64 lifecycle audit rebuilds the W2-predecessor package from the
audited baseline commit `c986db2c611c7255ba92b1d44fc6ac3fb6b098bf` and verifies
a normal MSI lifecycle on native `windows-11-arm`:

- the predecessor contains the historical x64 `llama-server.exe` and the four
  legacy ggml / llama DLLs
- installing the current MSI over that predecessor replaces the ProductCode
  while preserving the Mozc UpgradeCode
- the upgraded payload contains the native ARM64 llama/scorer runtime
- the four legacy runtime DLLs are removed
- the post-upgrade scorer context E2E gate passes
- a second install of the exact current MSI does not duplicate firewall rules
- final uninstall removes product registration, Zenz payload, and firewall rules
- forced `REINSTALL=ALL REINSTALLMODE=amus` is not part of the normal lifecycle
  contract

Development CI artifacts can have the same ProductVersion while validating the
upgrade mechanics. A published release must increase ProductVersion relative to
the previous published release, use a new ProductCode, and preserve the Mozc
UpgradeCode.

## Current install-directory compatibility note

The current WiX layout keeps `MozcDir` under `ProgramFilesFolder\Mozc` for
compatibility with the existing product layout. On 64-bit Windows this can
resolve to `C:\Program Files (x86)\Mozc`. Runtime architecture is therefore
verified from PE identity and hashes, not inferred from the directory name.

Changing this directory contract to `ProgramFiles64Folder` is outside this
runtime-architecture phase because it affects installer compatibility and must
be handled with its own upgrade/lifecycle audit.
