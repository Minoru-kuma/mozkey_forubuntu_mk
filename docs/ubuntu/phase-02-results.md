# Phase 2 実行結果

## 現行の判定（2026-11-12追加指示）

**A5未解決でも独立工程2〜6を進める方針へ更新し、製品分離・選択移植・専用ビルド・限定試験・導入禁止stagingを実施した。2A全体は未完了で、2B移行不可。前回の「未解決だから全工程停止」は今回の進行条件ではない。**

打鍵→ひらがな300ms未満は暫定目安、ライブ変換500ms程度を許容し1秒前後も評価、通常終了のみ400msまで待機を許容する。focus移動/IME切替へ終了待ちを転用しない。旧100ms/250msは参考値で、数値超過だけで全工程を止めない。以下の旧判定に優先する実装・検証結果とA1〜A7は末尾「新A5方針と独立工程の実装」を参照。

アドオンはEnabled=Falseかつfactoryが常にloadを拒否する。生成された`.so`を入力可能な製品と誤認しない。実IME/server/tool起動、ホスト導入、既存設定/個人データ変更、sudo/apt、2B以降、main統合は0。

## 前回の判定（2026-11-12、以下は履歴）

**2A工程1〜6の実施は再承認されたが、A5の安全なworker終了・キー配送契約を確立できず、安全停止。2A未完了、2Bへ移行不可。通常の工程間レビュー待ちを理由に停止したものではない。**

`feat/ubuntu-phase2a`の開始HEADは`8f39184501cadca22666ae50e9f0533e991704cb`、開始時clean。既存worktreeを継続使用した。Fcitx5の`PreventUnloadHint`によるコード保持を確認したが、350ms停止workerを所有するfixtureの破棄は340.195ms、実イベントループの最大遅れは340.208msだった。これは要求応答が100ms/250msを超えたためではなく、終了callbackがjoinでloopを止める構造の不適合である。詳細・A1〜A7・未検証事項は末尾「2A再開時の安全ゲート」を参照。

製品ソース・工程2〜6・専用stagingは変更/作成していない。新規の共有ライブラリは一時領域の負例fixtureであり、IMEアドオンの成果物ではない。IME起動・設定変更・sudo/apt・2B以降・main統合は未実施。以下の旧「最新状態」「未承認」は各実施時点の履歴で、この節を優先する。

**現行方針：ユーザー指示でA5を入力イベントループの長時間停止防止へ再設計。100ms/250msは参考値として保持する。[新設計・受入条件案](a5-async-design.md)と末尾のPoC結果はレビュー待ちで、A5合格・製品統合・工程再開は未承認。以下の旧停止条件と実測は履歴である。**

**最新状態：helperのaddon unload後までの回収責任を確認できず、今回の明示的な停止条件によりA5の隔離実装を開始しない。既存PoCの軽量CI組込みと調査記録だけを実施し、A5未達・工程1停止を維持する。詳細は末尾を参照。**

**最新状態（2026-11-11）：helper processの限定PoCは合成条件の8ケースで成功。実検証・製品統合は未実施で、A5未達・工程1停止を維持する。詳細は末尾「helper process限定PoC」を参照。**

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

## helper process限定PoC（2026-11-11）

**判定：PoCの合成条件では時間目標を満たした。A5本実装・2A全体は引き続き未達で、工程1停止・レビュー待ち。** ユーザーは非ブロッキング起動、SCM_RIGHTS、取消、終了回収、増殖防止に限定したPoCと故障注入、結果記録、作業ブランチへのcommit/pushを承認した。基点は`feat/ubuntu-phase2a` / `bcf263b2a45da51596c4cfbdbf86a9c3004e76a9`、remote mainは`b489fca1561bf12ff6b43205378220163d71c21c`。開始時の作業worktreeはclean。

PoCは`tests/ubuntu/test_linux_helper_poc.py`だけで完結するLinux用の単一スレッド診断であり、Bazel製品target、既存client/transport、Fcitx5、実server、個人profileに接続しない。親はsocketpairを作りfork後すぐに戻り、子がexecする。helperは合成socketpairの片方をSCM_RIGHTSで送信し、親はgeneration、FD数、切詰め、CLOEXECと単一の絶対deadlineを確認する。取消はcontrol channelを閉じ、所有する直接の未回収子をpidfdでSIGKILLする。waitpid(WNOHANG)が終了を確認するまで同じownerは新しいhelperを作成しない。失効済み・過剰FDやqueued FDの破棄、通常転送後の通信、再起動とFD数の保持を試験した。

### 最終測定

`/usr/bin/python3 tests/ubuntu/test_linux_helper_poc.py`を、専用socketpair以外のIPC・ネットワークを使わずsandbox外で実行。8ケース成功、所要2.013秒。時間はmonotonic clock、呼出し開始から親の復帰まで。過去の52ケースは今回再実行していない。

| 合成条件 | 実測 | 判定 |
|---|---:|---|
| 正常fork/exec・SCM_RIGHTS転送・synthetic応答、20回の最大 | 43.832ms | 100ms以内 |
| 子のexec前に350ms停止、親のfork/start復帰 | 0.449ms | 100ms以内 |
| 上記停止中のdeadline打切り | 100.277ms | 250ms以内 |
| 検証処理に相当する箇所を350ms停止しdeadline打切り | 100.203ms | 250ms以内 |
| 上記の親復帰後からwaitpidによる子回収 | 5.199ms | 当該試験では回収成功。親の復帰時間とは別 |
| 明示取消 | 0.666ms | 250ms以内 |
| exec失敗 / helper即終了 | 1.015 / 37.946ms | 250ms以内、回収成功 |

両350ms停止の直前にhelperから到達通知を受けたこと、明示取消後の100回の起動拒否、検証待ち失敗後の回収前起動拒否と回収後再起動、異なるgeneration、余分なFD、受信前にqueueへ届いたFDの取消、20回の異常FD転送後の親FD数一致も確認。試験内の2秒回収watchdogは診断終了のためであり、250msの親復帰の代用ではない。

初回の既定`python3`はconda Pythonに`os.pidfd_open`がなく失敗。`/usr/bin/python3`ではsandboxが継承Unix socketの操作をEPERMで拒否し正常転送が失敗したため、同一試験をsandbox外で実行した。これらの失敗は最終成功と区別する。既存buildifier/IPC実装・52ケースの成功記録は変更していない。新規依存、辞書・モデル取得、Bazel再ビルド、IME起動、システム変更は行っていない。

### 限界と次のレビュー点

- 停止は`time.sleep(350ms)`によるモデルで、実filesystem、readlink、execのカーネル内停止や割込み不能D状態ではない。SIGKILL後の実際の回収期限は保証できない。
- FDはsynthetic socketpairであり、profile/key、SO_PEERCRED、peer PID・実行パス・製品IDの検証や、検証済み実接続FDの移譲を証明しない。SCM_RIGHTS前後のTOCTOU、悪意のあるhelperや各種不正ancillary dataの完全な処理は未検証。
- `fork`/`exec`は診断用の単一スレッドPython実装。forkからexecまでPythonコードが動くため、マルチスレッドFcitx5内でのasync-signal-safeな起動方式を示すものではない。fork/pidfd_open自体のカーネル待ちもdeadlineで中断できない。exec前停止は親側で待っていないことのモデルである。
- このownerは回収まで生存する前提。addon unload・Fcitx5終了後も回収責任を持つ主体、長期停止時の資源上限の実運用、InputContext寿命・preedit・重複確定、実server性能・既存client deadline統合、Windows/macOSへの影響は未実装・未検証。子が終了不能なら新規作成を止めるが、回収完了を保証しない。
- 通常100ms・障害250msは限定診断の実測であり、実Fcitx5 event loopの次dispatchや実IMEの要求全体ではない。helper採用、A5合格、工程2〜6・2B/2C/3への進行を意味しない。

## A5 lifecycle再検討とPoCの軽量CI組込み（2026-11-11）

**最新判定：安全なunload後の回収責任を確定できず、ユーザー指定の停止条件に該当。A5未達・工程1停止を維持する。** 今回の承認対象は、マルチスレッドFcitx5からの起動、addon unload後を含む所有/取消/終了回収、検証済み接続FDの移譲の3点を一体で成立させる設計と、その成立範囲の隔離実装・検証、既存PoCの軽量CI組込みである。基点は`ac608bfe29906efb8b525b60eddb7214473b72d8`、リモートmainは`b489fca1561bf12ff6b43205378220163d71c21c`。対象worktreeは開始時cleanだった。

### 設計監査と停止理由

- 既存Python PoCは単一スレッドの直接の子をownerが生存中に回収する前提で、fork後にPythonコードを実行する。マルチスレッドFcitx5での安全なfork/execやaddon unload後の所有権を証明しない。
- UbuntuにあるFcitx5 5.1.19の`EventLoop`公開APIはIO/time/defer等を提供するが、addonより長寿命の子の所有・回収サービスを示さない。公式の[5.1.19 startProcess実装](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx-utils/misc.cpp)は親が`waitpid(..., 0)`で中間子を待ち、helperの所有handleを返さない。event loop上の非ブロッキング起動と所有権契約の代わりにはならない。これは同APIが他の仕組みを提供しないことの網羅的証明ではない。
- 直接子をpidfdで識別し、期限切れ時は取消・SIGKILL、`waitpid(WNOHANG)`で回収確認し、回収前は同一ownerで次の子を起動しない契約は、ownerが生きている間の候補。SIGKILLは割込み不能I/O中の即時終了を保証せず、pidfd自体もwait/reapの責任を引き受けない。Fcitx5の別のSIGCHLD処理との共存、既存のownerより長いlifetime、unload後に継続する回収先が未確定である。
- 検討候補は、addonとは独立した生存期間を持つ信頼できる監督主体がhelperを所有し、channel切断で取消、非ブロックにreapし、未回収時は全体の上限を保つ構成。別サービスやFcitx5本体への統合が必要か、その起動・クラッシュ・再起動・unload時の回収、親死亡時の再親化、namespace/subreaper、FDとコードの寿命を実環境で確定する必要がある。今回のシステム変更・製品統合禁止の範囲では、この主体を用意・検証できない。
- helper側で明示profileをfd基準で辿りkeyの権限/種類・PID・版を確認し、接続済みsocketのSO_PEERCREDと`/proc/PID/exe`を照合、その同一FDをSCM_RIGHTSで渡す案は候補に留まる。世代・期限・不正/過剰FDの拒否、所有権の移譲、プロセス終了・PID再利用・execと検証の競合、異製品拒否を同じ契約で検証しなければならない。実serverを使った版・session整合は今回検証できない。

