# Phase 2 実行結果

## 最新の判定（2026-11-05）

**Phase 2Aは工程1のIPCゲートで停止、未完了・再設計レビュー待ち。A5未達。**
ユーザーは工程1〜6、A1〜A7に対する2A実行を承認したが、「IPCの応答性・安全条件を満たせない場合は停止」を指定した。[計画第5節](phase-02-plan.md)も同期ブロックを経路全体で制限できない場合の停止・再設計レビューを定めている。既存の同期client/transportを実コードで診断し、100ms設定でも故障経路の同期呼出しが250msを超えることを確認した。これを受け、工程2〜6の製品実装・stagingへ進んでいない。

これは現行同期経路の不適合という結果であり、期限付きtransportや非同期adapterによる実現が不可能だという結論ではない。A5を満たす再設計案は後述する。新アドオン・専用server/toolは未作成。2A合格、Phase 2前半合格、Phase 2全体完了のいずれにも該当しない。

## 承認範囲と基点

| 項目 | 実施・固定値 |
|---|---|
| 最新main | `git fetch origin`後の`b489fca1561bf12ff6b43205378220163d71c21c` |
| 作業ブランチ | `feat/ubuntu-phase2a`、上記mainから作成。HEADは同SHAのまま |
| 専用worktree | `/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase2a/checkout` |
| 作業領域 | 以下、`P2=/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase2a`、`P1=/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase1`と表記 |
| 元作業ツリー | `adopt_ubuntu` / `dfc744b1e719528b32ba3c0337a49c242f64fba7`を保持 |
| 参照元 | fcitx/mozc固定SHA `384a85cf2f5db982e64c5c33cade66b2a5e5176c` |
| PR #231再確認 | [Linux対応](https://github.com/koyasi777/mozkey/pull/231)はopen・未マージ、head `f0867583f00f70f6e21af76219477106e8f54f66`。Fcitx5の追加は含まない。Phase 3のPOSIX/Zenz差分を移植していない |
| 許可された実施 | 調査、実装、jobs=2のビルド、限定テスト、作業領域のstaging、結果文書。全体60GiB・新規取得2GiB以内 |
| 行っていない操作 | sudo、apt操作、依存再導入、システム導入、実IME/server起動・登録、Fcitx5/Hazkey設定変更、辞書/モデル取得、Zenz起動、2B/2C/3、本家PR、commit/push/rebase/merge |

`AGENTS.md`、`PLANS.md`、Phase 2計画とPhase 1結果を確認した。作業worktree内に追加のAGENTS.mdはない。元作業ツリーの`.agents/`内にある未コミット規則は、そのディレクトリ外の本worktreeへは移植していない。

## 環境・依存・由来

Ubuntu 26.04.1 LTS / x86_64、GCC 15.2.0、Phase 1の検証済みBazelisk 1.30.0 / Bazel 9.0.2を再利用した。

| パッケージ/検出 | 結果 |
|---|---|
| libfcitx5core-dev / utils-dev / config-dev | dpkgで3件とも`5.1.19-1`、`install ok installed` |
| `pkg-config --modversion Fcitx5Core Fcitx5Utils Fcitx5Config` | すべて`5.1.19` |
| `pkg-config --cflags --libs`（上記3件） | Core/Utils/Configのincludeと`-lFcitx5Core -lFcitx5Utils -lFcitx5Config`を正常検出 |
| `pkg-config --exists Fcitx5Module` | 終了1。`Fcitx5Module.pc`と`/usr/include/Fcitx5/Module`は未提供。製品移植再開時にmodule依存の要否・承認範囲内の対応を再確認する。今回の診断ターゲットはFcitx5に依存しない |

バージョン・pkg-config引数・3つの.pcのSHA-256を`P2/logs/environment.json`へ記録した。ユーザーが手動導入した依存を再導入していない。

固定参照元のディレクトリ一覧とblob SHA、および以下2ファイルの本文をGitHub接続ツールで読み取り、`P2/logs/reference-sources.json`に保管した。**調査のみであり採用/コピー済みファイルではない。**

