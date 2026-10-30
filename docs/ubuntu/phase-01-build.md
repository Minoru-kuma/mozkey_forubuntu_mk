# Phase 1 ビルド再現仕様案

**レビュー用ドラフト・未承認。以下は実行指示ではない。**
[共通ルール](../../AGENTS.md) / [進捗・承認](../../PLANS.md) / [調査](phase-00-audit.md)

## 目的・開始条件
Ubuntu 26.04 LTS（x86_64）+ Fcitx5を第一対象として、通常Linux成果物を再現可能にビルドし、Fcitx5統合差分・依存・ビルド構成を調査し、Ubuntu 24.04 CIとの相違を明らかにする。対象OSは確定済み。利用予定のDesktop環境、Wayland/X11、CPU/RAM/空き容量を確定する。OS版は/etc/os-release、アーキテクチャはuname -mで記録する。24.04は比較用であり、主対象の代替合格にはしない。
Phase 0レビューとPhase 1の明示承認、依存取得の容量・ネットワーク範囲の合意を開始条件とする。Zenz、モデル取得、Fcitx5/IBus登録・切替、システム導入は範囲外。ソース修正が必要になった場合は調査結果と修正案を先に提示する。

## 環境と依存の案
使い捨てVMまたはコンテナ等の隔離ビルド環境を優先し、ホストの日常Fcitx5・既存Mozcへ影響させない。対象SHA、イメージ版/digest、全ツール版、依存解決結果を記録する。
- CIに明示されたパッケージ：libibus-1.0-dev、qt6-base-dev。
- 開発環境で確認するもの：GCC/G++（build-essential相当）、pkg-config、Git、Python 3、zip/unzip、Bash、PowerShell（pwsh）、TLS証明書・取得ツール。
- 上記は26.04 LTSで導入確認済みのリストではない。隔離環境で提供版・依存を確認して固定する。ホストでsudoを実行しない。
- MODULE.bazelのglib-2.0、gobject-2.0、ibus-1.0、Qt6Core/Gui/Widgetsをpkg-configで検出できることを確認する。

## BazeliskとBazel
Bazelは `src/.bazeliskrc` の **9.0.2** を使い、wrapper設定も保持する。Bazelisk本体の版はリポジトリのLinux CIで明示固定されていないため未決定。Phase 1で公式配布のLinux amd64版を選び、版・取得URL・ハッシュを記録して作業用ディレクトリ内へ配置する。システムへインストールしない。利用可能な既存版があれば版を記録して採用可否を確認する。BazeliskはBazelを追加取得し得るため初回実行も取得範囲に含める。
`ANDROID_NDK_HOME` はCI同様、当該プロセス内のみ空にする。srcを作業ディレクトリとしてバージョンとwrapper選択をログで確認する。

## 辞書準備
`tools/dictionary/prepare_daily_dictionary.ps1` と各import/profile/convertスクリプト、`src/data/dictionary_koyasi/BUILD.bazel` を対象SHAで再読する。取得元、ライセンス、入力版・ハッシュ、必要容量を先に一覧化する。大容量取得は追加承認なしに開始しない。
CI方式はPowerShellでdaily生成し、generatedとdist/dictionaryを引き継ぐ。既存の同一SHA CI artifactを利用する場合も由来とハッシュを記録する。入力が揃う場合のみSkipDownloadを検討する。`python` が意図したPython 3を参照することを作業用環境で確認し、ホストのリンクは変更しない。sample/rich/maxを無断でdailyの代替にしない。プロファイル切替による追跡ファイル差分も保存し、製品修正に混ぜない。

## 実行候補とログ
承認後、対象SHAを固定した隔離checkoutで実行する。以下は案でありPhase 0では実行しない。

```sh
# srcを作業ディレクトリにする
bazelisk version
pkg-config --modversion ibus-1.0 Qt6Core Qt6Gui Qt6Widgets
bazelisk build package --config release_build --jobs=2
# 任意の参考検証。Fcitx5の動作検証ではない
bazelisk test //unix/ibus:key_translator_test //unix/ibus:surrounding_text_util_test --jobs=2 --test_output=errors
```

テストラベルは対象SHAのBUILD定義と再照合する。CPU/RAMに応じjobsを承認範囲で調整する。全件 `test ...` は本計画の既定にしない。コマンド、開始終了時刻、終了コード、標準出力・標準エラー、test.log、Bazel版・依存解決情報を保存する。tee利用時はpipefailで失敗を隠さない。ログ保管先は作業用領域を使い、入力履歴や秘密情報を含めない。
失敗時は依存取得・辞書欠落・コンパイル・リンク・テストに分類し、最小再現と根拠を残す。成功に見せるためターゲットや辞書を黙って省かない。

## 成果物と競合回避
`src/bazel-bin/unix/mozc.zip` の存在、サイズ、SHA-256、zip整合性と一覧を確認する。`src/unix/BUILD.bazel`、`build_package.py`、`src/config.bzl` と照合してserver、IBus、GUI tool、renderer、XML、アイコン等の欠落を確認する。展開は作業用ディレクトリに限定し、絶対パス・親ディレクトリ参照の有無を先に調べる。
ELFはfile/readelfでx86_64、インタプリタ、NEEDEDを検査し、実行環境のライブラリと照合する。必要なら隔離環境内でのみ追加の動的依存検査を行う。GUI/IME本体は起動しない。scorerを含まない通常zipの成功はZenz成功と区別する。
/usr等へのコピー、IBus component登録、既存Mozcの停止・置換、設定変更は行わない。Mozcと識別子・パスが重なる箇所を列挙してPhase 2へ引き継ぐ。

## 受入条件と引き継ぎ
1. 正確なUbuntu版・x86_64・対象SHA・ツール版・辞書入力を固定し、手順とログで再現可能。
2. 通常packageビルドと選定した限定テストが成功し（IBus向けは任意参考で受入条件外）、成果物一覧・ハッシュ・依存検査結果が記録される。
3. 24.04 CIとの相違を記録し、CI実行結果を参照する場合はSHA・run URL・対象ジョブを明示する。
4. ホストFcitx5・既存Mozc・システム領域に変更がない。未解決障害がある場合は未完了として報告する。
5. Phase 2へ成果物、競合箇所、Fcitx5統合差分表・コールバックの未検証事項、隔離入力試験案を引き継ぐ。ビルド合格を実機入力合格にしない。

Phase 1開始前の未決事項：Fcitx5の版・導入アドオン、隔離環境方式、Bazelisk版、辞書取得量と許可範囲、資源上限、ログ保存先。Phase 2以降は別途承認を待つ。

## Fcitx5統合調査の追加仕様
Phase 1のFcitx5対応部分は調査までとし、`src/unix/fcitx5/` の追加やBUILD変更は実施しない。根拠と固定SHAは[Phase 0追加調査](phase-00-audit.md)を参照する。IBusは既存実装・通常packageの比較対象であり、入力動作やIBusテスト成功を受入条件にしない。

成果物として次の差分表を作成する。
- 固定したfcitx/mozcのFcitx5ソース・設定・アイコン・翻訳・ライセンス一覧と、Mozkey側に必要な変更先。IPC方式を第一候補とし、direct方式との差を明示する。
- `@fcitx5`依存を提供するFcitx5Core/Fcitx5Moduleのpkg-config検出、Ubuntu 26.04の開発パッケージ名・実版・API/ABI対応。候補はlibfcitx5core-dev、libfcitx5utils-devを含むが、提供ファイルと推移依存を確認して確定する。名称だけから必要十分と判断しない。
- 元実装の `//unix/fcitx5:fcitx5-mozc.so` と `use_server`、Mozkeyのclient/protocol/辞書ターゲットとの依存対応。MODULEのOS限定、Windows/macOSで不要なFcitx検出を起こさない構成案。
- 通常 `package` はFcitx5を含まないため、共有ライブラリ・addon/inputmethod設定・アイコン・Mozkey server/toolの独立した成果物一覧とstaging配置案。現段階ではFcitx5ビルドコマンドを実行可能と扱わない。
- アドオンID `mozc`、ライブラリ名、server探索パス、ユーザープロファイル、IPCが既存Mozcへ接続・上書きしないための方式案。ホストで名前を変えるだけでは隔離完了としない。

追加受入条件は、上記差分・依存・未解決API・ビルド構成案・ライセンスとPR #231との分担がレビュー可能であること。Fcitx5実装がない現行mainの通常Linuxビルド成功をFcitx5対応完了と扱わない。

## 必須の隔離検証計画とPhase 2への引き継ぎ
ビルドのみのコンテナは候補だが、実入力試験はホストと分離したUbuntu 26.04 LTS x86_64の使い捨てVMで行う。VMの作成・起動や導入操作も後続承認後に実施する。ホストHOME/XDG設定・個人辞書、session D-Bus、表示socket、runtime/IPC領域を共有しない。日常Fcitx5へ `--replace`、kill、再起動、アドオン導入・置換、IME切替を行わない。
試験用VMはスナップショットと空の試験プロファイルを用意し、合成した日本語入力を使う。検証終了はVMの停止・破棄で戻せる構成とする。試験前後にホストの既存設定・アドオンが無変更であることを内容を公開せず確認する計画を残す。
Phase 2は別途承認した最小統合を経て、Fcitx5から実際の日本語入力、ライブ変換、preedit、候補表示・選択・ページ移動、確定/取消、フォーカス移動、入力欄破棄、遅延応答取消を確認する。GTK/Qt等の代表アプリと利用予定の表示セッションを記録し、未確認のセッションを合格にしない。IBusの参考テスト結果は別欄に記録し、Fcitx5実機試験と区別する。
