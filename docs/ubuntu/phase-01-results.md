# Phase 1 実行結果・Fcitx5統合差分

[共通ルール](../../AGENTS.md) / [計画](../../PLANS.md) / [実行仕様](phase-01-build.md) / [Phase 0](phase-00-audit.md)

## 結論・基点
2026-10-31調査。最新origin/main `4bc953dca52110e4dccf469df6d1c1da707baafe` から `feat/ubuntu-phase1-build` を別worktree `/tmp/mozkey-phase1` に作成。元のadopt_ubuntuのステージ済み5ファイルは保持。
ホストはUbuntu 26.04.1 LTS（resolute）、x86_64で対象OSと一致。ただし **ビルド再現・成果物検証の受入条件は未達**。必要ツール・開発パッケージ・daily辞書入力が不足。調査と限定オフライン検証を実施し、レビュー待ちとする。Phase 2は未承認。

## 環境・確認コマンド
| コマンド | 観測結果 |
|---|---|
| `cat /etc/os-release` / `uname -m` | Ubuntu 26.04.1 LTS / x86_64 |
| `gcc --version` | GCC 15.2.0（Ubuntu 15.2.0-16ubuntu1）。g++も存在 |
| `/usr/bin/python3 --version` / `git --version` | Python 3.14.4 / Git 2.53.0 |
| `command -v bazelisk bazel pkg-config pwsh` | いずれもPATH上に存在しない |
| `dpkg-query -W`（pkg-config、pkgconf、libibus-1.0-dev、qt6-base-dev、libfcitx5core-dev、libfcitx5utils-dev） | 指定パッケージはいずれも登録なし |
| `pkg-config --modversion ibus-1.0 Qt6Core Qt6Gui Qt6Widgets Fcitx5Core Fcitx5Module` | pkg-config自体がなく実行不可。各ライブラリの実版は未確認 |
| `command -v curl unzip readelf file` | /usr/binに存在 |
| `df -h /tmp` / `free -h` | /tmpは7.4GiB tmpfs、空き約5.0GiB。RAM約14GiB、available約6.9GiB。大規模生成・ビルド置場に不向き |

PATH先頭のpython/python3はMiniconda配下なので、今回の限定テストは明示的に/usr/bin/python3を使用した。sudo、システムインストール、IME起動、設定・辞書の読取りや変更は行っていない。Hazkeyの実版は調査していない。

## 実行記録と成功・失敗
以下はリポジトリルート（Bazelのみsrc）で実行したコマンド。表は終了結果を転記したもので、個人情報を含む生ログはコミットしない。

| コマンド | 結果 |
|---|---|
| `bazelisk build package --config release_build --jobs=2` | 終了127、command not found: bazelisk。コンパイラ・リンカには到達していない |
| `bazelisk test //unix/ibus:ibus_mozc_test --jobs=2 --test_output=errors` | 終了127、同上。IBus参考テスト未実行であり、Fcitx5検証ではない |
| `pwsh -NoProfile -File tools/dictionary/prepare_daily_dictionary.ps1 -SkipDownload` | 終了127、command not found: pwsh。辞書生成は開始していない |
| `/usr/bin/python3 tools/dictionary/pin_dictionary_inputs.py --lock tools/dictionary/locks/daily_sources.json validate` | 終了0、LOCK_SCHEMA=PASS |
| `/usr/bin/python3 -B -m unittest discover -s tools/dictionary/tests -p test_pin_dictionary_inputs.py -v` | 終了0、6件成功。固定revision、latest回避、キャッシュ整合性、daily入力明示等のオフライン回帰検証 |
| `test -f src/bazel-bin/unix/mozc.zip` | 終了1、成果物なし。zip整合性・ハッシュ・ELF検査は未実施 |
| `git diff --exit-code -- src` | 成功、ソース変更なし |

辞書テストの成功は辞書生成成功でも通常ビルド成功でもない。全件Bazelテスト、GUI実行、Fcitx5読み込み、Zenz/llama-server実行は行っていない。CI定義は確認したが、対象SHAのCI run成功・artifactは取得確認していない。

## 辞書・通常依存と再開条件
`src/.bazeliskrc` はBazel 9.0.2。Bazelisk本体の版は未選定・未導入。IBus/Qt6開発依存も不足し、ツール単体取得ではビルドを進められないため、今回は通常依存のバイナリ取得も行っていない。

