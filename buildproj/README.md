# buildproj — ビルドプロジェクトファイルのバックアップ

このフォルダーは、**2本のプラグイン**の Visual Studio プロジェクトファイルの**バックアップコピー**です
(2026-08-15 に KCMUI 分を追加＝model/UI 分割 第2段 Task 1・2):

| | プロジェクトファイル | ソースの場所 | 種別 |
|---|---|---|---|
| **KCM**(model 側) | `KohakuExtendScriptChangeMarker.vcxproj` / `.filters` | `source/` | 現在は `kUIPlugIn`。第2段 Task 11 で `kModelPlugIn` にする |
| **KCMUI**(UI 側) | `KohakuChangeMarkerUI.vcxproj` / `.filters` | ★`ui/` | `kUIPlugIn` |

## なぜここに置くか
KCM の git リポジトリは `source/sdksamples/KCM/`(このフォルダーの親)です。
一方、実際にビルドで使う `.vcxproj` は SDK のビルドツリー
`build/win/prj/` にあり、**git リポジトリの外**です。
そのため、プロジェクトファイルに加えたカスタム(下記)が git だけでは保全されません。
クリーンな環境に展開し直したときにビルドを再現できるよう、ここにコピーを置いて版管理します。

★**KCMUI のソースも 2026-08-15 にこのリポジトリへ入れた**——元は SDK 側の
`source/sdksamples/KCMUI/`(git 管理外)にあったが、`source/sdksamples/KCM/ui/` へ移した。
理由＝分割の途中でファイルが model 側と UI 側を何度も行き来するので、**1コミットで両側を直せる**ほうが安全
(移動が「削除＋追加」に割れて履歴が切れるのを避ける)。**雛形10本のうちに移したので移動コストは最小だった。**

⚠**`.sdk.props` 4本(`KCMUI*.sdk.props` / `KCM*.sdk.props`)は控えていません。** DollyXs が生成する定型で、
復元するなら DollyXs を回すのが早いためです(`devtools/dolly/win-input.xml` は**新 prefix `0x1EA580` と
新パス `KCM\ui` に更新済み**＝次に回しても旧値に戻りません)。

## このプロジェクトファイルに入っているカスタム(復元時に必要な要点)
- KCM の全ソース(`KCM*.cpp` / `.h`)の登録。特に近年追加分:
  `KCMChangeNav.*`(変更ページへの Next/Prev ナビ)、`KCMThumbnailRefresh.*`
  (Pages パネルのサムネイル更新)、`KCMPageMap.*`、`KCMPageNumberMarker.*`、
  `KCMScrollMap.*`(スクロールバー地図 strip)、`KCMStory*.*`(Story Edits)、
  `KCMBook*.*`(ブック比較)ほか。
- **リンクライブラリに `DV_WidgetBin.lib` を追加**(2026-07-11、全4構成)。`KCMScrollMap.cpp` の
  自前描画ビュー基底 `DVControlView`(`AbstractControlView`/`DVHostedWidgetView`)の実体は
  `WidgetBin.lib` ではなく `DV_WidgetBin.lib` にあるため(dumpbin で確認)。
- ★★**ビルド生成物の名前を `KohakuChangeMarker` に統一**(2026-08-19)。`TargetName`(`.pln`)だけが
  `KohakuChangeMarker` で、**`.pdb` が `KohakuExtendScriptChangeMarker.pdb` のまま出力先に並んでいた**ため。
  直したのは `.vcxproj` 内の**生成物名だけ**——`ProgramDatabaseFile` / `ImportLibrary` /
  `PrecompiledHeaderOutputFile` / `TypeLibraryName` / `IntDir` / `.rsp` の参照(全4構成・計32か所)。
  **`build/win/prj/` の応答ファイル2本もリネームした**:
  `KohakuExtendScriptChangeMarker{CPP,ODFRC}.rsp` → `KohakuChangeMarker{CPP,ODFRC}.rsp`
  (中身は `/I`・`-i` のパス列だけで、名前は入っていない)。
  ⚠**変えていないもの**: `.vcxproj` のファイル名・`SDKSamples.sln` の登録名(`ProjectName` は元から
  `KohakuChangeMarker` に上書き済み)、`KCM*.sdk.props` 4本(元から短い `KCM` 名)、そして
  **内部名 `kKCMPluginName`**(`KCMBoundaryID.h:78`。`.rc` の `InternalName` と KCMUI の
  `PluginDependency` が名乗る名前で、**互換のため据え置き**)。
  ⚠`IntDir` が変わるので**中間フォルダーが `objRx64\KohakuChangeMarker` に移り、初回はフルビルド**になる。
  旧 `objRx64\KohakuExtendScriptChangeMarker` / `objDx64\…` は残骸なので消してよい。