3点を満たすlifecycleは確定していないため、新しい隔離helperや実peer検証への接続は実装しない。有限のfixtureを終了できたことを、addon unload後の回収保証やA5合格に読み替えない。通常要求100ms・障害時loop復帰250msの初期目標も維持する。52ケースの対象コードは変更せず、過去の成功記録を参照し今回は再実行していない。

### 既存PoCとCI

`tests/ubuntu/test_linux_helper_poc.py`を`.github/workflows/linux-light.yaml`に独立stepとして追加し、文書のみの変更でも実行する。Bazel限定テストの選択条件・依存取得・既存製品targetは変更していない。Python標準ライブラリのみ、合成socketpairのみで実IME・実serverを起動しない。[CI運用](ci-policy.md)にも対象を記録した。

ローカルでは`/usr/bin/python3 tests/ubuntu/test_linux_helper_poc.py`を以前と同じ理由でsandbox外で実行した。最初の実行は8ケース中2失敗（検証停止の到達通知が80ms以内に得られず、正常転送でも100ms以内にFDを取得できなかった）。同じ条件で再実行すると8ケース成功、正常20回の最大78.002ms、exec前停止の親復帰1.180ms、350ms停止時の親復帰100.166ms、回収確認5.158ms、取消0.958msだった。失敗を消去・閾値緩和していない。確認時load averageは5.59/2.39/1.79、CPU数14だったが、それが失敗原因と断定できない。一般OSのスケジューリングとプロセス起動費用により、この100ms試験が環境負荷に敏感であることは未解決。失敗runを含む結果はA5の目標達成の証明ではない。

YAML構文、追加step、トリガー/既存stepの不変性と`git diff --check`を検査する。actionlintは未導入で、追加取得・フルビルドは行わない。push後のGitHub Actions結果と静的検査は区別し、完了報告に記す。既存資産、元worktreeの未コミット変更を保持し、実IME起動、システム変更、工程2〜6/2B/2C/3、本格統合には進まない。

## A5方針変更：イベント駆動IPCの設計と隔離PoC

基点`1a3f25c7c653f8d7b2d923c4ed2816497bfa3672`、同worktreeは開始時clean、remote mainは`b489fca1561bf12ff6b43205378220163d71c21c`。ユーザーの追加指示で100ms/250msを参考値へ変更し、Fcitx5内で完結するイベント駆動IPCを第一候補にした。systemd・外部常駐サービスに依存しない。AGENTS.md、PLANS.md、Phase 2計画の現行契約を更新し、過去の結果は履歴として保持した。

詳細は[a5-async-design.md](a5-async-design.md)。IOイベント＋timer、worker、helper、準備分離を比較し、IC/要求世代、有限queue、preedit保持、一度だけのcommit/キー処理、取消/終了の所有権を記載した。入力/IO callbackのp99 5ms・最大20ms、heartbeat遅れp99 20ms・最大50ms等は低負荷時のレビュー候補であり、まだ確定基準ではない。通常要求1秒・準備2秒の期限も検討案であって、同期待機の許容時間ではない。100ms/250msの参考値と安全条件は削除しない。

### 新PoCと実測

`tests/ubuntu/test_async_ipc_poc.py`は単一スレッドselectors/socketpairの模型。別スレッド・子process・実server・Fcitx5を使わない。合成peerの遅延は同じloopの予定時刻で表し、UIは文字列/commit台帳で表す。recvはnonblocking、EOFで完了、4KiB上限のfixtureを用い、各dispatchで有限量を処理する。既存helper PoC・52ケース・製品ソースは変更しない。軽量CIへ独立stepを追加した。

sandbox内は送信EPERMにより失敗し、sandbox外で`/usr/bin/python3 tests/ubuntu/test_async_ipc_poc.py`を実行した。5テスト（不正/過大応答のsubcaseを含め6シナリオ）、3.322秒、すべて成功。

| fixture | 要求完了/打切り | 最大IO callback | 5ms周期の最大遅れ | 周期イベント数 |
|---|---|---:|---:|---:|
| 即応（10ms予定） | 10.319ms、commit 1回 | 0.047ms | 0.310ms | 107 |
| 350ms遅延 | 351.923ms、commit 1回 | 0.111ms | 0.337ms | 107 |
| 50ms後取消、350ms後に古い完了を模擬 | commit 0、preedit保持 | 0ms | 0.382ms | 108 |
| 不正応答 | commit 0、preedit保持 | 0.140ms | 0.294ms | 108 |
| 過大応答 | commit 0、preedit保持 | 0.077ms | 0.324ms | 108 |
| 無応答、450msで取消 | commit 0、preedit保持 | 0ms | 0.151ms | 108 |

正常/遅延応答の完了をさらに2回配送する模型でもcommitは1回、取消後の完了は0回であった。350ms応答待ちが他イベントの350ms停止を生じないことは、この模型で確認できた。旧100ms応答目標を超えたことだけでは、新しい応答性方針の不合格とはしない。測定は短い低負荷試験でp99を算出しておらず、全準備/アプリ入力/実Fcitx5の受入達成ではない。

### 残件・停止条件・保護

profile/keyの同期I/O・実peer検証・起動準備は非同期socketだけでは解決しない。timer/deferへの単純移動で解消したとは扱わない。実接続FDの安全な準備、cold start、ログsink、実Fcitx5のkey consume/forward順序、keyup・modifier・focus/Wayland差、unload時の資源回収とserver session残存は未検証。物理キー全面非同期化はこれらを確認するまで製品へ採用しない。旧helperの終了責任の未解決は残るがhelper自体は必須としない。

時間基準の変更を理由に入力消失・二重確定・古い応答・他IME干渉・無期限joinを許容しない。制御不能な準備I/Oや未確定のキー順序を製品へ接続する前に停止し再レビューする。今回の許可は設計と隔離PoCだけであり、新A5合格・本格統合・IME導入/起動・システム変更・次工程へは進まない。

YAML/inline構文・文書リンク・差分を検査し、対象変更のみcommit/pushする。actionlintは未導入。既存資産・元worktreeのステージ済み変更を保持し、辞書/モデル取得やBazelのフルビルドは行っていない。CI実行状態とpush後のremote HEAD一致は完了報告へ記載する。

応答到着前の進行も明示的なアサートに追加した最終再試験は5テスト成功、3.323秒。350ms遅延の完了355.108msまでに70回の周期イベントを処理した。全scenarioの最大周期遅れ0.324ms、最大IO callback 0.138ms、即応10.284msだった。上表は最初の成功測定としてそのまま保持する。

## 実Fcitx5 API限定検証（製品統合前）

**判定：実ライブラリ上のsocket待ち・取消・IC寿命の限定API検証は成功。準備I/Oと実アプリのキー配送契約は未解決で、A5実装への移行は保留・レビュー待ち。** 基点`bcfc5519fd80e7503515687db829dee3be35ebac`、remote mainは`b489fca1561bf12ff6b43205378220163d71c21c`。開始時worktreeはclean。ユーザー承認はAPI検証・故障注入・記録と作業ブランチへの反映まで。

### 実行した検証

`tests/ubuntu/fcitx5_api_probe.cc`を既存pkg-configのFcitx5Core/Utils 5.1.19、g++のC++20/O2/Wall/Wextra/Werrorで単独コンパイル。`/usr/bin/python3 tests/ubuntu/run_fcitx5_api_probe.py`が一時領域でビルドして最大15秒で実行・回収する。新規依存取得、Bazelビルド、Instance/IME/実server起動はない。9つのloopシナリオ＋InputContext契約試験と、全loop終了後のFD数不変を確認した。実装backendはsd-eventで、systemdサービスの利用・起動はしない。libuvは未検証。

| 実APIの合成シナリオ | 応答完了 | 最大IO callback | 5ms周期の最大遅れ | 周期回数 / commit回数 |
|---|---:|---:|---:|---|
| 即応（10ms予定） | 10.115ms | 0.022ms | 0.174ms | 108 / 1 |
| 350ms応答遅延 | 350.105ms | 0.031ms | 0.164ms | 108 / 1 |
| 350/410msの断片応答 | 410.110ms | 0.039ms | 0.178ms | 108 / 1 |
| 切断 | 確定なし | 0.038ms | 0.338ms | 108 / 0 |
| 無応答・450msで取消 | 確定なし | 0ms | 0.341ms | 108 / 0 |
| 50msで取消・古いDefer完了 | 確定なし | 0ms | 2.259ms | 108 / 0 |
| focusOut後の遅延完了 | 確定なし | 0.024ms | 0.343ms | 108 / 0 |
| IC破棄後の遅延完了 | 確定なし | 0.030ms | 0.375ms | 107 / 0 |
| 負例：Defer内の350ms停止 | 375.292ms | 0.028ms | **350.095ms** | 39 / 1 |

遅延応答の前に69回、断片応答の前に81回のheartbeatが進んだ。EOFからDeferへ完了を戻し、重複Deferでもcommitは一度だけ。取消/focusOut/破棄では世代・watch・focusで完了を拒否し、fixture preeditを保持した。Timer sourceを発火前に破棄して未発火を確認し、IO sourceを無効化してFDを閉じ、exitイベントでwatcherを停止、loop破棄後のFD数も一致した。実addonのunloadではなく独立プログラムの所有権試験である。負例は停止を正しく検出したという診断結果であって、350ms停止を許容するものではない。

初回sandbox内はsocket送信拒否からfixtureが失敗し、callback内例外をbackendがfatalとして扱った。callback外で失敗判定するよう修正。sandbox外実行の自動承認審査は一度timeoutし、許可された1回の再試行で実行できた。初回のsandbox外実行はTimer accuracy=0でcoalescingが入り周期条件に失敗。accuracy=1usを明示した最終実装では上記すべて成功。失敗を時間基準変更で隠していない。

### InputContext・キー契約：実行と静的確認の区別

