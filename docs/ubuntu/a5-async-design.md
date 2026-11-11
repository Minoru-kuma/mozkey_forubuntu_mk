# A5再設計：イベントループの継続性（レビュー案）

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
