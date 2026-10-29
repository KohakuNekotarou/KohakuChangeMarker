//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuExtendScriptChangeMarker (KCM)
//
//  Run-time Japanese. The jaJP string table was retired (same decision as KBS): every locale
//  reads the enUS table, and the few strings that speak Japanese are switched in here by UI
//  language. The test is the UI LANGUAGE, not the feature set -- PMLocaleId carries the two
//  separately -- so a Roman engine with a Japanese UI gets Japanese as well.
//
//  ★**What THIS half holds is what is below**: the How to Use text (kHint / kHint2), and
//    nothing else since 2026-09-08 -- the book comparison's warning went English with its
//    confirmation (spec map BK-11), so no alert of that feature passes through here any more.
//    ⚠**Do not count the product's Japanese here.** The model half has its own KCMLoc.h with
//      its own string (the Hide Unchanged confirmation, which changes the document, so it
//      belongs to that side). While both files described the whole product, each of them
//      explained strings it did not hold. **Read the counterpart; do not re-count it here**
//      ([[one-question-one-place]]).
//  ★**The line is drawn by what the user asked for in Japanese**, not by the nature of the
//    text. KBS draws the same line: the how-to-use text is Japanese, the panel, the menus and
//    the status line stay English.
//    ★**The book comparison's two alerts are BOTH English** (2026-09-08, spec map BK-11).
//      For four weeks they were not: the confirmation had been moved on its own, because the
//      instruction that moved it named only the confirmation, and the warning stayed here.
//      ⇒ **An instruction that names one of a pair leaves the other behind, and nothing fails**
//      -- no build breaks and no test notices, so it is found by reading, if at all.
//  ⚠**About is English only** -- it is one line, "<name> version x.y.z", with nothing to
//    translate.
//
//  ***** This file is UTF-8 with BOM ***** so that the u"..." literals stay readable as
//  Japanese. (Without the BOM, MSVC reads them as CP932.)
//
//  ★**The include guard is deliberately DIFFERENT from the counterpart's** (this one is
//  `__KCMUILoc_h__`). **The UI project has the model half's folder on its include path**, so
//  both files are visible from one translation unit, and **two files with different contents
//  sharing a guard means the one included first silently erases the other** -- and the name of
//  the erased file appears nowhere.
//  ⚠`KCMBoundaryID.h` is the opposite case: the two copies are **identical**, so sharing a
//  guard is correct there.
//
//========================================================================================

#ifndef __KCMUILoc_h__
#define __KCMUILoc_h__

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

#include "KCMUIID.h"

namespace KCMLoc
{
	/** Is the UI language Japanese? */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}

	/** The Japanese text on a Japanese UI, the enUS table's translation of the key otherwise.
	    The result is finished text (marked untranslatable). */
	inline PMString Text(const char* englishKey, const char16_t* japanese)
	{
		// Not a key: the wchar_t constructor marks it untranslatable itself (publiclib's PMString.cpp),
		// and wchar_t is UTF-16 on Windows, the one platform KCM is built for.
		if (JapaneseUI())
			return PMString(reinterpret_cast<const wchar_t*>(japanese));
		PMString s(englishKey);
		s.Translate();
		s.SetTranslatable(kFalse);	// a translated key has no constructor that marks it
		return s;
	}
}

// The Japanese that used to live in KCM_jaJP.fr, the part of it this half holds. The matching
// keys (kKCMHintKey / kKCMHint2Key) are alive in KCMUIID.h and
// KCMUI_enUS.fr -- an English UI reads those.
namespace KCMJa
{
	// (⛔kImported / kImportTracked - the status line's "Importしました" and the dialog after an import - went on
	//  2026-09-25: the user asked for the dialog to stop and the result to be shown on the panel only.)

