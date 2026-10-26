# 20. PDF の比較 ―― ★**2026-10-27 に廃止**

> ★**この機能はもうありません。** 作者の決定＝10-24「PDF同士の比較はやめる」・10-25「PDFの機能は、取りやめますね」・
> 10-26「撤去お願いします」（エクスチェンジ前の計画の束2＝`docs/superpowers/plans/2026-10-26-kcm-pre-exchange.md` Task 5）。
>
> 廃止したのは、フライアウトの **Set PDF as Target...** と **Set PDF as Source...** の2項目と、その一式＝
> PDF から比較用の文書を作る部品（`KCMPdfDoc`）・自前の PDF の読み手（`KCMPdfInflate`／`KCMPdfObjects`／`KCMPdfPages`）・
> 見開きの組み立て（`KCMPdfSpreads`）・書き出しの検査の数え方（`KCMPatchFilter` と `KCMConstants.h` の 160／6／8）・
> 各所の「PDF の文書なら断る」（Task Start の2つ・Import／Export Story Text・Compare with Tracked Changes・Export Pages to Word）・
> Pixel 以外のモードの断り・報告書の表の「Not compared - … PDFs.」の1行・How to Use の節（英語の6つ目のキーと日本語の節）。
> ⚠**2つの ActionID（`kKCMUIPrefix` + 89・+90）は欠番**＝墓（`ui/KCMUIID.h`）。ショートカットは番号で保存されるため使い回しません
> （この2つはショートカットの編集画面に出さない指定だったので、割り当てた人はいない見込み）。
> ⚠**facade の2つの口（`ChoosePdfEnd`・`IsPdfDoc`）は消していません**＝クラスの途中の仮想関数は消さない決まり。
> `ChoosePdfEnd` は断り、`IsPdfDoc` は常に偽を返します（ABI の印は据え置き＝型は変えていない）。
>
> ★**残したもの**＝Before/After 報告書の PDF（**別の機能**）と、その PDF のページを置く部品 `KCMPlacePdf`／ページの組の規則の
> 「Target 側も鍵で答える」9引数の形（`KCMPagePairRule.h`・一般の形で、オフラインの試験が証明している）。
>
> 項目（PDF-01〜PDF-30）の原文と実機の記録は git（`53d5b05` までのこの章）と
> `docs/ai-notes/kcm-pdf-compare-live-2026-10-14.md`／`kcm-pdf-export-check-live-2026-10-14.md` にあります。
> **章番号は詰めません**（[00-index.md](00-index.md) の決まり）。

---

- **PDF-D1** ⚠**「PDF」という語の残りは、全部 Before/After 報告書のもの**です（第15章）。報告書は今どおり2つの文書を比べて
  1つの PDF に書き出し、どのモードでも Story・Resources の表を作ります。
  - 訂正:

- **PDF-D2** ⚠**PDF のファイルを Target／Source に選ぶ道は無くなりました。** 端に選べるファイルは Task Start (Save)... の
  写し（.indd）だけです。
  - 訂正:
