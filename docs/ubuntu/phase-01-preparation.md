# Phase 1 再開準備案（承認待ち）

[共通ルール](../../AGENTS.md) / [計画](../../PLANS.md) / [仕様](phase-01-build.md) / [前回結果](phase-01-results.md)

## 調査範囲と状態
2026-10-31、Ubuntu 26.04.1 LTS x86_64、ブランチfeat/ubuntu-phase1-build、基点51487b0c39dfb8887b3e8aa11f28a4b82a225b0c。
今回は既存aptメタデータによるシミュレーション、公式Release API・固定コミットのREADME/tree等の小さなメタデータ照会のみ。インストール・開発ツール本体取得・辞書取得・ソース修正・ビルド・IME操作は行っていない。Phase 1の受入条件は未達のまま。以下の実行は承認待ち。

## 提案A：ホストの更新を伴わない開発依存追加
apt-cache policyとapt-get -sで確認。新しいapt indexを取るapt-get updateは実行していない。Ubuntu indexの多くは10月28日時点で、実施時に版・入手可否・トランザクションを再確認する。

| 案 | 新規 | 更新 | 削除 | 取得量（apt Size合計） |
|---|---:|---:|---:|---:|
| 通常install | 39 | 11 | 0 | 23,164,188 B |
| --no-install-recommends | 38 | 11 | 0 | 23,102,764 B |
| 下記のlibsystemd-dev版指定案 | 38 | 0 | 0 | 14,342,510 B（13.68 MiB） |

通常案の更新対象はlibsystemd0、libpam-systemd、systemd-sysv、systemd-resolved、systemd-oomd、libsystemd-shared、systemd、systemd-cryptsetup、udev、libudev1、libnss-systemd（259.5-0ubuntu3.4 → 259.9-0ubuntu0.1）。入力セッションやサービスに影響し得るため、通常案は今回の候補から外す。
版指定案は既存systemdと同じ259.5-0ubuntu3.4のlibsystemd-devを追加する。永続hold・apt pin・ダウングレードは行わず、将来のセキュリティ更新を固定しない。候補が取得不能・依存不整合なら自動で別版へ切替えず停止する。

実行前に次をsudoなしで再シミュレーションし、下表と一致すること、更新/削除0であることを確認する。

```sh
apt-get -s --no-install-recommends --no-upgrade --no-remove install \
  pkg-config=2.5.1-4 libibus-1.0-dev=1.5.34~rc2-1 \
  qt6-base-dev=6.10.2+dfsg-7 libsystemd-dev=259.5-0ubuntu3.4
```

**承認対象のシステム操作**は上記コマンドから `-s` を除き、先頭にsudoを付けたインストール。無人の-yは付けない。実行直前にも計画を比較する。apt-get updateが必要ならその取得・再計画を先に提示し、変更されたトランザクションに承認を流用しない。
取得元はUbuntu署名済みresolute/main、universe、resolute-updates/main、resolute-security/main（jp.archive.ubuntu.com、security.ubuntu.com）。新規リポジトリ追加なし。

### 追加38パッケージの全一覧
導入後サイズの合計は113,107 KiB（約110.46 MiB）。aptキャッシュ・dpkg管理情報・トリガー生成物は別。1 GiBのシステム空き予算を見込む。各debはaptの署名済み索引に記載されたハッシュで検証する。

