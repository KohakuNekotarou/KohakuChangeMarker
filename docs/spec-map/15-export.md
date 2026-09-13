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
  **左（旧版）は「Print comparison marks」を一時的に ON にして書き出す**ので、リングと削除ページの赤「/」が載る。右（新版）は素のまま
  （2026-09-13 ユーザー要望「マークはソースの方のみ」。最初の実装は逆だった）。
  ⚠Task Start の写しは比較後に離してあるので、書き出しの間だけ `sSrcDB`／`sSrcPageToTarget`／`sOverflowS` を写しに貸し、終わったら離した状態に戻す。
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

- **REP-06** 出力先＝**保存ダイアログで選ぶ**（2026-09-13 ユーザー要望「保存場所と名前をつけられるように」。最初は Target の隣に固定だった）。
  初期名は `<Target 名>.compare-report.pdf`。書けたら**既定の PDF ビューアで開く**。
  ステータス行に `Report: N pages -> <パス>`。キャンセルは `Report cancelled.`、失敗は `Report failed: <理由>`。
  - 訂正:

- **REP-08** ★**左側のマークにはページの縁の枠（frame）を付けない**（2026-09-13 ユーザー要望）＝変わった箇所のリングだけ。
  実装＝`KCMDrawEventHandler::sRingFrameOff` を Before の書き出しの間だけ立て、リング画像のキャッシュを前後で無効化（`InvalidateRingCache`）。
  画面・印刷・サムネイルの枠は従来どおり。
  - 訂正:

- **REP-09** 報告書に足す文字（見出し・注記・要約）は **18pt**（既定 12pt の 1.5 倍。2026-09-13 ユーザー要望「2倍」→「1.5 でいい」）、左揃え。
  - 訂正:

- **REP-07** 書き出しの前後で Target／Source の `modified` は変えない（`SaveRestoreModifiedState`）。実測＝2文書とも false のまま。
  ⚠**Source が貸し DB（KIDMCP のクローン）のときは断る**（`the Source is a lent copy ...`）＝クローンへの書き出しは落ちる恐れがあり試さない（09-13 再検査で追加）。
  保存ダイアログは**書き出しの前**に出す（キャンセルが無料）。配置設定の復元は `PlacePrefsRestorer` のデストラクタ＝途中で失敗しても戻る。
  - 訂正:

## 2b. 3部構成への拡張（Export Before/After **PDF** Report・2026-09-13 午後）

- **REP-10** メニュー名は「Export Before/After PDF Report」（ユーザー指示「Report の前に PDF を。メニュー名も」）。1P目のタイトルも `Before / After PDF Report`。
  - 訂正:

- **REP-11** 1P目は**3行だけ**（タイトル／`Before (Source): 名前`／`After (Target): 名前`）。**最下部に、左の絵の位置に「Before」、右の絵の位置に「After」**。
  旧 REP-02 の Mode・Pages・Made・Story Edits の行・Resources の行は撤去。Pixel モード以外では 4 行目に `Pixel: not compared in this mode (only added / removed pages are shown)`。
  - 訂正:

- **REP-12** Pixel の変更ページ（REP-03）には**文字を一切入れない**＝見出し・注記・キャプションを撤去。追加／削除ページの空側も白紙のまま（ユーザー指示「実際のレイアウト画面が配置されているほうには文字情報なにもいりません」）。
  - 訂正:

- **REP-13** ★**Story の表**＝Pixel のページの後に、見出し `Story Changes: N stories, M edits` と**2列の表**（左＝Source、右＝Target）。ストーリーごとに見出し行 `p.<番号>  <冒頭の語句>  [text / attr / added story / removed story / text agrees]`、変更1件ごとに1行。
  セルは**パネルの3片**（前の文脈・変わった部分・後の文脈）で、**文脈は 40% tint・変わった部分は 100%**。挿入で左が空／削除で右が空の中央片は `|`（パネルのキャレットと同じ「場所」の印）。
  **ルビ**＝中央片に本物のルビ（`KCMApplyRuby`）、**圏点**＝本物の圏点（`KCMApplyKentenKind`。書けない種類は `[名前]` を上付きで）、**脚注／文末脚注**＝番号を上付きで中央片の直後に。アンカー付きオブジェクトは抜粋の ⚓ のまま。追加／削除ストーリーは1行（無い側に `(added story)`／`(removed story)`）。
  - 訂正:

- **REP-14** ★**Resources の表**＝Story の後に、見出し `Resources Changes: <要約>` と同じ2列の表。定義ごとに見出し行 `<Kind>  <Key（%エスケープ復号）>  [added / removed / changed]`、差のある属性ごとに1行（左＝`名前: 旧値`／右＝`名前: 新値`・片側に無ければ `-`・値はパネルと同じ短縮 `KCMShortResourceValue`）。属性差 0 の Changed は `(the difference is inside a child element)`。
  - 訂正:

