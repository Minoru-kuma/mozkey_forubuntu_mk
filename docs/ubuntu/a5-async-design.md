# A5設計・暫定受入条件：イベントループの継続性

**再開後の最新境界（2026-11-13〜14）：native worker join/TLS、通常終了の最終snapshot、compose保持、非同期session解放、再変換/削除guardを実装・限定検証した。工程間レビュー待ちではない。危険な機能の有効化と2A合格は、残る安全条件・実frontend/実server・全6系統CIの証拠が不足するため保留する。以下の過去記述と差がある箇所は末尾と[最新結果](phase-02-results.md)を優先する。**

**2026-11-13製品側接続の現状：同期ClientFactoryをIO/Timer/Defer駆動のclient/sessionへ置換し、準備済み同一FD、キーjournal、取消・UI適用ガード、worker上の設定I/Oを接続した。18新規ケースと関連回帰、実APIの隔離試験を実施するが、実IME/serverでの成功を意味しない。入力の安全な救済と終了時保全、実keyup/反復/compose、worker/runtime終了、server起動/セッション解放が未達・未検証。factoryは引き続き常に拒否。最新判定は[実行結果](phase-02-results.md)末尾を優先し、過去の保留/停止は当時の記録として保持する。**

## 現行方針と独立工程（2026-11-12追加指示）

本節を以下の旧基準/停止判定に優先する。前回のjoin負例で不適合だったのは入力loop内のowner破棄であり、全方式の実現不能ではなかった。A5未解決を理由に独立工程を全停止せず、2Aの分離・ビルド・限定検証・stagingを進める。未達部分を合格扱いせず、実IME起動/導入と2B以降は行わない。

| 指標・必須条件 | 現行の扱い |
|---|---|
| 打鍵→ひらがな表示 | 300ms未満を初期目安とする。入力受付からfrontendでの表示までを測る。今回の合成応答250msは代替証明ではない |
| ライブ変換 | 500ms程度を許容し、1秒前後も実測・評価。送信/設定delay/実表示を分ける。遅い応答中も入力loopを継続する |
| 通常入力・focus移動・IME切替 | 応答/準備/worker終了を同期待機しない。focus/切替に400msの終了予算を使わない |
| 通常終了 | 最大400msの待機を許容。候補は実プロセス終了だけに限定し、待機予算390ms＋終了処理の余裕10msを用いる。一般的なOSスケジューリングまでの上限保証は未証明 |
| 旧100ms/250ms | 履歴比較用の参考値。超過のみで全工程を停止しない。既存deadline試験の診断条件は保持する |
| 入力・資源・独立性 | 異常I/Oの無期限同期待機、文字消失、二重確定、誤配送、他IME干渉は禁止。所有者なしのdetachや無限worker生成で回避しない |

### 今回の実装境界

`PreparationWorker`はプロセス寿命のrootがstd::threadを直接1本所有する。rootは静的破棄を行わず、コード保持はFcitx5 5.1.19のDefaultHint/PreventUnloadHintが前提。要求データとnotification FD、未移譲結果FDはjob/state自身が所有し、addon/IC/loopをcaptureしない。Cancel/owner破棄はjoinせず、停止jobがある間は追加Startを拒否する。完了時にworkerがrootのslotを解放し、取消済み結果は閉じる。正常時も接続FDの移譲は一度だけ。idle時のworkerはrootに所有されたままプロセス終了で回収される。query/終了待ちだけで新規threadは生成しない。

`StartConnectionPreparation`は明示endpointの値コピーだけをworkerへ渡す。既存Linux専用transportの`PrepareLinuxDeadlineSocket`で、実profile/key protobuf、private権限、全祖先のsymlink拒否、製品prefix、peer UID/PID/実行パス、protocol/product版を検証し、**同じnonblocking/CLOEXEC FD**を移譲する。UI側で再接続しない。metadata検証からのTOCTOU/exec/PID再利用を全解決したとは扱わない。100ms deadlineの旧clientは診断資産として残し、通常キーから呼ばない。

`CallbackQueue`は完全なSessionCommandをコピーし、consumed=falseでも有効callbackをtimerへ予約する。0msも次のdispatchへ渡し、1 ICあたり同時timer 1件、即時連鎖16回を上限にする。IC/activation epoch・focus・現在IMEを適用時に照合する。新入力/reset/focus/切替/config/session変更で失効し、キー自体はtimerから再送しない。callback受信側と描画側を製品非同期clientへ接続する作業は残る。

