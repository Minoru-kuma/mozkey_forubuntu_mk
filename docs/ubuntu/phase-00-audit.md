# Phase 0 リポジトリ調査

[ルール](../../AGENTS.md) / [計画](../../PLANS.md) / [Phase 1案](phase-01-build.md)

## 調査基点
- 調査日：2026-10-30（JST、15:13以降）。originをfetch済み。
- 対象：origin/main `e5ffcb070a5dbf1a2b1cc89e5377e651441a977a`。
- 開始時：adopt_ubuntu / `dfc744b1e`、origin/mainより517コミット後方。originはgit@github.com:Minoru-kuma/mozkey_forubuntu_mk.git。upstreamリモート登録なし。
- 元作業ツリーのステージ済み変更：`.agents/AGENTS.md`、`.agents/agents/architect.md`、`.agents/agents/engineer.md`、`src/MODULE.bazel`、`src/unix/BUILD.bazel`。変更せず保持した。
- 最新origin/mainから別worktree `/tmp/mozkey-phase0` に `docs/ubuntu-harness-phase0` を作成。ローカルmainへの取り込みはしていない。
- 第一対象はユーザーの追加指定によりUbuntu 26.04 LTS（x86_64）+ Fcitx5に確定。IBusは参考・比較対象で受入条件外。CIの24.04と混同しない。

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

CI定義の存在は確認済み。対象SHAのGitHub Actions実行結果・ログ・artifactの成功は未確認。READMEのLinuxビルド案内にもupstream CIリンクがあるため、forkの成功証拠と混同しない。Ubuntu 26.04 LTSのビルド・実機動作は未確認。

