# Phase 1 実行結果・Fcitx5統合差分

**最新状態（2026-10-31）：Ubuntu 26.04.1 x86_64で通常packageビルド・限定テスト・zip/ELF検査に成功。受入検証済み、レビュー待ち。過去の未達記録は経緯として保持する。Fcitx5実入力・Phase 2は未実施。**

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

## 条件付き承認A〜D後の実行（2026-10-31）
今回の開始SHAは9ee6f676171bcbc9e800dde9a7b71a3afce67c3e。A〜Dはユーザーが予算と安全条件付きで承認済み。**通常ビルドの受入条件は引き続き未達**。sudoの対話認証が利用できず、aptは開始できなかった。認証回避や別権限経路は使用していない。

### A：再シミュレーション合格、実導入は未実施
準備案の `apt-get -s --no-install-recommends --no-upgrade --no-remove install pkg-config=2.5.1-4 libibus-1.0-dev=1.5.34~rc2-1 qt6-base-dev=6.10.2+dfsg-7 libsystemd-dev=259.5-0ubuntu3.4` を再実行した。
結果は **新規38件・更新0件・削除0件**。準備案の38件の名前・版をプログラムで照合し完全一致。apt索引更新はしていない。
続いて同じinstallを `sudo -n apt-get ...` で実行したが、終了1・`sudo: interactive authentication is required` でパッケージ取得/導入開始前に停止。-nは認証対話を抑止する指定であり、権限回避ではない。システム変更なし。確認時もsystemd/udev/libsystemd0は259.5-0ubuntu3.4、pkg-config/IBus-dev/Qt6-devは未導入。
再開にはユーザーの対話端末で準備案のsudo操作を行うか、利用可能な認証手段を用意する必要がある。パスワードを会話へ送信しない。再開時も再シミュレーションの一致を条件とし、索引/候補が変わった場合は再評価する。

### B：公式ツール取得・ハッシュ・起動確認成功
準備案の固定URLからBazelisk 1.30.0、Bazel 9.0.2（JDK同梱）、PowerShell 7.6.9 self-containedを取得。3点とも準備案のSHA-256と完全一致してから実行権限を付与。取得合計146,976,754 B。/usr/bin・/optへの配置、シェル設定変更なし。
Bazelはdownloads内の検証済みバイナリを保持し、`USE_BAZEL_VERSION` にその絶対パスを指定してBazeliskから再取得せず実行した。Bazelisk経由の `--output_user_root=... --batch version` は終了0、Bazelisk v1.30.0 / Build label 9.0.2を確認。PowerShellの版確認は終了0、7.6.9。
初回sandbox内のBazel versionは版表示後にNetworkMetricsCollectorのNullPointerExceptionで終了37。同じコマンドをsandbox外で再確認すると終了0となった。ソースや設定フラグの変更は行っていない。
PowerShell初回はデータ保存先が既定の~/.local/share/powershellとなり読取り専用エラー。XDG_DATA_HOMEも作業ディスク内へ指定して正常起動した。ユーザーの既存設定・辞書は変更していない。

### C：daily生成成功
worktreeをtmpfsから `/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase1/checkout` へ移動。git worktree moveはcross-device linkで失敗したため、mvとgit worktree repairで移動を完了。ブランチは同じfeat/ubuntu-phase1-build。
作業プロセスのPATHを専用tools/bin・/usr/bin・/binに限定し、専用binのpythonを/usr/bin/python3へ向けた。TMPDIR、XDG_CACHE_HOME、XDG_CONFIG_HOME、XDG_DATA_HOMEを同じ作業ディスク内に設定。PowerShellはtelemetry/update check無効、NoProfileで実行。
実行コマンド：`pwsh -NoLogo -NoProfile -File tools/dictionary/prepare_daily_dictionary.ps1`。終了0（約75秒）。lock検証、固定Wikipedia indexのハッシュ検証、親・子辞書revision検証、パッチ契約テスト3件、nico/pixiv・人名ハッシュ検証、dailyプロファイルチェック、syntax guard生成を通過。profile設定はdaily。生成手順がBUILD更新と表示したが、追跡ソースの実差分は0。
生成物の来歴・ライセンスは[準備案](phase-01-preparation.md)の固定入力に従う。生成物を再配布・Git登録しない。成果物はローカルのgenerated/profiled配下に保持。

