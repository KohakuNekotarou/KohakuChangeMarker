# 20. PDF の比較 ― 書き出した PDF 2つを、各ページを置いた文書にして Pixel で比べる

> コード地図での位置: model＝`source/KCMPdfInflate.*`（Flate の展開）／`source/KCMPdfObjects.*`（xref・オブジェクト・ストリーム）／
> `source/KCMPdfPages.*`（ページ・箱・回転・元のページ UID）＝ここまでの3つは SDK なしで試験（`work/kcm-pdf-test`）／
> `source/KCMPlacePdf.*`（PDF の1ページを置く・利用者の配置の設定を戻す＝報告書と共用）／`source/KCMPdfDoc.*`（PDF の文書・登録・組み方）。
> 入口の変更＝`KCMPairChoice.cpp`（ファイルの端が PDF なら文書を作る）・`KCMComparisonRun.cpp`（断り・2行目の一言）・`KCMPageMap.cpp`（組み方・マスター）・
> `KCMDrawEventHandler.cpp`／`KCMColorSampler.cpp`（本物の解像度）・`KCMPeek.cpp`（閉じたときの掃除・終了）。UI＝`ui/KCMActionComponent.cpp`（2項目・灰色・モード切替・ダイアログ前の断り）。
> 設計＝`docs/superpowers/specs/2026-10-14-kcm-pdf-compare-design.md`（D1・D2・§10）。計画＝`docs/superpowers/plans/2026-10-14-kcm-pdf-compare.md`。
> 実機の記録＝`docs/ai-notes/kcm-pdf-compare-live-2026-10-14.md`（2026-10-14・項目ごとの実測＝`work/kcm-pdf-live/run.md`）。
> 訂正の書き方は [00-index.md](00-index.md)。番号の頭字は **PDF-**。

---

## 1. 選ぶ

- **PDF-01** ★フライアウトの「Set as Source」の下に **`Set PDF as Target...`**・**`Set PDF as Source...`**。押すと PDF を1つ選ぶダイアログ（題 `Set PDF as Target - the newer PDF`／`Set PDF as Source - the older PDF`・絞り込み `PDF (*.pdf)`）。**キャンセルなら何も変えず、何も言わない。**
  - 訂正:
- **PDF-02** 選んだ PDF はその端の**ファイルの選択**（Task Start (Save)... の写しと同じ入れ物）。パネルの Target:/Source: の行に PDF のパス。Start の後は作った文書の名前。
  - 訂正:
- **PDF-03** 押せる＝比較していない間。**文書が1つも開いていなくても押せる**（Start も、両端がファイルなら押せる）。
  - 訂正:
- **PDF-04** PDF かどうかは**中身の頭（`%PDF-`）**で決める（拡張子ではない）。違えば `Not a PDF: <名前> (its first bytes are not a PDF's).` で何も変えない。
  - 訂正:
- **PDF-05** ステータス＝`PDF chosen as the Target: <名前>. Choose a PDF as the Source too, then Start.`（Source も同じ形）・両方そろえば `Both ends are PDFs. Start compares them.`・PDF を Source に選んでメモリの Task Start を手放したら後ろに ` The Task Start kept in memory was let go.`
  - 訂正:

## 2. Start

- **PDF-06** ★Pixel 以外のモードで PDF の端があれば断る（**何も作らない**）＝`PDFs are compared in the Pixel mode only. Switch the Compare mode to Pixel.` Before/After 報告書はこれを問わない（どのモードでも絵を作る）。
  - 訂正:
- **PDF-07** ★片方だけ PDF は断る（D1・何も開かず作らない）＝`One end is a PDF and the other a document. Choose PDFs for both ends (a document against its PDF is a different check).` 報告書も同じ文で断る。
  - 訂正:
- **PDF-08** 同じ PDF を両方に選ぶと同じ文書になり、今の「同じ文書」の断り＝`Target and Source are the same document. …`
  - 訂正:
- **PDF-09** 比べ方は今の Pixel。印は Target の PDF の文書の窓（既定では隠れていて、ツールを押している間か Always Show Marks on Target で見える＝今のまま）。Start の後は Target が前。
  - 訂正:
- **PDF-10** ステータスの2行目の頭に組み方の一言（第4節）。1行目は今どおり `marks start`。
  - 訂正:

## 3. PDF の文書

- **PDF-11** 読めなければ `The PDF could not be read: <理由>.`＝`the PDF is encrypted`／`the file has no startxref (it may be cut short)`／`the PDF has no pages` など・9999 ページより多い＝`it has N pages, more than a document holds (9999).`・1pt 未満か 15552pt を超えるページ＝`page N is too small or too large for a document page.`
  - 訂正:
- **PDF-12** ★1ページに PDF の1ページ。ページの大きさは**仕上がり**（TrimBox → 無ければ CropBox → MediaBox）・¼回転なら縦横を入れ替える（/Rotate は置くときに InDesign が効かせる）。PDF のページは原点に置き、**TrimBox を持つページは仕上がりで、持たないページは CropBox（無ければ MediaBox）で切り抜く**。2ページ目以降は `kNewSpreadCmdBoss` でそのページの大きさ付きで足す。
  - 訂正: ✅2026-10-14 実機で変更＝旧「全ページを仕上がりで切り抜く」（InDesign は TrimBox の無いページで「仕上がりで切り抜く」を拒否し、ページが空白のまま比べていた）
- **PDF-13** 名前 `<ファイル名>.pdf (KCM)`・窓で開く・**取り消しの履歴は空**・保存しなくてよい状態（閉じても聞かない）。チェック・猫の手を付けた後、Hide Unchanged Spreads を使った後は閉じるときに保存を聞かれる。
  - 訂正: ✅2026-10-14 実機で変更＝①取り消しに KCM の配置が残っていた（Ctrl+Z でページが消えた）→作り終えたら履歴を空にする ②Hide Unchanged Spreads の後も聞かれる（隠す命令が文書を変更ありにする）を足した