**現行実装の事実**：`src/data/dictionary_oss/BUILD.bazel` のbase_dictionary_dataは `//data/dictionary_koyasi:mozcdic_ut_daily_local` を直接参照する。sampleファイルが存在しても自動フォールバックしない。`src/data/dictionary_koyasi/BUILD.bazel` のdaily入力4点（nico/pixiv delta、syntax guard、merge-ut daily、人名daily）はworktreeに存在しない。sampleへの切替は無断で行わない。

`tools/dictionary/prepare_daily_dictionary.ps1` → import/profile/convert/check/use_profileがCIと同じ経路。SkipDownloadでも既存入力が必要。入力来歴は `tools/dictionary/locks/daily_sources.json` とimport_merge_ut.ps1で確認した。

| 入力 | 固定値・規模 |
|---|---|
| merge-ut | utuhiro78/merge-ut-dictionaries、640844881690e877d0e54dff024edb8ec7b77290 |
| 子辞書 | place-names 98cf8e9d7731695a52e5bc72ed633933c71d1913、sudachidict 13129ddebe1cb8eabb63feccbe157adec4e1de01 |
| Wikipedia | dumps.wikimedia.org、20261001、圧縮31,437,876 bytes |
| nico/pixiv | Niccori250k/dic-nico-intersection-pixiv、4d0d8d219d0b9abf34745016e398e762182fce61、7,659,574 bytes |
| 人名 | utuhiro78/mozcdic-ut-personal-names、b557dd4c1548cc7c2164d5f8b14170c40cbb6f81、圧縮729,168 bytes |

明示された3入力だけで39,826,618 bytes。Git取得、展開・加工・出力、Bazel外部依存・キャッシュの容量は別で総量未確定。SHA-256はlock参照。ライセンス・総量確認と追加承認前に辞書取得しない。

再開案：まずBazelisk公式配布版と検証ハッシュ、Bazel 9.0.2、pkg-config、IBus/Qt6開発ファイル、PowerShellの作業領域への導入・依存解決方法を確定する。システム導入が必要ならパッケージ一覧・変更範囲を提案してsudoの承認を得る。代案はUbuntu 26.04の隔離ビルド環境に依存を用意する方式（イメージ版/digestと容量を事前確認）。このホスト自体は26.04なので別OSの成功に読み替える問題はない。
辞書取得承認と入力準備後に、仕様のdaily生成、srcで通常ビルドと限定テストを再実行する。作業領域はtmpfs外の容量確保を優先。成果物ができたら `unzip -t`、一覧、SHA-256、file/readelfによるELF x86_64・NEEDED・インタプリタ照合を行う。GUI/IMEは起動しない。GCC 15でSFrame関連障害が出た場合は `.bazelrc` のno_sframe設定を原因と照合して検討するが、今回はそこまで到達していない。

## Fcitx5統合差分（静的調査、未実装）
2026-10-31再照会でもfcitx/mozcの既定fcitxブランチ先端は `384a85cf2f5db982e64c5c33cade66b2a5e5176c`。固定SHAのBUILD、MODULE、engine、response parserを再取得して照合。外部ソースをMozkeyへ配置していない。