- **REP-15** ★**どのモードで比較していても表2つは出る**＝Story モード以外では `KCMStoryDiffRun::Run` を報告書のために回し、終わったら各行の子・fTextCompared・fTargetTextCount を元に戻す（`StoryDetailLoan`）。Resources の結果が無ければ `KCMResourceStore::RebuildForPair` で作り、終わったら `Clear`＋通知（`ResourceLoan`）。
  ⚠Pixel のページだけは借りられない（ラスタ化は画面のリング・対応表・あふれキャッシュを作り替える）＝Pixel モード以外では出さず、1P目に書く。
  ⚠Pixel モードの Story 行は `DropRowsWithNoContentChange` を通った後なので、ルビだけ・圏点だけのストーリーは行が無い（Pixel モードの既知の限界）。
  - 訂正:

- **REP-16** 表の作り方＝`KCMReportTable.cpp`。節の先頭ページに見出し（18pt）と全幅のテキストフレーム → `ITableUtils::InsertTable`（2列・行の高さ 0＝自動）→ セルは `ITableModel::QueryCellContentBoss`→`ITextStoryThread::GetTextStart` の位置に `ITextModelCmds::InsertCmd`、属性は範囲ごとに（14pt・左揃え・tint・上付き・ルビ・圏点）→ `ITextUtils::IsOverset` の間、`kNewSpreadCmdBoss` でページを足し `kTextLinkCmdBoss` で連結（上限 200 ページ）。表の行数は節ごと 400 行で打ち切り（`... (the table stops here)`）。
  - 訂正:

- **REP-17** 表は **4列「ID（Resources は Kind）｜Δ｜Before｜After」**、1行目は表のヘッダー行（ページをまたぐと繰り返す）。Δ はパネルと同じ4記号（`+` 新側だけ／`-` 旧側だけ／`=` 比べて同じ／`≠` 違う）。
  ストーリーの親行＝ID・Δ・`p.N  冒頭の語句`（Before/After のセルは結合して1つ）。子行＝Δ と左右の文。Resources の親行＝Kind・Δ・キー（結合）。属性行＝属性名・Δ・旧値／新値。
  - 訂正:

- **REP-18** ★**Story の ID セルは、同じストーリーの子行と縦に結合**する。⚠結合セルはページをまたげない（実測＝80 件を1セルにすると永遠に収まらず 200 ページ超えで止まった）ので、**表を全ページに流してから、各行の載ったフレーム（`ICellContent::GetParcelFrameUID`）が同じ範囲だけ結合し、ページの先頭の塊に ID を再記入**する（`MergeLabelRunsByPage`）。
  - 訂正:

- **REP-19** ルビは本文と同じ大きさ（14pt・`kTARubyPointSizeBoss`）の本物のルビで2行、圏点は 0.6 倍の本物の圏点。セルの余白 4pt（`kCellAttr*InsetBoss`）、上の行を持つ行は上の余白を広げる。1列目は 180pt・12pt（⚠長い属性名は折れないので狭いと空欄になる＝`SwatchColorGroupReference` で実測）。
  - 訂正:

- **REP-20** 1P目＝3行を 36pt、**猫の足あと 10 個**（赤・青交互、蛇行、`kKCMPawOutlines` からのスプライン＋報告書文書の RGB スウォッチ＝`KCMReportPaws.cpp`）、最下部に `Exported: YYYY-MM-DD HH:MM:SS`。Before／After の語は **Pixel の各ページの絵の上**（1P目の足には置かない）。
  - 訂正:

- **REP-21** ★**Resources の値に文書の単位を添える**＝`8.503937007874015 (12 Q)`（`KCMResourceUnits.h`）。属性名で振り分け＝`PointSize`／`…FontSize`→文字サイズ単位、`…Weight`→線の単位、`Leading`／`BaselineShift`／`Space*`／`…Indent`／`…Offset`→テキスト単位（J 以外は水平単位）、`Left/RightInset`／`…Gutter`／`…Width`→水平、`Top/BottomInset`／`…Height`→垂直。知らない名前・数でない値・単位が pt のときは添えない。**PDF・パネルの行・パネルの帯の3か所が同じ inline 関数**。
  - 訂正:

## 3. やっていないこと

ページの中の差分行（Story Edits をページ別に振る）／全ページ出力／プリセットの選択 UI／ページ内の変更領域の枠（右側のリングで代替）／Story／Resources モードで Pixel の絵を出すこと（REP-15）。
