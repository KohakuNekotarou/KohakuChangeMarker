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

#include <string>

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
		PMString s;
		if (JapaneseUI())
		{
			s.SetXString(reinterpret_cast<const UTF16TextChar*>(japanese),
				static_cast<int32>(std::char_traits<char16_t>::length(japanese)));
		}
		else
		{
			PMString k(englishKey);
			k.Translate();
			s = k;
		}
		s.SetTranslatable(kFalse);
		return s;
	}
}

// The Japanese that used to live in KCM_jaJP.fr, the part of it this half holds. The matching
// keys (kKCMHintKey / kKCMHint2Key) are alive in KCMUIID.h and
// KCMUI_enUS.fr -- an English UI reads those.
namespace KCMJa
{
	// ----- How to Use... (the operating reference; it used to be the panel's description) -----
	const char16_t kHint[] =
		u"Kohaku Change Marker ツール(ツールボックス、またはパネルのツールボタン)を選び、レイアウト上で:\n"
		u"左ボタン長押し=\n　比較枠を表示(押している間/透明度はパネルの25%・75%)\n"
		u"左ボタン+Shift長押し=\n　元(旧版)を表示(押している間)\n"
		u"左ボタン+Shift+" kKCMAltKeyName u"長押し=\n　元を50%で表示(押している間)\n"
		u"左ボタン+" kKCMAltKeyName u"長押し=\n　クリック点の色情報(CMYK)をカーソルとパネルに表示\n\n"
		u"Shift・" kKCMAltKeyName u"は押したままでも離してもOK\n\n"
		u"【ページの対応づけと追加・削除(Pair Pages by UID / Add / Remove)】\n"
		u"ページは既定で「同じ UID(内部ID)を持つページ同士」を比較します(フライアウトの「Pair Pages by UID」ON)。"
		u"旧版を別名保存して作った新版なら、ページを追加・削除・移動しても正しい相手と比較され、"
		u"新版だけにあるページ(追加)と旧版だけにあるページ(削除)は自動で見つかり赤の斜線で表示されます。\n"
		u"・別々に作った2文書や IDML を経た文書は UID を共有しないため、何も組にならず全ページが赤の斜線になります。"
		u"その場合は「Pair Pages by UID」を OFF にすると、従来の「並び順で先頭から組にする」規則に戻ります\n"
		u"・並び順規則では、ページを追加・削除するとそれ以降がズレて誤検知されるので、追加・削除したページを比較対象から外して登録できます\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Register as Added/Removed Pages」を選びます\n"
		u"・Target(新しい方)で登録=追加ページ / Source(古い方)で登録=削除ページ\n"
		u"・もう一度同じ操作で登録を解除\n"
		u"・登録したページは緑の斜線、比較相手がいないページは赤の斜線で表示\n"
		u"・Start済みの状態で登録・解除・トグル切替をすると、その場で自動的に比較し直します\n\n"
		u"【ページのチェック(Check)】\n"
		u"確認が済んだページにチェック印を付けられます。消すまで残ります(Stopでも消えず、文書を閉じたときに忘れます)。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Check」を選びます(もう一度で解除)\n"
		u"・比較していなくても、開いているどの文書のどのページにも付けられます。比較中の2文書だけは従来どおりで、Pixel比較モードではマークの付いているページだけ、Story比較モードではどのページにも付けられます\n"
		u"・チェック印・登録(Add / Remove)・猫の手は保存・読込できます。対象は「アクティブな文書1つ」です。パネルのフライアウトメニューから:\n"
		// ★A "&" inside a menu name is doubled to "&&" here: this text goes to CAlert::ModalAlert,
		//   and CAlert eats a lone "&" as a mnemonic ([[ampersand-eaten-in-ui-strings]]; KBS does
		//   the same).
		u"　「Clear Checks in This Document」「Clear Cat Paws in This Document」= アクティブドキュメントの印を消します\n"
		u"・チェック印と肉球は、置いた時点で文書のページに自動で書き込まれます(Undo/Redoで戻せます)。文書を保存すれば、次に開いたときも残っています\n"
		u"・登録(Added/Removed)は比較のための指定なので、文書には書き込まれず、Stopで消えます\n\n"
		u"【ページ比較の更新(Refresh Page Comparison)】\n"
		u"編集したページだけを比較し直せます。Start中で、かつPixel比較モードのときだけ使用できます"
		u"(Story比較モードはページをラスタ化しないため、代わりにStory Editsの行を右クリックして"
		u"「Refresh Story Comparison」を使います)。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Refresh Page Comparison」を選びます\n"
		u"・右クリックするのはTarget(新しい方)です。選んだページと、対応するSource側のページの両方について、"
		u"比較枠とサムネイルを最新の状態に更新します\n\n";