### 残る製品接続条件

移植したフロントエンドの同期Client interfaceを非同期送信/適用へ置換し、KeyEvent受付時のconsumeと後続consumed=false配送を確定させる必要がある。focus/IC破棄後の未配送入力、復旧UIと一度だけの配送、実周辺テキスト/再変換/マクロ/候補世代、live ON/OFF設定の経路は未完成。今回の救済レコード保持だけで文字消失防止を保証しない。

workerの隔離試験はコード保持後の取消と有限350ms/3.5秒停止を確認した。製品のprotobuf処理/依存ライブラリの終了処理との競合、D状態など実kernel I/Oの永久停止、実AddonManager/Instanceの終了順序、thread作成のcold start/高負荷は未検証。400msの数値調整だけでこれらを合格にしない。

したがって専用`.so`のfactoryは常にnullptrを返し、addon定義もEnabled=False。これは明示的な導入禁止のstagingで、機能完成したIMEではない。独立工程を実施した結果と未完成部分を区別し、A5/2A全体は未達のままとする。実測・試験数・成果物は[結果末尾](phase-02-results.md)参照。

**現行の承認（2026-11-12）：ユーザーは2A工程1〜6の実装・ビルド・限定試験・stagingを再承認し、都度レビュー停止を不要とした。以下の旧「製品統合未承認」「レビュー後」は過去の承認状態である。今回の停止は承認不足ではなく、末尾の所有/終了ゲートと入力安全の未解決による安全停止。2A完了・A5合格ではない。**

ユーザーの追加指示により100ms/250msは過去と比較する参考測定値とする。最優先はFcitx5の入力イベントループをIPC応答待ちで長時間停止させないこと。systemd・外部常駐サービスを利用せず、helperを必須としない。新A5基準の合格・Phase再開はレビュー後とし、この文書は実装完了を意味しない。

## 比較と推奨範囲

| 構成 | 利点 | 残る制約 | 判断 |
|---|---|---|---|
| Fcitx5 IOイベント＋timer＋nonblocking Unix socket | 同じスレッドで順序・取消・UI寿命を管理。worker/子の回収不要 | filesystem/peer検証は非同期socketだけでは解決しない。物理キーの非同期処理も要検証 | 第一候補。検証済み接続を受け取って以降の成立性を先に確認 |
| 単一直列worker | profile/readlink等を入力スレッドから移せる | 停止中syscall、join、unload後のコード寿命は残る。参考値化しても無期限待機は容認しない | Fcitx5内の生存期間・終了契約が確定する場合だけ再検討 |
| helper process | 停止する検証を隔離できる | 安全な起動・取消・unload後の回収が必要 | 必須にしない。既存PoCは比較資料として保持 |
| 事前準備だけ | 通常キーから初期化を減らせる | UI callbackで同期準備してもevent loopは止まる。metadataだけのキャッシュでは実接続の検証を代替できない | 上記と組み合わせる。単独解決とは扱わない |

Fcitx5 5.1.19公開APIのaddIOEvent/addTimeEvent/addDeferEventを使う。最初のPython PoCは類似モデルだったが、後続の独立C++ probeで実ライブラリの限定検証を行った（末尾参照）。初期設計の外部サービス案は現方針の対象外。

## 状態・所有権・安全条件

状態はUnprepared → Preparing → Ready → Sending → Receiving → Applying → Ready。失敗はFailed、停止はClosing → Closed。ICごとに一つのsession、同時に一つの送信中要求と最大16件の保留入力を上限案とする。送信済み要求を後続live要求で上書きしない。送信前のlive再予約だけは旧予約を失効できる。上限超過時は当該ICをFailedへ移し、未確定文字と保留キーの救済UIを保持する。黙ってキーを捨てない。