	// ----- How to Use... (the operating reference; it used to be the panel's description) -----
	const char16_t kHint[] =
		u"【注意】どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。\n"
		u"\n"
		u"【Kohaku Change Marker ツール】\n"
		u"・左ボタン長押し\n"
		u"　比較枠を表示(押している間/透明度はパネルの25%・75%)\n"
		u"・左ボタン+Shift長押し\n"
		u"　元(旧版)を表示(押している間)\n"
		u"・左ボタン+Shift+" kKCMAltKeyName u"長押し\n"
		u"　元を50%で表示(押している間)\n"
		u"・左ボタン+" kKCMAltKeyName u"長押し\n"
		u"　クリック点の色情報(CMYK)をカーソルとパネルに表示\n"
		u"・Shift・" kKCMAltKeyName u"は押したままでも離しても可\n"
		u"\n"
		u"【ページの対応づけと追加・削除(Pair Pages by UID / Add / Remove)】\n"
		u"ページは既定で「同じ UID(内部ID)を持つページ同士」を比較します。\n"
		u"・別々に作った2文書や IDML を経た文書は UID を共有しないため、何も組にならず赤の斜線になります。その場合は「Pair Pages by UID」を OFF にすると、「並び順で先頭から組にする」規則になります。\n"
		u"・並び順規則では、ページを追加・削除するとそれ以降がズレて誤検知されるので、追加・削除したページを比較対象から外して登録できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Register as Added/Removed Pages」を選びます。\n"
		u"\n"
		u"【ページのチェック(Check)】\n"
		u"確認が済んだページにチェック印を付けられます。消すまで残ります(Stop Comparison でも消えず、文書を閉じたときに忘れます)。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Check」を選びます(もう一度で解除)\n"
		u"・チェック印・登録(Add / Remove)・猫の手は保存・読込できます。\n"
		u"・チェック印と肉球は、置いた時点で文書のページに自動で書き込まれます(Undo/Redoで戻せます)。文書を保存すれば、次に開いたときも残っています。\n"
		u"・登録(Added/Removed)は比較のための指定なので、文書には書き込まれず、Stop Comparison で消えます。\n"
		u"\n"
		u"【ページ比較の更新(Refresh Page Comparison)】\n"
		u"編集したページだけを比較し直せます。Start Comparison 中で、かつPixel比較モードのときだけ使用できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Refresh Page Comparison」を選びます。\n"
		u"・右クリックするのはTarget(新しい方)です。選んだページと、対応するSource側のページの両方について、比較枠とサムネイルを最新の状態に更新します。\n"
		u"\n";

