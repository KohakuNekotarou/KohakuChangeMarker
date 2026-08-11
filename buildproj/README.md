# buildproj — ビルドプロジェクトファイルのバックアップ

このフォルダーは、KESCM の Visual Studio プロジェクトファイル
(`KohakuExtendScriptChangeMarker.vcxproj` / `.vcxproj.filters`)の**バックアップコピー**です。

## なぜここに置くか
KESCM の git リポジトリは `source/sdksamples/KESCM/`(このフォルダーの親)です。
一方、実際にビルドで使う `.vcxproj` は SDK のビルドツリー
`build/win/prj/KohakuExtendScriptChangeMarker.vcxproj` にあり、**git リポジトリの外**です。
そのため、プロジェクトファイルに加えたカスタム(下記)が git だけでは保全されません。
クリーンな環境に展開し直したときにビルドを再現できるよう、ここにコピーを置いて版管理します。

## このプロジェクトファイルに入っているカスタム(復元時に必要な要点)
- KESCM の全ソース(`KESCM*.cpp` / `.h`)の登録。特に近年追加分:
  `KESCMChangeNav.*`(変更ページへの Next/Prev ナビ)、`KESCMThumbnailRefresh.*`
  (Pages パネルのサムネイル更新)、`KESCMPageMap.*`、`KESCMPageNumberMarker.*`、
  `KESCMScrollMap.*`(スクロールバー地図 strip)、`KESCMStory*.*`(Story Edits)、
  `KESCMBook*.*`(ブック比較)ほか。
- **リンクライブラリに `DV_WidgetBin.lib` を追加**(2026-07-11、全4構成)。`KESCMScrollMap.cpp` の
  自前描画ビュー基底 `DVControlView`(`AbstractControlView`/`DVHostedWidgetView`)の実体は
  `WidgetBin.lib` ではなく `DV_WidgetBin.lib` にあるため(dumpbin で確認)。
- 追加のインクルードパスは**無し**(標準構成のまま)。
  ※2026-07-06 まで一時的に `$(ID_SDK_DIR)\source\open\interfaces\ui` を追加していた
  (`IPendingUpdateController.h` 用)が、サムネイル更新を `IImageCacheMgr::Purge` + `ForceRedraw` の
  最小2手に整理して `IPendingUpdateController` 依存を撤去したため、このパスも撤去済み。

## 復元方法
1. このフォルダーの2ファイルを、ビルドツリーへ上書きコピーする:

```
cp buildproj/KohakuExtendScriptChangeMarker.vcxproj          <SDK>/build/win/prj/
cp buildproj/KohakuExtendScriptChangeMarker.vcxproj.filters  <SDK>/build/win/prj/
```

2. プラグインのソース(このリポジトリの `source/` フォルダー — `.cpp` / `.h` / `.fr` / `.rc` / `.png`)を
   `source/sdksamples/KESCM/source/` へ置く。**SDK 側のフォルダー名は短い `KESCM` のまま**で、
   プロジェクト名に合わせて改名はしない。`.vcxproj` の参照も ODFRC の `-i` フラグも
   `KESCM\source` を指している(**ソースは 2026-08-12 に一段下へ移した**。KBS と同じ構成に
   揃えたもの。`.fr` や `.h` の include は自分のフォルダー基準で解決されるので、
   **全部まとめて同じ場所に置くこと**)。

（`.vcxproj` の相対パスはビルドツリー基準 `..\..\..\source\sdksamples\KESCM\source\...` なので、
`build/win/prj/` に置いて初めて正しく解決される。ここに置いたままではビルドに使えない=あくまで控え。）

## 更新のしかた
`build/win/prj` の `.vcxproj` を編集したら、その最新をこのフォルダーへコピーし直してコミットする
（ビルド側が正・ここは控え）。

## ⚠ ソースを移動したときの注意(2026-08-12 に実際に踏んだ)
`.fr` のパスが変わったビルドは、中間フォルダが**前回の `.fr` のパスを覚えている**ため
`Previous .fr file ... and current .fr file ... do not match` で落ちる。
コードの問題ではないので、`build/win/objRx64/KohakuExtendScriptChangeMarker` を削除して
ビルドし直せばよい(中身は `.obj` / `.tlog` / `.pdb` などの中間生成物だけ)。

## ⚠ Xcode プロジェクトについて
`KohakuExtendScriptChangeMarker.xcodeproj` と `*.xcconfig` は **Mac 版を取りやめた
(2026-08-07 ユーザー判断)時点のまま**で、その後に増えたソースが登録されておらず、
**今回のソース移動にも追随していない**。Mac 対応を再開するなら、まずここを作り直すこと。

ビルド: Release|x64。InDesign は閉じておく(開いていると `.pln` がロックされ LNK1104)。