- IO callbackで応答待ちのpoll/select、sleep、join、waitpidや再帰的な要求ループを行わない。EAGAINは登録イベントへ戻す。IO処理は一dispatchあたり64KiB以下・有限回、応答全体は既存1MiB上限を維持する案。大きい処理はdeferへ分割する。常時POLLOUT登録によるbusy loopを避ける。
- 現行の一接続一要求・SHUT_WR/EOF framingとserver protocolを維持する。CREATE/SET_REQUEST/KEYの状態遷移を非同期に継続し、各RPCで予算や要求番号を巻き戻さない。プロトコル全体の再設計はしない。
- timerは期限超過をイベントで検出して該当FDと要求を失効させる。準備・通常要求・cleanupは別の期限ポリシーを持ち、期限は応答採用と資源制限のために使う。長いRPC timeoutを同期待機へ転用しない。
- ICのwatch参照/UUID、activation/session epoch、要求番号、focus・現在IMEを完了時に照合する。適用前に要求を一度だけ完了扱いにし、同じresultの重複commitを防ぐ。focus out/reset/IME切替/破棄はepoch更新とwatcher解除を先に行う。
- preedit/候補は有効な新応答を検証するまで保存する。失敗・部分応答・取消でClearAllしない。結果不明なcommit/キーは再送・再生・自動確定しない。Failed時の保持表示・明示取消/再入力を設計する。焦点移動後に旧欄へ非同期確定しない。従来の同期SUBMIT許可はこの非同期案では引き継がない。
- 未準備かつ未確定入力がない場合だけ、新規キーを通常pass-throughする。送信したキーを同時にpass-throughしない。送信中はkeyupを含む順序情報を保存する。consumed=falseの応答時にはFcitx5 forwardKey等による一度だけの転送が候補だが、focus移動、timestamp、modifier、再入、Wayland/アプリごとの順序保持を確認するまで物理キー全面非同期化を採用しない。callbackだけのPoCで先頭文字・Spaceの安全を保証しない。
- Closingでは世代更新→IO/timer解除→FD解放。応答が来るまでdestructorを待たせない。未実行DELETEを正常削除と報告しない。残存sessionの回収とserver上限は別課題。worker/helperを追加しない範囲ではunload後に実行コードを残さない。
- 起動・profile/key・実peer UID/PID/実体パス・製品版の検証は省略しない。addDeferEventへ移しただけでblocking I/Oが非同期になるとはしない。検証済みFDを通常経路へ渡す準備方式、ログsink/メモリ初期化、cold startの停止は未解決。filesystem停滞を含めた方式が決まらない間は準備実装を製品へ接続しない。
- Mozkey専用profile/IPC/実行パスのみを使う。Hazkey/Mozcへfallback・signal・設定書込みしない。プロセス全体のSIGCHLD設定や他addonのevent sourceを変更しない。既存IPC製品ID分離も完了扱いにしない。

## 受入・測定基準案

100ms（要求全体）・250ms（障害復帰）は履歴比較用に必ず記録するが、それだけで停止/合格を判定しない。準備時間、要求のend-to-end、キー受付callback時間、IO callback時間、heartbeatの予定時刻との差、取消完了、最終資源回収を別々に測る。

今回の合成PoCは全6シナリオで5ms周期イベント107〜108回、最大予定時刻遅れ0.382ms、最大IO callback時間0.140ms、即応10.319ms、350ms遅延応答351.923msだった。350msの応答を待ちながら他イベントが進むため、応答時間超過のみで不合格としない設計の根拠となる。ただし実Fcitx5・CPU負荷時の根拠には不足する。

レビュー候補は次のとおり。数値は今回の低負荷モデルに余裕を設けた提案であり、確定閾値ではない。

| 指標 | 提案と条件 |
|---|---|
| キー受付/IO callback | p99 5ms以内・最大20ms以内を低負荷時の候補とする。同期応答待ち0、有限処理量を静的にも確認 |
| 周期イベントの遅れ | 5ms heartbeatのp99 20ms以内・最大50ms以内を低負荷時候補とする。350ms/2s/無応答/切断でも進行すること |
| 応答/準備時間 | 100ms/250msを参考記録。初期案として通常要求1秒で取消、準備2秒で状態表示更新を検討。継続処理は許可するが無限queue・無限retryは禁止。操作別の妥当値は実server測定後に決める |
| 入力安全 | 古い応答適用、二重確定、意図しないpreedit消失、入力欄間混入、他IME接続/変更は0。時間目標と交換しない |
| 資源/終了 | IC破棄後のwatcher/FD/保留要求残存0。正常DELETEと不明な終了を区別。無期限join・放置thread/processは不可 |