| パッケージ | 版 | Download B | Installed KiB |
|---|---|---:|---:|
| gir1.2-glib-2.0-dev | 2.88.0-1ubuntu0.1 | 867,020 | 11,603 |
| native-architecture | 0.2.6build1 | 2,256 | 8 |
| girepository-tools | 2.88.0-1ubuntu0.1 | 110,770 | 336 |
| libsystemd-dev | 259.5-0ubuntu3.4 | 1,348,332 | 5,743 |
| libpkgconf7 | 2.5.1-4 | 38,796 | 127 |
| pkgconf-bin | 2.5.1-4 | 22,724 | 85 |
| pkgconf | 2.5.1-4 | 20,022 | 73 |
| libdbus-1-dev | 1.16.2-2ubuntu4 | 189,128 | 954 |
| libffi-dev | 3.5.2-4 | 66,458 | 361 |
| libblkid-dev | 2.41.3-3ubuntu2.2 | 225,948 | 979 |
| libsepol-dev | 3.9-2 | 409,086 | 1,792 |
| libpcre2-posix3 | 10.46-1build1 | 7,182 | 36 |
| libpcre2-dev | 10.46-1build1 | 823,604 | 2,824 |
| libselinux-dev | 3.9-4build1 | 175,796 | 763 |
| libmount-dev | 2.41.3-3ubuntu2.2 | 29,250 | 95 |
| libsysprof-capture-4-dev | 50.0-1 | 51,924 | 296 |
| zlib1g-dev | 1:1.3.dfsg+really1.3.1-1ubuntu3.2 | 895,424 | 1,324 |
| libgio-2.0-dev | 2.88.0-1ubuntu0.1 | 1,825,882 | 10,839 |
| python3-packaging | 26.0-1 | 58,686 | 288 |
| libgio-2.0-dev-bin | 2.88.0-1ubuntu0.1 | 129,716 | 685 |
| libglx-dev | 1.7.0-3 | 14,116 | 87 |
| libgl-dev | 1.7.0-3 | 102,558 | 1,364 |
| libglib2.0-dev-bin | 2.88.0-1ubuntu0.1 | 18,108 | 84 |
| libglib2.0-dev | 2.88.0-1ubuntu0.1 | 18,834 | 88 |
| libibus-1.0-dev | 1.5.34~rc2-1 | 194,202 | 2,847 |
| libopengl-dev | 1.7.0-3 | 3,396 | 21 |
| libqt6concurrent6 | 6.10.2+dfsg-7 | 33,410 | 177 |
| libqt6openglwidgets6 | 6.10.2+dfsg-7 | 43,556 | 214 |
| libqt6printsupport6 | 6.10.2+dfsg-7 | 224,118 | 798 |
| libqt6sql6 | 6.10.2+dfsg-7 | 147,480 | 506 |
| libqt6test6 | 6.10.2+dfsg-7 | 193,650 | 625 |
| libqt6xml6 | 6.10.2+dfsg-7 | 82,090 | 327 |
| libvulkan-dev | 1.4.341.0-1 | 1,709,730 | 37,056 |
| pkg-config | 2.5.1-4 | 7,684 | 34 |
| qmake6-bin | 6.10.2+dfsg-7 | 656,844 | 1,854 |
| qmake6 | 6.10.2+dfsg-7 | 143,002 | 1,085 |
| qt6-base-dev-tools | 6.10.2+dfsg-7 | 1,160,062 | 4,685 |
| qt6-base-dev | 6.10.2+dfsg-7 | 2,291,666 | 22,044 |

Fcitx5/Hazkey、IBusエンジン実行パッケージ、Mozc本体はこの追加一覧に含まれない。Qt関連ランタイムも新規追加で、既存のQt/Fcitx/Hazkeyパッケージ更新はシミュレーション上ない。ただしrootのdpkg処理・ldconfig等はシステム変更であり、実行時のサービス影響をゼロとは保証しない。autoremove・purge・既存IMEの再起動や設定変更は計画に含めない。

## 提案B：ユーザー領域への開発ツール配置
GitHub公式Release APIから版・asset.size・SHA-256 digestを確認した。sudo不要。実ファイルは未取得。圧縮/単体サイズと展開・実行時必要量を区別する。

| ツール | 固定版・asset | 取得B | SHA-256 |
|---|---|---:|---|
| Bazelisk | 1.30.0 / bazelisk-linux-amd64 | 7,823,520 | a248ae2b2b62b5baebb748f6fba7203bff1a27e5d01b55399a8406744cb539e8 |
| Bazel | 9.0.2 / bazel-9.0.2-linux-x86_64（JDK同梱） | 65,753,714 | 422e7a1690b76d7e615c29091d3aca28d0bd3a93fe3c93cbefb8f72d774926d5 |
| PowerShell | 7.6.9 / powershell-7.6.9-linux-x64.tar.gz（self-contained） | 73,399,520 | 1da6d6d6035d651f20e70ddee7c4c15ac6ab8b9fb40ee92240d91263f28a884b |

合計146,976,754 B（約140.17 MiB）。展開・Bazelインストールキャッシュは未実測で、ツール用に1 GiBを予算化する。PowerShellのfxdependent版は別途.NETが必要になるため採用しない。

取得元・根拠：