	// ★**The SECOND part of the How to Use text.** KCMActionComponent::DoUsage concatenates it
	//   after kHint above into one alert, so **the reader sees one piece of writing**. It holds
	//   everything from Task Start on (Task Start -> the Before/After PDF report -> Story IDs ->
	//   the round trip -> tracked changes -> books -> Story Edits -> the cat paw); kHint holds the
	//   disclaimer, the tool, the page pairing, Check and the page comparison's refresh.
	//   ★★**THE WORDS ARE THE AUTHOR'S** (2026-10-28: "デスクトップの KCM How to Use（日本語）.txt を
	//     更新しました。これに変えて欲しい") - both parts built line for line from that file, with the
	//     disclaimer moved to the head by the author. Change them with the author, not by rewording here.
	//   ⚠**The reason for the split is on the English side**: odfrc caps the length of a single
	//     string in a StringTable ([[odfrc-long-string-limit]]). **This side has no such limit** -- it
	//     is a u"" literal -- **but both languages are split at the same points**: a key that exists
	//     on one side only, or a seam in a different place, is what the next person has to work out.
	//     ⚠**The English side (kKCMHintKey / kKCMHint3Key / kKCMHint4Key / kKCMHint5Key / kKCMHint2Key
	//       in KCMUI_enUS.fr) is split at the same points. Do not move one without the other.**
	const char16_t kHint2[] =
		// ★Task Start first in this piece: on the English side it opens kKCMHint3Key (with the PDF report and Story
		//   IDs), read between hint and hint2, so the two languages read in the same order.
		u"【少し前の状態と比べる(Task Start)】\n"
		u"いまの文書の状態を取り、Source にします。\n"
		u"・「Task Start」: 状態をメモリに取ります。\n"
		u"・「Task Start (Save)...」: 文書の写しを、選んだ場所にファイルとして保存し、そのファイルを Source にします。\n"
		u"・Pixel 比較では、日付の変数を載せたページはいつも変更として出ます(変数がそのときの日付を描くため)。\n"
		u"\n"
		u"【Before/After PDF 報告書(Export Before/After PDF Report)】\n"
		u"2つの文書を報告書が自分で比べ、1つの PDF に書き出します。\n"
		u"\n"
		u"【ストーリー ID(Show Story IDs)】\n"
		u"テキストを持つフレーム(テキストフレーム・パス上テキスト・アンカー付きフレーム)の上に、載っているストーリーの ID を「StoryID:番号」の形で表示します。番号はスクリプトの story.id です。\n"
		u"・使い方: パネルのフライアウトメニューの「Show Story IDs」を ON にします。\n"
		u"\n"
		// ★The story text round trip - the English side's kKCMHint4Key, at the same seam.
		u"【ストーリーの往復(Export Story Text / Export Pages to Word / Import Story Text)】\n"
		u"ストーリーを Word 文書に書き出し、InDesign の外で編集してもらい、その編集を文書へ1件ずつ入れます。\n"
		u"・「Export Story Text...」は 1ストーリー＝1つの .docx を書き出します。フレームを選択していればそのストーリーだけ、何も選択していなければ全ストーリーを書き出します。\n"
		u"・「Import Story Text...」は文書を変えません。一覧の見出しは「Proposed Edits」になり、変更の行は「文書がこう変わる」という提案です。\n"
		u"・写しに入らなかったものは一覧の「先頭」に、Change 列に赤い「!」と、場所と理由を添えて並びます。\n"
		u"・段落の中の表: Word には無いので、ファイルでは「表の前の文字」「表」「表の後の文字」が別々のブロックになります。表の前後で段落がどこで切れるかはファイルではなく今の文書が決めます。Word で表の隣に改行を足しても消しても何も変わりません。表の隣で段落を割る・つなぐのは InDesign で行ってください。\n"
		u"・「Export Pages to Word...」(ページパネルのページのメニュー、またはこのフライアウト)は、選んだページを1ページ=1つの .docx に書き出します。名前は「p<ページ>.docx」、フォルダーは「Pages <日時>」です。文字の枠は、枠と同じ位置のテキストボックスになり、そのストーリーが丸ごと入ります(枠の多いカタログのページ向けです)。表はテキストボックスの中に Word の表として入ります。脚注の参照の所には印「⟦NOTE n⟧」(n は脚注の番号)が入り、脚注の文は同じファイルの2ページ目に、見出し「Box b ⟦NOTE n⟧」の下の箱としてまとめて入ります。脚注の文はその箱で直してください。印は中を直せず、消すことはできます。印を消すと、その脚注を消す提案になります。Word のテキストボックスには脚注を新しく作れないので、脚注を足すのは「Export Story Text...」で行ってください。連結した枠(連結したストーリーは「Export Story Text...」で丸ごと書き出してください)、あふれている枠、表のある縦組みの枠(Word では組めません)、文末脚注の参照がある枠は書き出さず、ページと一緒にステータス欄に出します。直したファイルは、ストーリーのファイルと同じく「Import Story Text...」で取り込みます。\n"
		u"\n"
		// ★The Track Changes listing - the English side's kKCMHint5Key, at the same seam.
		u"【変更履歴との比較(Compare with Tracked Changes...)】\n"
		u"InDesign の変更履歴(変更をトラック)が文書に記録した変更を一覧します。\n"
		u"\n"
		// ★From the books on - the English side's kKCMHint2Key.
		u"【ブックの比較(Compare Books)】\n"
		u"ブック(.indb)どうしを章(ドキュメント)単位で比べ、どの章が変わったかを一覧します。\n"
		u"・使い方: ブックパネルで比べたいブックのタブを前面にし、もう1冊のブックも開いてから、パネルのフライアウトの「Compare Books」を選びます。\n"
		u"・前面タブのブックが Target(新しい方)、それ以外で最初に開いているブックが Source(古い方)です。\n"
		u"・章ごとに結果が出ます: Changed(変更あり) / NoChange(変更なし) / ChapterAdded(Targetにだけある) / ChapterDeleted(Sourceにだけある) / Failed(開けなかった、または開けたが判定できなかった。理由が隣に出ます) / NotCompared(中断して判定していない)\n"
		u"・Change欄に、3つの比較のうちどれが変更を見つけたかが出ます: Pixel / Story / Resources(判定できなかった比較には「?」が付きます)\n"
		u"・行をダブルクリックすると、その章の新旧2つの文書を開きます。\n"
		u"・行を右クリックして「Start Change Marker」を選ぶと、その章の2文書を開いて比較を開始します(両方のファイルが揃っている行だけ選べます)。\n"
		u"\n"
		u"【変更されたストーリーの一覧(Story Edits)】\n"
		u"パネル下部の三角ボタンで開くと、Source版とTarget版で変更のあったストーリーを一覧します(Story比較モードで比較中のみ。Pixel比較モードは画素だけを比べ、ストーリーは比べません)。\n"
		u"・行の左に本文の先頭、狭い欄に記号を1つ表示(+=Targetにだけある / -=Sourceにだけある / ==比べて同じ / ≠=違う)\n"
		u"・行をクリックすると、そのストーリーをTarget側とSource側の両方で表示します\n"
		u"・過去の文書からコピーして同じ場所に貼った枠は、ストーリーの ID が新しくなっても、そこにあった枠のストーリーと比べます(対応するページの上で、位置と大きさがまったく同じとき)。その行をクリックすると、ステータス欄に組んだ Source のストーリーの番号が出ます\n"
		u"・以後、↑↓キーで行を移動できます(移動した先のストーリーを表示します)\n"
		u"・変更の行をクリックすると、その文字を Target で選択します(文字ツールに切り替わります)。キーボードは一覧に残るので、文書に打ち込むときは文書をクリックしてください\n"
		u"・文書に書き込むのは変更の行とストーリーの行の右クリックの項目で、どれも Ctrl+Z 1回で戻ります(一緒に選んだ行は1回)。取り込んだ文章が Source のときは、上の「ストーリーの往復」の項目です。2つの文書の比較では、「Restore from Source」(Source の語を、ルビ・圏点・縦中横・割注ならその印を、Target の上に戻します。戻すと物が作り直されるところは上と同じく断ります)、ストーリーの行の「Restore All Changes in This Story」(そのストーリーの語の変更を同じ形で1件ずつ)、戻した「=」の行の「Restore from Copy」(Target の元の語に戻します)、「Match the Source」(Table の行で: 行・列・結合・ヘッダー／フッター行を Source の表と同じにし、構成が変わったセルの中身と設定(行の高さ・列の幅も)を Source から写します。表はそのままの表で、両側にあるセルには触りません(その文字の変更は Cell Text の行として別に出ます)。写した後に、文字・表そのものから読んだ高さと幅・INX に書き出したそれ以外のすべて、の3通りで読み直し、約束どおりでなければ何も変えずに理由を出します)。Source は Task Start (Save)... の写しでも比較相手の文書でも Start が窓を開くので、常に目の前にあります(メモリに取った Task Start と取り込んだ文章には窓がありません)\n"
		u"\n"
		u"【猫の手スタンプ(Kohaku Cat Paw Tool)】\n"
		u"ページの好きな場所に肉球の目印を置けます。ツールボックスの Kohaku Change Marker ツール(またはパネルのツールボタン)を押しっぱなしにするとフライアウトが開くので、スタンプツールを選びます。\n"
		u"・クリックで置く／Shift+クリックで剥がす\n"
		u"・Alt+クリック(Alt+ドラッグ)は、離したあとに言葉を聞いてから置きます(1行でも複数行でも。言葉は肉球の横に描かれます)\n"
		u"・Shift+Alt+クリックは色を赤と青で入れ替えるだけで、肉球は置きません(次に置く色が変わります)\n"
		u"・印刷とPDFには「Print comparison marks」がONのときだけ出ます\n"
		u"・肉球は文書のページのScriptラベルに書き込まれます。";

	// (About has no Japanese: it is one English line, "<name> version x.y.z", with nothing to
	//  translate, and DoAbout passes the string key straight to CAlert.)

	// (The Compare Books confirmation passed through here and left again -- the user asked for it
	//  in English -- so its only wording is kKCMBookCompareConfirmKey in the enUS table.)

	// (The "no two books" warning left this file on 2026-09-08, at the author's instruction
	//  through the spec map (BK-11): both alerts of the book comparison are English now, so its
	//  only wording is kKCMBookNoPairKey in the enUS table -- the same shape as the confirmation
	//  above. Nothing the book comparison shows passes through this namespace any more.)
}

#endif // __KCMUILoc_h__

// End, KCMLoc.h.