無負荷および記録したCPU/メモリ負荷で各1000要求以上、median/p95/p99/maxとOS・loop backend・sample数を保存する計画とする。今回の短いPoCはmaxのみで、p99や新基準達成の証明には使わない。負荷時に長時間停止を検出したら原因を分離し再レビューする。旧helper PoCの100/250msアサートは旧構成の診断として保存し、新A5の合格閾値へ流用しない。

停止条件は、入力安全の違反、UI上の長時間同期待機、制御不能な準備I/Oや終了待ち、queue/resource増殖、他IME干渉、未検証のキー順序へ依存すること。参考値超過だけで停止しないが、原因不明の大きなloop停止は保留とする。新A5の合格は上記レビューと実測条件の合意後に判定する。製品統合・IME起動・次工程の承認は含まない。

## 実Fcitx5 APIで確認できた範囲

`tests/ubuntu/fcitx5_api_probe.cc`と`run_fcitx5_api_probe.py`は、既存Fcitx5Core/Utils 5.1.19をリンクする独立プログラム。Instance・addon・IME・実serverは起動せず、合成socketpairと合成frontendを使う。IO、Timer、Defer、exit、source破棄、focus/IC破棄後のwatch無効化を実APIで確認した。現在のbackendはsd-event（既存ライブラリのloop実装であり、systemdサービスを起動・接続する設計ではない）。libuv backendは未検証。

単一スレッドのIO待ちで他イベントは進む。Timerのaccuracyを0にするとsd-eventの既定のcoalescingが入るため、5ms heartbeat試験はaccuracy=1usを明示する。callbackからの例外はbackendでfatalとなり得るため、fixtureの失敗もcallback外で判定する。Deferへ同期停止を移してもイベントループは止まる。350msの負例で最大周期遅れ350.095msを検出した。これは許容・合格事例ではない。

InputContextManagerはInstanceなしで作成できる。合成ICのfocusIn/focusOut、UUID検索、破棄後watch無効化、KeyEventのrawKey/release/timeとfilterAndAccept、forwardKeyのpress→release→commit配送順序を確認した。ただし、`keyEvent`はその呼出し時点の結果を返すAPIであり、後で応答が来てから元の戻り値を変更できない。`forwardKey`を遅延して呼べることと、Wayland/X11/frontend/アプリをまたいで元のキーと同じ意味・順序になることは別である。

`setBlockEventToClient`/InputContextEventBlockerは公開ヘッダーでfrontend専用とされる。合成frontendでqueue順序を試験しただけで、Mozkey addonが非同期キー消費のために使用してよいことを証明しない。focusを失ったICへの`keyEvent`はfalseとなったが、forwardKey/commit側に同等のfocus検査を期待せず、適用側でwatch・focus・epochを検査する。実engine経由のconsume、キー転送の再入、modifier/keyupのアプリ配送、実候補/未確定文字描画は未検証。

移行判定は「非同期socket段階の限定実装候補は成立、A5製品実装への移行は保留」。一接続一要求の現行IPCでは新規接続ごとのkey/peer検証が必要であり、検証結果のキャッシュだけで省略しない。regular fileのopen/readやreadlinkの停滞をevent loopから隔離し、取消・unload時の所有権も成立させる準備方式は未確定。defer、O_NONBLOCK、単純workerへの移動だけで解決したとはしない。外部常駐サービスへ依存しない方針を維持し、実serverを使う版/session/起動・復旧検証も未実施のままとする。

## 2残課題の追加隔離検証

実InputContextManager/KeyEvent/forwardKeyに、テスト側の直列非同期キューを重ねて、Shift press・Shift付きA press/release・Shift release・消費するx press/releaseを350ms保留する試験を追加した。安定focus時は4件を元のrawKey/modifier・release・timestampで順番通り転送し、xは1回だけcommitする。重複完了は排除。focus移動・一度戻った場合はepochで旧完了を拒否し、保留6件を救済レコードとして保持する。ただしInstance/engineはなくkeyEventの返却は0、filterAndAcceptとconsumed方針はテストが与えたもの。実入力受付が既にpass-throughしたキーを後でconsumeできるという証明ではない。frontend側の実表示・入力救済を実装せず文字消失が解消したとは扱わない。

