# 15. 書き出し ― 変更ページの TSV と Before/After 報告書

> コード地図での位置: `source/KCMChangedPagesTSV.{h,cpp}`（TSV）／**`source/KCMReport.{h,cpp}`（報告書・2026-09-13 新設）**。
> どちらも model 側で、フライアウトの項目は UI（`ui/KCMActionComponent.cpp`）が facade を呼ぶ。

## 1. 変更ページの TSV（Export Changed Pages...）

- **EXP-01** 比較中に、変更ページの一覧（Page／Type＝Changed・Inserted・Deleted）をタブ区切りで保存する。保存先はファイルダイアログで選ぶ。
  Inserted／Deleted は画面の赤「/」と同じ集合（あふれキャッシュ）から取る。第3章 CMP-06。
  - 訂正:

## 2. Before/After 報告書（Export Before/After Report・2026-09-13）

- **REP-01** ★**変更ページだけ**を出す（ユーザー決定）。対象＝リングの付いた Target のページ∪追加ページ（文書順）、そのあとに削除ページ（Source の文書順）。
  Story／Resources モードではリングが無いので、要約と追加・削除ページだけになる。
  - 訂正:

- **REP-02** 1ページ目は**要約**＝Before（Source か Task Start の時刻）／After（Target）の名前・モード・`changed / added / removed`・作成日時・
  Story Edits の行（先頭 40 行。`+`＝追加・`-`＝削除・`*`＝変更、右に種類）・Resources の要約 1 行。
  - 訂正:

- **REP-03** 変更ページごとに横長 1 ページ＝上に見出し `p.N   Before: <名前> p.M   After: <名前> p.N`、左に旧版、右に新版。
  **右側は「Print comparison marks」を一時的に ON にして書き出す**ので、リングと赤「/」が載る。左は素のまま。
  追加ページは左が空で `(added - nothing on the Before side)`、削除ページは右が空で `(removed - ...)`。
  - 訂正:

- **REP-04** 寸法＝Target の 1 ページ目の大きさ W×H から、報告書ページ＝`2W+3×24pt` × `H+48+18+2×24pt`。**縮めない**（両側の縮尺を揃えるため）。
  Source のページが別寸法でもそのまま置く。
  - 訂正:

- **REP-05** 作り方＝両側を**一時 PDF** に書き出し（`kPDFExportCmdBoss`・セッションの PDF 設定・見開き OFF・セキュリティ OFF・UI なし・進捗バーなし・
  書き出し後に開かない）→ 窓なしの新規文書に `kSetPDFPlacePrefsCmdBoss`（ページ番号・Media で切る）＋`SDKLayoutHelper::PlaceFileInFrame` で配置
  → 見出し・注記はテキストフレーム（左揃えを明示。既定の段落設定が均等配置だったので）→ 報告書を書き出し → 文書を閉じ、一時 PDF を削除、
  配置設定を元に戻す。
  ⚠Task Start の写しは報告書のために**もう一度再水和**する（比較のときの写しは離して閉じてある）。ページの対応は写しのラベルで取れる（第3章 CMP-04）。
  - 訂正:

- **REP-06** 出力先＝Target の隣に `<Target 名>.compare-report.pdf`（未保存の文書ならデスクトップ）。書けたら**既定の PDF ビューアで開く**。
  ステータス行に `Report: N pages -> <パス>`。失敗は `Report failed: <理由>`。
  - 訂正:

- **REP-07** 書き出しの前後で Target／Source の `modified` は変えない（`SaveRestoreModifiedState`）。実測＝2文書とも false のまま。
  - 訂正:

## 3. やっていないこと

ページの中の差分行（Story Edits をページ別に振る）／全ページ出力／プリセットの選択 UI／ページ内の変更領域の枠（右側のリングで代替）。