| ファイル | bytes | 行数 | SHA-256 |
|---|---:|---:|---|
| dic-nico-pixiv-delta.txt | 3,118,386 | 49,275 | 67036560c6d82d5b7b3efae4d6af56fe1061babc43a1137b7501b11012969a3c |
| mozcdic-ut-daily.txt | 66,644,125 | 1,082,557 | dca04c1b5a4518bfdda3bbe4b3a20fcafdd9e3449ddb9921f9200427232390b2 |
| mozcdic-ut-personal-names-daily.txt | 2,727,540 | 54,123 | ac72222de099de9bef99c2eb2406927d1e0199f445f832563b5357c64d8cbd86 |
| koyasi-syntax-guard.txt | 21,568 | 537 | 1d29b99c8b4bff45239e6ae2c5b97e43356fe0740a96c758f1020a32a28007fd |

ネット監視は実行前後の全非loopbackインターフェースのrx_bytes差を2秒間隔で確認した。他プロセスの通信も含む保守的な参考値であり、辞書アプリ単体のHTTP payload実測値ではない。248MiB到達、辞書領域が10GiBの128MiB手前、全体59GiBのいずれかで子プロセス群を停止する監視を付けた。監視間隔による超過余地はあるが、今回は境界から十分小さい。
辞書ステージ受信増分53,993,115 B（約51.49MiB、256MiB予算内）、辞書領域監視ピーク230,466,420 B（約219.79MiB、10GiB予算内）。終了時のdist/dictionaryは31,489,778 B、generatedは152,017,111 B。ツール等含む全体は979,387,302 B（約0.91GiB、60GiB予算内）。全て大規模キャッシュ・加工出力はディスク側で、/tmpを使用していない。
監視スクリプト・実行ログ・入力manifestは作業ルートのlogs等にローカル保管し、Gitには含めない。個人入力・Hazkey辞書は取得していない。

### D：通常ビルド・Bazelテスト・zip/ELFは未実施
A未導入のため、今回の通常Bazel依存取得・コンパイルは開始していない。Bazel本体のインストールキャッシュ193,832,469 BはBの起動確認によるもので、通常ビルド成功の証拠ではない。Dの外部依存取得は0 B、成果物zipは未生成。IBus参考テストも未実施。既存Fcitx5/HazkeyへのIME登録・起動・切替・設定変更、Zenz実行、Phase 2着手なし。

### 再開条件・報告
B/Cは成功。Aは承認条件に合致したが対話認証が必要。通常ビルド未達を完了に変更しない。Aが実施可能になった時点で再シミュレーションと導入後差分確認を行い、Dを2GiB取得予算・全体60GiB以内で継続する。条件変更・予算超過見込みは停止して報告する。
今回コミットするのは文書だけ。元adopt_ubuntuのステージ済み変更は保持。mainへマージしない。

## apt導入後のD実行結果（2026-10-31、受入検証済み）

### 基点・apt・再利用
開始HEADは `4c4b9b3c1ca4a121c1b0657a2672c0fb6bdb4098`。originをfetchし専用リモートブランチと一致、mainより3コミット先行・後方0を確認した。製品ソースの基点はmainの `4bc953dca52110e4dccf469df6d1c1da707baafe` のまま。
ユーザーが対話端末でAを導入済み。準備案の全38パッケージについてdpkgの版とinstall ok installed状態が一致した。systemd/udev/libsystemd0は259.5-0ubuntu3.4を維持。今回のエージェント操作ではsudo・apt導入・索引更新を実施していない。

| 検出 | 結果 |
|---|---|
| pkg-config | 2.5.1 |
| glib-2.0 / gobject-2.0 | 2.88.0 / 2.88.0 |
| ibus-1.0 | 1.5.34-rc2 |
| Qt6Core / Qt6Gui / Qt6Widgets | すべて6.10.2 |
| pkg-config --cflags --libs | IBus/GLib・Qt6のinclude/link設定を正常出力 |