準備候補は「Fcitx5内の長寿命ownerが1本の直列workerを所有、workerだけがprofile/key・connect・SO_PEERCRED/readlinkを処理し、eventfdでroot loopへ完了通知する」。試験では専用一時profileの実open/openat/fstat/read、private権限/symlink拒否、同じテストプロセスがlistenするランダム抽象Unix endpointへの実connectとSO_PEERCREDのUID/PID、/proc/PID/exe照合を実行した。検証した同一FDを所有権移動し、再接続しない。350ms停止モデル中もroot loopのheartbeatが進み、50msで模擬addonを閉じてもownerを保持して未採用FDを回収できた。workerはInputContextを参照しない。

ただし、これはfixtureにroot ownerを置いた試験。製品でそのownerをFcitx5終了/unloadまで保持するAPI/lifetimeは未確定である。joinは有限fixtureの完了確認後にloop外で行うだけで、実I/Oの永久停止、プロセス終了時のjoin、addonコードunloadの安全を保証しない。FDを通知する前の取消・検証済みFD到着前の取消を区別し、後者は検証成功でも適用せず閉じる。外部サービスは使わないが、owner条件を満たすまでこの方式を製品へ接続しない。

key形式はfixtureのPIDテキストで製品protobufではなく、profileの全祖先、製品prefix/版、改変・inode/exec/PID再利用競合、cold filesystemを網羅しない。connectはその場で成功したfixtureだけを扱う。実Mozkey/Hazkeyへは接続せず、実serverの起動/版/session試験も未実施。したがって移行可否は引き続き「隔離候補の局所成立、A5製品実装は保留」。参考100ms/250msを超えた準備完了時間だけで不合格にせず、loop継続性と入力/資源安全を別に評価する。

## 2A再開時に適用するA5必須条件（2026-11-12）

今回の承認に基づき、次を製品接続の必須条件として扱う。100ms/250msを合否閾値へ戻さない。前掲p99/maxの数値は十分なサンプルがないため測定候補のままで、都合のよい新閾値による合格は行わない。

| 条件 | 必要な証拠 | 今回の判定 |
|---|---|---|
| loop継続 | キー/IO/timer/終了callbackに応答待ち・filesystem I/O・join/waitを置かず、処理量を有限化。350ms/2s/無応答等でheartbeatが継続 | 非同期socketの短時間fixtureは成立。終了join負例は不適合。2s/負荷/p99は未検証 |
| 所有・終了 | addon/IC/loopの破棄後に参照せず、取消した結果/FDを採用しない。停滞・再ロードでworker/queueを増殖させず、所有と回収責任を明示 | 長寿命fixture内のみ成立。製品終了契約は未解決 |
| 入力安全 | 最初のKeyEvent戻り値と送信対象を一意に決定。keyup/modifier順序、consumed=falseの一度だけの配送、focus/IC破棄後の未配送入力保護を保証 | 合成受付方針のみ。実engine・frontendの配送/救済は未検証 |
| IPC境界・独立性 | 完全な準備検証→同一FD移譲→非同期送受信→epoch検証→一度だけ適用。専用profile/prefix/peer/版を維持し、他IMEへfallbackしない | PIDテキストkey/同一process peerのfixtureまで。製品protobuf/版/異製品sentinelは未統合 |

### コード保持とowner寿命の再確認

導入済み5.1.19の`fcitx-utils/library.h`では`LibraryLoadHint::DefaultHint`に`PreventUnloadHint`が含まれる。[公式addonloader.cpp](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/addonloader.cpp)も既定でこのhintを使う。一方、[AddonManager::unload](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/addonmanager.cpp)はaddon instanceを破棄する。従来の「コードunloadが未確定」という調査だけでは不十分だった。コードをpinできることと、workerが使うowner/通知先/ICが生き続けることは別の条件である。

`worker_owner_fixture.cc`は350ms停止workerを持ち、destructorで取消フラグ設定とjoinを行う負例。`fcitx5_owner_gate.h`は実Fcitx5 Libraryでロード/ラッパーunloadし、RTLD_NOLOADでコード保持を確認した上で、実EventLoopの10ms timerからownerを破棄する。測定は破棄340.195ms・heartbeat最大遅れ340.208ms・周期41回。`REJECTED_FOR_PRODUCT`の検出成功であり、A5成功ではない。実AddonManager/IMEのload/unloadは実行していない。有限sleepは停止I/Oのモデルであり、実filesystemを350ms停止した試験ではない。

