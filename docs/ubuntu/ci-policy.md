# Ubuntu開発のCI運用

2026-11-14更新。AGENTS.mdとPLANS.mdに従い、通常pushは軽量、Phase完了・main統合前はフルCIとする。2Aの再開/実装範囲は最新ユーザー指示とPLANS.mdに従い、CI成功だけでA5を合格扱いしない。

## 実行条件

| workflow | 従来の通常push | 開発ブランチpush | main向けPR | 手動 | main push |
|---|---|---|---|---|---|
| Lint | 実行 | 実行 | 実行 | 実行 | 実行 |
| Linux limited（新規） | なし | 実行 | 実行 | 実行 | なし |
| Linux / Windows / macOS / Android | フル実行 | なし | フル実行 | フル実行 | フル実行 |
| Zenz runtime lock | 実行 | 実行 | 実行 | 実行 | 実行 |
| Secure Offline Checks | main・feature/fix/harden/choreで実行 | なし | 実行 | 既存検査＋6系統フルCI | 実行 |
| macOS formal package / reaper | main中心／reaperは該当pathの全branch | なし | 既存条件で実行 | 実行 | 既存条件で実行 |
| llama native / portable・macOS installable候補 | 専用ciブランチで実行 | なし | なし | 既存処理を実行 | なし |

既存の全OSビルド、辞書生成、MSI、回帰、セキュリティ検査、成果物生成は削除しない。変更は起動条件、手動6系統への接続、ref/SHA表示と実行グループ。各workflow内のneeds・cache・検査stepを維持し、別runの辞書artifactへ依存させない。手動で同一runに呼び出すOS間の衝突を防ぐため、辞書artifact名だけはdaily-dictionary-linux/windows/macosへupload/download双方を変更する。他のartifact名は維持する。正式macOS梱包・reaper・Secure Offlineの検査はmain統合時にも残す。実験候補は手動専用。

## 通常pushの限定範囲

Fcitx5 5.1.19以上の開発ファイルが既にあるrunnerでは`run_fcitx5_api_probe.py`で独立C++ API試験を実行する。ない場合は未実行をnotice/summaryへ明記し、apt等で導入しない。stepの成功を実API検証成功へ読み替えず、ログのPASSを確認する。

同じ開発ファイルがある場合、src変更時には専用設定ONのsystem_util/ipc_path_managerとcallback_queue/connection_preparation/frontend_boundary/async_session/input_pipeline/async_config/input_recovery/frame_translator/response_guard/worker_runtimeの限定試験も実行する。辞書・package・実IMEは不要。Ubuntu 24.04で未導入ならNOT EXECUTEDを明記する。新規Fcitx製品targetはmanual/opt-inとし、既定の全OSビルドへSDK依存を強制しない。通常の6ターゲットは設定OFFを保つ。cacheの入力hash変更時は同一OS/archの旧cacheも復元候補にし、Bazel側の入力/action digestで再利用を判定する。依存の版・チェックサム検証は変更しない。

非同期IPCのモデルPoC `tests/ubuntu/test_async_ipc_poc.py`も毎回実行する。旧helper PoCの数値アサートはその診断資産として維持するが、A5の判定は[現行の設計・受入条件](a5-async-design.md)に従い、未達/未検証を合格扱いしない。

Lintは既存buildifier処理。ZenzはPython単体試験とruntime_lock.py checkだけであり、直前の実測run 42563537869は開始から完了まで約12秒。モデル・辞書・scorerの取得起動はしないため軽量側に維持する。

Linux limitedはUbuntu 24.04・Bazel 9.0.2・jobs=2・最大20分。src変更時は既存system_util/process_mutex/ipc_path_manager/ipc/client/linux_deadlineの6ターゲット、config変更時はconfig_handler/stats_config_util、zenz変更時はzenz_unix_socket_pathも追加する。さらに`tests/ubuntu/test_linux_helper_poc.py`を毎回実行する。これは合成socketpairを使うPoCで、実IMEの検証ではない。これらは限定回帰であり、全変更の網羅的テストではない。session/engine等の機能変更は関連試験の選定をレビューし、必要ならフルCIを早める。