Bの3公式配布ファイルと配置済みBazelisk、Cの生成済みdaily4ファイルのSHA-256を再照合し完全一致。再取得・辞書再生成なし。ビルド終了後にもdaily4ファイルのハッシュ一致を確認した。

### 実行環境・再現コマンド
作業ルートをR=`/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase1`、checkout/srcをcwdとする。PATHはR/tools/bin:/usr/bin:/bin、TMPDIR=R/tmp、XDG_CACHE_HOME=R/cache、XDG_CONFIG_HOME=R/config、XDG_DATA_HOME=R/data、BAZELISK_HOME=R/bazelisk-cache。USE_BAZEL_VERSION=R/downloads/bazel-9.0.2-linux-x86_64（ハッシュ検証済み）、ANDROID_NDK_HOMEは当該プロセス内で空にした。ホストの永続設定は変更していない。

```sh
# Rは上記作業ルート。大規模出力・キャッシュは全て同じディスク上
bazelisk --output_user_root="$R/bazel-output" --batch build package \
  --config release_build --jobs=2 \
  --repository_cache="$R/repository-cache" --disk_cache="$R/disk-cache"
bazelisk --output_user_root="$R/bazel-output" --batch test \
  //unix/ibus:ibus_mozc_test //zenz:zenz_wire_protocol_test \
  //zenz:zenz_unix_socket_path_test --config release_build --jobs=2 \
  --test_output=errors --repository_cache="$R/repository-cache" \
  --disk_cache="$R/disk-cache"
```

通常buildは終了0。最初の試行は一時sandboxファイルが容量計測中に消え、監視スクリプトがduの非0終了で停止した。直ちにビルドのプロセス群を終了し、duが出すサイズ値を消滅ファイルの警告と区別する監視へ修正してキャッシュから再開。変更は作業ルート内の監視用スクリプトのみで、製品ソースは無変更。再試行のBazel時間は485.552秒（先行キャッシュあり）、Bazelは2,026工程を完了した。完全なキャッシュなし所要時間は今回未測定。
QtのC locale→C.UTF-8自動切替、辞書生成の語形警告、prediction属性のdeprecated警告があったがエラーではなく、回避のソース修正・システム設定変更は行っていない。

### 限定テスト
| ターゲット | ケース数 | 結果・範囲 |
|---|---:|---|
| //unix/ibus:ibus_mozc_test | 22 | 全成功、失敗/エラー/disabled 0。IBus参考検証でFcitx5受入の代用ではない |
| //zenz:zenz_wire_protocol_test | 3 | 全成功。通信形式の単体検証 |
| //zenz:zenz_unix_socket_path_test | 3 | 全成功。パス生成の単体検証 |

Bazel testは終了0、3ターゲット・計28ケース成功。Bazel時間449.556秒、IBusテスト実行約40.1秒。Zenzの2テストはIPC utilityの単体テストであり、scorer/llama-serverの起動やAIモデル実行はしない。全件テストやIMEセッションでの動作試験は行っていない。

### zip・ELF静的検査
成果物は `src/bazel-bin/unix/mozc.zip`、33,345,803 bytes、SHA-256 `1228c8ccd9f3c7c833c61265a3c00f86ec187f721bfb8b91a8b2fcfa7c0de066`。ZIP CRC検査に成功。絶対パス・親ディレクトリ参照・symlinkエントリがないことを確認し、R/artifact-inspection配下だけに展開した。
`src/unix/BUILD.bazel`、build_package.py、config.bzlと照合し、server/tool/renderer、IBusエンジン/XML、Emacs helper/el、アイコンを確認。zip内のtmp/icons.zipはbuild_package.pyの既存配置に由来する同梱物で、ホスト/tmpへ展開していない。Fcitx5アドオンとZenz scorerは通常packageに含まれない。