## PR #231との関係
[Linux対応 PR #231](https://github.com/koyasi777/mozkey/pull/231) をGitHub接続ツールで本文・差分・コメントまで確認。調査時点open、未マージ。headは `f0867583f00f70f6e21af76219477106e8f54f66`、baseは `ea2e9f5cfb80fccf50caca1af097b2f0830be52a`。4コミット、6ファイルの変更。状態は再着手時に再確認する。

PRはLinuxの環境変数・実行ファイルパス取得、Zenz Unix socket接続とscorer起動を扱う。本文の旧関数名だけに依存せず差分を確認した。作者は別途Fcitx5を追加したNixOS 26.05構成で動作を確認したと報告している。これはPR単体のUbuntu 26.04 + Fcitx5検証ではない。コメントでは入力フレームワーク共通部分として分離した意図が説明されている。

重複を避ける領域はPOSIX API置換とZenz client factory。独立して貢献しやすい領域はUbuntu 26.04 LTSの固定ビルド手順・辞書再現、Fcitx5統合と遅延コールバック・表示・入力の実証、Linux成果物配置、既存Mozcとの競合回避。現行mainのscorer BUILD制約も含めて再比較する必要があり、PRをそのまま取り込めば完成するとは判断しない。PRの本文・差分は取得したが、コードへの適用やPR作成はしていない。

## 推測・未解決事項
- IBusの遅延コールバック処理不足がライブ変換やZenz反映を妨げる可能性がある。必要修正と再現ケースは未確定。
- Ubuntu 26.04 LTSのGCC/Qt/Fcitx5差による障害は未検証。24.04 CI依存パッケージの単純転用を成功保証にしない。
- 通常packageからscorerが除外されるため、通常Linuxビルド成功でもZenz対応を意味しない。
- 実行ファイル・データ・socket・ユーザー設定の配置、既存Mozcと同じ識別子の競合、Wayland等の入力環境差は後続検証が必要。

## 推奨順序・今回の検証
通常ビルドと辞書再現・Fcitx5差分調査 → 成果物の静的検査 → 承認後のFcitx5最小統合と隔離VMでの入力・ライブ変換・候補表示 → PR再比較とZenz対応 → 配布・CI整備。IBus検証は任意の比較資料に限定する。
今回の検証はソース・設定・PRの読取りと文書整合性・差分検査に限定。ビルド、テストバイナリ実行、辞書・モデル取得、Zenz起動、IME設定変更、システム導入は未実施。Git反映と元作業ツリー保持の最終確認結果は完了報告に記録する。

## Fcitx5への対象変更と追加調査（2026-10-30）
追加調査開始時の文書HEADは `ba2fcb8027407067607c07670b15276e2393d651`。origin再fetch後もmainは上記SHAで、文書ブランチは1コミット先行・後方0。PR #231は再照会でもopen・未マージ、head SHAは前記と同一。ユーザーが既にUbuntuでFcitx5を日常利用していることを前提とする。

### 確認済みの外部実装
`fcitx/mozc` の既定ブランチ `fcitx`、調査固定SHAは `384a85cf2f5db982e64c5c33cade66b2a5e5176c`（2026-10-28コミット）。以下の参照はこのSHAに固定した。Mozkeyの調査基点には `src/unix/fcitx5/` とMODULEのFcitx5依存宣言がない。

| 根拠 | 確認した内容 |
|---|---|
| [Fcitx5 BUILD](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/BUILD.bazel) | `//unix/fcitx5:fcitx5-mozc.so` は共有ライブラリ。`use_server`既定trueでIPC client、falseでdirect clientを選ぶ |
| [MODULE](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/MODULE.bazel) | pkg-configの`Fcitx5Core`、`Fcitx5Module`から`@fcitx5`を定義。Fcitx4用の別宣言もあるため丸ごと移植しない |
| [状態処理](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_state.cc) | キー送信、preedit、候補操作、focus out時のsubmit/revert、Fcitx UI更新を実装 |
| [応答処理](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_response_parser.cc) | ExecuteCallbackはUNDO/CONVERT_REVERSEのみ処理し、それ以外はreturn。delay_millisecに基づく遅延実行をこの処理では行わない |
| [client pool](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_client_pool.cc) | 入力コンテキスト・プログラム・全体のポリシーに応じたclient共有。遅延応答の宛先と寿命の検証が必要 |
| [addon設定](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc-addon.conf) / [入力方式設定](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc.conf) | Library=fcitx5-mozc、Addon=mozc。既存Mozcアドオンとの名前・配置競合がある |
| [通常package](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/BUILD.bazel) | このforkでも通常packageはIBus系で、Fcitx5共有ライブラリを含まない。通常zip成功はFcitx5ビルド成功ではない |

### 統合方式案と互換性課題（未実装・未検証）
第一案は固定SHAの `src/unix/fcitx5/` をライセンス表記・由来と共に選択移植し、Mozkeyの現行client/protocol/sessionに接続する方式。リポジトリ全体のマージやMODULE丸ごとの置換は避け、必要なBazel依存宣言・Linux限定のターゲット・設定/アイコン/翻訳等の配置を差分として列挙する。初期評価は既定のIPC方式でMozkey自身のserverを使う。direct方式はエンジン同居による寿命・依存の違いがあるため別評価とし、採否はPhase 1レビューで決める。

両者の `.bazeliskrc` はBazel 9.0.2だが、互換性を保証しない。BUILDラベル、client API、生成protobuf、データマネージャ、辞書選択、Fcitx5ヘッダー/API・共有ライブラリABIを照合する。`src/protocol/commands.proto` のMozkey固有コマンド・callback遅延・世代情報を失わず渡す必要がある。現状のFcitx側callback分岐では独自コマンドが無視されるため、ライブ変換を単純コピーで保証できない。タイマー取消、focus変更、入力欄破棄、古い応答破棄、候補更新をPhase 2の試験対象にする。

PR #231は入力フレームワーク共通のZenz/POSIX対応で、Fcitx5フロントエンドの導入を含まない。作者の別構成によるNixOS実績はUbuntu 26.04上のMozkey統合証拠ではない。PRのAPI置換を重複実装せず、Fcitx5フロントエンド導入・遅延イベント対応・Ubuntuでの再現を独立貢献候補とする。ZenzのBUILD制約・runtime配置は引き続き別課題。

### 隔離・検証の境界
Phase 1は通常Linuxビルドと統合調査まで。Fcitx5ソース導入は未承認で、後続Phaseの範囲承認が必要。Phase 2の実入力はUbuntu 26.04の使い捨てVMを必須とし、ホストのHOME/XDG・D-Bus・Wayland/X11 socket・Mozc IPCを共有しない。ホストのFcitx5停止、replace起動、アドオン上書き、設定同期は行わない。独立した試験用プロファイルと合成入力を使用する。
静的調査のみ実施済み。Fcitx5のコンパイル、ロード、実入力、ライブ変換、候補表示、Ubuntuパッケージの版・最小依存は未検証。IBusテストは参考検証であり、Fcitx5受入の代用ではない。