- [Bazelisk 1.30.0](https://github.com/bazelbuild/bazelisk/releases/tag/v1.30.0)、[Bazelisk設定説明](https://github.com/bazelbuild/bazelisk/blob/v1.30.0/README.md)
- [Bazel 9.0.2](https://github.com/bazelbuild/bazel/releases/tag/9.0.2)
- [PowerShell 7.6.9](https://github.com/PowerShell/PowerShell/releases/tag/v7.6.9)、[公式binary archive手順](https://learn.microsoft.com/en-us/powershell/scripting/install/alternate-install-methods?view=powershell-7.6)

assetのURLは各Releaseの `/releases/download/<tag>/<asset>`。Bazelisk経由のBazel取得は `BAZELISK_BASE_URL=https://github.com/bazelbuild/bazel/releases/download` と `BAZELISK_VERIFY_SHA256` を上表値へ設定して公式GitHubへ固定する案。src/.bazeliskrcの9.0.2を維持し、環境変数で別版を選択させない。Bazel単体を別途二重に取得しない。
承認後はcurl --fail --locationで作業用downloadsへ取得 → SHA-256検証 → Bazeliskをtools/binへ、PowerShellをtools/powershell-7.6.9へ展開 → 必要ファイルだけ実行権限設定。/usr/binや/optへ置かず、シェル設定も書き換えない。
PowerShellの依存候補libc6、libgcc-s1、libgssapi-krb5-2、libstdc++6、libunwind8、libicu78、libssl3t64、zlib1gはdpkg上導入済み。native依存の充足と展開量は取得後にreadelf/起動確認する。起動不能時に依存を自動追加しない。公式のarchive方式は追加のnative依存確認が必要な手動配置方式として扱う。

## 提案C：daily辞書入力の取得・生成
`tools/dictionary/locks/daily_sources.json` の固定入力を使う。ライセンス調査はコードと生成データで分ける。

| 入力 | 固定コミット・スナップショット | 取得量の根拠 | 条件・未解決 |
|---|---|---|---|
| merge-ut | 640844881690e877d0e54dff024edb8ec7b77290 | Git tree全blob合計126,463 B | コードApache-2.0、合成辞書Mixed |
| place-names | 98cf8e9d7731695a52e5bc72ed633933c71d1913 | tree合計1,743,824 B（bz2 1,743,054 B） | READMEは日本郵便データ由来、Public Domain表記 |
| sudachidict | 13129ddebe1cb8eabb63feccbe157adec4e1de01 | tree合計11,477,637 B（bz2 11,477,046 B） | READMEはApache-2.0、SudachiDict LEGAL参照 |
| Wikipedia index | 20261001 | lock記載31,437,876 B（bz2） | READMEはCC BY-SA。辞書コスト算出に利用、生成物の再配布条件は別途確認 |
| nico/pixiv | 4d0d8d219d0b9abf34745016e398e762182fce61 | lock記載7,659,574 B | コードMIT。生成物に著作権を主張しないとの記載だが、元サイトの権利まで放棄されたとは判断しない |
| 人名 | b557dd4c1548cc7c2164d5f8b14170c40cbb6f81 | lock記載729,168 B（bz2） | READMEはApache-2.0 |

6入力のtree/blob単純合計は約53.2 MB。Git pack通信量・メタデータと展開/加工容量は含まない。dictionary本体は取得していないので生成後の実サイズは未確定。辞書ネット取得の提案上限は256 MiB、辞書作業領域は10 GiB。上限が近づけば停止して再評価し、モデル・latestの巨大dump・rich/maxは取得しない。

ハッシュ（lock転記、実取得後に照合）：

- Wikipedia `8A040564302DC0BA57160172ABEF3D4F69CC7CA36EA67B6439959D441CD037E7`
- nico/pixiv `E16990B24D75FCCAC9CF2B0B4382E59988E735BC9FA634626078DC500E0B8BC4`
- 人名 `610E2E52FAD15DF0EDAC854960A05451A7E9A62F3DF09C3F065DB9E1AB6F51F4`
- merge-utと子辞書は固定Git commit IDで検証する。ファイル単位のSHA-256はlockにないため取得後に記録する。親merge scriptのGit blob期待値は778e4620117aa0eda85f7572a40413e2783cef9a。

取得先はgithub.com/utuhiro78の上記各repo、raw.githubusercontent.com/Niccori250kとutuhiro78の固定revisionファイル、dumps.wikimedia.org/jawiki/20261001。任意の最新版へフォールバックしない。
ライセンス根拠は各固定README：[merge-ut](https://github.com/utuhiro78/merge-ut-dictionaries/blob/640844881690e877d0e54dff024edb8ec7b77290/README.md)、[place-names](https://github.com/utuhiro78/mozcdic-ut-place-names/blob/98cf8e9d7731695a52e5bc72ed633933c71d1913/README.md)、[sudachidict](https://github.com/utuhiro78/mozcdic-ut-sudachidict/blob/13129ddebe1cb8eabb63feccbe157adec4e1de01/README.md)、[nico/pixiv](https://github.com/Niccori250k/dic-nico-intersection-pixiv/blob/4d0d8d219d0b9abf34745016e398e762182fce61/README.md)、[人名](https://github.com/utuhiro78/mozcdic-ut-personal-names/blob/b557dd4c1548cc7c2164d5f8b14170c40cbb6f81/README.md)。今回はローカル検証用途のみを承認対象とし、生成辞書・バイナリを再配布せず、Gitにも入れない。

承認後の生成順：

1. 作業用PATHでpythonを/usr/bin/python3へ向ける専用binを用意（ホストのpythonリンクを変更しない）。/usr/bin/bash、/usr/bin/bzip2、Git、Python、PowerShellを確認。
2. repoルートでlock validate。`pwsh -NoLogo -NoProfile -File tools/dictionary/prepare_daily_dictionary.ps1` を実行する。
3. import_merge_utはsampleという引数だが、フルのplace-names/sudachidictを取得してから加工する。generate_latest=falseで固定wiki indexをコスト算出に使う。nico/pixiv・人名の固定入力も取り込む。
4. profile_merge_ut daily → nico/pixiv delta → 人名daily → check_merge_ut_profile daily → syntax guard → use_merge_ut_profile daily。
5. 生成入力4点・件数・ハッシュと追跡ファイル差分を記録。sampleへ無断切替しない。スクリプトのprofile切替が作業用checkoutの辞書BUILD等へ触れる場合は生成手順上の変更として隔離し、製品コード修正・コミットへ混ぜない。想定外のソース差分が出たら停止する。

## tmpfs外の配置とビルド取得予算
既存worktreeは/tmp（7.4 GiB tmpfs）だが、作業リポジトリのディスクは414G、空き282G。提案ルートは `/home/mizki/workdir/mozkey_forubuntu_mk/.local/ubuntu-phase1`。以下は承認後の配置案で、今回は作成していない。

- worktree：上記配下のcheckout。同じブランチを同時checkoutせず、git worktree moveで/tmp/mozkey-phase1から移動する案（Git管理変更のみ）。
- tools/bin、tools/powershell-7.6.9、downloads、tmp、logs、bazelisk-cache、bazel-output、repository-cache、disk-cacheを全て提案ルート配下へ。
- dictionaryスクリプトのdist/dictionaryとgeneratedは移動後のcheckout内なのでディスクに載る。
- 親リポジトリの未追跡表示を避ける場合、グローバルignoreは変えず、承認後に専用のローカルexcludeを検討する。`.local`をGitに追加しない。

作業プロセス内だけTMPDIR、BAZELISK_HOME、XDG_CACHE_HOMEを上記作業領域へ設定。PowerShellはPOWERSHELL_TELEMETRY_OPTOUT=1、POWERSHELL_UPDATECHECK=Offを指定する。PATHはtools/bin、作業用python bin、必要な/usr/bin等に限定し、Miniconda由来のPython/ライブラリ混入を避ける。

Bazel起動案：

```sh
# 移動後checkout/src、タスク専用の環境変数を設定済みとする
bazelisk --output_user_root="$phase1_root/bazel-output" build package \
  --config release_build --jobs=2 \
  --repository_cache="$phase1_root/repository-cache" \
  --disk_cache="$phase1_root/disk-cache"
```

MODULE依存はBCR、github.com、raw.githubusercontent.com、Bazel公式mirror等から解決され、Abseil/protobuf/rules_*、Python toolchain、辞書・アイコンを含む。ソース定義のハッシュ・固定版に従う。依存全体の転送量は未実測で確定できないため、追加の通常Bazel依存取得はネット2 GiB、展開/ビルド/cacheは40 GiBを暫定予算として提案する。予算は実測値ではなく停止・再相談の目安で、厳密なOS quotaは設定しない。上限監視の方法を実行前に確定し、無制限取得の承認として扱わない。
ディスク全体の作業予算は60 GiB（辞書10、Bazel40、ツール1、ログ/checkout/余裕9）。空き60 GiB以上を維持し、通常ビルド・限定テスト・zip/ELF検査までとする。生成物はGit外に保持し、手動キャッシュ削除は別判断とする。

## Hazkeyへの影響と承認単位
Aは新規dev/runtimeの追加というシステム変更でありsudo承認が必要。既存版へのlibsystemd-dev指定でsystemd/udev更新を避ける候補。前後のパッケージ差分を確認し、Fcitx/Hazkey変更やサービス再起動が追加で必要になれば停止する。Hazkeyの設定・個人辞書を読み取ったりコピーしたりしない。
B/CとBazel通常依存は外部取得の承認が必要。B/Cのコード・辞書生成は作業領域内でのみ実行し、出力のインストール・IME登録はしない。ビルド成果物の静的検査までで、Fcitxアドオンやmozc_serverを起動しない。
Aを許可しない場合は26.04隔離ビルド環境へ切り替える別案を具体化する必要がある。VM/containerイメージの取得・作成は今回の承認対象案には含めていない。

承認は **A：sudoで新規38件追加、B：公式ツール約147 MB取得、C：固定daily入力取得（256 MiB予算）・生成、D：通常Bazel依存（2 GiB予算）とビルド/検証・ディスク60 GiB予算** に分けて依頼する。Aを含む全て未実施。承認が得られた範囲だけ進め、Phase 2は開始しない。