- 追加のインクルードパスは**無し**(標準構成のまま)。
  ※2026-07-06 まで一時的に `$(ID_SDK_DIR)\source\open\interfaces\ui` を追加していた
  (`IPendingUpdateController.h` 用)が、サムネイル更新を `IImageCacheMgr::Purge` + `ForceRedraw` の
  最小2手に整理して `IPendingUpdateController` 依存を撤去したため、このパスも撤去済み。

## KCMUI(UI 側)のプロジェクトファイルに入っているカスタム

DollyXs が生成した雛形は**そのままではビルドもロードもできない**([[dollyxs-new-plugin-traps]])。
2026-08-15 に次を入れて、Release|x64 が 0 警告 0 エラーで通ることを確認した:

- **`ProjectGuid` `{83EAC044-5929-43FF-9ADF-9BF3D3FFB98F}` を付与し、`SDKSamples.sln` に登録**
  (Project 行＋4構成×2＝8行の `ProjectConfigurationPlatforms`)。**GUID は両方で一致させること**
  (ずれると `MSB8028`)。
- **`WindowsTargetPlatformVersion` を `10.0.26100.0`(生成マシンの固定値)から `10.0`(自動選択)へ。**
  そのままだとその Windows SDK が無い環境で `MSB8036`。
- ★★**リンクライブラリは `$(UI_PLUGIN_LINKLIST);user32.lib;%(AdditionalDependencies)`。**
  **lib を手書きで並べない**——Adobe は `.props` のマクロで側を切り替えている
  (`build/win/prj/ReleaseX64.sdk.props:19-21`):
  - `$(MODEL_PLUGIN_LINKLIST)` ＝ `PMRuntime` `Public` `Database` `ASLSupport`
  - `$(UI_PLUGIN_LINKLIST)` ＝ 上に ＋ **`DV_WidgetBin`** ＋ `WidgetBin`

  ⚠**雛形は `WidgetBin` しか持っておらず `DV_WidgetBin` が無い。** KCMUI はツリー(`NodeID`)と
  スクロールバー地図(`DVControlView`)を引き取るので、**マクロにしないとその段階で `LNK2019`/`LNK2001` の山**になる。
  `user32.lib` は半透明・Win32 フック用に KCMUI 固有で足している。
- **`TargetName` を `$(ProjectName).sdk` から `KohakuChangeMarkerUI` へ**(KCM 側と揃えた)。
  ⚠ 変えたときは**古い `KohakuChangeMarkerUI.sdk.pln` と `(… .sdk Resources)` を消すこと**——
  残すと InDesign が2本ロードしようとして ID が衝突する。

- ★★**応答ファイル `KohakuChangeMarkerUICPP.rsp` にインクルードパスを1本足した**(2026-08-20):
  `/I "..\..\..\source\open\includes\widgets"`。Story Edits の**変更行のテキストセル**
  (`KCMStoryCellView.cpp`)が `DrawStringUtils.h`＝**パレットの文字を任意の色で描く公式ヘルパー**を
  使うため。
  ⚠**呼ぶ側で相対パス include しても解決しない**——`DrawStringUtils.h` **自身**が
  `DVPublicUtilities.h` を include しており、**プリプロセッサは include 文を見た時点でファイルを
  探しに行く**(インクルードガードは見つかった後にしか効かない)。⇒ パスを通すしかない。
  ★**KBS が同じ理由で同じ1行を持っている**(`KohakuFindChangeCPP.rsp`)＝社内で同じ問いの答えは1つ。
  ⚠★**`.rsp` はこのリポジトリに控えが無い**(実体は `build/win/prj/` だけ)。
  **クリーン環境へ復元したらこの1行を手で足すこと**——足さないと
  `error C1083: 'DVPublicUtilities.h'` で `KCMStoryCellView.cpp` だけが落ちる。

## ⚠★★KCMUI で実際に踏んだ罠 — `KCMUIID.h` は **UTF-8 BOM 必須**

