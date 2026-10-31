# Phase 2 詳細実装計画・受入条件（レビュー案）

**状態：計画作成のみ承認済み。実装・ビルド実行・試験用IME起動・システム導入・既存IME変更・Phase 3は未承認、未実施。**

[共通ルール](../../AGENTS.md) / [ロードマップ・承認](../../PLANS.md) / [Phase 0調査](phase-00-audit.md) / [Phase 1結果](phase-01-results.md)

## 1. 目的・調査基点

Ubuntu 26.04.1 LTS x86_64 + Fcitx5で、Hazkeyを残したままMozkeyを独立したIMEとして追加する。最優先はHazkeyの設定・個人辞書・入力環境の保護。通常変換とMozkeyのライブ変換をPhase 2で扱い、Zenzの実接続・scorer・llama-server・モデル取得はPhase 3に残す。

2026-10-31にGitHub接続ツールと`git fetch origin main`で確認した基点：

| 対象 | 固定値・確認結果 |
|---|---|
| フォーク最新main | `9c75ffcc1e99c0efb0d65ad0e1f25148dc1ef7a1`。指定4文書を確認。Phase 0・1は完了、Phase 2実行は未承認 |
| 参照元fcitx/mozc | 既定fcitxブランチ先端 `384a85cf2f5db982e64c5c33cade66b2a5e5176c`。Phase 1調査時と同一。BUILD・MODULE・engine/factory・state・parser・pool・設定を再確認 |
| 本家PR #231 | open、未マージ、head `f0867583f00f70f6e21af76219477106e8f54f66`。本文・差分・コメントを再確認。Fcitx5追加を含まない |
| 元作業ツリー | `adopt_ubuntu` / `dfc744b1e719528b32ba3c0337a49c242f64fba7`。mainより526コミット後方。ステージ済み5ファイルと未追跡`.local/`を保持 |
| 文書用worktree | 最新mainから`docs/ubuntu-phase2-plan` / `/tmp/mozkey-phase2-plan`。既存Phase 1 checkoutには変更しない |
| Phase 1資産 | `R=/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase1`。checkout、tools、downloads、生成辞書、bazel-output、repository-cache、disk-cacheが存在 |

Phase 1結果の前半には未達時点の履歴が残る。結論は冒頭・「apt導入後のD実行結果」・最終レビュー記録に従い、通常release package・限定28ケース・zip/ELF検査成功と扱う。これらはFcitx5実入力の証拠ではない。

## 2. 承認と実行段階

| 段階 | 実施案・成果物 | 開始条件・終了条件 |
|---|---|---|
| 今回：計画レビュー | 本書とPLANSの参照・提案状態を整える。読取り調査と文書検査のみ | 今回の依頼範囲。コード追加、ビルド、IME起動、導入はしない |
| 2A：実装・ビルド・静的/単体検証 | 選択移植、名前/パス分離、callback、限定テスト、作業領域へのstaging、差分・由来・成果物manifest | 本計画の実装範囲・依存取得/資源予算の承認。必要なシステム開発依存の導入は別途承認。A1〜A7を満たす |
| 2B：隔離環境での初期入力試験 | Ubuntu 26.04.1 x86_64のVMを第一案とし、読み込み、通常/ライブ変換、寿命・障害・撤去復旧を検証 | 初期試験環境、取得物、ゲスト内導入・IME起動の範囲を明示して承認を受ける。A8〜A11を満たす |
| 2C：実ホスト共存試験 | Hazkeyを残してMozkeyを追加し、切替・独立性・復旧を確認 | 2A/2B合格、衝突調査と導入/復旧手順レビュー後、**実ホストへの追加・必要なFcitx5設定変更/再起動の別途明示承認**。A12を満たす |

2A/2Bの合格は「Phase 2前半合格」と報告する。2C未実施のままPhase 2全体完了や実ホスト共存成功としない。各段階で停止・証拠を報告する。Phase 3進行、コミット/プッシュ、PR作成・マージも今回の承認に含めない。

## 3. 統合方式と変更対象

Fcitx5アドオン → Mozkey client/protocol → **Mozkey専用server**というIPC方式を採用案とする。Fcitxプロセス内に辞書・変換エンジンを直接持つdirect方式は今回採用しない。IPC方式でもアドオンはFcitx5プロセスにロードされるため、フロントエンドの不具合による影響は初回ロード前に隔離する。

fcitx/mozc全体のマージやMODULE全置換は行わず、固定SHAから必要部分だけを移植する。C++の`mozc::`名前空間、protobuf定義、既存Bazelラベルは原則維持し、利用者が見るID・実行時識別・保存先をMozkey専用にする。