- **PDF-14** ★利用者の PDF 配置の設定（ページ番号・切り抜き・背景）は、作り終えても失敗しても戻る（報告書も同じ部品）。
  - 訂正:
- **PDF-15** 置けたが枠がページと合わないページは ` Pages not at their page size in <文書名>: 3, 7.` で名指す（比べるのは続ける）。
  - 訂正:
- **PDF-15b** ★InDesign が**置かなかった**ページが1つでもあれば、その PDF を断る＝`The PDF could not be placed: InDesign refused page N.`（作りかけの文書は閉じる）。空白のページ同士は「変化なし」になるため。
  - 訂正: ✅2026-10-14 実機で追加（旧＝置けなかったページも PDF-15 の名指しで続けていた）
- **PDF-16** ページが多ければ3秒後に進捗バー（`Making the PDF document...`・ページの間で Cancel を聞く）。Cancel なら作りかけを閉じて `Cancelled.`
  - 訂正:
- **PDF-16b** 使い回し＝開いていて、PDF の更新日時と大きさが作ったときと同じならその文書を使う。書き直されていたら作り直し、古い文書は変更が無ければ閉じる。窓を閉じたら登録から外れ、**選択は残る**（次の Start でまた作る）。前に選んだ PDF の文書は、選び直しても開いたまま残る。
  - 訂正:

## 4. 組み方

- **PDF-17** ★InDesign が書き出した PDF（全ページに元のページ UID があり、全部が1つの `DocumentID`）は UID で組む＝`PDF pages paired by their InDesign page ids.`＝ページを足した・消した・並べ替えた後も同じページ同士。
  - 訂正:
- **PDF-18** ★UID の無い PDF は順番で組む（D2）＝`These PDFs carry no page ids - pages paired in order.` 一部のページだけ・複数の文書の UID＝`A PDF holds pages of several documents, or pages without ids - pages paired in order.`
  - 訂正:
- **PDF-19** Pair Pages by UID を外せば順番＝`PDF pages paired in order (Pair Pages by UID is off).`
  - 訂正:
- **PDF-20** 見開きで書き出した PDF のページは横長の1ページ（2倍の幅）。そのページの身元は最初の UID。
  - 訂正:
- **PDF-21** PDF の文書のマスターは比べない（KCM が作った空のもの）。
  - 訂正:

## 5. 描き方

- **PDF-22** ★PDF の文書は**本物の解像度**で描く（比較・旧版べた載せ・色の読み取り）。比べる2つは必ず同じ条件（片方が PDF の文書なら両方）。置いた PDF をプロキシで描くと、変えていないページでも置くたびに 700〜1,000 画素ずれるため。
  - 訂正:

## 6. 機能ごと

- **PDF-23** Refresh・Prev/Next・Hide Unchanged・追加/削除の登録・✓・猫の手は今のまま。
  - 訂正:
- **PDF-24** ★PDF の組を比較中に Pixel 以外へ切り替えると、比べ直さずに止める＝`Compare mode: story changes. (stopped - PDFs are compared in the Pixel mode only. Switch the Compare mode to Pixel.)`
  - 訂正:
- **PDF-25** Before/After 報告書の表は2つとも1行＝`Not compared - the two ends are PDFs.`（絵のページは今どおり）。
  - 訂正:
- **PDF-26** Task Start（メモリ）・Task Start (Save)... は、前面（Target）が PDF の文書なら断る＝`Task Start not taken: The Target is a PDF. A Task Start takes a document.`（保存ダイアログを出さない）。Target を PDF の**ファイル**で選んでいるときは、今どおり前面の文書を取る。
  - 訂正:
- **PDF-27** Import Story Text...・Export Story Text...・Compare with Tracked Changes... は PDF の文書なら断る＝`The Target is a PDF. This works on a document.`（どれもダイアログの前。スクリプトの道は model が同じ文で断る）。
  - 訂正:
- **PDF-28** Ignore Page Number Marker は PDF の中には効かない（ノンブルは絵の一部）。
  - 訂正:

## 7. 読み手

- **PDF-29** 自前の C++（外部のコード無し）＝xref 表と xref ストリーム（予測子）・増分更新（/Prev・/XRefStm）・オブジェクトストリーム・Flate。暗号化は断る。ページごとに MediaBox・CropBox・TrimBox（と持っているか）・回転・`/PieceInfo /InDesign` の `PageUIDList`・`DocumentID`。
  - 訂正:
- **PDF-30** どんな PDF でも落ちない＝変わり種24本・InDesign の書き出し47本（実機試験の9本を含む）・切り詰め／バイト化け／ゴミ 1,089 回（最遅 12.5 ms）＝`work/kcm-pdf-test`（1243 checks, 0 failures・2026-10-14）。
  - 訂正:

---

## ⬜この章で未確認（2026-10-14・実機で届かなかったもの）

- PDF-22 の旧版べた載せ（Shift）と色の読み取り（Alt）を PDF の文書で＝KCM ツールを修飾キー付きで押す必要があり、試験の間 InDesign が最小化されていた（send_input は人のマウスを奪う）。
- PDF-08（同じ PDF を両方）・PDF-16 の Cancel・PDF-18 の「一部・複数の文書」の一言・PDF-19・PDF-28・PDF-11 の 9999 ページと大きさの範囲＝コードで確かめた文言のまま、実機では未試験。
- PDF-23 の登録・✓・猫の手を PDF の文書で（Next > と Hide Unchanged だけ試した）。