| 終了方式 | 判定と残件 |
|---|---|
| addon destructorでjoin | 停止中syscallが取消を見ない間、入力loopを止める。負例で不採用 |
| ownerを破棄してdetach | owner/eventfd/loopへの遅延参照があればUAF/FD再利用競合。コードpinだけでは解決せず不採用 |
| コードpin＋プロセス寿命の専用registry | 候補として残る。job自身が資源を所有し、addonへの参照を断ち、取消後のFDを閉じる設計が必要。永久停滞時の所有、再ロード時の上限、プロセス終了時の静的破棄との競合、最終回収を未検証。今回この方式が不可能だと証明したわけではない |
| Fcitx5/coreへの新しい回収owner追加 | API/所有契約の拡張に当たり、既存環境やFcitx5本体を変更しない今回の範囲では採用しない |

### キー配送と製品IPCへの接続を保留する理由

非同期送信の前にfilterAndAcceptしないと、元のキーがpass-throughした後のserver応答で遡及consumeはできない。先にconsumeすれば、consumed=falseの応答を待つ間にfocus移動/IC破棄が起きた場合の未配送入力を誰が保持し、どこへ一度だけ届けるかが必要になる。旧IC/新しい欄へ無条件forward/commitする方法や、結果不明の要求の再送は入力安全条件に反する。既存試験の救済6レコードはこの製品側の配送/救済を実装していない。前処理で消費を決定する方式も、現在のserverのconsumed判定との等価性は未証明。

したがって今回は、コードpin＋joinを安全解として接続せず、安全停止する。全方式の一般的な実現不能を主張するものではなく、現在の製品境界で安全性を立証できなかったという判定。製品移行にはregistry方式の所有/終了検証と、focus/破棄を含む未配送キーの明示的な保護・配送契約および隔離試験が必要。同期clientをUIから呼ぶ、検証を省略する、外部常駐サービスへ移す、といった回避は採用しない。


## 製品側非同期境界・所有契約（2026-11-13）

`MozcState`は物理キーのraw symbol/code/modifier、release、時刻、generationを最大16件のprocess-owned journalへ格納してからconsumeを返す。初回GET_CONFIG完了まで翻訳/送信をdeferし、CREATE_SESSION→SET_REQUEST→GET_CONFIG/各要求を直列化する。送信対象はworkerがprofile/protobuf key/private path/readlink/UID/PID/exe/版/製品を検証した同一FDであり、loopへ戻って別socketへ再接続しない。各要求でSO_PEERCREDとsession IDも照合する。IPC失敗をserver自動起動、別IMEへのfallback、要求再送で隠さない。

filesystemとprofile cache/NSSのmutexはworker上へ置く。`AsyncConfigFile`も明示的なFcitx PkgConfig root配下の`conf/mozkey.conf`だけをworkerで読む/一時ファイルからrenameする。loop上のStandardPath::userDirectoryは5.1.19のcached getterを使用する（deprecated警告あり）。`AsyncRpc`はMSG_DONTWAIT、IO event、absolute Timer、完了Deferを使い、1dispatch最大64KiB、要求/応答最大1MiB、operation初期予算2s、worker busy時5ms再試行を予算内だけ行う。これは入力表示の受入目標を2sへ変更したものではない。無応答時にloopを止めず失敗を確定する上限である。OSのschedulerとsyscall自体の厳密な上限は保証しない。

応答はtokenで一度だけ配送し、UI/modelを組み立ててから削除・確定・描画する。それぞれの外部作用の間にIC watch/State lifetime/focus/IME/generationを確認する。再変換は選択snapshot/cursor/anchorの再一致を必要とし、古い候補ID/page callbackもgenerationが違えば採用しない。live設定とdelayを要求へ付け、callback commandは全フィールドを保持する。応答に含まれるtool/browserの自動起動、clipboard fallback、未検証の同期toolメニュー起動は無効。

focus移動/IME切替/IC・addon破棄は応答待ちもjoinも行わず、timer/IO/FDを閉じて入力を保留する。物理入力がある応答適用中に破棄された場合もdelivering frameと不確定outcomeを残し、次の要求で上書きしない。記録は最大32context、process内だけで保持し、回復メニューは明示的な閲覧だけを行う。結果不明のcommitを自動再送/確定しない。