| 変更先案 | 作業内容・完了の証拠 |
|---|---|
| `src/unix/fcitx5/`（新規） | engine/factory、state、response parser、key handler/translator、surrounding text、client interface/pool/IPCを移植し現行APIへ適合。direct clientは除外。コピー元パス・SHA・元blob hash・変更理由の一覧を同梱 |
| 同ディレクトリの設定・資産 | mozkey-addon.conf、mozkey.conf、Mozkey名のアイコン・action/property登録、gettext domain。既存の画像を再利用し出典・ライセンスを保持。`.po/.mo`のdomain・生成手順・検索先を一覧化し、翻訳なしでも読める表示名を保証。AppStream整備は配布Phaseで扱う |
| `src/MODULE.bazel` | 必要な`@fcitx5`のみ追加しFcitx5Core/Fcitx5Moduleを検出。既存依存の版は維持。Fcitx4宣言は移植しない |
| Fcitx5 BUILD・root BUILD/.bazelrc | Linux限定共有ライブラリ`//unix/fcitx5:fcitx5-mozkey.so`、限定テスト群、新規stagingターゲット案を追加。非Linuxや既存通常packageがFcitx5開発環境を要求しない依存グラフを確認 |
| `src/base/{const.h,system_util.cc,BUILD.bazel}`、`src/config.bzl`等 | Linux Mozkey用ビルド設定でserver/tool名・探索先・profileを一括選択。アドオン/server/toolで同じ設定を使用する。Windows/macOS/Androidと従来設定は既定のまま |
| `src/ipc/ipc_path_manager.cc`、関連テスト | Linux Mozkey設定でIPC接頭辞を分離し、専用profileからのみ鍵情報を読む。抽象socketのUID/peer・実行パス検証を維持。異製品・異なるserverへの接続/終了を拒否 |
| `src/unix/`の専用staging処理案 | `.so`、設定、アイコン/翻訳、server/tool、必要な辞書データとライセンスをmanifest付きで作業領域に配置。既存IBus用mozc.zipをインストール入力にしない |
| `docs/ubuntu/phase-02-results.md`（実行後） | 環境・SHA・テスト表・hash/依存・未検証事項・2A/2B/2C別判定・導入/無効化/撤去/復旧手順を記録 |

ビルド設定名の提案は`--//:linux_mozkey=true`（既定false）、専用server配置設定は`--//:mozkey_server_dir=絶対パス`とする。これらの設定と上表の新規ターゲットは**未実装**。マクロ/生成ヘッダー等で全対象へ一貫して伝搬し、ファイルごとの手動書換えや一部バイナリだけの名前変更を避ける。通常packageの置換・IBus登録は対象外。

2A内の作業順序は次のとおり。各項目をレビュー可能な差分に分け、初回ロード前にすべての静的・単体検証を終える。

1. 依存・来歴manifestとビルド設定を用意し、client/launcherのdeadline・終了対象の契約を先に設計する。
2. profile・実行名/探索先・IPCを一括分離し、合成profileと異製品sentinelによる負試験を先に通す。
3. 固定SHAのフロントエンドを選択移植し、Linux限定依存と専用.so/server/toolのリンクを成立させる。まだロードしない。
4. InputContext別client、timer/epoch、共通応答・描画経路を実装し、fake clock/IPCによるcallback/寿命/障害試験を通す。
5. 通常入力・候補・周辺テキスト・再変換/マクロ・専用設定とtool起動を適合させる。
6. staging、export/依存/梱包検査、既定設定との回帰を完了し、2A結果と2B実行手順を提示する。

