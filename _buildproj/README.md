# _buildproj — ビルドプロジェクトファイルのバックアップ

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
  (Pages パネルのサムネイル更新実験)、`KESCMPageMap.*`、`KESCMPageNumberMarker.*` ほか。
- **追加のインクルードパス** `$(ID_SDK_DIR)\source\open\interfaces\ui`
  (`IPendingUpdateController.h` が source/open 配下にあるため。これが無いと C1083)。

## 復元方法
このフォルダーの2ファイルを、ビルドツリーへ上書きコピーする:

```
cp _buildproj/KohakuExtendScriptChangeMarker.vcxproj          <SDK>/build/win/prj/
cp _buildproj/KohakuExtendScriptChangeMarker.vcxproj.filters  <SDK>/build/win/prj/
```

（相対パスはビルドツリー基準 `..\..\..\source\sdksamples\KESCM\...` なので、
`build/win/prj/` に置いて初めて正しく解決される。ここに置いたままではビルドに使えない=あくまで控え。）

## 更新のしかた
`build/win/prj` の `.vcxproj` を編集したら、その最新をこのフォルダーへコピーし直してコミットする
（ビルド側が正・ここは控え）。