workerはprocess rootのjoinable thread memberが単一slot/jobを所有する。jobはUI/IC/addonへの参照を持たず、取消後のFDは閉じる。Fcitx5既定loaderのコードpinを前提とし、通常loop終了時だけ全ownerを取消しrootをsealして最大390ms待つ（400msから10msをdispatch/unwind用に留保）。390ms経過後にもworkerが止まらない場合のC++ runtime/static破棄との競合や最終回収は未証明であり、単にownerが存在することを「安全な終了完了」とは扱わない。focusやunloadへこの待機を転用しない。

### ロード解除までの必須未解決条件

- 保留入力を安全に取り出す/復元する手段、明示的な解決・破棄の承認、process終了時の保全がない。memoryに残すだけでは文字消失禁止の完成条件を満たさない。
- queue満杯では新しいキーをconsumeせず旧キーを保留するため、appが受ける順序と旧入力救済の整合が未証明。Wayland/X11のkeyup、repeat、modifier、dead-key/composeを実frontendで検証していない。以前の同期compose経路をそのまま使わず、今の経路ではcompose互換性は未達。
- UI/候補/live/callback/再変換の製品接続はコンパイル済みだが、実engine/frontendとserverを使うend-to-end試験は未実施。削除後・確定後にfocusが変わる部分適用の安全な救済も未完成。
- 準備I/Oが永久停滞したworkerとruntime終了の契約、実AddonManager終了順序、初回thread生成/高負荷での最大loop遅延は未証明。
- 専用serverの安全な非同期起動・回収とDELETE_SESSIONによるserver側session解放は未実装。起動済みと仮定したfixtureの成功を通常利用成功へ読み替えない。

新目標は300ms未満の暫定表示目安、live500ms程度/約1sの評価、通常終了のみ400ms。旧100/250msは参考値として残す。短時間の合成測定や限定ケース成功をA5合格とせず、上記安全条件と全6系統CIの証拠が揃うまでfactoryは解除しない。


## 再開後の終了・復旧・応答適用契約（2026-11-13〜14）

job完了通知とthread終了を分ける。Rootはjoinable pthreadを1本だけ所有し、通常終了時のseal後はjob・値だけの最終保存タスク・TLS destructorsまで進め、`pthread_tryjoin_np`の成功を回収完了とする。400msから10msを留保し、serialize等の経過時間も引いた残予算で待つ。focus/IME切替/unloadでこの待機を使わない。予算切れは未回収であり、後続の最終保存も論理取消し、後になって新しい保存を開始しない。すでにsyscall中の取消不能I/Oを強制終了できる保証はない。

最終タスクはsingle workerの現在jobの後ろに1件だけ置く。snapshotにはraw key/code/modifier/release/time、未配送request、compose prefix/完成Unicode、preedit、既知outcome、選択/周辺snapshotを含める。Fcitx PkgConfig rootの専用`mozkey-recovery/held-*.snapshot`へ、祖先NOFOLLOW・所有/権限・hardlink拒否、0700/0600、サイズ1MiB、checksum、temp→RENAME_NOREPLACE、file/directory fsyncを使って保存する。`latest`は独立したprivate pointerであり、snapshot本体は上書きしない。保存完了とnative joinの両方が必要で、join成功だけを保全成功にしない。index更新失敗・超過・破損を成功へ読み替えない。これは平文の復旧用データであり、暗号化や入力ログではない。壊れた既存ファイルを削除/上書きしない。

明示的な保存/閲覧と通常終了時の保存を製品へ接続した。復旧は読み取り表示だけで、結果不明のcommitやraw keyを自動再配送しない。表示は16KiBとUTF-8境界で制限し、元のデータは削らない。archiveの一覧/選択・明示解決/撤去・容量管理は未完成。保存不能時や強制終了/永久停滞での文字保全は保証できていないためA5未達のままである。