実InputContextManager（instance()==nullptr）と合成InputContextのfrontend実装で、UUID検索・focusIn/focusOut・破棄後watch/UUID失効、KeyEventのrawKey/time/filterAndAcceptを実行。frontend用setBlockEventToClientでpress(123)→release(124)→commitのqueueと配送順序を実行確認した。focusなしのkeyEventはfalseだった。[公式5.1.19 InputContext実装](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/inputcontext.cpp)と導入済みヘッダーも照合した。

一方、実engineを通すkeyEvent/consumed判定、元の物理キーと遅延forwardKeyの等価性、Wayland/X11/アプリの順序・再入・modifier、実preedit描画、focus跨ぎの入力救済、実serverの版/session整合は未実行。frontend専用のblockerをaddonへ転用しない。forwardKey/commit自体のfocus保護を仮定せず、適用側の寿命/世代検査が必要。

### 準備I/Oの成立性・移行判定

IO/Timer/Deferのsocket段階は成立する限定実装候補。しかし同期open/openat/fstat/read/readlinkをDeferへ移すだけでは入力loopを止めることを負例で確認した。SO_PEERCREDの照合だけで実体パス検証を省略せず、cached metadataから別socketへ再接続して検証済みと扱わない。一接続一要求の現行protocolのため、新規接続の準備方式は毎回の実接続検証と両立する必要がある。安全な同一プロセス内準備workerの終了/コード寿命も未確定。実filesystemの停滞や実peer検証の障害注入は今回未実行で、負例のnanosleepはそのモデルに過ぎない。

100ms/250msは参考値、APIの低負荷短時間測定は新A5のp99や合格の証明ではない。準備I/Oと物理キー契約を解決するまでは本格統合へ移行しない。systemd・外部常駐サービスへ依存せず、A5合格/次工程はレビュー後とする。

軽量CIではFcitx5 5.1.19以上の開発ファイルが既にある場合のみ同じrunnerを実行し、未導入ならNOT EXECUTEDをnotice/summaryへ出す。apt追加をせず、step成功と実API成功を混同しない。YAML/runner構文・差分を検査する。既存52ケースの製品コードは変更しておらず、既存資産・元worktreeの変更を保持。実IME/Hazkey/システム設定変更、辞書/モデル取得、次工程は0。push後CI結果とremote HEAD一致は完了報告に記す。

## キー順序と接続準備の追加隔離検証

**判定：2課題の限定候補を実API・実syscallで検証したが、実engineの非同期consumeと製品workerの終了所有権は未解決。A5製品実装への移行は保留・レビュー待ち。** 基点`82877dd740cbfc8028baaa158ff4748095e78a67`、remote mainは`b489fca1561bf12ff6b43205378220163d71c21c`。AGENTS.md/PLANS.md/Phase 2計画とa5-async-design.mdを確認し、同じclean worktreeで開始した。

### キーの実APIと合成非同期方針

既存C++ probeに、Shift press→Shift付きA press/release→Shift release→x press/releaseの6件を受け付け、350ms後に直列完了する3モードを追加。元のrawKey・modifier・keyup・timestampを保存し、合成消費方針で先頭4件を転送、xは1回commitする。重複完了を2回呼んでも配送・確定は一度だけ。focus移動と、focusを一度失って戻した場合も、古いepochを適用しない。

| mode | 合成受付数 | 転送数 | commit数 | 救済レコード |
|---|---:|---:|---:|---:|
| stable | 6 | 4 | 1 | 0 |
| focus-away | 6 | 0 | 0 | 6 |
| focus-back | 6 | 0 | 0 | 6 |

実Fcitx5のKeyEvent/forwardKey/commitStringを実行したが、Instance/engineは起動しておらず`keyEvent`返却は全0。filterAndAcceptと消費の決定はテストが行い、engineの受付hookではない。実物理キーのpass-through後の遡及consumeを検証したものではない。文字データと保留入力をfixtureで保持する確認であり、実frontend/アプリの文字消失防止や入力救済完了ではない。Wayland/X11・modifierの実アプリ解釈・engine再入・実preeditは未検証。

### 準備worker候補の実syscall試験

`fcitx5_preparation_probe.h`を追加。同一プロセスの試験root ownerが1本のworkerとnotification eventfdを所有する。workerが専用mkdtemp内の実profile/keyをopen/openat/fstat/readし、private権限と通常fileを確認。実Unix抽象socketの合成listenerへconnectし、SO_PEERCREDのUID/PIDとreadlink(/proc/PID/exe)を照合し、検証した同一FDをrootへ移す。listenerは試験自身のprocessで、実serverは起動・接続しない。FD移譲は同一process内の所有権移動でSCM_RIGHTSではない。root完了通知は実Fcitx5 IO APIで処理する。

| 条件（350ms停止モデル含む） | 完了通知 | 周期回数 | 最大周期遅れ | 検証/適用 |
|---|---:|---:|---:|---|
| 正常private key・peer一致 | 350.757ms | 69 | 0.129ms | 成功/適用 |
| public key | 350.379ms | 69 | 0.100ms | 拒否/なし |
| key symlink | 350.349ms | 69 | 0.356ms | 拒否/なし |
| key内PIDと実peer PID不一致 | 350.457ms | 69 | 0.290ms | 拒否/なし |
| peer実行パス不一致 | 350.524ms | 69 | 0.113ms | 拒否/なし |
| 50msで模擬addon閉鎖、検証前取消 | 350.327ms | 69 | 0.068ms | 未検証/なし |
| 検証済FDを保持してから350ms停止、途中取消 | 350.443ms | 69 | 0.112ms | 成功/適用せず回収 |

完了までroot owner/通知sourceは生存し、完了確認後にloop外でjoin、FDをRAIIで回収。全試験後のFD数不変も成功した。取消時もworkerの終了を入力callback内で待たない点は確認したが、実addon unloadにこのroot ownerが存在することは未証明。実syscall自体を止めたのではなく、その前/後に有限sleepを入れたモデルである。

fixture keyはPID文字列で製品protobuf/版/prefixを扱わず、profile全祖先の不正やfile改変/exec/PID再利用競合も未検証。workerが実I/Oで永久停止した場合、このハーネスは終了を保証できず、外側runnerの15秒timeoutは製品の安全回収の代替ではない。無条件detachやaddon内の長時間joinへ進めない。Fcitx5内で完結する長寿命ownerの実装/終了契約が確定するまで製品移行しない。

### 検証と範囲

`/usr/bin/python3 tests/ubuntu/run_fcitx5_api_probe.py`をsandbox外で実行し、既存Fcitx5 5.1.19・g++ C++20/-pthread/-O2/-Wall/-Wextra/-Werrorで単独build。最終試験は既存9 loopシナリオ＋IC契約、追加3キーシナリオ＋7準備シナリオすべて成功。既存の遅延応答は350.108ms、断片は410.088ms。非ブロッキング8 loopの最大周期遅れ0.477msに対し、Defer内350ms停止の負例は350.070msの遅れを検出した。100ms/250msは参考値であり、p99/製品合格は判定しない。

軽量CIの既存API runnerが新headerを含めて実行する。開発ファイル不足の場合は従来通りNOT EXECUTED。製品ソース・既存52ケース・旧PoCを変更せず、辞書/モデル取得・全件回帰・実IME起動・既存設定変更は0。新規依存導入も行わない。構文/差分と既存資産保護を確認し、対象のみcommit/pushする。CIの実行状態・remote HEAD一致は完了報告へ記録する。

## 2A再開時の安全ゲート（2026-11-12）

### 承認・基点・作業範囲

ユーザーは2Aの残作業を一括承認し、通常の工程間レビュー停止を不要とした。製品実装・限定ビルド・stagingまでが範囲内である。一方、解決できない安全問題では停止する指示を維持。これに従い、工程1のworker終了とキー配送の安全性を先に調べた。承認済みの工程2〜6を「未承認」と取り違えて止めたものではない。

開始HEAD/remote作業ブランチは`8f39184501cadca22666ae50e9f0533e991704cb`で一致、originは`git@github.com:Minoru-kuma/mozkey_forubuntu_mk.git`、remote mainは`b489fca1561bf12ff6b43205378220163d71c21c`。既存`feat/ubuntu-phase2a`worktreeはcleanだった。AGENTS.md・PLANS.md・Phase 2計画/結果・A5設計・CI運用・既存C++/Python PoCを確認した。mainとの統合、rebase、force push、PR作成は行わない。

### 実装・故障注入と測定

新規`tests/ubuntu/worker_owner_fixture.cc`と`fcitx5_owner_gate.h`を既存C++ runnerへ接続。Fcitx5 Libraryの既定PreventUnloadHintによるコード保持と、owner破棄時のjoinによるloop停止を分けて検証した。ソース確認は[公式5.1.19 loader](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/addonloader.cpp)・[manager](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/addonmanager.cpp)と導入済みヘッダーに基づく。実AddonManager/Instance/IMEは起動しない。

`/usr/bin/python3 tests/ubuntu/run_fcitx5_api_probe.py`で一時領域に負例.soとprobeを順番にコンパイル。既存Fcitx5Core/Utils 5.1.19とg++ C++20/-pthread/-O2/-Wall/-Wextra/-Werrorを使用。buildは同時1件でjobs=2以下。停止中I/Oの代わりに350msの有限sleepを入れたモデルで、無期限停止やcold filesystemの直接試験ではない。

| 測定対象 | 実測・判定 |
|---|---|
| コード保持後、10ms timerからowner破棄 | destructor 340.195ms、5ms heartbeat最大遅れ340.208ms、41周期。join方式を製品用として拒否 |
| 非同期socketの正常/遅延/断片/切断/無応答/取消/focus/破棄 | 即応10.057ms、遅延350.065ms、断片410.046ms。8条件の最大heartbeat遅れ1.237ms。取消/focus/破棄のcommit 0 |
| Defer内350ms停止の既存負例 | heartbeat遅れ345.078ms。負例を検出 |
| 既存3キー条件 | stable:受付6/転送4/commit1。focus-away/back:転送0/commit0/救済6。engine返却は全0で、受付方針は合成 |
| 既存7準備条件 | 350.169〜350.392ms、各69周期、最大heartbeat遅れ0.438ms。正常だけ適用、権限/symlink/PID/exe不一致は拒否、取消後の検証済FDは回収 |

この実行の`FCITX5_API_PROBE PASS`は期待した負例拒否と既存fixtureの成功を示す。A5/製品試験の合格を示さない。コードpinは確認できたが、ownerの安全な終了を自動的に保証しない。参考100ms/250ms超過だけで不合格にしたのではなく、joinがloopを占有することを問題とした。最終的なFD回収検査は新しいownerゲートも含めて行う。