DollyXs 生成後に日本語コメント(prefix の由来)を書き足したが**BOM が無かった**ため、MSVC が CP932 と誤読して
行を食い、**`#define kKCMUIPrefixNumber` そのものが消えた**。症状は
`error C2065: 'kKCMUIPrefixNumber': 定義されていない識別子` が**`#define` より後ろの行**で大量に出る形
(＝**コンパイラの行番号が実ファイルとずれていたら、行が食われている**)。
⇒ **非 ASCII を1文字でも書いたら BOM を付ける**([[cpp-japanese-needs-bom]])。
直しはバイト先頭に `EF BB BF` を足すだけ(中身は触らない)。
★ 現状 `ui/` で非 ASCII を持つのは `KCMUIID.h` だけ(474 バイト)。**他の9本は 0 なので BOM 不要。**

## 復元方法
1. このフォルダーの**4ファイル**を、ビルドツリーへ上書きコピーする:

```
cp buildproj/KohakuExtendScriptChangeMarker.vcxproj          <SDK>/build/win/prj/
cp buildproj/KohakuExtendScriptChangeMarker.vcxproj.filters  <SDK>/build/win/prj/
cp buildproj/KohakuChangeMarkerUI.vcxproj                    <SDK>/build/win/prj/
cp buildproj/KohakuChangeMarkerUI.vcxproj.filters            <SDK>/build/win/prj/
```

⚠ **`SDKSamples.sln` への KCMUI の登録は控えていない**(sln は SDK 共有物のため)。
復元時は上の「`ProjectGuid` と sln 登録」を手で入れ直すこと。

⚠ **応答ファイル(`.rsp`)も控えていない。** `build/win/prj/KohakuChangeMarkerUICPP.rsp` に
`/I "..\..\..\source\open\includes\widgets"` を手で足すこと(理由は上の KCMUI の節)。

2. プラグインのソース(このリポジトリの `source/` フォルダー — `.cpp` / `.h` / `.fr` / `.rc` / `.png`)を
   `source/sdksamples/KCM/source/` へ置く。**SDK 側のフォルダー名は短い `KCM` のまま**で、
   プロジェクト名に合わせて改名はしない。`.vcxproj` の参照も ODFRC の `-i` フラグも
   `KCM\source` を指している(**ソースは 2026-08-12 に一段下へ移した**。KBS と同じ構成に
   揃えたもの。`.fr` や `.h` の include は自分のフォルダー基準で解決されるので、
   **全部まとめて同じ場所に置くこと**)。

3. ★**KCMUI のソース(このリポジトリの `ui/` フォルダー)は `source/sdksamples/KCM/ui/` に置く。**
   `KohakuChangeMarkerUI.vcxproj` の参照も ODFRC の `-i` フラグも
   `..\..\..\source\sdksamples\KCM\ui` を指している(2026-08-15 に `sdksamples\KCMUI` から移した)。

（`.vcxproj` の相対パスはビルドツリー基準 `..\..\..\source\sdksamples\KCM\source\...`
(KCMUI は `...\KCM\ui\...`) なので、`build/win/prj/` に置いて初めて正しく解決される。
ここに置いたままではビルドに使えない=あくまで控え。）

## 更新のしかた
`build/win/prj` の `.vcxproj` を編集したら、その最新をこのフォルダーへコピーし直してコミットする
（ビルド側が正・ここは控え）。

## ⚠ ソースを移動したときの注意(2026-08-12 に実際に踏んだ)
`.fr` のパスが変わったビルドは、中間フォルダが**前回の `.fr` のパスを覚えている**ため
`Previous .fr file ... and current .fr file ... do not match` で落ちる。
コードの問題ではないので、`build/win/objRx64/KohakuChangeMarker` を削除して
ビルドし直せばよい(中身は `.obj` / `.tlog` / `.pdb` などの中間生成物だけ)。
※フォルダー名は 2026-08-19 に `KohakuExtendScriptChangeMarker` から変わった(上記)。

## ⚠ Xcode プロジェクトについて
`KohakuExtendScriptChangeMarker.xcodeproj` と `*.xcconfig` は **Mac 版を取りやめた
(2026-08-07 ユーザー判断)時点のまま**で、その後に増えたソースが登録されておらず、
**今回のソース移動にも追随していない**。Mac 対応を再開するなら、まずここを作り直すこと。

ビルド: Release|x64。InDesign は閉じておく(開いていると `.pln` がロックされ LNK1104)。