composeは[Instance 5.1.19](https://github.com/fcitx/fcitx5/blob/5.1.19/src/lib/fcitx/instance.cpp)の、初期化時table読取りと打鍵時state更新を静的照合した。未完了prefixを別dequeで保護し、完成Unicodeをkey_stringへ渡す。unconsumedなら現在の有効ICに完成文字列を一度だけ確定し、最終物理キーだけを誤転送しない。明示Escapeだけを取消とし、無効sequenceは保留する。実locale/XCompose table・Wayland/X11の動作は未検証。

`SessionCloseQueue`はaddonが最大32件を所有し、既存IDのDELETE_SESSIONだけを非同期送信する。新しいsession作成・key再送・旧IC callbackを伴わない。context所有者を破棄してもqueueが残り、addon/通常終了ではqueueをsealしてIOを取消す。成功・失敗/2s deadline・上限・owner取消を区別し、未完了のserver側資源解放は未検証。空のcontextはfocus returnで再初期化でき、入力が残るcontextは自動再開/再送しない。

`response_guard`は副作用前にmode/UTF-8/consumedの整合を確認する。再変換は受理・非空replacement・選択snapshot/cursor/anchor一致が必要。一般の削除もsurrounding capabilityと送信時のexact snapshot、文字単位の範囲を要求する。拒否された再変換、空replacement、overflow、capability消失は削除せず保留する。古い非物理callbackは純UIだけskipし、確定/削除/挿入の意味がある応答を黙って捨てない。focus喪失時の既知outcomeも保持する。callback Spaceのunconsumedは現在generationだけで一度だけ挿入する。

15ターゲット138成功と実API fixtureはこの限定境界の証拠。実IME、表示300ms、実live500ms/約1s、全6系統成功の代替ではない。満杯時の旧新入力順序、部分適用後の完全復旧、永久I/O/異常終了、専用server起動・終了所有、実frontend検証を残し、factoryは常に拒否する。旧100/250msは引き続き参考値である。


## 2026-11-14 継続実装：応答の直列適用・押下対応・保存形式

通常の工程間停止を設けず、`6865199fa58952609aab6dd582f49ab906bab22a`から継続する。最新mainはGitHub APIで`b489fca1561bf12ff6b43205378220163d71c21c`のままと確認。factory常時拒否とEnabled=Falseを維持し、以下はロード前の製品コード・隔離fixtureでの検証とする。

- **応答適用の直列化**：InputPipelineはfrontendの確定/転送hookが戻り、所有者が生存し、保留状態でないことを確認してから次のDeferを予約する。適用中の再入で別frameがdelivering/outcomeを上書きしない。AsyncSessionもapplication callback後にlifetimeと取消generationを確認して次要求を予約し、callback内のCloseが所有するDELETE_SESSION予約を上書きしない。
- **押下/解放の対応**：同一activationの最大256種類の押下について、raw keydownをアプリへ一度でも転送したかを保持する。serverが消費したkeydownやUnicode確定に置き換えたkeydownのkeyupだけをアプリへ送らない。転送済みmodifierのkeyupはserverがmodifier単独要求を消費しても転送する。反復では一回でも転送した押下をreleaseまで保持し、hardware codeがあればkeysymの大文字/小文字の変化より優先する。対応する押下が不明なreleaseはactivation前の押下かもしれないため転送する。focus/他IMEへ合成releaseを注入しない。この規則は実Wayland/X11/各appで未検証であり、全focus遷移を解決したとの判定には使わない。
- **保存の成功条件**：pendingだけでなくcomposing/delivering内のcompose enum、Unicode文字列、selection、requestも符号化時に検査し、復元側が拒否するsnapshotを成功扱いしない。各frame/outcomeのコピーごとに集約1MiB上限を検査する。失敗時は出力bytesと元の全入力を保持し、切詰めて成功にしない。これは保存不能時の完全保全を実現するものではない。
- **自動警告の情報制限**：focus復帰/打鍵時は保留の存在だけを表示し、process全体の入力内容を表示しない。journal本文は明示した「Show retained input」のみ。非focus、他IME、password/sensitiveでは表示しない。実UIでの動作は未検証。

再入の故障注入はSDK公開EventLoopInterfaceを実装した限定Defer schedulerを使う。導入済みsd-event backendは終了後のexec再実行を拒否するため、それを実frontendのnested-loop成功と読み替えない。他の通常キー・modifier/composeのCore fixtureは導入済みFcitx5の実EventLoop/InputContext/KeyEvent/forwardKeyを使い、Instanceや製品IME/serverは起動しない。

未解決のqueue満杯時の新規キー通過、保存先停止時の終了保全、永久停止workerとruntime破棄、専用serverの起動/終了所有は引き続き実装課題であり、この変更でA5を合格扱いしない。16枠を増やすだけ、入力を捨てて消費扱いにする、古い要求を自動再送する、focus移動先へ保留入力を送る対処は採らない。