FD数の検査範囲をownerゲートの前から全試験後まで広げた最終再実行も成功。owner破棄339.682ms/heartbeat遅れ339.682ms、非同期8条件の最大遅れ2.547ms、350ms応答350.055ms、断片410.077ms、Defer負例の遅れ350.082ms。準備7条件は350.240〜350.437ms/各69周期/最大遅れ0.281msだった。ゲートを含めてFD数は不変で、全workerを終了回収した。負例の有限350ms停止を回収できたことを、実I/O永久停止の回収保証に拡張しない。

### 未解決と安全停止

単純joinは長時間停止、所有を捨てたdetachはデータ寿命を保証しない。プロセス寿命の専用registryは代替候補であり、一般的な実現不能は断定しない。ただし永久停止・addon再ロード時の上限・終了時の静的破棄/通知先失効を含む所有/回収を立証できていない。Fcitx5本体や既存環境へ回収機構を追加する変更は今回の範囲外。

さらに合成キーキューの「救済6件」は、focus移動/IC破棄後に実アプリへ安全に一度だけ届ける証拠ではない。同期KeyEvent返却と非同期consumed判定の間を接続する受付/保護/配送契約が未完成。frontend専用blockerや古いICへの無条件配送は採用しない。製品protobuf、専用profile/prefix/版、実serverへの接続準備と実sessionも未統合。このまま製品へ接続する安全性を保証できず、ユーザー指定の安全停止条件を適用した。

工程2〜6の製品変更・製品ビルド・専用stagingは実施していない。負例.soは一時fixtureであり、インストール入力ではない。1000要求/p99/CPU負荷、2s停滞、実filesystem停滞、Wayland/X11アプリ配送、実addon load/unload、実IME/server起動は未検証。既存52ケースの製品ソースは変更せず、過去の52成功は保持するが今回再実行したとは扱わない。

### A1〜A7と2B移行

| 条件 | 状態 | 証拠・不足 |
|---|---|---|
| A1 基点・由来 | 部分実施 | 固定SHA・既存環境/辞書hash・76資産を保持。製品採用ファイル/ライセンスmanifestは未作成 |
| A2 独立性 | 未達 | 専用ID/profile/IPC/exportと製品異接続負試験は未実装。今回他IMEに接続/変更しなかったことと区別 |
| A3 ビルド | 未達 | 製品.so/server/tool/stagingなし。単体fixtureのビルド成功で代替しない |
| A4 callback | 未達 | 既存API/epochの限定fixtureのみ。製品protocol全フィールド/UI/候補経路は未統合 |
| A5 IPC応答性・安全 | 未達 | socket段階は局所成立、owner join負例は不適合。入力保護/配送・製品IPC準備/終了契約が未解決 |
| A6 既存回帰 | 部分実施 | 既存52成功資産は保持。今回は関連C++ fixtureのみ。製品独立設定OFF/ON・package回帰・全OSは未実施 |
| A7 資源・保護 | 今回の範囲で充足 | 既存資産/元worktree差分を保持。新規依存/辞書/モデル取得なし。jobs上限内。設定変更/実IME起動なし。製品未作成部分の安全性を示すものではない |

2A未完了、2B移行不可。全6系統フルCIは2A完了条件に達していないため今回は起動しない。通常pushのLint・限定Linux・Zenz軽量CIと、ローカルの実API試験を区別して完了報告する。Ubuntu 24.04の軽量CIが実APIを開発ファイル不足でNOT EXECUTEDとする場合は、成功扱いで上書きしない。

保護確認では`P2/logs/baseline.json`の76ファイルすべてSHA-256一致、元worktreeのステージ済み/未ステージ差分hashも一致。`.local`全体は3,993,184KiB（約3.81GiB、60GiB未満）で、新規大容量取得は0。構文・参照先・差分検査を行い、このタスクのテスト/文書だけを明示してcommit/pushし、remote HEAD一致とpush後の軽量CI状態を完了報告へ記す。

## 新A5方針と独立工程の実装（2026-11-12追加指示）

### 承認・基点

今回の指示は、打鍵→ひらがな300ms未満を暫定目安、ライブ変換500ms程度/1秒前後を測定評価、通常終了のみ400msまで待機可へ変更した。focus移動・IME切替は終了予算の対象外。旧100ms/250msは参考値で、数値超過だけで全工程を停止しない。A5未達でも独立工程2〜6を進め、工程間レビュー待ちを挟まない。未達条件を合格にせず、実IME起動・導入/既存設定変更・sudo/apt・2B以降・main統合を行わない。

同じworktree/`feat/ubuntu-phase2a`、HEAD `2a74875292b299f5c5ca758b8c212f89ab70980b`、開始時cleanで継続した。remote mainはGitHub APIで`b489fca1561bf12ff6b43205378220163d71c21c`を再確認し、mainのAGENTS.mdも読んだ。元worktree `adopt_ubuntu`の未コミット変更・Phase 1資産を保護した。以下は「2Aを完了した」報告ではなく、今回実施できた製品分離/ビルド/限定試験/stagingと未達の記録である。

### 工程別の実装

| 工程 | 今回実施した範囲 | 残る製品接続 |
|---|---|---|
| 1 | 新A5条件をAGENTS/計画/設計へ反映。プロセスroot所有の単一worker、取消、終了待ち、実検証済FD移譲を実装/隔離検証 | 実AddonManagerの終了順序、製品依存runtime終了との競合、実キー消費/配送 |
| 2 | `--//:linux_mozkey=true`で製品名・server/tool/renderer名・ready event prefix・profile・抽象IPC prefixを分離。旧profileを探索/移行しない。設定OFFは従来のまま | 全ID/起動可能経路の最終棚卸し、server/tool側の全private-path/TOCTOU監査、実共存 |
| 3 | 固定SHAから22ファイル選択移植、direct client除外、来歴とBSD/LGPL別の通知・本文を保持。専用.so/server/toolとfrontend/準備ライブラリをビルド | `.so`のfactoryは常にload拒否。機能完成したfrontendではない |
| 4 | 完全なcallback commandコピー、consumed=false対応、monotonic timer/epoch/取消、16回連鎖制限、共通応答経路を実装。focus/IME切替の同期RPCを除去し、所有preeditは保持 | senderは移植した同期interfaceのまま。非同期session・候補世代・部分応答/UI全体の統合試験 |
| 5 | 実キー変換/Space/control/keyup/履歴reset/Unicode/周辺文字範囲を限定試験。clipboard fallbackとpassword/sensitive周辺取得を抑止。設定/toolパスとgettextを専用化 | live ON/OFFの一貫設定・実表示、再変換/マクロ継続・削除/救済の全契約、frontendへの実配送 |
| 6 | 専用`stage_inputs`とstagingスクリプトを追加。作業領域へ無効化した成果物・定義・icon・通知・frontend sourceを配置し、manifest/ELF/依存/export/hashを検査 | 導入可能成果物の完成、全6系統CI、2B向けロード試験の前提充足 |

### ビルド・profile・IPCの分離

`src/BUILD.bazel`のbool flagは既定falseで、Linux constraintとのANDだけで`MOZKEY_LINUX_BUILD`を伝搬する。Android/Windows/macOSへ定義せず、新規Fcitx targetはmanual/opt-in。Fcitx5Core/Utils/Configのrepoを追加したが、clipboard module/SDKの追加導入は不要。既定packageや既存依存版は変更していない。

| 項目 | 専用設定ON |
|---|---|
| 製品/実行名 | Mozkey / mozkey_server / mozkey_tool。未梱包rendererもmozkey_rendererとして旧製品へfallbackさせない |
| 実行時配置 | `/usr/lib/mozkey/mozkey_server`、`/usr/lib/mozkey/mozkey_tool`。`config.bzl`の固定値を共有。提案されていたserver_dir flagは未実装 |
| profile | 絶対XDG_CONFIG_HOME配下のmozkey、なければHOME/.config/mozkey。HOMEなしはgetpwuidのhome/.config/mozkey。旧.mozcの存在を優先しない |
| IPC | `\0tmp/.mozkey.<key>.<channel>`。通常IPCPathManagerとLinux専用deadline transportの両方を同じ条件で変更 |
| Fcitx ID/登録 | addon/inputmethod/factory V2=mozkey、Library=fcitx5-mozkey、property/action/icon/domainもmozkey固有。C++内部のmozc名は保持 |
| frontend設定 | conf/mozkey.conf、sessionはIC別。旧configやclipboardは読み込まない |

名前を分けただけでA2全条件合格とはしない。既存ディレクトリ権限・symlink/名前置換競合、profile解決失敗時の全呼出し側、renderer/GUI起動可能経路等の最終監査は残る。実行禁止のstagingであり、server/toolも起動していない。

初回ビルドは生成辞書4入力の不足で失敗。Phase 1の`data/dictionary_koyasi/generated/`内12ファイルをコピーして再利用し、新規取得は行わなかった。製品defineによるcache差分のため内部datasetの再生成はローカルで発生した。raw daily入力の再取得/再生成やモデル取得ではない。辞書を埋め込む専用serverをビルドし、IBus用mozc.zipをインストール入力へ流用していない。

既存Bazel 9.0.2/Bazelisk、GCC 15.2.0、Fcitx5 5.1.19、Phase 1 repository/disk cacheを使用し、全ビルドを`--jobs=2 --repository_disable_download`と60GiB/2GiB受信上限監視付きで実行した。最終`//unix/fcitx5:stage_inputs`は成功（約20秒、cache再利用）。通常のserver/tool/.soの初回成功は約360秒。ログは`P2/logs/product-build*.log`とmetrics JSONに保持する。

### worker・接続準備・取消・通常終了

`PreparationWorker`のrootはheap上にプロセス寿命で1個存在し、std::threadを直接所有する。所有者なしのdetachは使わない。addon側handleの破棄は取消だけで、joinしない。停止中の追加Startは拒否し、完了後にworkerがroot slotを解放する。通知先はstate所有のeventfdで、addon/loop破棄後にも別用途で再利用されたFDへ書き込まない。workerはIC/loop/addonを参照せず、結果FDの未移譲/取消/移譲後の所有を分ける。