| 調査ファイル | 元blob SHA-1 | 確認事項 |
|---|---|---|
| [mozc_ipc_client.cc](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/mozc_ipc_client.cc) | `d11f3b00253c2633b8f6954ab4dff0127ceb4411` | ClientFactoryのclientを直接呼び出す同期wrapper。独立したdeadline/取消の実装はない |
| [BUILD.bazel](https://github.com/fcitx/mozc/blob/384a85cf2f5db982e64c5c33cade66b2a5e5176c/src/unix/fcitx5/BUILD.bazel) | `2c499d373be8cdbc8d781f95ad8e93363ce7d429` | IPC/directの切替と共有ライブラリ構成。direct方式は採用しない |

外部ソース・画像・翻訳を製品へ移植していないため、採用ファイルのライセンス/THIRD_PARTY_NOTICES一覧は未作成。後続では計画に記録されたLGPLを含めファイルごとの確認が必要。新規診断コードはMozkey contributorsによるBSD-3-Clause表記を付した。

## 工程1の設計確認と限定検証

### 実コードから確認した待機・所有権の不足

| 経路 | 根拠 | ゲートへの影響 |
|---|---|---|
| 受信・送信 | `src/ipc/unix_ipc.cc`の`RecvMessage`/`SendMessage`はループごとに同じtimeoutでselectし直す。socketはblockingで生成 | 要求全体の絶対deadlineにならない。断片の到着が続くと待ちが累積。select後のblocking送信も別途制御が必要 |
| 接続 | `IPCClient::Init`は`Call(timeout)`より前にkey読取り・blocking connect・peer検証を行い最大2回試す | `set_timeout`では接続の全体時間を制限できない |
| 起動 | `src/client/server_launcher.cc`は20秒のready待ちと1秒間隔の最大20回ping、別の待機分岐を持つ | 起動準備をそのままイベント処理内へ置けない。今回は実行せず静的確認のみ |
| 再接続/版違い | `src/client/client.cc`の`CheckVersionOrRestartServerInternal`はShutdown/ForceTerminate/Wait/EnsureConnectionへ分岐 | timeoutを1回設定するだけでは複数往復・再起動全体を制限できない |
| 破棄 | `Client::~Client`がtimeoutを1秒に上書きしてDeleteSession | 呼出元で100msを設定しても、破棄の250ms復帰を保証できない |
| 強制終了 | `src/ipc/ipc.cc:TerminateServer`はserver_pathなしで接続し、key内process_idへsignalを送る。launcherのWaitは10秒設定 | Mozkey専用のpeer/実体パス/終了対象の検証を追加する必要。今回この経路を実行していない |
| key/profile | `IPCPathManager`のPOSIX key読取りは通常stream、既存profile選択は`.mozc`優先 | 専用profileと安全なファイルopenの契約を満たす修正は未実装。所有者・symlink・特殊ファイルも扱う必要 |

### 診断の分離範囲

新規`//ipc:ipc_deadline_probe`は**手動指定のtestonly診断実行ファイル**で、既定ビルド/テストへ追加していない。Linux専用とし、productionのIPC/clientをリンクして測定する。Fcitx5自体は起動・ロードしない。

- 新規作成した本人所有0700の空profileを必須にし、`SystemUtil::SetUserProfileDirectory`で注入する。HOME/XDG/TMPDIRもP2内へ向けた。
- IPC鍵は合成profile内に新規生成。ランダム鍵の抽象Unix socketをこの診断プロセスだけがlistenし、peerの実行パスは`/proc/self/exe`の実体と照合する。既存`.session.ipc`や他IMEのsocketへ接続しない。
- launcherをfakeに差し替え、実server起動を行わない。終了・wait・fatal dialogへの予期しない到達はfixtureエラーとする。入力は固定の`fixture`と合成protobufのみ。
- fake peerの作業threadはsocketの応答だけを生成する。測定対象のIPC/client呼出しは診断の主threadで同期実行する。GUI/InputContext操作はない。
- 外側runnerの20秒watchdogはこの診断子プロセスだけを対象とする。Fcitx5の250ms復帰保証の代わりとして数えない。

### コマンドとビルド結果

以下はP2のローカルrunnerで実行した内容の要約。`PATH=$P1/tools/bin:/usr/bin:/bin`、`USE_BAZEL_VERSION=$P1/downloads/bazel-9.0.2-linux-x86_64`、`BAZELISK_HOME=$P2/bazelisk-cache`、`ANDROID_NDK_HOME=''`を当該プロセスへ指定した。

```sh
# cwd: P2/checkout/src。外部依存の本体ダウンロードを禁止してキャッシュを再利用
bazelisk --output_user_root="$P2/bazel-output" --batch build \
  //ipc:ipc_deadline_probe --config release_build --jobs=2 \
  --repository_cache="$P1/repository-cache" --disk_cache="$P1/disk-cache" \
  --repository_disable_download --repo_env=PKG_CONFIG_PATH=

# 新しい本人所有0700の空ディレクトリを用意し、合成profileへ指定
profile=$(mktemp -d "$P2/tmp/gate-profile-XXXXXXXX")
HOME="$P2/home" XDG_CONFIG_HOME="$P2/config" XDG_DATA_HOME="$P2/data" \
XDG_CACHE_HOME="$P2/cache" TMPDIR="$P2/tmp" \
DBUS_SESSION_BUS_ADDRESS='' DISPLAY='' WAYLAND_DISPLAY='' \
  ./bazel-bin/ipc/ipc_deadline_probe "$profile"
```

最初のsandbox内BazelはNetworkMetricsCollectorのNullPointerExceptionで終了37。権限昇格の確認を経て同じ監視付きコマンドをsandbox外で再実行し、終了0。初回成功のBazel実時間27.325秒、1,093 actionsのうち756 disk-cache hit。fixtureの空profile検査等を確定後、再ビルドも終了0（runner経過12.904秒）。jobs=2、辞書再生成・外部依存本体取得なし。

sandbox内の診断はsocket bind不可でfixture終了1。別途権限昇格の確認を経て合成profile/fake peerだけの同一診断をsandbox外で実行し、次の測定を得た。診断の**終了2は受入ゲート未達を示す設計上の終了コード**であり、成功コードに読み替えていない。

| ケース | 条件 | 同期呼出し実測 | 結果 |
|---|---|---:|---|
| 即応 | timeout=100ms、即時応答 | 0.089372ms | 通常100ms目標以内 |
| 無応答 | timeout=100ms、peerは350ms無応答 | 100.208ms | Call=false、障害復帰250ms以内。peer終了待ちは測定外 |
| 断片応答 | timeout=100ms、60ms間隔で1 byteずつ6回、その後EOF | 360.775ms | **Call=trueで遅い応答を返し、100msの要求全体目標・250msの障害復帰とも未達** |
| client破棄 | 合成session作成後。事前set_timeout=100ms、DELETE_SESSION応答を350ms遅延 | 350.146ms | **250msの破棄/障害復帰目標未達**。destructorの1秒への上書きと整合 |

1回の決定的な故障注入で停止条件を満たしたため、閾値を変更したり再試行で良い数値を選んだりしていない。実Fcitx5のevent-loop dispatch時間、実serverの通常性能、cold start時間は未測定。この結果は、同期adapterの呼出しをイベント処理へ置いた場合に当該時間を費やす根拠であり、実IMEの測定を装うものではない。適用/commit・世代管理は未実装なので、遅いprotobuf応答から実入力へ確定が起こるかは未検証。

## 再設計レビュー用の対処案

計画の「直列IPC workerとFcitx5への完了通知」を具体化する候補は次のとおり。**提案段階であり、今回の停止後に製品実装していない。**

1. Fcitx5側は要求番号・InputContextのwatch参照/UUID・epoch・session世代を持つレコードを発行する。Clientは直列workerだけが所有し、UIから同時アクセスしない。起動/再接続待ちの間は準備状態を表示して新規キーをpass-throughする。
2. workerへ移すだけでは破棄時joinやkey読取りの永久待ちは残る。専用transportにCLOCK_MONOTONIC相当の絶対deadlineと取消fdを持たせ、nonblocking connect/send/recvの各pollへ**残り時間**を渡す。同一要求のsession作成・設定・retryも同じ予算に含め、受信サイズを制限する。通常要求100ms/障害復帰250msの目標は維持する。
3. profileをMozkey専用へ一括分離し、directory/key/mutexを本人所有・private権限で扱う。keyは安全なopenとfstatでsymlink・FIFO等の特殊ファイル・別所有者を拒否する。実行時探索先は`/usr/lib/mozkey`に固定し、stagingパス/PATH/他製品へfallbackしない。
4. 起動/ready待ちはUI外で有限状態として処理する。版違いはまずfail closedとし、自動的な別server終了は行わない。終了が必要な場合もpeer UID・実行パス・実peer PID・対象所有権を照合し、key内PIDだけを終了根拠にしない。addon終了は取消後に期限付きで終了できる構造にする。単なるdetachによるworker/Client放置は採用しない。
5. 完了通知はFcitx5のevent loopへ戻し、生存・focus・現在IME・epoch・要求番号が一致した応答だけを共通描画経路へ適用する。取消/切替後の応答は破棄する。結果不明のキー/commitは再送しない。通信失敗時のpreedit保持と取消/再入力UI、consumed=falseの1回だけのpass-throughを設計する。
6. 再開時は、今回の断片応答・破棄に加え、connect queue飽和、特殊keyファイル、起動失敗、版違い、全往復retry、focus out、再接続、worker/addon終了をfakeで検証する。UI dispatch時間と要求全体時間を別計測し、取消・古い応答・重複commit・他IME不干渉を検証してから工程2〜6へ進む。

## 工程・受入条件

| 工程 | 結果 |
|---|---|
| 1 依存/由来、ビルド設定とdeadline/終了契約 | 依存検出・固定参照元/PR再照合・診断ターゲットと待機/所有権調査・故障注入まで実施。製品用設定と完全な移植manifestは未作成。A5未達により停止 |
| 2 profile/名前/IPC分離と負試験 | 未実施 |
| 3 選択移植と.so/server/toolビルド | 未実施 |
| 4 callback/寿命/障害試験 | 未実施（今回のtransport故障注入だけでA4合格とはしない） |
| 5 入力/候補/周辺テキスト/専用設定 | 未実施 |
| 6 製品staging/ELF/回帰/2B実行手順 | 製品作業は未実施。今回の停止結果のみ文書化 |

| 条件 | 判定 | 証拠・残件 |
|---|---|---|
| A1 基点・由来 | 一部確認、未達 | main/参照元SHA、依存・辞書hashを確認。採用ソース/ライセンスmanifestと製品差分は未完成 |
| A2 独立性 | 未検証 | 製品ID/profile/IPC/export分離とT10負試験は未実装 |
| A3 ビルド | 未達 | 診断実行ファイルだけビルド成功。専用.so/server/tool/stagingとELF/梱包検査は未実施 |
| A4 callback | 未検証 | callback/IC/clock/候補寿命の実装・fake試験は未実施 |
| A5 IPC応答性 | **未達・停止** | 断片応答360.775ms、client破棄350.146ms。接続/launcher/終了も経路全体の制限が未実装 |
| A6 既存回帰 | 未検証 | 製品設定ON/OFF、既定package・28ケース、関連回帰は今回未実施。診断以外の実装がない段階で停止したため再ビルドしない。Phase 1成功証拠は保持 |
| A7 資産/ホスト保護 | 今回の実施範囲で合格 | 下記hash照合・予算内。ホストIME/設定への接続・変更なし |

2B/2C/3、A8〜A12、実入力、Hazkeyとの同時ロード、他OS/CI、製品の移動後実行は未実施。製品manifestと初期試験の前提がないため、実行可能な導入/staging手順は提示しない。再開後にA1〜A7を満たし、2B環境・取得/導入/起動の明示承認を受ける必要がある。

## 保護・資源予算

元作業ツリーのステージ済み5ファイルは開始/終了時のbinary diff SHA-256がともに`d2f1169c16923e8faf6706bf3500608dfe61f02727258e961a5cabe17e1e3a30`で一致。未ステージ追跡差分は両時点とも空。`.local/`を保持し、stash/reset/上書きはしていない。

Phase 1のdownloads、既存logs、展開成果物、generated配下の計76ファイルを開始/終了でSHA-256照合し、変更0。daily辞書4点は[Phase 1結果](phase-01-results.md)の記録hashと全一致、mozc.zipも`1228c8ccd9f3c7c833c61265a3c00f86ec187f721bfb8b91a8b2fcfa7c0de066`のまま。Phase 1 checkoutの追跡変更は0。bazel-output/repository-cache/disk-cacheは存続し、削除・cleanはしていない。repository/disk-cacheは診断ビルドに再利用しており、キャッシュ全ファイルの不変性までは主張しない。

記録時点のworkspace実割当は5,203,832,832 B（約4.85GiB）、60GiB以内。各ビルドは2秒間隔の容量・受信量監視付きで、全体59GiBまたは新規受信が2GiB-64MiBへ達したら子プロセス群を停止する設定、監視停止なし。新規大規模出力はP2内の作業ディスクに置いた。

全非loopback interfaceの受信カウンタ合計は906,215,370 B（約0.844GiB）で、作成以来の全受信を含めても2GiB以内。baseline記録後の増分49,035,302 Bには他アプリ通信も含む。これらはアプリ単体の厳密な取得量ではない。GitHub接続ツールによる参照元/PRの少量テキスト取得は別経路であり、`P2/logs/reference-sources.json`に保管した応答本文は2GiB予算に対し十分小さい。Bazelは外部依存本体の取得を禁止して既存cacheから成功、辞書/モデルの取得・再生成は0。

## 変更・成果物・Git状態

変更対象は5ファイルに限定した。

- `src/ipc/BUILD.bazel`：Linux専用・manual・testonlyの診断ターゲット。
- `src/ipc/ipc_deadline_probe.cc`：合成profile/fake peerを使う実transport/clientのdeadline診断。
- `docs/ubuntu/phase-02-results.md`：本書。
- `docs/ubuntu/phase-02-plan.md`：冒頭に今回の実行承認と停止状態を追記。技術契約・数値目標は変更しない。
- `PLANS.md`：今回の承認・2Aの停止/レビュー待ちを反映。

製品用の.so/server/tool/staging/archiveはない。診断成果物は`P2/checkout/src/bazel-bin/ipc/ipc_deadline_probe`、2,897,320 B、SHA-256 `4bc30ad6ea030f5506f7e0c3ade58f8438825399cbf7759443e3fc588b4a4224`。導入用成果物として扱わない。

ローカル証拠は`P2/logs/`の`baseline.json`、`reference-sources.json`、`environment.json`、`initial-gate-build*`、`gate-build*`、`ipc-deadline-probe.jsonl`/`.stderr`/`-run.json`、sandbox失敗ログ、`diagnostic-artifact.json`、`protection-check.json`。監視付きrunnerは`P2/run_gate_build.py`、診断runnerは`P2/run_gate_probe.py`。生ログ・生成profile・キャッシュ・実行ファイルはGit登録しない。

最終確認で`git diff --check`、`git diff --cached --check`、文書ローカルリンク/コマンド構文の検査を行い、上記5ファイルだけを明示してGit indexへstageした。HEADは基点のまま、commit/push先はなし。元作業ツリーの既存変更は保持。レビュー後の再開指示を待って停止する。

## A5最小修正・再測定の追記（2026-11-05）

**最新判定：既知のsocket待機と破棄の数値目標は限定試験で達成。ただしA5全体は保留、従来の未達を解除せず停止・レビュー待ち。**

ユーザーが、調査済み最小修正案の実装・限定テスト・再測定を追加承認した。対象は既存`feat/ubuntu-phase2a`、HEADは`b489fca1561bf12ff6b43205378220163d71c21c`のまま。今回はA5の期限付き同期経路のみを実装した。Worker非同期化、工程2〜6、IME/server起動・登録、ホスト設定変更、sudo/apt、2B/2C/3、commit/push/merge/rebase/PRは実施していない。以下のP1/P2は本書冒頭の作業領域を指す。

### 実装と限定範囲

既存の全利用者へtimeoutの意味変更を波及させないため、Linux専用の明示的な`Client(LinuxFcitxOptions)`と専用transportを追加した。従来constructor、Linux serverの送受信helper、Windows/macOS transport、既存launcher、serverのsession削除/回収コードは変更しない。

- `LinuxDeadline`は`std::chrono::steady_clock`を使用。外側の操作で一度だけ最大100msの期限を作り、profile/key確認、最大2回の接続試行、CREATE_SESSION、SET_REQUEST、キー/コマンド送信へ同じ期限を渡す。nested RPCごとの予算更新はしない。時計はfakeへ注入可能。
- 専用transportはnonblocking connect/send/recvと残り時間によるpollを使う。AF_UNIXのqueue-full EAGAINは有限の接続試行として失敗を返す。部分送受信・EINTR/EAGAINの再試行でも期限を更新しない。期限切れの受信内容を破棄し、request/responseは1MiB上限。wire形式は変更しない。
- profileと実行パスを必須の明示値とし、HOME/PATH/profileのfallbackを行わない。directory fdでsymlinkを拒否してprofileへ到達し、最終directory/keyの本人所有・private権限、keyの通常ファイル/サイズを検証する。keyのopenはO_NOFOLLOW/O_NONBLOCK。SO_PEERCREDのUID/PID、key内PID、`/proc/PID/exe`の実体パスを照合する。IPC prefixなどの製品ID移行は工程2のため未実施。
- Fcitx5用constructorは既存ConfigHandlerによるprofile読取りやdefault launcherの構築を行わない。通常入力から起動・ready待ち・Shutdown/ForceTerminate/Wait・fatal dialog・tool/browser起動を排除し、版違いは送信前に拒否する。通信失敗後の自動再送・履歴再生・session再生成を行わない。復旧には明示的に準備した新clientが必要であり、起動/復旧の製品フローを実装完了とはしない。
- session作成/SET_REQUESTの失敗と応答ID不一致を伝搬し、期限切れ/不正/遅いOutputは呼出元へ公開しない。失敗はfalseと空Outputで返す。実InputContextのpreedit保持・取消UIは工程4/5に属し、今回未実装・未検証。
- `CloseLinuxFcitxSession`は正常時にDELETE_SESSIONを送り、応答を確認する。終了済みフラグとローカルsessionの切離しを先に行い、明示Closeとdestructorで二重送信しない。故障時も起動/復旧せず接続込み100msで打ち切る。同じaddonのclient群には共有`LinuxFcitxCloseGroup`を渡し、一括破棄の**前にBeginCloseを呼ぶ**ことで全体を一つの100ms予算へ制限する。個別破棄を独立したgroupで繰り返しても累積上限は保証されないため、将来のaddon側でこの契約を守る必要がある。

### 限定テストと再測定

監視付きrunnerは`P2/a5-retry/run_retest.py`。jobs=2、専用HOME/XDG/TMPDIR、Phase 1の固定Bazelisk/Bazelとcacheを再利用し、`--repository_disable_download`を維持した。辞書・モデルの取得や再生成はない。

```sh
# cwd=P2/checkout/src。実IME/serverを起動せず、合成profile/fake peerだけで実行
bazelisk --output_user_root="$P2/bazel-output" --batch test \
  //ipc:linux_deadline_test //client:client_test //ipc:ipc_test \
  --config release_build --jobs=2 --test_output=errors \
  --nocache_test_results --test_timeout=60 \
  --repository_cache="$P1/repository-cache" --disk_cache="$P1/disk-cache" \
  --repository_disable_download --repo_env=PKG_CONFIG_PATH=
```

初回sandbox内のBazelは従来と同じNetworkMetricsCollectorの内部エラーで終了37。sandbox外の同一監視付き実行で再開した。初回コンパイルでは新規テストのGoogleTest namespace指定誤りがあり修正した。最終ソースでは上記コマンドが終了0、Bazel実時間25.675秒、runner経過30.323秒。既存テスト側のprotobuf戻り値未使用warningは残るが失敗はない。

| ターゲット | ケース数 | 最終結果 |
|---|---:|---|
| `//ipc:linux_deadline_test` | 21 | 全成功。fake clock、実socketのfault注入と再測定 |
| `//client:client_test` | 30 | 全成功。既存の版違い・復旧・履歴再生を含む既定動作の回帰 |
| `//ipc:ipc_test` | 1 | 成功。既定Linux transportのecho/複数接続/データサイズ試験 |
| 合計 | 52 | failure/error/disabled=0 |

新規試験では、CREATE→SET_REQUEST→KEYの累積deadline、SET_REQUEST失敗/ID不一致時の中断、失敗後の再送/履歴再生禁止、遅い有効応答/不正/過大応答の排除、版違いとpeer PID変更、正常な削除・二重Close、64clientのfake一括破棄、key symlink/FIFO/public directory拒否とsentinel不変を確認した。

| 実socket測定（接続前から計測） | 最終実測 | 判定・範囲 |
|---|---:|---|
| 正常CREATE/SET_REQUEST/KEY、20回の最大 | 0.456188ms | 通常要求100ms以内。各回の正常DELETEも確認 |
| 60ms間隔の断片応答 | 100.387934ms | 旧360.775msから改善、Call=false・出力破棄、障害250ms以内 |
| DELETE応答350ms遅延、client破棄 | 100.210135ms | 旧350.146msから改善、障害250ms以内 |
| 20client一括破棄、最初のDELETEを遅延 | 100.247953ms | 共有終了予算、累積待機を制限 |
| CREATE/SET_REQUEST/KEYを各40ms遅延 | 100.821078ms | 単一予算で最後の要求を打切り、出力非公開 |
| 無応答 | 100.244915ms | 再送なし、障害250ms以内 |
| 切断 / 不正応答 | 0.271021 / 0.202769ms | 失敗伝搬・出力破棄 |
| 1MiB送信中のbackpressure | 100.417282ms | blocking sendを回避、障害250ms以内 |
| 接続queue飽和 | 0.110547ms | 有限試行で拒否、アプリ要求送信0 |

論理予算は100msのまま。pollの丸め・スケジューリングにより故障呼出しの実復帰が100msをわずかに超える値もそのまま記録し、100ms以内だったとは扱わない。通常要求100ms・障害復帰250msの目標は変更していない。faultのdispatch値は診断呼出しから次のfake dispatch境界までであり、実Fcitx5 event loopを測定したものではない。

### A5を保留する理由と次の設計レビュー点

**socketに対するdeadlineだけで、すべての同期ブロックを取消可能にはできない。** 専用transportのdirectory/keyに対するopenat/fstat/read、およびpeer検証のreadlinkは期限の前後確認を行うが、regular filesystemやカーネル内で停止したsyscall自体を期限で中断できない。O_NONBLOCKはFIFO/通常socketの安全性には役立つが、regular fileのI/O停滞全般を100ms以内へ制限する仕組みではない。今回、その停滞の実注入・最大待機を証明する測定は行っていない。この既知の制御不能点を通常入力経路に残したまま、たまたま良い計測値を得たことだけでA5合格にしない。

また、server起動・復旧を通常入力から切り離して拒否する境界は実装したが、UI外での準備・再接続を完了させるlifecycle、実InputContextでの未確定文字列保護、addonからの共有終了group利用は未統合。実serverの通常性能・cold start・データ同期時間、実Fcitx5のdispatch、Windows/macOS実行は未検証。EINTRの処理はコードにあるがsignalによる実注入試験は未実施。

DELETE打切りでは、serverが削除済みか未実行か不確定となる場合がある。正常な削除とserver既存の回収機構は維持したが、故障時の残存sessionが即時回収される保証はない。既存watchdogの5分/60分等の回収条件と最大64sessionの制約は変更していない。Fcitx5自体が生存していると、入力欄破棄だけではプロセス死亡による回収にはならない。多数の故障時破棄で残存が蓄積する可能性は残る。

再設計候補は、検証済みendpoint/metadataを通常入力の外で準備して入力経路からfilesystem I/Oを除去し、準備中は送信しない設計と、その準備・起動/復旧の寿命/取消/所有権を定めること。Workerは代替案として残すが、今回実装していない。100ms/250msの緩和や通常入力への同期起動待ちの復活で合格扱いにせず、ここで停止する。

### 変更・証拠・保護・Git状態

今回の追加差分は9パス：`src/client/{BUILD.bazel,client.h,client.cc,linux_fcitx_client.cc}`、`src/ipc/{BUILD.bazel,linux_deadline.h,linux_deadline.cc,linux_deadline_test.cc}`、本書。新規4ファイルはLinux用実装/試験であり、Fcitx5アドオンやID/profile移行・製品梱包を作成していない。

Linux用guardを除いた既存`client.cc`がHEADと完全一致すること、`unix_ipc.cc`/`mach_ipc.cc`/`win32_ipc.cc`/`server_launcher.cc`/`session_handler.cc`/`session_watch_dog.cc`がHEADから無変更であることを確認した。非Linuxへ新しいLinux依存を要求しないBUILD分岐としたが、非Linuxのビルド/実行成功を主張しない。

開始時のステージ済み5ファイルを`P2/a5-retry/baseline/`へ保管し、Git indexも変更していない。index差分SHA-256は開始/終了とも`e1d3f87b6059ce478af6c8941eeac79e9cb4b9bb89865b9c9e2dce5294c5eacc`。今回分は未ステージとして残し、全体レビューは`git diff HEAD`と新規ファイルを対象にする。元worktreeのステージ済み差分とPhase 1保護対象76ファイルのhash一致、既存診断実行ファイルのhash一致、3 cache directoryの存続を再確認した。既存測定jsonl・成果物・成功ログは上書きしていない。

保護確認時点のworkspace実割当5,241,929,728 B（約4.88GiB）、再開後の全非loopback受信増分56,228,622 B、interface作成以来の総受信上界1,037,645,257 B。全体60GiB・新規取得2GiB以内、監視による停止なし。Bazel依存本体の新規取得、辞書/モデル取得は0。

証拠は`P2/a5-retry/logs/`の`retest.log`、`retest-metrics.json`、`attempt1-*`/`attempt2-*`、`test-summary.json`、3ターゲットの`*-test.log`/`*-test.xml`、`protection-check.json`。最終差分の空白・文書参照/コマンド構文とGit状態を確認して終了。commit/push/merge/rebase/PRは0、工程2〜6へ進まずレビュー待ちとする。

## A5残存ブロックの分離検討・停止境界検証（2026-11-05）

**最新判定：A5未達。安全な限定非同期案を本体へ採用する段階には至らず、故障注入と設計評価の結果を報告して停止する。**

ユーザーが、残るfilesystem/readlink/mutex等の調査、安全な最小構成の検討・実装・再検証を追加承認した。一方、安全な実装が成立しない場合、設計変更が大規模になる場合、またはA5未達の場合の停止を指定した。今回、検証処理だけをthreadへ移す単純案を診断内で試作・測定したが、破棄待機の条件を満たさなかった。実運用のthread/helper processは追加していない。承認範囲を超える工程2〜6・IME起動等へ進めていない。

### ブロック経路の再監査

| 経路 | 現状・判断 |
|---|---|
| `ReadKey` | `open`/`openat`によるdirectory/key探索、`fstat`、`read`。前後のdeadline確認ではsyscall内部の停滞を中断できない。FIFO/symlink等の拒否は維持 |
| peer実行パス検証 | 接続後の`readlink(/proc/PID/exe)`。SO_PEERCREDのUID/PID・key内PID・実体パスの検証を省略して解消する案は不採用 |
| mutex | 現在の専用transportはIPCPathManagerやProcessMutexを呼ばず、独自のmutexも持たない。従来経路のmutexを追加したり、thread間の結果受渡しに無期限mutex待ちを持ち込んだりしない |
| client構築 | default ConfigHandler/launcherを回避。既定キー取得は`system://`の組込keymapを読む経路。今回調べた既定経路ではprofileファイル読取りはない |
| version情報 | `Version::GetMozcVersion`は組込version文字列を返す。今回のfilesystem停滞とは別 |
| 破棄 | 正常なDELETE_SESSIONにも新しい接続・検証が必要なため、同じfilesystem停滞が破棄予算を超過させ得る |

### 評価した最小構成と採用しない理由

1. **検証だけを別threadへ移し、終了時にjoinする案**：key/peer検証は分離できるが、syscallが戻らなければstop要求だけでは終了しない。診断内のjthread試作で取消後の破棄待機が250msを超えた。`future`/timerの使用だけでも、この終了待機問題は解決しない。
2. **joinせずdetachする案**：InputContextへの反映を世代/weak参照で止めても、実行中のコード、addonのunload、FD、ジョブ上限と最終回収の寿命が残る。所有者を失ったthreadを放置する構成は採用しない。
3. **検証専用helper processへ分離する案**：IPCのキー送受信までWorkerへ移さず、検証済みの接続socketだけを親へ渡す候補。ただし、起動処理自身をUIでblockさせない手段、信頼できるhelperの配置/実行契約、同じ接続FDに対する検証証拠、FD受渡し、要求世代と取消、所有processだけの終了、非ブロックの終了回収、実I/O停止で終了未完了の場合の資源上限まで一体で設計・試験する必要がある。現行の最小修正へ局所的にthreadを足す規模を超えると判断し、今回の限定実装として独断で採用しない。

process方式を続ける場合、PID再利用を避ける所有権管理には[CLONE_PIDFD](https://man7.org/linux/man-pages/man2/clone.2.html)等と[pidfdによるsignal送信](https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html)を検討し、multithreadedなFcitx5から子processを作る場合はexecまでの処理を[async-signal-safeな操作](https://man7.org/linux/man-pages/man7/signal-safety.7.html)へ限定する必要がある。これらは参照した設計候補で、実装・動作確認済みの機構ではない。

### 故障注入と52ケースの再検証

追加した`//ipc:linux_blocking_boundary_probe`はLinux専用・manual・testonlyの診断実行ファイルである。既存のfactory注入境界に350msの停止を置き、期限を確認する位置へ戻れない状況を再現する。**実filesystemやreadlinkを実際に350ms停止させた試験ではない。** profile/keyとpeer検証の2行は、同じfactory境界で別々に実行したモデルであり、各syscallの個別実測として数えない。実IME、server、個人profile、socketへは接続しない。

| 診断ケース | 実測 | 判定 |
|---|---:|---|
| profile/key取得を模したfactory停止 | 350.121ms | false・空Outputを返すが250ms復帰未達 |
| peer実行パス取得を模したfactory停止 | 350.168ms | false・空Outputを返すが250ms復帰未達 |
| 正常session作成後、Close中の検証停止 | 350.112ms | Close=false、破棄予算未達。二重Closeは起こさない |
| 検証threadの停止要求後のjthread破棄 | 350.551ms | joinの待機により250ms復帰未達 |

診断の終了コードは2（ゲート未達）。遅い結果を公開しない安全条件は確認したが、時間条件を満たしたとはしない。診断内のsleep/jthreadは不適合案を示す有限のfixtureであり、本体へ追加した非同期Workerではない。外側runnerの10秒watchdogは当該診断子processだけを対象とし、A5の250ms条件の代用にはしない。

既存52ケースはソースを変更せず、`P2/a5-isolation/run_retest.py`で再実行した。従来の3テストターゲットに新しい診断ターゲットのビルドを加え、jobs=2・release_build・nocache_test_results・test_timeout=60・repository_disable_download・専用HOME/XDG/TMPDIR・予算監視を維持した。初回sandbox内のBazelは既知のNetworkMetricsCollectorエラーで終了37。sandbox外再試行の自動承認審査が一度timeoutしたが、許された1回の再試行で実行できた。最終build/testは終了0、Bazel実時間26.976秒、runner経過40.772秒。

| 回帰ターゲット | ケース数 | 結果 |
|---|---:|---|
| `//ipc:linux_deadline_test` | 21 | 全成功 |
| `//client:client_test` | 30 | 全成功 |
| `//ipc:ipc_test` | 1 | 成功 |
| 合計 | 52 | failure/error/disabled=0 |

通常要求20回の最大0.442881ms、断片応答100.999983ms、単一client破棄100.525715ms、20client一括破棄100.452359ms、session/設定/キー累積100.388051ms、無応答100.379563ms、送信backpressure100.469345ms、queue飽和接続0.121954ms。これらは既知のsocket故障試験の値であり、上の検証処理停止ケースを除外してA5合格にはしない。実Fcitx5 loop・cold start・実serverは今回も起動せず未検証。

### A5全条件の判定

| 条件 | 判定 |
|---|---|
| 通常要求全体100ms | 合成peerの正常20回では達成。検証処理の停止を含む一般経路の保証は未達 |
| 障害時loop復帰250ms | socket故障のfake dispatchでは達成。検証停止350msにより**全体では未達** |
| 起動/再接続 | 通常経路で他製品を起動/終了しない既存境界は保持。UI外で準備から復旧まで完了するlifecycleは未実装 |
| 取消/破棄/終了 | Output非公開・二重破棄抑止・既存の共有終了予算は保持。検証処理停止と検証thread joinの時間条件は未達 |
| InputContext寿命/未確定文字列/重複確定排除 | 今回はUIへ結果を返す新callbackや本体Workerを追加していない。実InputContext統合は未実装・未検証であり、完了扱いにしない |
| 接続先検証/他IME不干渉 | 既存の検証を変更・省略していない。fakeの負試験は再成功。実ホスト共存は未検証 |

**A5未達を受け、今回の指示の停止条件を適用する。** 次の候補は検証専用processの配置・起動・FD受渡し・取消・回収をまとめた設計レビューである。検証省略、待機閾値の緩和、detachによる資源放置で先へ進めない。

### 変更・保護・証拠

今回の変更は`src/ipc/BUILD.bazel`のmanual診断ターゲット、`src/ipc/linux_blocking_boundary_probe.cc`の新規診断、本書への追記だけ。本体のdeadline/接続先検証/client/既存21テストは変更していない。

開始時のステージ済み差分・未ステージ差分・9パスの本文・直前のテスト実行ファイル/ライブラリは`P2/a5-isolation/baseline/`へ保管した。ステージ済み5ファイルのindex差分は不変。以前の未ステージ9パスのうち7パスは本文hash不変で、BUILDには新規ターゲット、本書にはこの節だけを追加した。既存結果書本文・旧ログを上書きしていない。Phase 1保護対象76ファイルのhash、元worktreeのステージ済み差分、3 cache directoryの存続も再確認した。

記録時のworkspace実割当5,249,105,920 B（約4.89GiB）、今回baseline後の全非loopback受信増分51,828,484 B、interface作成以来の受信上界1,185,266,161 B。60GiB/新規取得2GiB以内で監視停止なし。辞書・モデル取得/再生成、依存本体の新規ダウンロードは0。

証拠は`P2/a5-isolation/logs/`の`retest.log`/`retest-metrics.json`、`test-summary.json`、3ターゲットの`*-test.xml`/`*-test.log`、`blocking-boundary-probe.jsonl`/`.stderr`/`-run.json`、`protection-check.json`。runnerは`run_retest.py`/`run_probe.py`。HEADは基点のまま、今回の変更も未ステージ。commit/push/merge/rebase/PR、IME起動、sudo/apt、ホスト設定変更、工程2〜6/2B/2C/3は0。レビュー待ちで停止する。

## 非同期化境界の設計レビューとGit反映承認（2026-11-09）

**判定はA5未達・工程1停止を維持する。** ユーザー指定の既存worktreeで、AGENTS.md、PLANS.md、Phase 2計画、本書、専用client/transportと既存証拠を読取り、方式比較・最小実装範囲・再検証案を報告した。レビュー中はソース・文書変更、ビルド、追加取得、IME起動、システム変更、Git反映を行っていない。既存52ケースの成功記録を参照したもので、今回の再実行結果ではない。

### 設計上の結論

| 方式 | 評価 |
|---|---|
| 事前検証・準備状態への分離 | 入力経路の処理削減に必要だが、準備処理自体の実行場所・取消・回収は別途必要。metadataのキャッシュだけでは新しい接続の検証にならない |
| 専用Worker thread | UIから待ちを移せるが、停止中のfilesystem I/Oをstop tokenで終了できず、join/unload待ちが残る。detachによる放置は採用しない |
| 検証専用helper process | 最小候補として条件付き推奨。profile/key読取り・接続・peer検証を隔離し、検証した同じsocket FDを親へ渡す。既存deadline付き送受信と同期キーAPIを維持できる |

通常キーだけでなく、CREATE/SET_REQUEST、readiness/管理RPC、DELETE/一括破棄が同じfactoryへ到達する。open/openat/fstat/read/readlinkに加えてFD終了も隔離境界へ含める。専用transportにIPCPathManager/ProcessMutexの利用はないが、従来経路へのfallbackを復活させない。共通CreateSessionの失敗ログにも到達するため、同期ログsinkの出力・ロック待ちを追加の監査・故障注入対象とする。既定keymapは組込データであり、個人profileの読取りとは区別する。

helperの検証依頼・FD受信・CREATE/SET_REQUEST/KEY・送受信は同じ外側の100ms期限を共有し、親はnonblocking制御channelを残時間だけ待つ。helper未準備・回収待ちは準備状態へ戻し、新規キーを待たせない。SCM_RIGHTSのFD移譲、要求番号・helper/endpoint世代・期限を照合し、取消後・不正な受信FDは閉じる。UID/PID、key、実行パス、protocol/product versionの検証は省略しない。InputContext/UIへhelperから直接アクセスせず、通知時に生存・focus・epoch等を再確認する。結果不明のキー・commitを再送しない。

一接続一要求のprotocolと直列server loopにより、未使用接続の大量先行準備は後続要求を妨げ得る。全IPC基盤やserver protocolの再設計は提案範囲に含めない。Linux限定targetと明示opt-inを維持し、Windows/macOSの実行成功は未検証とする。

### 実装前に解決すべき事項と再検証案

最大の設計ゲートは、helper起動でUIを止めない方法と、割込み不能I/O中のhelperをaddon unload後も所有・回収する責任主体である。pidfd等による自分のhelperだけの識別、非ブロック回収、有限のhelper/要求数が必要。未回収helperの代替を増殖させない。これが新しい監督基盤なしで成立しなければ、最小修正案として停止する。親の250ms復帰とprocess回収完了は区別する。DELETE結果不明時のserver session残存・最大64session問題も残る。

実装候補は`src/ipc/linux_deadline.{h,cc}`の分離、新規Linux専用helper/管理部、`src/client/linux_fcitx_client.cc`/`client.h`の接続、`client.cc`の専用失敗ログ経路、関連BUILDと限定テスト。順序は起動/回収契約の確定、実FD移譲と異製品拒否、単一deadline統合、準備/失敗/Close、故障注入と52ケース回帰とする。helper実装や工程2〜6の製品移植を開始したものではない。

再検証は各I/O境界の350ms/解除待ち、起動失敗・exec停止、検証/FD受信/各RPCの累積遅延、key/PID/peer差替え、不正・遅延FD、制御channelのbackpressure、mutex/ログ停止、IC取消・一括Close・addon終了・未回収helperを対象とする。要求全体と次dispatchまでを別々に測定し、通常100ms・障害復帰250msを維持する。実I/O停止の試験はモデル注入と区別し、分離方法・権限を別途定める。実Fcitx5/実server、cold start、preedit保護と取消UI、他OSは引き続き未検証。

### 今回のGit反映範囲

レビュー後の追加指示により、既存13パスの工程1差分を作業ブランチへコミット・プッシュすることと、今後の承認済み作業完了時に作業ブランチへ都度反映することが承認された。新しい指示をAGENTS.mdへ記録し、PLANS.md・Phase 2計画の最新承認状態と本節を整合させる。過去節のcommit/push未実施・禁止は、その時点の履歴として保持する。

今回の新規編集はこの4文書のみ。既存ソース10パスは変更せず、保存済みXMLの21+30+1ケース成功とA5停止診断を照合し、差分・空白・文書参照・Git反映範囲を確認する。再ビルド・再テストは行わず、基点mainへのmerge/rebase、PR、helper実装、IME起動、システム変更、工程2〜6/2B/2C/3へは進まない。元worktreeの変更とPhase 1資産・既存ログ/測定値を保持する。実際のcommit SHAとpush後のリモート一致は完了報告に記録する。
