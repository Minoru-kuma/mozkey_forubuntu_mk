# Phase 0 リポジトリ調査

[ルール](../../AGENTS.md) / [計画](../../PLANS.md) / [Phase 1案](phase-01-build.md)

## 調査基点
- 調査日：2026-10-30（JST、15:13以降）。originをfetch済み。
- 対象：origin/main `e5ffcb070a5dbf1a2b1cc89e5377e651441a977a`。
- 開始時：adopt_ubuntu / `dfc744b1e`、origin/mainより517コミット後方。originはgit@github.com:Minoru-kuma/mozkey_forubuntu_mk.git。upstreamリモート登録なし。
- 元作業ツリーのステージ済み変更：`.agents/AGENTS.md`、`.agents/agents/architect.md`、`.agents/agents/engineer.md`、`src/MODULE.bazel`、`src/unix/BUILD.bazel`。変更せず保持した。
- 最新origin/mainから別worktree `/tmp/mozkey-phase0` に `docs/ubuntu-harness-phase0` を作成。ローカルmainへの取り込みはしていない。
- 第一対象はユーザーの追加指定によりUbuntu 26系 x86_64 + IBus。正確な版は未確定。CIの24.04と混同しない。

## 静的に確認した事実
| 領域 | 状況と根拠 |
|---|---|
| 対応宣言 | `README.md` はLinux未検証。upstream Mozc対応とfork独自機能の検証を区別。macOSは実機検証済みとの記載 |
| 貢献・ライセンス | `CONTRIBUTING.md` はGoogle CLAとGoogle Style Guideを案内。forkへの提出時の運用は別途確認。`LICENSE` はBSD系条件と辞書由来の追加表記を含む。独自辞書・モデルの再配布条件は一括で推定しない |
| Linuxビルド | `docs/build_mozc_for_linux.md` はBazeliskとIBus・Qt6依存を説明。upstreamの案内なので独自辞書前処理を補う必要がある |
| ツールチェーン | `src/.bazeliskrc` はBazel 9.0.2とwrapperを指定。`src/.bazelrc` はLinux/GCC系C++20、nolinux除外、release_build設定を持つ |
| 依存 | `src/MODULE.bazel` はAbseil 20260107.1、protobuf 34.1、rules_python 1.9.0等を指定。pkg-configによるIBus/Qt6検出、外部辞書・アイコン取得がある |
| IBus・成果物 | `src/unix/ibus/` にキー、preedit、候補、周辺テキスト、エンジンとテストが存在。`src/unix/BUILD.bazel` のpackageはserver/tool/renderer/ibus/emacs/XML/アイコンをzip化。Zenz scorerは同梱リストにない |
| ライブ変換 | `src/session/session.cc` に遅延ライブ変換と設定処理が存在。ただしセッション層の存在だけではIBus動作成功を示さない |
| IBusコールバック | `src/unix/ibus/mozc_engine.cc` のcallback処理はUNDO/CONVERT_REVERSEを扱い、他はfalse。遅延ライブ変換・Zenz更新を受け渡す経路の適合性確認が必要 |
| Zenz接続 | `src/session/zenz_client_factory.cc` はmacOSでUnix socket、他OSではnamed pipeクライアントを選択。LinuxへのUnix接続経路切替が未実装 |
| 共通IPC | `src/zenz/zenz_wire_protocol.h` と `zenz_unix_socket_path.cc` が存在。非WindowsではUID別の/tmp配下socketパスを組み立てる。Linux実機での権限・ライフサイクル検証は未実施 |
| scorerのOS依存 | `src/zenz_scorer/main_posix.cc` にmach-o/dyld.hと_NSGetExecutablePath、`llama_server_process.cc` にcrt_externs.hと_NSGetEnvironがある |
| scorerビルド制約 | `src/zenz_scorer/BUILD.bazel` のPOSIXライブラリ・テストはmacOS制約、実行ターゲットもmacOS/Windows以外incompatible。OS API置換のみではLinux有効化は完了しない |

## 辞書とCI
`.github/workflows/linux.yaml` はpushでUbuntu 24.04上の辞書生成、build、testを定義する。辞書はPowerShellの `tools/dictionary/prepare_daily_dictionary.ps1` で生成し、artifactを後続ジョブへ渡す。buildはsrcで `bazelisk build package --config release_build`、testは `bazelisk test ... -c dbg`。今回これらは実行していない。

辞書スクリプトはmerge-ut、nico/pixiv、人名の取得、dailyプロファイル・syntax guard生成、active profile切替を行う。`src/data/dictionary_koyasi/BUILD.bazel` はgenerated/profiled配下のdaily、人名、delta、syntax guard等を参照する。`-SkipDownload` は既存入力を用意する必要があり、空のcheckoutで成功する保証ではない。Python呼出しが `python` である点、PowerShell/Bashの可用性、取得サイズ・ライセンス・生成の再現性を次Phaseで確認する。

CI定義の存在は確認済み。対象SHAのGitHub Actions実行結果・ログ・artifactの成功は未確認。READMEのLinuxビルド案内にもupstream CIリンクがあるため、forkの成功証拠と混同しない。Ubuntu 26系のビルド・実機動作は未確認。

## PR #231との関係
[Linux対応 PR #231](https://github.com/koyasi777/mozkey/pull/231) をGitHub接続ツールで本文・差分・コメントまで確認。調査時点open、未マージ。headは `f0867583f00f70f6e21af76219477106e8f54f66`、baseは `ea2e9f5cfb80fccf50caca1af097b2f0830be52a`。4コミット、6ファイルの変更。状態は再着手時に再確認する。

PRはLinuxの環境変数・実行ファイルパス取得、Zenz Unix socket接続とscorer起動を扱う。本文の旧関数名だけに依存せず差分を確認した。作者は別途Fcitx5を追加したNixOS 26.05構成で動作を確認したと報告している。これはPR単体のUbuntu + IBus検証ではない。コメントでは入力フレームワーク共通部分として分離した意図が説明されている。

重複を避ける領域はPOSIX API置換とZenz client factory。独立して貢献しやすい領域はUbuntu 26系の固定ビルド手順・辞書再現、IBus遅延コールバックと表示・入力の実証、Linux成果物配置、既存Mozcとの競合回避。現行mainのscorer BUILD制約も含めて再比較する必要があり、PRをそのまま取り込めば完成するとは判断しない。今回PRの取得・適用・作成はしていない。

## 推測・未解決事項
- IBusの遅延コールバック処理不足がライブ変換やZenz反映を妨げる可能性がある。必要修正と再現ケースは未確定。
- Ubuntu 26系のGCC/Qt/IBus差による障害は未検証。24.04 CI依存パッケージの単純転用を成功保証にしない。
- 通常packageからscorerが除外されるため、通常Linuxビルド成功でもZenz対応を意味しない。
- 実行ファイル・データ・socket・ユーザー設定の配置、既存Mozcと同じ識別子の競合、Wayland等の入力環境差は後続検証が必要。

## 推奨順序・今回の検証
通常ビルドと辞書再現 → 成果物の静的検査 → 隔離したIBus入力・遅延処理 → PR再比較とZenz対応 → 配布・CI整備。Fcitx5は独立の将来検討とする。
今回の検証はソース・設定・PRの読取りと文書整合性・差分検査に限定。ビルド、テストバイナリ実行、辞書・モデル取得、Zenz起動、IME設定変更、システム導入は未実施。Git反映と元作業ツリー保持の最終確認結果は完了報告に記録する。