`StartConnectionPreparation`はendpointの値コピーを渡し、worker内で実key protobufとpeer/版を検証する。`PrepareLinuxDeadlineSocket`は既存検証を維持し、protocol/product版も確認して、接続した同一FDを返す。接続準備だけでapplication requestは送らない。受取後に再connectしない。検証後のTOCTOU/exec/PID再利用を全解決した主張ではない。

正常終了の待機APIは実プロセス終了だけに限定し、390ms待機＋10msの余裕とした。focus/IME切替/addon handle破棄では呼ばない。timeout後もrootが所有し、同期待機を続けない。コード保持はFcitx5既定PreventUnloadHintを前提とし、実Libraryラッパーunload/reloadで確認した。実AddonManager/IMEのload/unloadそのものは未実行である。

| 実Fcitx5/終了fixture | 実測・結果 |
|---|---|
| 350ms停止中のhandle取消/破棄 | 0.004ms、5ms heartbeat最大遅れ0.218ms、109周期。追加Start 1,000回を拒否、完了時に資源回収 |
| 通常終了・350ms処理 | 待機349.170ms、完了を確認し正常C++終了 |
| 停滞・3.5秒処理中に通常終了 | 390.075msで待機打切り、完了=falseを区別し、試験プロセスは正常終了。_Exit回避は使用しない |
| 合成250ms応答 | 250.073ms、89周期、最大周期遅れ0.085ms |
| 合成500ms応答 | 500.043ms、139周期、最大周期遅れ0.151ms |
| 合成1秒応答 | 1000.050ms、239周期、最大周期遅れ0.164ms |

250ms応答は実打鍵→ひらがな表示ではなく、500ms/1秒も実ライブ変換表示ではない。350ms/3.5秒の停止は有限sleepによる故障モデルで、実kernelのD状態や永久停止を再現したものではない。製品protobuf処理/依存ライブラリの終了競合やcold start/負荷時の尾部遅延は未検証。400msの設定値を全条件の実時間上限保証へ読み替えない。

旧owner destructor joinの負例とDefer内350ms停止も残し、通常入力loopを止める構造を引き続き検出する。これらを「通常終了400ms以内だから通常入力でも許可」と扱わない。全fixture後のFD数不変も成功した。ログは`P2/logs/a5-new-api.log`。

### 限定試験

| 構成 | 実行対象・結果 |
|---|---|
| 専用設定OFF | system_util 2、process_mutex 3、ipc_path_manager 4、ipc 1、linux_deadline 21、client 30の**61ケース成功** |
| 専用設定ON | system_util 3、process_mutex 3、ipc_path_manager 5、ipc 1、linux_deadline 21、client 30、callback 5、準備境界5、frontend境界6の**79ケース成功** |
| 既存52ケース | linux_deadline 21＋client 30＋ipc 1をOFF/ONで確認。診断資産・旧数値アサートは削除しない |
| 実API runner | 既存12 loop条件（追加3応答遅延を含む）、IC契約、3キー/7準備条件、所有負例/新root候補、2終了条件が成功 |

callback試験はconsumed=false・完全なcommandコピー・0ms defer・一度だけの配送・取消/無効context/置換/破棄・16回上限・非対応command拒否を確認した。接続試験は実private profile/key protobuf・UID/PID/exe/版からの同一FD移譲、要求未送信、CLOEXEC/nonblocking、wrong exe/版/public key拒否、**旧製品prefixだけに置いたsocket sentinelへの未接続**を確認した。sentinelも試験自身のlistenerで、実Hazkey/Mozc/serverを起動/接続していない。

frontend境界6ケースは実移植handlerの先頭a/Space、control chord/keyupの二重要求抑止、修飾履歴reset、Unicode compose保持、選択範囲のUnicode文字数とsigned overflowを確認した。GetKeyEventの返却はprotocolに送るかの判定であり、Fcitxのconsume/pass-throughの代替ではない。実アプリのkeyup/modifier配送は未検証。

試験用visibility・補助関数名・status assertion、0400保存keyへの故障注入fixtureは初回失敗後に修正し、最終成功と区別してログを保持した。必要な再実行ではBazel cacheを再利用し、全件回帰やローカルの全OSビルドは行わなかった。

### staging・静的検査・再現

`tools/ubuntu/stage_phase2a.py`は指定先が存在すれば拒否し、一時treeで準備してから配置する。ELF64/x86_64、NEEDED、RPATH/RUNPATH、exportを静的検査し、binaryは実行しない。manifestにソースrevision/dirty状態、build設定、各相対配置先/hash、埋込みdataset hashを記録する。実行時pathにP2_STAGE/checkout/Bazel出力を埋め込まず、3 ELFのRPATH/RUNPATHは空だった。

候補staging `P2/staging-candidate`は47ファイル/約46MiBでhash照合成功。その後の来歴本文/境界試験追加を含む最終stagingは新しいディレクトリへ作る。最終manifestのrevisionをcommit後のHEADへ合わせ、完了報告で配置先を示す。両方とも`INCOMPLETE_NOT_FOR_IME_LOAD`であり、factoryは常にnullptr、addonはEnabled=False。無効化を外すだけで使えるIMEではない。

| ELF | SHA-256 |
|---|---|
| mozkey_server | `ca48bf99d5518790d6eda427a897ce4c9506ea9528dfba5e00234e8f7733633f` |
| mozkey_tool | `3fae3bc04976ce1df115fcc245e3dcc2a9b23e1b5790b47f3126852ece4e0bc4` |
| fcitx5-mozkey.so | `20e139c52e813828b12f31819eaa60089596e1043b0e8dec435bf9d18673799e` |

`.so`の動的exportは`fcitx_addon_factory_instance_mozkey`の1個のみ。NEEDEDは既存systemのFcitx5/Core/Utils/Config・C++/Cランタイム等、toolは既存Qtを使う。これらのsystemライブラリはコピーしない。選択frontendのsource/来歴・BSD/LGPL通知と本文・既存iconを保持するが、配布時の完全な再リンク/辞書ライセンス確認は別途必要。

実際のビルド環境/キャッシュ引数は`P2/run_product_build.py`とlogsに保持。一般的な明示targetとstaging手順は`src/unix/fcitx5/README.md`へ記載した。初回source/dictionary inputのhashは既存Phase 1の正本を参照し、今回それらの取得物・成功証拠を変更していない。

### CI・資源・未検証

軽量CIに、5.1.19以上の開発ファイルが既にある場合だけ専用設定ONの境界試験を追加した。なければNOT EXECUTEDをnotice/summaryへ出す。SDKをaptで追加しない。既存6ターゲットは設定OFFのまま。cache入力hashが変わっても同一OS/archの旧cacheを復元候補にし、Bazelのdigestで再利用を判定する。起動条件・permissions・concurrency・既存テスト/成果物生成は維持した。system Pythonの既存PyYAMLで構文、bash -nで埋込みshell、buildifierとdiffを検査する。actionlintは未導入で実行しない。

共通BUILD/MODULE/IPCとセキュリティ境界の変更を含むため、CI方針上の全6系統確認は必要であり未検証。接続GitHub toolはworkflow起動に対応せず、gh/REST dispatch用credentialも利用できないため、手動起動を実行したとは扱わない。ツール/credentialの新規導入やmainへの反映で回避しない。既存Secure Offline ChecksのRun workflowで作業ブランチを選べる手順はCI運用書にある。通常pushの実際の軽量CI状態は完了報告で示し、ローカルUbuntu 26.04試験とUbuntu 24.04 CIを区別する。

監視したbuild/testの作業領域ピークは約6.3GB（60GiB未満）、新規依存/辞書/モデル取得0。固定参照ソース約124KBとLICENSE本文の取得だけで、新規取得2GiB以内。jobs=2を使用し、元ステージ済み/未ステージ差分と76保護資産のSHA-256一致を確認した。sudo/apt、IME/server/tool起動、既存Hazkey/Fcitx5やシステム設定の変更、2B以降、main統合、PR/rebase/force pushは0。

### A1〜A7・残作業

| 条件 | 現時点 | 判定根拠 |
|---|---|---|
| A1 基点・由来 | 実施 | main/作業/参照元SHA、22採用ファイルの元blob/改変理由/ライセンス、BSD/LGPL本文、環境・辞書由来・成功資産を保持。配布適否の最終判断とは区別 |
| A2 独立性 | 部分実施・未達 | 名前/profile/IPC/ID/export分離とsentinel負試験成功。全起動経路/private-path/TOCTOU/実共存は未完了 |
| A3 ビルド/staging | 部分実施・未達 | 専用server/tool・移植frontendライブラリ・load拒否factoryをビルド/静的検査/staging。入力可能な.soは未完成 |
| A4 callback | 部分実施・未達 | 完全command/timer/取消/連鎖試験は成功。非同期sender/UI全体・live設定・再変換/マクロの全契約は未完了 |
| A5 応答性・安全 | 部分実施・未達 | 新root/終了/実検証済FD候補は局所成立。実KeyEventのconsume/keyup/誤配送防止・救済、非同期session統合、製品終了順序は未証明 |
| A6 既存回帰 | 部分実施・未達 | OFF61/ON79、既存52と実API fixture成功。既定package/限定28全体・全6系統・他OS実行・製品ON/OFFの実表示は未検証 |
| A7 資産/ホスト保護 | 今回範囲で充足 | 元差分/76資産保持、資源予算内、個人データ/設定/IME変更0 |

2A全体を合格にしない。2B移行不可。残りは、(1) 非同期client/sessionとキー消費・keyup/未配送入力の保護/救済を一貫実装する、(2) 製品runtime/AddonManager終了順序と停止I/Oを検証する、(3) 全private-path/起動経路・live設定/再変換/候補寿命を完了する、(4) load拒否factoryを外せるだけの限定統合/全6系統の証拠を揃えること。実IMEの起動許可がない検証は静的/隔離試験と区別して残す。

### 実装コミット・staging・実CIの確認

実装`5290c564b791423a85b5de20a738062bc17f5abe`を`origin/feat/ubuntu-phase2a`へpushし、remote HEAD一致を確認した。`P2/staging-5290c564b/manifest.json`は同じrevision・dirty=false・49ファイルで全hash一致。3 ELFのhashは上表と一致した。元worktree差分/76資産の保護と作業worktree cleanも再確認した。