	// ★**The SECOND part of the How to Use text.** KCMActionComponent::DoUsage concatenates it
	//   after kHint above into one alert, so **the reader sees one piece of writing**. It holds
	//   everything from "comparing books" on (books -> overset -> the story list -> print/PDF ->
	//   the disclaimer).
	//   ⚠**The reason for the split is on the English side**: odfrc caps the length of a single
	//     string in a StringTable, and the enUS text had about 100 bytes of headroom left
	//     ([[odfrc-long-string-limit]]). **This side has no such limit** -- it is a u"" literal --
	//     **but both languages are split at the same point**: a key that exists on one side only,
	//     or a seam in a different place, is what the next person has to work out.
	//   ★★**The seam was chosen as the place to put the book section** (user's instruction:
	//     before "finding overset").
	//     ⚠**The English side (kKCMHintKey / kKCMHint2Key in KCMUI_enUS.fr) is split at the same
	//       point. Do not move one without the other.**
	const char16_t kHint2[] =
		// ★Task Start (2026-09-12) comes first in this piece: on the English side it is a key of its
		//   own (kKCMHint3Key), read between hint and hint2, so the two languages read in the same
		//   order - refresh -> Task Start -> books.
		u"【少し前の状態と比べる(Task Start)】\n"
		u"アクティブな文書の状態をメモリに記録し(ディスクには何も書きません)、それを Source にします。"
		u"編集してから Start(または Refresh Comparison)を押すと、その時点からの変更が3つのモードのどれでも見られます。\n"
		u"・使い方: パネルのフライアウトメニューから「Task Start」を選びます。Source の行に「Task Start」と記録した時刻が出ます\n"
		u"・記録は同時に1つだけです。「Clear Target and Source」か、その文書を閉じると解放されます。比較中には記録できません\n"
		u"・記録が Source の間は Source の窓が無いので、Sync Layout Views・Always Show Marks on Source・ダブルクリックの Source 側は効きません。"
		u"Shift+押しっぱなし(旧版の表示)は使えます。スプレッドごとに最初の1回だけ、作り直しに少し時間がかかります\n\n"
		u"【ブックの比較(Compare Books)】\n"
		u"ブック(.indb)どうしを章(ドキュメント)単位で比べ、どの章が変わったかを一覧します。\n"
		u"・使い方: ブックパネルで比べたいブックのタブを前面にし、もう1冊のブックも開いてから、"
		u"パネルのフライアウトの「Compare Books」を選びます\n"
		u"・前面タブのブックが Target(新しい方)、それ以外で最初に開いているブックが Source(古い方)です\n"
		u"・章ごとに結果が出ます: Changed(変更あり) / NoChange(変更なし) / "
		u"ChapterAdded(Targetにだけある) / ChapterDeleted(Sourceにだけある) / "
		u"Failed(開けなかった、または開けたが判定できなかった。理由が隣に出ます) / "
		u"NotCompared(中断して判定していない)\n"
		u"・Change欄に、3つの比較のうちどれが変更を見つけたかが出ます: Pixel / Story / Resources"
		u"(判定できなかった比較には「?」が付きます)\n"
		u"・行をダブルクリックすると、その章の新旧2つの文書を開きます\n"
		u"・行を右クリックして「Start Change Marker」を選ぶと、その章の2文書を開いて比較を開始します"
		u"(両方のファイルが揃っている行だけ選べます)\n\n"
		u"【変更されたストーリーの一覧(Story Edits)】\n"
		u"パネル下部の三角ボタンで開くと、Source版とTarget版で変更のあったストーリーを一覧します(比較中のみ)。\n"
		u"・行の左に本文の先頭、狭い欄に記号を1つ表示"
		u"(+=Targetにだけある / -=Sourceにだけある / ==比べて同じ / ≠=違う)\n"
		u"・行をクリックすると、そのストーリーをTarget側とSource側の両方で表示します\n"
		u"・以後、↑↓キーで行を移動できます(移動した先のストーリーを表示します)\n"
		u"・ダブルクリックすると、そのストーリーの全文を選択します(文字ツールに切り替わり、キーボードは文書に戻ります)\n\n"
		u"比較枠の印刷をONにすると、プリントだけでなく、ファイル＞書き出しのPDFにも枠が出ます。\n"
		u"PDF/X-1aなど「Acrobat 4（PDF 1.3）」の入稿用プリセットでも半透明のまま出ます。\n\n"
		u"【オーバーセットの検出について】\n"
		u"あふれ(オーバーセット)の検出は、InDesign標準の「プリフライト」パネルをお使いください。\n"
		u"そちらは編集に追随して自動で更新され、該当箇所へジャンプできます。\n"
		u"※このプラグインにあった「Find Overset」「Refresh Overset」は、役割が重なるため廃止しました\n\n"
		u"【猫の手スタンプ(Kohaku Paw Stamp)】\n"
		u"ページの好きな場所に肉球の目印を置けます。ツールボックスの Kohaku Change Marker ツール(またはパネルのツールボタン)を押しっぱなしにするとフライアウトが開くので、スタンプツールを選びます。\n"
		u"・クリックで置く／Shift+クリックで剥がす／Alt+クリックはシアン、Shift+Alt+クリックは緑\n"
		u"・すでに肉球がある場所には重ねて置けません\n"
		u"・比較していなくても使え、Stopでも消えません(チェック印と同じ)\n"
		u"・画面には常に出ます。印刷とPDFには「Print comparison marks」がONのときだけ出ます。濃さは「Marks opacity 25% / 75%」に連動します\n"
		u"・肉球は文書のページに書き込まれます(置いた時点で自動。Undo/Redoで戻せ、文書を保存すれば次に開いたときも残ります)\n"
		u"・フライアウトの「Clear Marks from Document」で、その文書のチェック印と肉球をまとめて消せます\n"
		u"・フライアウトの「Clear Cat Paws in This Document」でアクティブな文書の肉球だけを消せます(チェック印は別の項目)\n\n"
		u"【注意】どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。";

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