由来調査で`i18nwrapper.h`と`mozc_engine_factory.cc`はLGPL-2.0-or-laterの表記を確認した。他の取得済みファイルにはBSD形式の表記がある。一括でBSD扱いせず、採用ファイルごとの著作権・ライセンスと必要なLICENSE本文/配布時表記を維持し、THIRD_PARTY_NOTICESへ反映する。[参照元ソース](https://github.com/fcitx/mozc/tree/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5)

## 4. ID・IPC・データパスの分離契約

次表は製品構成案。staging時は専用prefix/ルート下に配置し、ホストの`/usr`や`~/.local`へコピーしない。Fcitx5の実際のlibdir、検索順、翻訳/アイコン配置は対象環境のpkg-config・導入ファイルと照合する。

| 要素 | 現行/参照元 | Mozkey案・不変条件 |
|---|---|---|
| addon / inputmethod ID | `mozc` / `mozc` | 両方`mozkey`、表示名Mozkey、Addon=mozkey。factory V2 IDもmozkey。IME有効判定の文字列も変更 |
| 共有ライブラリ | fcitx5-mozc.so | `<libdir>/fcitx5/fcitx5-mozkey.so`、Library=fcitx5-mozkey |
| addon/inputmethod定義 | mozc.conf | `share/fcitx5/addon/mozkey.conf`、`share/fcitx5/inputmethod/mozkey.conf` |
| フロントエンド設定 | `conf/mozc.conf` | `$XDG_CONFIG_HOME/fcitx5/conf/mozkey.conf`。他addon設定やFcitx5共通profileは2C承認前に触れない |
| action/property、アイコン、gettext | mozcState、mozc-*、fcitx_mozc*、fcitx5-mozc | mozkey専用prefix/domain。全登録名を棚卸しし同一Fcitx5内で一意 |
| server/tool実行名 | mozc_server / mozc_tool | mozkey_server / mozkey_tool、製品配置`/usr/lib/mozkey/`案。staging用にビルド時絶対パスを指定。`PATH`や`/usr/lib/mozc`へのfallbackを設けない |
| server設定・ユーザー辞書・学習 | 既存`~/.mozc`優先、XDG側もmozc | `$XDG_CONFIG_HOME/mozkey`、未設定時`$HOME/.config/mozkey`のみ。HOMEなしもgetpwuid経由の`.config/mozkey`に統一。既存`.mozc`/Hazkeyから読取り・自動移行しない |
| IPC鍵・mutex | profile内`.session.ipc`、`.server.lock`等 | ファイル名を維持しても**親profileはMozkey専用**。tool/singleton/log/history関連も同profileへ。作成は本人所有・private権限、symlink/別所有者は安全側で失敗 |
| 通常server IPC | `/tmp/.mozc.<key>.session`相当 | Linuxでは先頭NULの**抽象Unixソケット**。同形式のmozkey接頭辞へ変更。ファイルunlinkやTMPDIR変更では分離できない。profile鍵・製品prefix・peer/実行パスを併せて検証 |
| 辞書データ | Phase 1で生成したdaily | 同じ入力を専用serverへ組み込み/配置。server BUILDのデータ依存を確認し、組込済みなら個別dataファイルを要求しない。未梱包・別製品データへの暗黙参照を検査 |
| Zenz IPC・runtime | UID別socket等 | Phase 2では取得・起動・接続しない。Zenz無効で試験。共有IPC実装の将来の製品分離はPhase 3の開始前条件として引き継ぐ |

Linuxの抽象ソケットの根拠は現行`src/ipc/ipc_path_manager.cc:GetPathName`。既存文書の「/tmpパス」は名前の表現であり、Linux上のファイルsocket作成を意味しない。profileの分離だけ、Library名変更だけ、HOME差替えだけのいずれも独立性の十分条件としない。

clientの版違い処理はserverを終了・再起動する場合があるため、接続だけでなくShutdown/ForceTerminate/Wait/ready eventの対象を専用serverへ限定する。renderer・GUI singleton等、実際に起動しない経路も誤ったプロセスを起動できないよう一覧化する。Fcitx5の候補UIを使い、外部mozc_rendererは通常入力には起動しない。

同一Fcitx5プロセス内のELFシンボル衝突も検査する。factory V2名を変え、動的exportをFcitx5が必要とする入口へ絞る方針を採る。`nm -D`/`readelf --dyn-syms`でmozc/Abseil/protobuf等の不要exportを確認し、必要ならvisibility/linker export制御を追加する。Abseil flags等のグローバル初期化・登録名の重複も試験する。library名の違いだけでシンボル独立を保証しない。

## 5. ライブ変換・遅延コールバック

### 確認した差分

[参照元parser](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_response_parser.cc)はcallbackからtypeのみを新しいSessionCommandに移し、UNDO/CONVERT_REVERSE以外はreturnする。delay_millisecを処理せず、`consumed=false`でも早期returnする。現行Mozkeyには`APPLY_LIVE_CONVERSION`、`RESUME_COMMAND_SEQUENCE`、`RECONVERT_SELECTION_OR_INSERT_SPACE`等と世代・読みフィールドがあり、このままでは対応できない。

また参照元stateは応答処理前にClearAllし、ParseResponse後にDrawAllする。タイマーからparserだけを呼ぶと描画更新・古いUI破棄の扱いが揃わない。現行macOS/Windowsのcallback処理とsessionの世代検証も比較し、Fcitx5用に次の契約を実装する。

### 実装契約

1. callbackのSessionCommandはprotobuf全体を複製し、type、live_conversion_generation、live_conversion_key、command_sequence_generation等を落とさない。UNDO/再変換で補う周辺テキストは、このコピーに必要部分だけ設定する。未知typeを無条件送信する方式にせず、対応コマンドを列挙する。
2. timerはFcitx5 EventLoopの`addTimeEvent(CLOCK_MONOTONIC, ...)`を利用する。delayはmsから64-bit usecへ安全に換算し、期限計算のoverflowを検査。0ms/未指定も次のevent loopへ渡し、無制限の再帰を避ける。sleep、独立スレッドからのInputContext/UI操作は行わない。[5.1.19のAPI](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx-utils/event.h)
3. 保留レコードはInputContextごとに所有する。watch可能な参照、context UUID、activation/session epoch、client session、要求番号、完全なcommandを保持。発火時に生存・focus・現在IME=mozkey・epoch一致を確認する。生ポインタだけをtimerへcaptureしない。[InputContextの寿命管理](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/inputcontext.h)
4. 初期実装ではバックエンドclient/sessionをInputContext単位に固定する（poolの共有ポリシーNo）。Fcitx5共通の共有設定は変更しない。参照元のProgram/All共有は採用せず、UI状態・timer・遅延応答が入力欄間で混ざらないことを優先する。将来共有する場合は別途設計する。
5. 新しい変換要求は旧live timerを置換。focus out、IME無効化/切替、reset/revert、確定、入力モード変更、context破棄、client再生成/再接続、設定reload、addon終了で関連timerを取消しepochを更新する。command sequenceもsessionの取消/継続規則に合わせる。短時間でfocusを戻しても古いcallbackを復活させない。
6. callback送信後のOutputは、通常キー応答と同じstateの応答適用・preedit・候補・DrawAll経路に戻す。応答の宛先/世代を検証してからUIを変更する。候補click/pageにも表示時の世代を持たせ、古い候補IDを新しい一覧へ送らない。modeのみ等の部分応答で有効なpreeditを誤って消さない。
7. `consumed`は物理キーをアプリへ渡すかの判定として保持し、callback有無と分離する。`consumed=false` + 有効callbackでも予約する。timer応答から物理キーの再送はしない。重複確定、先頭文字/Space欠落がないことをテストする。
8. callback種類は通常ライブ変換、遅延マクロ継続、UNDO、再変換と選択なしSpace fallbackまでを対象とする。再変換成功時は選択範囲だけを置換し、選択/削除能力がない場合のfallbackと継続通知を現行protocolに合わせる。password入力では周辺テキスト取得・学習・ライブcallbackを抑止する。clipboardへの代替取得は初期版では無効とし、勝手に履歴を読まない。
9. timer callbackの再予約も有限管理とし、同一contextの保留枠は最大4、即時連鎖は最大16回/要求を初期上限案とする。越えたら当該連鎖を止め、通常入力に戻す。正常なdelay設定を黙って0へ丸めない。Zenz callbackはfake clientで保持・順序・取消のみ検査できるが、AI補正成功とは扱わない。
10. `live_conversion`/`live_conversion_pending`/`pre_live_conversion_reading`の表示意味を保持する。基本受入は正しいpreedit・分節・cursor・候補・確定結果。Windows固有のruby描画やspinnerの完全再現はPhase 2必須にしない。ライブ変換ON/OFFはMozkey専用設定に保存する。

focus outは参照元の「明示IME切替ではSUBMIT、通常focus移動ではREVERT」を初期仕様とする。取消/epoch更新を先に行い、旧入力欄への同期確定だけを許す。遅延応答を新入力欄へ送らない。アプリやWaylandがcontextを維持する場合もfocus/activation条件で制御する。

### IPC待ち・障害

現行clientはopt既定30秒、debug100秒のIPC timeout、server起動待ちは20秒。これらをFcitx5のイベント処理へそのまま持ち込まない。2Aの先頭で専用IPC adapterの起動・再接続・終了経路を確認し、**通常の同期IPCは要求全体100ms以内、起動/復旧待ちはUI外で非同期**を初期設計値とする。単に`set_timeout`を1回呼ぶだけでは複数往復やlauncher/destructor待ちを制限できないため、経路全体のdeadlineを設ける。Clientへの同時アクセスは行わない。

未接続時は短い準備状態を表示して新しい通常キーをpass-throughし、準備完了後に新sessionを使用する。変換中の通信失敗は未確定文字列を保護したうえで当該要求/epochを打ち切り、遅い応答を確定・再生しない。結果不明なキー/commitを自動再送しない。失敗時の未確定文字列の取消/再入力手順をUIと試験仕様へ記す。この契約を満たせない場合は2A未達として設計を再提示し、初回ロードへ進まない。

## 6. ビルド依存・Phase 1資産の再利用

### 現在の依存調査（読取りのみ）

`apt-cache policy/show`、`pkg-config --modversion`、次のシミュレーションを実施。apt索引更新・取得・インストールは行っていない。Hazkey/Fcitx5設定・個人辞書の内容は読んでいない。

```sh
apt-get -s --no-install-recommends --no-upgrade --no-remove install \
  libfcitx5core-dev=5.1.19-1 libfcitx5utils-dev=5.1.19-1
```

| 観測 | 結果 |
|---|---|
| 導入済みruntime | fcitx5 `5.1.19-1`、fcitx5-hazkey `0.2.1-1`（パッケージ管理情報のみ） |
| 開発依存 | core-dev/utils-dev未導入。Fcitx5Core/Module/Utilsのpkg-config検出は失敗 |
| 現索引のsimulation | core-dev/utils-dev/config-dev、全て`5.1.19-1`、新規3・更新0・削除0 |
| 3 debの取得量 | core 35,060 B、utils 83,042 B、config 15,616 B、合計133,718 B。依存追加分と他のBazel取得は別計上 |
| deb SHA-256 | core `7b3a64c8c018d5d4661359619b99eb8eb33feef454d868748660e77a09cdcb99`、utils `fb801f85bb78ad1677fa7d3ab3460ed54a4b6c5075b56561593811fb5def19d7`、config `7861a3e7aeae1b2f420228ced6f10bf76df64758a312a4b21e1ec99ff63f3f72` |

推奨は、承認後に署名済みUbuntu索引・hashと照合した開発debを作業用sysrootへ展開し、pkg-configのprefix/sysroot/include/linkを対象runtimeと照合する方法。展開先にはruntimeをコピーしてホスト検索順を変えず、既存パッケージの中身とABIを確認する。Fcitx5Module.pcを含む提供ファイル、推移依存、生成BUILDの絶対パス・linkoptsは取得後に検証する。単なるPKG_CONFIG_PATH指定だけで正しくrelocateできるとはみなさない。難しい場合はVM内で開発依存を用意する。ホストのapt導入は別途承認対象。

5.1.19を今回の検証基準とし、最小対応版を推測して宣言しない。MODULEに追加したrepositoryが既存ターゲットの分析やWindows/macOSで不要に評価されないことも確認する。現行pkg_config_repositoryの失敗出力だけでは依存不足を確実に検出できないため、ビルド前の明示的pkg-config検査を必須とする。

### キャッシュと生成辞書

今回、Phase 1の生成daily4ファイルを`R/logs/generated-summary.json`とSHA-256再照合し全一致、既存mozc.zipもPhase 1記録の`1228c8ccd9f3c7c833c61265a3c00f86ec187f721bfb8b91a8b2fcfa7c0de066`と一致した。生成・再取得・Bazel実行はしていない。

| 資産 | 再利用方法・限界 |
|---|---|
| Bazelisk 1.30.0 / Bazel 9.0.2 / PowerShell 7.6.9 | R/tools・downloadsの検証済み資産を再hashして使用。Bazelを自動再取得しない |
| daily生成物・取得済み入力 | R/checkout内のgenerated（profiledを含む）・dist/dictionaryを来歴とともに維持。実装worktreeへ必要な未追跡生成物だけをcopy/reflinkし、hash/サイズ・BUILD入力完全性を確認。sample切替や辞書再生成は既定にしない |
| repository-cache / disk-cache | 同じディスク上の既存パスをBazelへ渡す。設定・入力が一致する工程だけの再利用であり、cache hitや再ビルド0件は保証しない |
| bazel-output | 既存`7997bb585620525b603d998f988fe569`のoutput baseを保持。別worktreeではworkspaceが異なるため新しいoutput baseが生じ得る。古いbaseを強制指定して別workspace間で使い回さない |
| Phase 1 checkout | mainと同じHEAD、追跡変更なしを確認。今回は保持。実装は最新mainの別worktreeを同一ディスク上に作る案。既存checkoutのブランチ切替による最大限のincremental利用は、別途その方式を合意した場合だけ |
| mozc.zip・ELF・ログ | Phase 1成功証拠として保持。従来mozc名/パスのバイナリはPhase 2の独立server/toolとして実行・配布しない。専用設定で再リンクが必要 |

`bazel clean/expunge`、キャッシュ削除、元作業ツリーのstash/reset、ビルド用symlinkの手動移植を行わない。Rの既存ログ・artifactを上書きせず、P2専用logs/output/stageを作る。大規模出力はtmpfsの/tmpへ置かない。

資源案はjobs=2、Phase 1資産とPhase 2作業領域を合わせて60GiB以内、新規Bazel等の取得は最大2GiB、辞書/モデル新規取得は0とし、実行前に承認範囲を確定する。過去の予算承認を自動継承しない。VMイメージ・guest依存の取得/領域は別見積り。受信量は対象通信の計測を優先し、interface全体の差を使うなら他アプリを含む上界であることを記録する。監視不能・上限接近で停止する。

### 承認後のコマンド構成案

以下は**未実装の設定・ターゲットを含む実行案**。本レビューで実行しない。srcをcwd、専用PATH/TMPDIR/XDG/BAZELISK_HOME/USE_BAZEL_VERSIONはPhase 1と同様に作業領域へ限定し、ANDROID_NDK_HOMEを当該プロセス内だけ空にする。生成されるrepositoryが参照するpkg-config環境もBazelへ明示する。

```sh
pkg-config --modversion Fcitx5Core Fcitx5Module Fcitx5Utils
pkg-config --cflags --libs Fcitx5Core Fcitx5Module

# P2_STAGEは作業ディスク上。既定の/usrを対象にしない
bazelisk --output_user_root="$R/bazel-output" --batch build \
  //unix/fcitx5:fcitx5-mozkey.so //server:mozc_server //gui/tool:mozc_tool \
  --config release_build --jobs=2 --//:linux_mozkey=true \
  --//:mozkey_server_dir="$P2_STAGE/usr/lib/mozkey" \
  --repository_cache="$R/repository-cache" --disk_cache="$R/disk-cache"

# 新規テスト群にはfake IPC/clock/ICを使用。Fcitx5実入力とは別判定
bazelisk --output_user_root="$R/bazel-output" --batch test \
  //unix/fcitx5:mozkey_tests \
  --config release_build --jobs=2 --test_output=errors \
  --//:linux_mozkey=true --//:mozkey_server_dir="$P2_STAGE/usr/lib/mozkey" \
  --repository_cache="$R/repository-cache" --disk_cache="$R/disk-cache"
```

existing labelsのmozc_server/toolは内部名として残せるが、staging上の実行名・埋込み名はmozkeyへ統一する。新規stagingターゲットでは上記と同じ設定を使い、想定内容/hashをmanifest化する。テスト起動時もHOME/XDGを合成profileへ向け、通常ユーザーのprofile初期化を避ける。

既存回帰は変更範囲に応じ`//base:system_util_test`、`//base:process_mutex_test`、`//ipc:ipc_path_manager_test`、`//ipc:ipc_test`、`//client:client_test`、`//session:session_test`のライブ変換/マクロ該当ケースを選ぶ。セッションのZenz関連はfakeを用い本物を起動しない。独立設定ON/OFF双方を確認し、従来package・Phase 1限定28ケースは必要な回帰として既定設定で再確認する。IBusテストをFcitx5受入に数えない。非Linuxの実行検証は利用可能なCI/端末がなければ未検証とする。

## 7. 検証環境・試験表

初回ロードは分離VMを推奨する。ホストとHOME/XDG、辞書、D-Bus、display/compositor、runtime、network namespaceを共有しない。ホストprofileの共有mount・clipboard共有・入力履歴同期を使わない。実行用VMへ必要な成果物だけをコピーし、ビルドキャッシュ/個人ファイルを持ち込まない。VMイメージ・Fcitx版・ABI・DE・GTK/Qt版・Wayland/X11を記録する。

ホスト導入前に、隔離環境でもホストと同版のHazkeyアドオンとMozkeyを同じFcitx5へロードし、合成profileで切替・設定独立・初期化衝突を確認する。Hazkey本体の取得元・hash・依存とゲスト内導入は2B承認範囲に含め、ユーザーの設定や個人辞書はコピーしない。同版を用意できない場合は未検証リスクを明記し、2Cへ進む前に条件を再レビューする。Mozcが導入済みなら同様に衝突を確認し、未導入ならMozcの導入を前提条件にはしない。

代替は別ユーザーの独立セッション等を候補とするが、同一network namespaceの抽象socket、display接続、D-Bus service所有権、fallback HOME、既存Fcitx5への接続を評価する。`dbus-run-session`やXDG差替えだけで完全隔離とはしない。ホストへ導入・既存sessionへ接続する代替案には2Cの明示承認を必要とする。未確定なら2Aまでで停止する。

合成profile・既知のテスト文字列を使い、各行に手順、期待結果、実測、証拠、pass/fail/未実施を記録する。代表入力は`nihongo`、`konnichiha`、促音/撥音/未確定ローマ字、日本語混在、emoji/多バイト文字とする。変換の順位に依存するケースは期待候補を辞書版とともに固定する。

| ID | 対象・操作 | 合格となる結果 |
|---|---|---|
| T01 | addon列挙・初回load、設定UI | 独立したMozkey名/ID、正しい.so/server、設定と辞書toolが専用profileを使う。既存addon登録と混同しない |
| T02 | ローマ字/かな、半角全角、key up/down、modifier、JIS/US配列 | 読み・cursor・preeditが一致、未処理キーは1回だけアプリへ。モード変更が正しく反映 |
| T03 | Space変換、候補選択/クリック、ページ移動、Enter/Esc/Backspace | 正しい候補ID、確定文字列1回、取消でpreedit/候補が消え、再入力できる |
| T04 | live OFF/ON、delay=0/228ms、最小文字数境界 | OFFは遅延変換なし。ONでは最終キー後、設定した遅延を経て通常live結果を表示。追加キーなしでも更新し、timer由来の確定はしない |
| T05 | 連打・中間delete・cursor移動・候補操作中の旧timer | 最新の入力/世代のみ適用。古い候補clickや旧読みの変換で上書きしない |
| T06 | 2入力欄切替、同じprogramの複数窓、focusを戻す、欄破棄/IME切替 | 旧callbackの新入力欄への反映0、use-after-free0。focus outのSUBMIT/REVERT仕様どおりで重複commit0 |
| T07 | `consumed=false`+callback、0ms連鎖、遅延マクロ、未知type | キーのpass-throughを保ち継続が1回、フィールド保持、上限で安全停止。再帰暴走・永久pollなし |
| T08 | 再変換選択あり/なし、UNDO、password/能力不足 | 選択範囲だけを変換、選択なしSpaceは仕様どおり。削除範囲逸脱0、password周辺取得/学習/遅延処理0 |
| T09 | fake timeout/切断/不正応答、専用server停止/復旧 | Fcitx5の入力処理を長時間止めず、遅い結果・二重commit0。未確定文字列保護/復旧手順を確認。別IMEプロセスの停止0 |
| T10 | 合成mozc/Hazkey相当の別profile・異製品IPC sentinel、版違いserver | 他profileのファイル/鍵を読まず、別socketへ接続せず、別製品の終了/上書き0。名前衝突時はfail closed |
| T11 | GTK/Qt各1アプリの実入力、Wayland、対応XWayland経路 | preedit/candidate位置とcursor、多バイト・長文・候補操作が使える。対象session経路を明記。別X11 session未試験なら未検証と限定 |
| T12 | 合成profileで100回focus切替・100回生成/破棄、1,000キー、10分連続 | crash/hang0、他欄への出力/文字重複欠落0、保留timer数が終了時0、session/メモリが反復ごとに単調増加しない |
| T13 | 初期環境でMozkey無効化・撤去・再追加・障害復旧 | manifest記載の対象だけを操作でき、既存IMEを残して入力復帰。再追加時も設定・辞書が独立 |
| T13b | 隔離環境で同版Hazkeyと同時ロード・交互入力・再起動 | addon初期化/flags/export衝突0、双方で入力可能、合成辞書と設定が独立。個人profileは使用しない |
| T14 | 別途承認後の実ホストHazkey↔Mozkey切替、両者の設定・合成辞書登録 | Hazkeyを保持し双方で入力可。Mozkeyへの登録がHazkeyへ現れず、Hazkeyの既存データを移行/変更しない。Mozkeyだけを無効化して元の入力環境へ戻せる |

T04の受入測定は同じVM・jobsを止めた条件で、設定遅延より早く適用しないこと（時計/計測誤差5ms）と、最終キーから「設定遅延+500ms」以内の表示を20回確認する。T09は通常要求のdeadline100ms、イベントloopへの復帰は250ms以内を目安としてfake障害試験で確認し、起動/再起動は別計測にする。環境負荷で達成できなければ数値を無断で緩めず、測定と修正案をレビューする。

## 8. 導入・無効化・撤去・復旧の設計

2B用手順を先に作り、隔離環境で成功してから2C用の正確な変更一覧を提示する。system path・ユーザーpath双方で既存ファイルがあれば内容を上書きせず停止する。

1. **導入前**：2A/2B結果、SHA/hash、ロード先、依存、変更ファイルmanifest、承認されたIME登録・再起動内容を確定。2CではHazkeyの実際のパス/識別子だけを調査し、内容をログへ出さない。設定/辞書/IME順序は私有の退避先へバックアップし、owner/mode/hashをローカル記録する。
2. **追加**：Mozkey専用ファイルだけを配置し、Hazkeyを削除・置換しない。既定IMEを勝手に変更しない。Fcitx5 profileへのMozkey追加や再起動は承認された手順・タイミングだけで実施する。
3. **無効化**：Mozkey IDだけを入力方式から外し、必要ならMozkey addonだけを無効化。同期中の未確定入力を処理したうえで、専用serverのPID/UID/実体パスを照合して終了。`pkill fcitx5`や名前の部分一致で他IMEを停止しない。
4. **撤去**：導入manifestとhashを照合し、今回追加したMozkeyファイルだけを撤去。Mozkeyのユーザーデータは既定で残す。Hazkey/Mozcの設定・辞書・共通Fcitx5設定を一括削除しない。
5. **復旧**：Mozkey読み込み失敗でも使える端末/別ログインから専用ファイルを退避し、必要なFcitx5設定差分だけを戻す。Fcitx5再起動が必要なら既存入力を保存して承認範囲で実施しHazkey入力を確認。バックアップ全体の無条件上書きで作業中の更新を失わないよう差分を照合する。

Hazkeyの稼働中にユーザー入力由来の辞書・学習データが変化し得るため、before/afterのhash不一致を直ちに破損とは扱わない。比較時点を揃え、変更範囲・書込み主体と合成入力を基に確認する。生ログ・個人辞書・バックアップはGit/報告に添付しない。

## 9. 受入条件とレビュー時の判断

| 条件 | 合格の証拠 | 対象段階 |
|---|---|---|
| A1 基点・由来 | main/参照元SHA、採用ファイル/ライセンス一覧、環境・依存・辞書hash、差分が再現可能 | 2A |
| A2 独立性 | 全ID/登録名/実行パス/profile/IPC/鍵/mutex/動的exportの一覧と負試験T10。mozc/Hazkeyへのfallback/誤停止0 | 2A |
| A3 ビルド | 専用.so/server/tool/stagingが同一設定で成功。manifest・hash・ELF64 x86_64・NEEDED/ABI・export・絶対パス・梱包内容検査に合格 | 2A |
| A4 callback | fake IPC/clock/ICでT05〜T09の該当分を再現。世代/command全フィールド保持、取消、consumed=false、候補寿命・再描画、制限の期待値一致 | 2A |
| A5 IPC応答性 | 起動/再接続/破棄経路までdeadlineと所有権を検証。通常要求100ms・障害時loop復帰250ms以内の基準を満たす | 2A |
| A6 既存回帰 | 独立設定OFF/ONで関連回帰と既定package/限定28ケースを確認。非LinuxにFcitx依存を強制せず、未実行OSは未検証と記録 | 2A |
| A7 資産/ホスト保護 | Phase 1入力・成功証拠・キャッシュと元staged変更を保持。予算内。ホストIME・個人データ・システム設定の変更0 | 2A |
| A8 初回入力 | Ubuntu 26.04.1 + Fcitx5 5.1.19基準の隔離環境でT01〜T04/T08/T11に合格、各結果と表示経路を記録 | 2B |
| A9 live・寿命・障害 | 実serverでもT05/T06/T09/T12/T13bに合格。liveの時間測定と確定/取消・候補更新が正しく、単なる無クラッシュ判定にしない | 2B |
| A10 復旧 | T13成功、導入/無効化/撤去/復旧を第三者が再現できる。ホスト共存はこの時点では未検証 | 2B |
| A11 ホスト不干渉 | ホストD-Bus/display/Fcitxプロセス・profile・IPC・Hazkeyデータに接続/書込みしない分離を確認。代替環境の残存リスクを記録 | 2B |
| A12 実ホスト共存 | 別途明示承認後にT14成功。既存Hazkeyデータの保持・独立登録・切替・Mozkeyだけの無効化/復旧と検証範囲を記録 | 2C |

今回のレビュー対象は、IPC方式、InputContext別session、Linux専用の製品分離、callback/応答性契約、2A/2B/2Cの区分、依存の用意と資産再利用、試験・合格基準である。実装開始前には承認範囲（2Aのみか2Bも含むか）、開発依存の取得/配置、予算・初期環境の具体案を確定する。実ホスト導入とPhase 3は常に別途承認とする。

## 10. 今回の検証記録

GitHubのmain・fcitx/mozc・PR #231と指定文書、関連する現行client/session/IPCを読取り照合した。ローカルではgit状態、既存資産、deb索引、aptシミュレーション、pkg-config検出、生成辞書4点とzipのhashを確認した。Fcitx5 5.1.19のtimer/InputContext APIも公式ソースで確認した。実行開始時には最新mainとPR状態を再確認する。

文書のdiff、新規ターゲットを未実装と明示していること、承認状態・Phase 1最終結果との整合を確認した。`git diff --check`と新規文書に対する`git diff --no-index --check /dev/null docs/ubuntu/phase-02-plan.md`は指摘なし、ローカル参照10件の存在検査は欠落0。計画worktreeの`src`差分は0、変更は本書新規作成とPLANS.mdのみ。元作業ツリーのステージ済み5ファイルと未追跡.localを保持し、Phase 1 checkoutは追跡変更なし。コミット・プッシュは未実施。

実装、コンパイル、Bazel起動、辞書再生成、IME起動、システム導入、既存IME変更、Zenz起動は行っていない。

[PR #231](https://github.com/koyasi777/mozkey/pull/231)の共通POSIX/Zenz factory部分は引き続きPhase 3で再比較する。Phase 2の通常live合格をAI補正合格に読み替えない。