同SHAのpushで[Lint 43177493495](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43177493495)、[Linux限定 43177493518](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43177493518)、[Zenz 43177493498](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43177493498)はすべてsuccess。Linux限定は約2分で、helper 8/モデル5ケース成功、既定6ターゲット成功（2ターゲットはtest cache利用）。旧module hashのcache `82877dd740cbfc8028baaa158ff4748095e78a67`を復元でき、入力変更分だけを再ビルドした。

ただしUbuntu 24.04にはFcitx5 5.1.19開発ファイルがなく、実API probeと新設の専用設定ON境界試験はログ上**NOT EXECUTED**だった。これらの成功証拠はローカルUbuntu 26.04の実行結果であり、CI stepのsuccessを実API成功へ読み替えていない。全6系統フルCIは引き続き未実行。後続の文書のみコミットを含めた最終remote HEADとCI状態は完了報告で確認する。

## 非同期client/sessionと入力保護の製品側接続（2026-11-13）

**判定：限定ビルド・97ケースと既存実API fixtureは成功。2A全体は未完了。保留入力の安全な復旧/終了時保全、実frontend契約、worker/runtime終了等を未達・未検証として、factoryの常時ロード拒否とEnabled=Falseを維持する。2B移行不可。** 工程ごとのレビュー停止は挟まず、独立して実装・試験可能な部分とstagingを更新した。

### 基点・承認・保護

最新AGENTS.md、PLANS.md、Phase 2計画/結果、A5設計、CI運用、既存C++/Python PoCと前回製品実装を確認した。既存worktree `P2/checkout`（`P2=.local/ubuntu-phase2a`）/`feat/ubuntu-phase2a`、開始HEAD `a823d68e62620d4d47c8e22460704f3a45b92acb`、開始時clean、origin `git@github.com:Minoru-kuma/mozkey_forubuntu_mk.git`で継続。GitHubのmainは`b489fca1561bf12ff6b43205378220163d71c21c`のまま。元worktreeのステージ済み5ファイル・未ステージ差分と、基準76資産のサイズ/SHA-256、既存cacheの存在を照合して保持した。旧stagingも上書きしない。

今回の承認は2A製品側の統合・ビルド・限定試験・staging・文書・作業ブランチへのcommit/pushまで。実IME/server/toolの起動・導入、Hazkey/システム設定変更、sudo/apt、systemd/外部常駐サービス、2B以降、main統合は行わない。A5は通常表示300ms未満の暫定目安、live500ms程度/約1s評価、通常終了のみ400ms以内。100ms/250msは参考値として保持し、数値超過だけで全工程を停止しない。

### 実装と製品境界

| 対象 | 変更・成立した限定範囲 | 残る境界 |
|---|---|---|
| `async_rpc` / `async_session` | IO/Timer/Defer、非ブロッキング送受信、1dispatch64KiB、要求/応答1MiB、absolute operation予算2s。CREATE_SESSION→SET_REQUEST→要求の直列化、UID/PID/session ID検査、再送なし | 実server起動なし。初期化と実運用の互換性・server側session解放は未検証/未実装 |
| `mozc_ipc_client` / pool | 同期ClientFactory依存を削除しper-IC非同期Submit/Cancelへ置換。GetConfig/readinessはcacheのみ | 共通client/IPC/protocolの再設計はしない。既存同期client資産は保持 |
| 接続準備/worker | profile解決・cache mutex/NSS、protobuf key/private path/readlink/UID/PID/exe/版/製品検証をworker上へ集約し、検証済み同一FDをloopへ移譲。取消をdeadlineにも反映 | thread生成/高負荷時の最大dispatch遅延、永久停止とruntime破棄の競合は未証明 |
| `input_pipeline` / `MozcState` | raw key/modifier/release/time/generationをconsume前にjournalへ保存。GET_CONFIG完了まで翻訳を保留。直列送信、unconsumedを一度だけ転送、token/weak lifetime/focus/IME検査。失敗・focus移動・破棄・配送中断では保留 | 実Wayland/X11のkeyup/repeat/compose、満杯時の新旧入力順序、安全な救済と終了時保全は未達 |
| UI/live/callback/再変換 | 完全なmodelを準備してから削除/確定/描画し、作用の間に寿命/focusを再検査。古い候補/page・callback generationを拒否。選択snapshot/cursor/anchor再一致。live flag/delayを各要求へ付与 | 実engine+frontend+serverによる全経路の動作、部分適用後の救済は未検証 |
| `async_config` | 明示されたFcitx設定root配下`conf/mozkey.conf`のみworkerでread/atomic rename。symlink/hardlink/他ownerを拒否、サイズ64KiB、取消でUI callbackを抑止 | 設定保存失敗のUI通知、実filesystem永久停止、rename前後の実障害注入は未検証 |
| 終了/独立性 | focus/切替/unloadは待たず取消。通常loop終了のみrootをsealして取消し最大390ms待機。自動server/browser/tool起動・clipboard fallback、同期GUI toolメニュー起動を無効化 | 390ms後のworkerとC++ static/runtime終了の契約は未証明。実共存試験なし |

journalはprocess内で最大32context、各16frame。配送中frameと不確定outcomeも保存し、中断後に次の応答で上書きしない。明示的なメニューで閲覧できるが、自動再送・自動確定は行わない。**メモリに残すだけでは入力保全の完成条件を満たさない。** 満杯後に新しいキーをpass-throughする場合の順序、process終了後の復旧、明示的な解決/破棄の手段は未完成。dead-key/composeも旧同期経路を引き継げておらず互換性未達。未配送キーを旧欄・別IME・新focusへ無条件に流す方法では解決しない。

製品source/libraryのコンパイルとfixture試験までであり、factory拒否の.soを入力可能な完成addonと報告しない。serverを起動しない方針で接続失敗はfail-closedとなる。DELETE_SESSIONと安全な非同期起動/回収は残作業。loaderの既定コードpinとprocess root所有は、jobが残る状態でruntimeを安全に終了できることの代替証拠ではない。

### 限定試験と測定

既存Phase 1 toolchain/repository・disk cache、隔離HOME/XDG、`--repository_disable_download --config=release_build --jobs=2 --//:linux_mozkey=true`を使用。`P2/run_product_verified.py`/対応logが最終ソースの12ターゲットと`stage_inputs`を実行する。以前のOFF61/ON79と52ケースの成功資産を保持し、今回はONで下記を確認した。cache hitは再実行と区別してBazel logに残す。

| 対象 | ケース | 結果 |
|---|---:|---|
| linux_deadline / client / ipc（従来の52） | 21 / 30 / 1 | 成功（入力digest一致のcache再利用を含む） |
| system_util / process_mutex / ipc_path_manager | 3 / 3 / 5 | 成功 |
| callback_queue / connection_preparation / frontend_boundary | 5 / 5 / 6 | 成功 |
| async_session + RPC | 6 | 初期化/順序、0/250/500/1000ms遅延、異session、完了中破棄、350ms準備中取消、無応答/切断/サイズ超過/取消で成功 |
| input_pipeline | 8 | consume前保存、一度だけの転送とkeyup、focus往復/破棄/遅延応答、満杯、初回設定待ち、RPC失敗、中断/破棄中の不確定結果保護で成功 |
| async_config | 4 | 専用ファイルwrite/read、target symlink・hardlink・parent symlink拒否、開始前取消で成功 |
| 合計 | 97 | 12ターゲット成功 |

新sessionのsocketpair fixtureは実serverではない。`ASYNC_SESSION_METRIC`の測定は、0/250/500/1000ms注入に対し応答0.759/250.785/500.874/1001.09ms、5ms heartbeatの最大間隔5.109/5.126/5.426/5.385ms。全て同じ短時間低負荷runの値であり、実画面のひらがな表示・実live変換、p99/1000要求、高CPU負荷での受入成功を示さない。

`/usr/bin/python3 tests/ubuntu/run_fcitx5_api_probe.py`はインストール済みFcitx5 Core/Utils 5.1.19とg++を使う独立プローブとして再実行し、`FCITX5_API_PROBE PASS`。Instance/IME/実serverは起動しない。`P2/logs/fcitx5-api-product-20261113.log`に保持した。

| 実API fixture | 今回の測定・解釈 |
|---|---|
| 正常 / 250 / live500 / live1000 | 10.043 / 250.109 / 500.170 / 1000.060ms。API応答値であり実製品の表示時間ではない |
| 350ms準備停止 | completion350.465ms、heartbeat最大遅れ0.094ms、69tick。実syscall検証前の有限sleep注入で、filesystem自体を止めた試験ではない |
| root取消/回収 | cancel0.009ms、heartbeat最大遅れ0.453ms、busy拒否/最終回収成立 |
| 通常process終了・350ms/3500ms停止 | 待機349.223msで完了 / 390.157msで未完了として打切り。終了400ms予算の限定測定であり、永久停止の安全回収成功ではない |
| owner destructor join負例 | 340.236ms・heartbeat遅れ340.175msを検出しREJECTED_FOR_PRODUCT。負例の検出成功を製品合格に数えない |

試験作成中、strict protobufの必須preedit fields不足でfixtureが失敗し、必要fieldsを補った。また単一EventLoopをexit後に再execする試験がESTALEとなり、単一run内の段階的timerへ修正した。入力安全条件や時間閾値を緩和して成功扱いしていない。daemon再起動時の最後のbuildはログ上成功したが親の終了code/metrics記録が途切れたため、稼働processがないことを確認して別名の最終runで確定した。

### staging・検査・CI

専用`mozkey_server`/`mozkey_tool`/factory拒否`fcitx5-mozkey.so`と定義・icons・通知・ライセンス・frontend sourceを、作業領域の新規stagingへ配置する。候補`P2/staging-async-candidate-20261113`は60ファイル、`INCOMPLETE_NOT_FOR_IME_LOAD`。ELF64/x86_64、exportが`fcitx_addon_factory_instance_mozkey`だけであること、RPATH/RUNPATHにbuild host pathがないこと、hash/manifestを検査。最終commit後は別ディレクトリへ同じ内容を再stagingし、revision・dirty=falseを確認する。旧成果物は保持し、ホストの`/usr`には配置しない。

buildifier `--mode=check -r src`、`git diff --check`、workflow YAML/埋込shell構文、Python構文、来歴JSONとCI target参照を検査する。actionlint/clang-formatは未導入につき未実行、新規導入なし。StandardPathのdeprecated警告は残る。静的調査したcached getterの利用であり、将来API更新と起動時初期化負荷は別途評価する。