| ELFパス（zip内） | bytes | SHA-256 |
|---|---:|---|
| usr/bin/mozc_emacs_helper | 2,970,952 | 5e47b61f71d8caf038148fbd9d8c11837acac896dc75dd3d326c20470145a964 |
| usr/lib/ibus-mozc/ibus-engine-mozc | 3,175,312 | be73c2c0439e07e8e418b60db3fb1655b12263252d4e5d90340d202cc115a487 |
| usr/lib/mozc/mozc_server | 43,206,048 | b1befe3851f0b38f4db281c7fa5d4129821296c86442d2c05b813da9a2966845 |
| usr/lib/mozc/mozc_tool | 4,230,752 | 0dae212840b0f95ee5c0b0f6c39d54f6d53e25a0972b14a984e62c8d61aa0da8 |
| usr/lib/mozc/mozc_renderer | 2,847,592 | df1bb8a8fee5df78e89b8f818738d0f8933e057acced1f222629cb11bc80e179 |

5つのELFはすべてELF64 x86-64。file/readelf -h/-l/-dでヘッダー、インタプリタ、NEEDEDを確認し、インタプリタ実体がホストに存在することを検証した。ldconfig -pのx86-64エントリとreadelfで推移依存も静的に辿り、合計55オブジェクトの参照で欠落0。Qt6/IBus/GLib・標準C/C++ runtimeを検出した。
検査は静的なライブラリ存在・形式照合であり、GUI/IME実行、入力、動的ロード時のすべての挙動、Ubuntu 24.04へのバイナリ互換性は保証しない。成果物をシステムへ導入していない。

### 予算と監視の限界
監視修正後は全非loopbackインターフェースの起動以来の受信カウンタを保守的な上界として計上した。これにはB/C取得・他アプリ通信も含まれ、D単体の正確な転送量ではない。監視中断前の取得を過小評価しないため、この上界で2GiBに64MiBの余白を設けて停止する方式にした。
再build中の受信増分は5,553,527 B、test中は6,574,679 B。最終の保守的上界は1,671,148,142 B（約1.56GiB）で2GiB以内。監視による停止はなし。
容量は2秒ごとにduで観測し、全体59GiBで停止する監視を使用。監視の見かけサイズピーク約2.63GiB。処理後du -s -B1では実割当3,218,370,560 B（約3.00GiB）、du -sbでは2,825,075,828 B（約2.63GiB）。見かけサイズとブロック割当の差があるため両方記録した。60GiBに十分余裕があり、ディスク空きは約279G。実行後のログ・文書追記によりサイズはわずかに増える。
作業用tmp・Bazel出力・依存キャッシュ・zip展開はディスク上のR内。生成物・キャッシュ・個人情報を含み得る生ログはGit登録しない。

### 最終受入判定とPhase 2への引き継ぎ
Ubuntu 26.04.1 x86_64、GCC 15.2.0、Bazel 9.0.2、固定daily辞書による通常packageビルド・限定テスト・成果物の静的検査を達成。Fcitx5統合差分・未解決事項は本書の既存調査を引き継ぎ、Phase 1は受入検証済み・レビュー待ちとする。
既存24.04 CIとの差はOS/GCC等のホスト環境と限定テスト範囲。CIと同じdaily生成経路・通常release_build package設定を使用したが、CIの全件debugテストやCI run成功確認の代わりとはしない。
Phase 2ではFcitx5フロントエンド、独立ID/ライブラリ/設定/IPC/データ配置、遅延callback/世代・候補更新・入力欄寿命、Hazkey共存と復旧を実装・検証する必要がある。初期試験は分離VM推奨、ホスト追加は別途明示承認。通常ビルド成功をFcitx5対応完了に読み替えない。
今回エージェントによるsudo・追加システム変更・ソース機能修正は0。既存Fcitx5/Hazkeyの設定・辞書・入力環境の変更、IME登録/切替/起動、Zenzサーバー実行、Phase 2開始、mainマージは行っていない。元作業ツリーのステージ済み変更を保持した。
ローカル証拠はR/logsのd-build.log、d-build-interrupted.log、d-test.log、各metrics.json、artifact-summary.json、elf-*、elf-dependency-check.json、test-summary.json、およびBazel test.xml/test.log。証拠の要約のみ文書としてコミットする。