文書のみのpushではチェックjob自体は作成し、Bazel関連stepだけを省略する。新規branch・比較元不明時は限定集合を実行する。PRはbase SHAと実際のmerge SHAとの差分を見る。full buildの辞書artifact、Phase 1・2Aのローカルcache、個人profileには依存しない。専用のrepository/disk cacheを利用し、キャッシュミス時は限定targetに必要なBazel依存だけを取得する。update_deps全体、辞書全面生成、package、実IMEは実行しない。cold cache時のコンパイル負荷は残る。

Ubuntu 24.04 CI成功をUbuntu 26.04 + Fcitx5の実入力・A5合格として扱わない。

## main統合前のフルCI（PRなしにも対応）

mainに既にworkflow_dispatchがあるSecure Offline Checksを入口にする。Actions画面で同workflowのRun workflowから対象branchを選ぶ。CLIの例：

```sh
gh workflow run secure-offline.yaml --ref feat/ubuntu-phase2a
```

この入口は、当該branchの同一コミットにある6つのworkflowをローカルworkflow_callで呼ぶ。新しいdispatch定義をmainへマージする前でも、既存main上の手動入口から起動できる。新しい6workflowへの直接dispatchはmainに定義が反映された後に使う。dispatch入力の変更やAPI token追加、権限拡大は不要。

実行前に対象branchのSHAを記録し、6系統すべてのrun/jobが同じhead_shaを検証し成功したことを確認する。run名にはrefとgithub.shaを表示する。PRの自動実行ではmerge commitが対象なので、branch HEADそのものの証拠が必要なら手動実行する。SHAが更新された場合は旧成功を新コミットの合格に流用しない。main統合には既存の明示承認も必要。

workflowごとのconcurrency名に固定prefixとevent/refを含め、手動umbrellaと呼出先が同じgroupで互いを取消・待機しないようにする。手動runは自動取消しないため、同じrefを連続dispatchすると待機する場合がある。通常push/PRは同一groupの古いrunを取消す。

## Phase途中でもフルCIを前倒しする基準

client.cc/client.h、共通IPC、protocolの互換性・生成、OS分岐、共通BUILD/.bzl/MODULE/.bazelrc、toolchain、依存版、共通config・process・session寿命を変更したときは、影響OSと実行する回帰をレビューして記録する。複数OSへの影響がある／判断できない場合は対象SHAの6系統フルCIを実行する。限定試験失敗、ABI/梱包/辞書生成/セキュリティ境界の変更、Phase完了・main統合前もフルCIが必要。通常pushを自動でフルCIへ切り替える複雑なpath判定は追加しない。

## 必須チェックと検証上の限界

既存workflow名・job IDを保持する。Lint・Linux limited・Zenzにworkflow単位のpaths除外を設けず、文書変更でもチェック結果を返す。フルCIもmain向けPRをpathsで除外しない。既存のmacOS reaperにはpath制限があるため、無条件の必須チェックに指定しない。

調査時rulesetsは空。branch protectionのrequired checksはGitHub接続の権限不足（403）で確認できず、設定は変更していない。管理者はmain統合前にrequired contextsと新しい実行条件を照合する。PRなしの統合はbranch保護だけに依存せず、上記の同一SHA・全6系統成功を確認する。

ローカルではYAML構文、既存job本体の不変性、トリガー表、呼出先・needs・権限・concurrency、git diff --checkを検査する。actionlintがない場合は未実行と記録し、新規導入は独断で行わない。静的検査はGitHubでの実行成功を意味しない。初回branch pushの軽量CI結果を確認し、PR/main/手動フルCIは実行しない限り未検証とする。