軽量CIの既存Fcitx5 5.1.19+条件内に新規3ターゲット（async_session/input_pipeline/async_config）を追加した。既存trigger・権限・jobs=2・cache方針は保持し、SDKの追加導入・通常pushでの全面辞書/OS buildは行わない。Ubuntu 24.04でSDKがなければAPI/製品試験はNOT EXECUTEDのままであり、jobのsuccessをA5成功へ読み替えない。

**全6系統CIは未実行・未検証。** このセッションには`gh`とGH_TOKEN/GITHUB_TOKENがなく、接続GitHub toolもworkflow読取り/既存runのrerunのみでdispatchを提供しない。認証/権限の回避や新規ツール導入はしない。Secure Offline Checksを対象branchで手動実行する既存経路（`gh workflow run secure-offline.yaml --ref feat/ubuntu-phase2a`、またはActions画面）と同一head_sha成功確認が必要。これはPhase完了条件を免除するものではない。通常pushの実runはpush後に確認し、静的検証と分けて記録する。

### A1〜A7・次への条件

| 条件 | 判定 | 証拠/未達 |
|---|---|---|
| A1 基点・由来 | 実施 | SHA・ライセンス通知・採用manifestを更新、既存資産保持 |
| A2 独立性 | 部分実施・未達 | Mozkey ID/profile/IPC/path、実peer等の隔離負試験。全実行経路・実共存の最終証明なし |
| A3 build/staging | 部分実施・未達 | 専用成果物と無効化staging更新。入力可能addonの安全条件未成立 |
| A4 callback/UI | 部分実施・未達 | 非同期sender、完全command、candidate/live/再変換へ接続・build。実全経路の動作未検証 |
| A5 応答性・入力安全 | 部分実施・未達 | 新session/retention/取消/終了fixture成功。入力救済・終了時保全、実frontend・runtime終了未達/未証明 |
| A6 回帰 | 部分実施・未達 | 97限定ケースと実API fixture成功。全6系統CI、他OS、実表示/IMEは未検証 |
| A7 資源・保護 | 今回の範囲で充足 | jobs=2、取得禁止cache build、workspace約6.1GiB。元差分/76資産・旧staging保持、実IME/個人設定への変更なし |

2Aを完了扱いせず、2Bへは移行しない。次に必要なのは保留入力の救済/終了時保全・満杯時順序・compose/keyup、worker/runtime終了とserver lifecycleの解決、許可された分離環境での製品全経路検証、同一対象SHAの全6系統CI成功。安全性の未証明を数値目標の問題に置き換えず、危険な機能は有効化しない。

### 実装SHA・staging・push後の実CI

実装`ad266957235f2f0395c8a3a145097e9b3bc8e707`を`origin/feat/ubuntu-phase2a`へpushし、`git ls-remote`でHEAD一致を確認した。`P2/staging-ad2669572/manifest.json`は同じrevision、dirty=false、60ファイルで全hashを照合した。最終限定runは終了code0、15.326秒、workspaceピーク6,523,076,608 bytes、RX差分上限3,806,682 bytes（依存取得量そのものではない）。依存取得は禁止したまま、原worktree差分と基準76資産の保持を再確認した。

同じ実装SHAに対し、通常pushの実際のActionsは次の3件がcompleted/success（attempt1）だった。

| workflow | 実行結果 |
|---|---|
| Lint | [success / 43367332537](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43367332537) |
| Linux limited | [success / 43367332709](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43367332709) |
| Zenz runtime lock | [success / 43367332851](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43367332851) |

Linux jobログでは既存6targetが全てcacheによる成功、helper/非同期モデルPoCも成功だった。Fcitx5 5.1.19+ SDKがないため、実APIと新規3targetを含む製品境界試験は実際に**NOT EXECUTED**。新3targetの実行証拠は前述のローカル97ケースであり、CI実行成功ではない。Linux/Windows/macOS/AndroidのフルCIと全6系統成功は引き続き未検証。

この追記は結果記録のみで実装/受入判定を変えない。追記後のHEADでも新規stagingへ再配置し、作業ブランチとの一致、dirty=false、同じ60ファイルhashを確認して報告する。追記commitのCIを実装SHAの結果から類推せず、最終報告で対象を区別する。

## 再開後：終了・復旧・compose・応答副作用の限定実装（2026-11-13〜14）

**判定：承認範囲で実装を再開し、製品buildと15ターゲット138ケースが成功。2Aは未完了、factory常時拒否/Enabled=Falseを維持。** 未達を工程間のレビュー待ちへ置き換えず、独立して進められる実装・試験を継続した。今回も実IME/server/toolの起動・導入、既存IME/システム設定変更、sudo/apt、2B以降、main統合は行わない。

### 基点と保護

既存`P2/checkout` / `feat/ubuntu-phase2a`、開始HEAD `e54d3110f313880b6948c61b332be5ad9c83c89a`、開始時clean。originは既存SSH URLのまま。最新mainはGitHub APIで`b489fca1561bf12ff6b43205378220163d71c21c`を再確認し、正本文書・前回実装と結果を読んだ。原worktreeのステージ済み5ファイル/未ステージ差分のhash、基準76資産のサイズ/hash、既存cacheを照合して保持した。新しいbranch/worktree、stash/reset/rebase/force push/PRは作らない。

### 実装した変更

| 対象 | 今回の実装・確認 | 限界 |
|---|---|---|
| worker所有・通常終了 | Rootが1本のjoinable pthreadを所有。seal後はjob、最終タスク1件、TLS destructorまで含めtryjoin。成功時は実thread回収済み。400msから10msとserialize等の経過時間を引く。予算切れ後は最終保存も取消す | syscall中の永久停止を強制停止できず、未回収時のruntime終了安全性は未証明 |
| 復旧snapshot | private proto、version/size/checksum、0700 directory/0600 file、NOFOLLOW/owner/hardlink拒否、temp/RENAME_NOREPLACE、file/directory fsync。明示save/inspectと通常終了時の最終保存を接続。writer終了後の新processでdecode成功 | 1MiBを超える記録、保存不能、異常終了時の全入力保全、archive一覧/解決/容量管理は未完成 |
| 入力/寿命 | validな既知応答はfocus喪失後もoutcomeとして保護。同期sender/translator中の破棄もweak lifetimeで拒否。古いcallbackで不確定outcomeを上書きしない。実Core KeyEventと製品pipeline/translatorを接続した隔離試験 | queue満杯後の旧新入力順序と、実frontendのend-to-end配送は未証明 |
| compose | prefixを別dequeで保持、完成Unicodeは非同期key_stringへ。一度だけ文字列確定し、最終raw文字だけを誤転送しない。Escapeで明示取消、無効sequenceは保留。prefix/Unicodeもsnapshotへ保存 | 実XCompose table/locale、Wayland/X11と実IMEで未検証 |
| session解放/focus | addon所有の最大32件のCloseQueueが既存IDへDELETE_SESSIONだけを送る。contextのapp callbackを切り、owner終了時はqueue/IOを取消す。空状態のfocus returnでは新sessionへ進める | 取消/上限/失敗時のserver側資源解放、専用server起動・終了所有は未検証/未完成 |
| 応答副作用guard | 再変換拒否/空replacementでは選択を削除しない。snapshot/cursor/anchor、文字単位範囲、frontend capabilityを検査。invalid mode/UTF-8、consumedとresult/deleteの矛盾を保留。古いcallbackの確定/削除/挿入は捨てず保留し、純UIだけskip | 実appでの部分適用後の完全な救済は未検証。入力安全全体の合格ではない |
| callback Space | 無選択時のSPACE要求がunconsumedでも、現在generationでは文字列を一度だけ挿入。raw keyを誤転送しない | 実serverの全callback/macro系列は未検証 |

`mozkey-recovery/`はFcitx PkgConfig root内のMozkey専用subdirectory。snapshotには入力・周辺/選択情報が**平文**で含まれる。入力ログや暗号化保管ではない。今回書いたデータは隔離fixtureの固定文字だけで、実ユーザー入力は読取り/保存していない。future製品ではpassword/sensitive capability時に保存/閲覧を拒否し、surrounding capabilityがなければ旧周辺textを採取しない。新しいsnapshotは旧ファイルを上書きせず、privateな`latest` pointerだけを更新する。破損/更新失敗の既存ファイルも保持する。読取り表示は16KiB/UTF-8境界で制限し、元データを切り詰めない。自動key再送・自動commit・別IME/別欄への自動配送は行わない。

### 限定試験・実測

同じ隔離HOME/XDG、既存Phase 1 toolchain/repository/disk cache、`--repository_disable_download --jobs=2 --config=release_build --//:linux_mozkey=true`を再利用した。再現commandは`P2/run_resume_tests.py`と`P2/logs/resume-product-20261113.log`/metricsに記録。最終runは終了code0、48.692秒、workspace peak6,666,981,376 bytes（約6.21GiB）、RX差分上限251,057 bytes。RXは依存取得量そのものではなく、依存取得は禁止したまま。cold解析/compileとcache再利用を区別する。最後のrunのtestは全て入力digest一致のcache hit、変更した製品境界は再build成功。

| 対象 | 成功数 | 内容 |
|---|---:|---|
| 既存linux_deadline/client/ipc | 21+30+1 | 既存52資産を維持、関連回帰成功 |
| system_util/process_mutex/ipc_path_manager | 3+3+5 | 専用設定ONの関連境界 |
| callback_queue/connection_preparation/frontend_boundary | 5+5+6 | 既存callback、検証済みFD/translator等 |
| async_session | 11 | 初期化/500ms・1s/取消に加えDELETE_SESSION、retired owner保持、500ms解放遅延、addon取消、未作成sessionの非起動 |
| input_pipeline | 19 | consume前保存、keyup/modifier/time順序、focus、既知outcome、破棄/同期callback、compose/取消/失敗、Unicodeとcallback Spaceを一度だけ配送 |
| input_recovery | 11 | private保存・別process読取り、破損/サイズ/権限/上書き拒否、終了最終タスク、予算切れ後の非保存、index失敗、表示上限 |
| frame_translator | 4 | compose prefix/Unicode、modifier/keyup迂回、Escape/無効sequence、invalid UTF-8 |
| async_config | 4 | 既存の専用設定ファイル・symlink/hardlink拒否・取消の回帰 |
| response_guard | 10 | 再変換拒否、Unicode選択両方向、text/cursor/anchor変化、空replacement、範囲overflow、capability、mode/result整合、古い副作用 |
| 合計 | 138 | 15ターゲット成功。fixture入口1 skipは親からの子process起動で別途実行されるもの |