| 領域・参照先 | Mozkeyとの相違・Phase 2の作業案 |
|---|---|
| [src/unix/fcitx5](https://github.com/fcitx/mozc/tree/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5) | Mozkeyにはディレクトリなし。engine/factory/state/response_parser、key event/translator、surrounding text、client interface/pool/ipc、i18n wrapper、addon/inputmethod設定を由来・著作権表記と共に選択導入。direct clientは別方式のため採否を分ける |
| [BUILD](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/BUILD.bazel) | fcitx5-mozc.soの共有ライブラリ、use_server既定true。まずIPC方式でMozkey自身のserverを使う案。既存BUILDラベル・client/protocol/辞書依存と照合しLinux限定にする |
| [MODULE](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/MODULE.bazel) | @fcitx5にFcitx5Core/Fcitx5Moduleのpkg-configが必要。Fcitx4宣言や他の依存差まで丸ごと取り込まない。Windows/macOSでFcitxを要求しないOS制約が必要 |
| 開発依存 | libfcitx5core-dev/libfcitx5utils-dev等の提供ファイル・推移依存・最小API/ABIを26.04で確認する。今回は未導入で実版未確認。Qtは通常GUIビルド依存とFcitxコア依存を区別する |
| [応答処理](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_response_parser.cc) | ExecuteCallbackはUNDO/CONVERT_REVERSE以外return。MozkeyのAPPLY_LIVE_CONVERSION、遅延コマンド列、世代フィールドとdelay_millisecに対応するタイマー処理が必要。再帰無制限実行を避け、focus変更・破棄・古い応答を取消する設計が要る |
| [engine](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_engine.cc) | InputContextごとの状態・候補UI更新、設定tool呼出し、アイコン名をMozkeyへ適合。候補ID・preedit・入力欄寿命・pool共有との関係を検証する |
| 配布 | 通常src/unix/BUILD.bazelのzipはIBus/server/tool/renderer等でFcitx5 addonを含まない。独立した.so、addon/inputmethod設定、アイコン・翻訳、server/toolと辞書データを成果物一覧へ追加する必要。翻訳・アイコンの具体的生成/導入経路は追加確認が必要 |

## 衝突防止・IPC・Hazkey保護
静的な根拠は `src/config.bzl`（/usr/lib/mozc等）、`src/base/system_util.cc`（既存~/.mozc、XDG_CONFIG_HOME/mozc、~/.config/mozc）、`src/ipc/ipc_path_manager.cc`（プロファイル内の隠しname.ipc、/tmp/.mozc.接頭辞）、`src/zenz/zenz_unix_socket_path.cc`（UID別Zenz socket）。アドオン名変更だけでIPC/データの独立を保証できない。
Fcitx側のLibrary=fcitx5-mozc、Addon=mozc、設定・アイコン名を含め、独立Mozkey用ID/共有ライブラリ/設定/実行ファイル/データ/IPCへ統一する差分が必要。Hazkeyの実際のパス・実装との一致は未調査なので衝突があるともないとも断定しない。Mozc未導入でも衝突確認を省略しない。個人辞書・設定を収集せず、必要な実パス調査は後続承認範囲で行う。

## PR #231の分担
[PR #231](https://github.com/koyasi777/mozkey/pull/231) は再確認時open・未マージ、head `f0867583f00f70f6e21af76219477106e8f54f66`。Linuxの環境変数・実行パス、Zenz Unix接続/scorer起動を扱う。Fcitx5導入は含まず、作者の別NixOS構成の実績はUbuntuでの成功証拠ではない。
共通POSIX置換・client factoryはPRと重複するため独自に再実装しない。Phase 2はFcitx5フロントエンド・イベント処理・独立IMEのID/配置・Hazkey共存設計を中心とし、Phase 3でPRの再比較とscorer BUILD制約・Zenz配布配置を扱う。通常ライブ変換とAI補正を混同しない。

## 受入状況と引き継ぎ
- 環境・基点・辞書来歴・障害・Fcitx5差分：記録済み。
- 通常ビルド、Bazel限定テスト、zip/ELF検査：未達。依存・辞書準備後にPhase 1を再検証する。
- 個人IME・システム設定・ソース：変更なし。成果物・生成辞書・生ログはコミットしない。
- Phase 2の初期試験は分離VM推奨で、ロード・基本入力・変換/ライブ変換・候補・確定/取消・入力欄切替・遅延応答・Fcitx5安定性を確認する。無クラッシュだけでは合格にしない。
- 実ホスト追加は初期試験成功、ID/IPC/データ衝突防止、導入・無効化・撤去・復旧手順確認、明示承認を条件とする。Hazkeyを残し、設定・個人辞書・入力環境を保護する。

レビュー依頼：不足開発依存を作業領域で用意するかシステム導入を承認するか、daily辞書取得の範囲・総容量、tmpfs外のビルド置場を確定する必要がある。Phase 1は完了扱いせず、検証阻害・レビュー待ちで停止する。

## 再開準備の追記
調査結果は承認済み。通常ビルドは未達のためPhase 1を継続し、[依存導入・取得の具体案](phase-01-preparation.md)を作成した。インストール・外部本体取得はユーザーの追加承認待ち。