復旧のfixtureは同じtest binaryをposix_spawnする。writerが通常終了した後、別readerがファイルを読んでdecodeする。製品IME/serverの起動ではない。childのBazel premature-exit/XML環境を引き継がないようにし、親test結果との干渉を除いた。実Core試験もInputContextManager/KeyEvent/forwardKey/commitStringをsynthetic frontendへ接続するもので、Instance/addon/実appは起動しない。

| snapshot終了fixture | 最新logの待機 | 結果 |
|---|---:|---|
| 350ms準備後に保存 | 350.324ms | joined=1、durable=1。別readerで内容確認 |
| 3500ms準備停止 | 390.060ms | joined=0、durable=0、未回収/未保存と判定 |
| 10ms予算切れ後もfixtureを生存 | 10.059ms | 後で準備が完了してもsnapshotを新規保存しない。後続joinは可能 |
| index更新拒否 | 349.741ms | joined=1、durable=0。snapshot本体と旧indexを保持し、成功へ読み替えない |

3500msは永久停止の有限モデルで、実kernel/filesystemをD状態へ入れた試験ではない。診断assertのscheduler余裕を400ms合格基準へ読み替えず、実測と未検証を残す。full Fcitx processの終了総時間は未測定。

`tests/ubuntu/run_fcitx5_api_probe.py`も最終worker sourceで成功（`P2/logs/fcitx5-api-resume-final-source-20261113.log`）。thread/TLS両方の350ms・3500msを追加した。通常job350msは349.374msでjoin/TLS完了、TLSだけ350ms停止は350.596msで完了。3500ms job/TLSは390.109/390.058msで未回収を返す。0ms jobの取消・idle終了は1.066ms。既存の所有/FD/peer/API/loop試験もPASS。旧100/250msは参考値、ひらがな表示300ms・実live500ms/約1sの受入成功は未検証。

応答guard追加の初回コマンドは自動承認レビューの時間切れで**実行前に拒否**された。状態確認後、workspace内編集と既存runnerによる限定検証を分けて1回再試行し成功した。危険性が理由の拒否ではなく、権限の回避・新しいtool導入はしていない。

### staging・CI・残る境界

`stage_inputs`の専用server/tool、factory拒否.soをbuildし、staging scriptはprivate `.proto`も同梱するよう拡張した。新しいstagingだけを作り、旧60ファイル等を保持する。ELF64/x86_64、単一factory export、host RPATH不在、manifest/hashを検査する。実行時パスは`/usr/lib/mozkey`のままで、stagingをPATH/実行経路へ混ぜない。

軽量CIの既存SDK条件へinput_recovery/frame_translator/response_guardを追加した。trigger/permissions/concurrency/cache方針は不変、SDKの追加導入なし。Ubuntu 24.04のSDK不足時は実API/製品境界がNOT EXECUTEDであることを維持する。buildifier check、YAML/埋込shell/Python構文、git diff --check、来歴manifestと参照targetを検査。未導入actionlint/clang-formatを追加取得しない。StandardPath deprecated警告は残る。

全6系統CIはこのセッションの`gh`/token/dispatch tool不足で未実行・未検証。認証の回避・別branch作成・main変更で代用しない。通常pushの実CI結果はpush後に対象SHAで別途照合し記録する。

| 条件 | 現在 | 残件 |
|---|---|---|
| A1 由来 | 実施 | 固定SHA/通知/来歴更新、元資産保持 |
| A2 独立性 | 部分実施 | 専用ID/profile/IPC/実行pathを保持。実共存・起動所有の最終証拠なし |
| A3 build/staging | 部分実施 | 専用成果物と導入禁止staging更新。入力可能addonではない |
| A4 callback/UI | 部分実施 | compose/再変換/副作用guard/Spaceを接続・限定検証。実macro/UI全経路未検証 |
| A5 応答性・入力安全 | 未達 | finite終了保存/join成立。満杯時入力順序、永久I/O/保存不能/部分適用後の完全復旧は未解決 |
| A6 回帰 | 部分実施 | 138成功、実API fixture成功。全6系統/実IME/他OS未検証 |
| A7 資源/保護 | 今回範囲で充足 | jobs=2、60GiB/新規取得2GiB以内、元差分/76資産・旧staging保持、ホスト設定変更0 |

factoryを外す条件は揃っていない。とくにqueue満杯で新キーが先にpass-throughし得る現行動作、保存に失敗したままのprocess終了、永久停止したworkerとruntime破棄、archiveの解決/容量管理、専用server非同期起動/終了所有が残る。安全性を省略せず無効のままにする。2A完了扱いと2B移行は行わず、承認済み実装の進捗・未達・実行禁止の検証を区別する。


### 再開実装のGit反映・実CI・staging

実装`14d73f127525557c5e341fdf4116f82fe024a9f9`を`origin/feat/ubuntu-phase2a`へ通常pushし、`git ls-remote`で一致を確認した。`P2/staging-14d73f127`はrevision一致/dirty=false/70ファイルで全hashを再照合。ELF/export/RPATH検査も成功。既存stagingと元差分/76資産を保持した。

同じSHAの通常pushは[Lint 43541197759](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43541197759)、[Linux limited 43541197749](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43541197749)、[Zenz 43541197753](https://github.com/Minoru-kuma/mozkey_forubuntu_mk/actions/runs/43541197753)が全てcompleted/success。Linuxの実job logではSDK不足により実APIと製品境界がNOT EXECUTED、既存6ターゲットは成功だった。138ケースと実APIの証拠はローカル隔離環境であり、CIで同じ試験を実行したとは扱わない。フル6系統は未実行・未検証のまま。

この追記は検証結果のみ。最終報告では追記HEADのpush/remote一致と、そのHEADに対応する新規staging/CI状態を確認し、上記実装SHAの成功から類推しない。


## 2026-11-14 継続実装：入力応答・keyup・snapshotの安全性修正

再開基点は`6865199fa58952609aab6dd582f49ab906bab22a`、同じ`feat/ubuntu-phase2a`を使用。最新mainをGitHub APIで再照会し`b489fca1561bf12ff6b43205378220163d71c21c`を確認。開始時の当worktreeはclean。元worktreeのステージ済み/未ステージ差分hash、Phase 1/2Aの76資産hashとcache存在を再確認し全て保持した（`P2/logs/resume-protection-20261114.json`）。

### 変更と安全契約

| 箇所 | 実装・検証範囲 |
|---|---|
| InputPipeline / AsyncSession | frontend/app callbackが戻る前に次要求を予約しない。再入中の入力追加入力、保留、所有者破棄、Cancel/Closeを区別。lifetimeと取消generationを確認し、二重予約/適用中journalの上書きを防止 |
| 物理キー対応 | 同一activation内のraw押下転送を最大256種類保持。消費済みkeydown/compose Unicode置換の孤立keyupを抑止。転送済みmodifierのkeyupをserver consumedに関係なく転送。反復とhardware code一致、activation前の不明releaseを検証 |
| InputRecovery | 3箇所のframeのcompose/文字列/selection/request制限をEncodeとDecodeで整合。frame/outcomeごとの集約上限確認。成功保存だがDecode不能となる経路を閉じ、失敗時は元入力と出力bytesを保持 |
| 保留通知 | suspended判定をEnsurePipelineより先に置き、到達不能だった通知を修正。自動focus/打鍵通知は本文を含めず、全context本文の表示は明示inspectだけ。非focus/他IME/password/sensitive表示を拒否 |

### 限定検証

`P2/run_delivery_tests_20261114.py`初回は新しい3再入試験が`EventLoopException: Stale file handle`で失敗した。sd-event backendをexit後に再execした試験方法が原因で、成功として記録しない。SDK公開EventLoopInterfaceを用いる有界Defer schedulerの故障注入へ修正し、`P2/run_delivery_v2_20261114.py`で15ターゲット成功（34.962秒、RX上限155,849 bytes、workspace peak 6,820,241,408 bytes）。

続いて押下対応を追加した`P2/run_key_ledger_20261114.py`は製品`stage_inputs`と15ターゲット全成功（68.495秒、RX上限313,399 bytes、peak 6,823,591,936 bytes）。成功151ケース、fixture子プロセス入口1件の意図したskip。前回の138成功から再入3、session2、codec3、keyup/反復/実Core modifier5を追加した。旧52ケースを含むlinux_deadline21 + client30 + ipc1の資産・成功を保持。取得禁止・jobs=2・既存repository/disk cache・隔離HOME/XDGを継続した。

再入3ケースは制御scheduler上の故障注入であり、実frontendやsd-eventのnested execの成功とは扱わない。キー/keyup/modifier/composeの通常Core fixtureは実SDKを使用。実Wayland/X11、各app、localeの実XCompose、製品IME/serverの動作と表示時間は未検証。sourceの3箇所のKeyStates初期化警告を修正し、通知修正を含む最終ソースを`P2/run_input_safety_final_20261114.py`で確認した。製品buildと15ターゲット成功（46.254秒、RX上限218,745 bytes、peak 6,823,682,048 bytes）。この最終確認でテストは全て入力digestに対応したcache再利用で、新規実行とは区別する。追加ケースの実行証拠は前段のlog。既存のStandardPath deprecationは残る。

今回の新規変更はLinux専用Fcitx5実装と関連文書に限定し、共通client/protocol/IPCの機能変更、依存取得、実IME/server/tool起動、Hazkey/システム変更、2B以降/main統合は行わない。新規snapshot形式versionの変更・旧snapshotの削除や自動再送も行わない。

### 継続課題

A1の由来・保護とA7の資源条件を維持。A2/A3/A4/A6は専用経路・build・限定fixtureの部分実施、A5は未達。queue満杯時の順序/救済、保存不能/永久I/O時の保全とruntime終了、archiveの明示的解決/容量管理、専用serverの非同期起動・終了所有は未解決。実frontend/実serverと全6系統CIも未検証。factory常時拒否・Enabled=False、2A未完了・2B移行不可を維持し、承認範囲の独立した修正・検証を継続する。
