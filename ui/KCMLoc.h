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
	// (⛔kImported / kImportTracked - the status line's "Importしました" and the dialog after an import - went on
	//  2026-09-25: the user asked for the dialog to stop and the result to be shown on the panel only.)

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
		u"・Start Comparison 済みの状態で登録・解除・トグル切替をすると、その場で自動的に比較し直します\n\n"
		u"【ページのチェック(Check)】\n"
		u"確認が済んだページにチェック印を付けられます。消すまで残ります(Stop Comparison でも消えず、文書を閉じたときに忘れます)。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「Check」を選びます(もう一度で解除)\n"
		u"・比較していなくても、開いているどの文書のどのページにも付けられます。比較中の2文書だけは従来どおりで、Pixel比較モードではマークの付いているページだけ、Story比較モードではどのページにも付けられます\n"
		u"・チェック印・登録(Add / Remove)・猫の手は保存・読込できます。対象は「アクティブな文書1つ」です。パネルのフライアウトメニューから:\n"
		// ★A "&" inside a menu name is doubled to "&&" here: this text goes to CAlert::ModalAlert,
		//   and CAlert eats a lone "&" as a mnemonic ([[ampersand-eaten-in-ui-strings]]; KBS does
		//   the same).
		u"　「Clear Checks in This Document」「Clear Cat Paws in This Document」= アクティブドキュメントの印を消します\n"
		u"・チェック印と肉球は、置いた時点で文書のページに自動で書き込まれます(Undo/Redoで戻せます)。文書を保存すれば、次に開いたときも残っています\n"
		u"・登録(Added/Removed)は比較のための指定なので、文書には書き込まれず、Stop Comparison で消えます\n\n"
		u"【ページ比較の更新(Refresh Page Comparison)】\n"
		u"編集したページだけを比較し直せます。Start Comparison 中で、かつPixel比較モードのときだけ使用できます"
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
		u"いまの文書の状態を取り、Source にします。編集してから Start Comparison を押すと、その時点からの変更が3つのモードのどれでも見られます。取り方は2つです。\n"
		u"・「Task Start」: 状態をメモリに取ります。ダイアログもファイルもありません。Source の行には「Task Start (in memory) - 文書名 - 時刻」と出ます。"
		u"Source を別のものにしたとき(Set as Source・Task Start (Save)...・Import Story Text...・Compare with Tracked Changes...・Clear Target and Source・次の Task Start)と、文書を閉じた・復帰したときに捨てます。保存しても捨てません。"
		u"Before/After PDF 報告書はこれを印刷できないので作れません(報告書には Task Start (Save)... を使ってください)\n"
		u"・「Task Start (Save)...」: 文書の写しを、選んだ場所にファイルとして保存し、そのファイルを Source にします。"
		u"文書のあるフォルダーに、文書名と日時から作った名前で保存ダイアログが開くので、保存を押します。キャンセルすると何も起きません。"
		u"Source の行には写しのパスが出ます。写しは開きません。開くのは Start で、止めた後も開いたままです。"
		u"写しはふつうの InDesign 文書です。消すまでそこに残り、開く・印刷する・比較する、どれも他の文書と同じようにできます\n"
		u"・どちらも: 取る文書はターゲットを指定していればその文書、していなければ前面の文書で、その同じ文書がターゲットになります。比較中でも押せます(その比較は先に止まります)\n\n"
		u"【Before/After PDF 報告書(Export Before/After PDF Report)】\n"
		u"2つの文書を報告書が自分で比べ、1つの PDF に3部構成で書き出します。1ページ目＝2つの文書の名前と、左 Before・右 After の見出し。"
		u"次に画素比較で変更のあったページごとに横長1ページ＝左に旧版(比較マーク入り)、右に新版(素のまま)。このページに文字は入れません。追加ページは左が空、削除ページは右が空です。"
		u"次に Story の表＝左に旧版の文、右に新版の文を変更1件ごとに1行。変わっていない部分は薄く、変わった部分は濃く出ます。ルビと圏点は本物のルビ・圏点で、脚注・文末脚注の番号は上付きで出ます。"
		u"最後に Resources の表＝定義ごとに、変わった属性を1行ずつ、左に旧値・右に新値。どちらの表もパネルと同じ列(ID または Kind と、記号 Δ)を持ち、長さの値には文書の単位を括弧で添えます(例: 8.5 (12 Q)。パネルでも同じ)。\n"
		u"・使い方: 比較していないときに、パネルのフライアウトメニューから「Export Before/After PDF Report」を選びます(比較中は灰色なので、先に Stop します)。Start と同じ2つ(設定した Target と Source、無ければ手前の文書と開いている別の文書)を比べ、作ったあとも比較は始まりません\n"
		u"・保存ダイアログで場所と名前を選びます(初期名は「<文書名>.compare-report.pdf」)。書き出したら PDF ビューアで開きます\n"
		u"・3部ともどの比較モードでも出ます(画素・ストーリー・リソースを報告書が自分で比べます)\n"
		u"・旧版側のマークは変わった箇所のリングだけで、ページの縁の枠は付けません\n"
		u"・現在の PDF 書き出し設定を使います(見開き・セキュリティは切ります)。Task Start (Save)... が Source のときは左側が保存した写しの状態です(メモリに取った Task Start が Source のときは作れません)\n\n"
		u"【ストーリー ID(Show Story IDs)】\n"
		u"テキストを持つフレーム(テキストフレーム・パス上テキスト・アンカー付きフレーム)の上に、載っているストーリーの ID を「StoryID:番号」の形で表示します。"
		u"番号はスクリプトの story.id で、Story Edits 一覧の ID 列と同じです。フレームの外(上辺の左)に出るので、本文に重なりません。\n"
		u"・使い方: パネルのフライアウトメニューの「Show Story IDs」を ON にします(既定は OFF。パネル設定に保存されます)\n"
		u"・画面では常に、ズームによらず同じ大きさで表示します。印刷・PDF には「Print comparison marks」が ON のときだけ出ます(Before/After PDF 報告書には After 側だけに出ます)\n"
		u"・パスにもテキストが載っているフレームはラベルが2つ並びます\n\n"
		// ★The story text round trip (2026-09-19) sits here, after Story IDs and before the books - the
		//   same seam the English side puts kKCMHint4Key at (DoUsage reads hint / hint3 / hint4 / hint2).
		u"【ストーリーの往復(Export Story Text / Import Story Text)】\n"
		u"ストーリーを Word 文書に書き出し、InDesign の外で編集してもらい、その編集を文書へ1件ずつ入れます。\n"
		u"・「Export Story Text...」は 1ストーリー＝1つの .docx を書き出します。フレームを選択していればそのストーリーだけ、何も選択していなければ全ストーリーを書き出します\n"
		u"・「Import Story Text...」は文書を変えません。選んだファイルを、メモリに取った文書の写し(メモリに取る Task Start と同じ・ダイアログもファイルもありません)に読み込み、"
		u"そこで各ストーリーを Word で見えているとおりにして(Word の変更履歴は承諾した形で。履歴が無くても同じです。Word のコメントは取り込みません)、Story 比較を始めます。"
		u"Source がファイルのとおりにした文書、Target が今のままのあなたの文書です。一覧の見出しは「Proposed Edits」になり、変更の行は「文書がこう変わる」という提案です。"
		u"先に選んでいた Task Start は(どちらの取り方でも) Import のものに置き換わります\n"
		u"・入れ方: 変更の行を右クリックして「Apply This Change」(行を選んで Return でも同じ。Shift+Return は入れてから次の行へ進みます)、"
		u"ストーリーの行の右クリックで「Apply All Changes in This Story」、見出しの右クリックで「Apply All Changes」。一緒に選んだ行は1回で入ります。"
		u"どれも Ctrl+Z 1回で戻り、一覧も戻ります。変更履歴も作者名も付けません(そのストーリーで自分で変更をトラックしていれば、InDesign がふつうの編集としてあなたの名前で記録します)\n"
		u"・入れた変更は赤い「=」の行として残ります。右クリックの「Restore from Copy」で、入れる前にそこにあった語に戻せます(KCM が写しを取っています)。"
		u"戻すと物が作り直されるところ(表・脚注の参照・アンカー付きオブジェクト・ハイパーリンクの元)、表や印(ルビなど)の行、段落ごと一緒に入った変更、Apply All で入れた変更には出ません。"
		u"「=」の前を後から直したときは、その行を比べ直す(右クリックの「Refresh Story Comparison」)と「=」が新しい位置に立ちます\n"
		u"・ファイルが足す・消す表と、ファイルが足す脚注は、ストーリーごと(「Apply All Changes in This Story」)でだけ入ります\n"
		u"・写しに入らなかったもの(作れない形に変えられた表(行の長さが揃わない・入れ子の表が邪魔をする・InDesign では結合できないセル)、アンカー付きオブジェクト・変数・索引マーカーを足す／消す変更、ファイルに無いセルや脚注、対応するストーリーの無いファイル)は"
		u"一覧の「先頭」に、Change 列に赤い「!」と、場所と理由を添えて並びます。書き出した後に InDesign で直した所は、それを上書きする提案として出ます\n"
		u"・あふれはすぐ見えます。入れた編集がフレームに収まらなくなれば、他の Story 比較と同じく行に OV が出ます\n"
		u"・段落の中の表: Word には無いので、ファイルでは「表の前の文字」「表」「表の後の文字」が別々のブロックになります。表の前後で段落がどこで切れるかは"
		u"ファイルではなく今の文書が決めます。Word で表の隣に改行を足しても消しても何も変わりません。表の隣で段落を割る・つなぐのは InDesign で行ってください\n\n"
		// ★The Track Changes mode (2026-10-05) - the English side's kKCMHint5Key, at the same seam.
		u"【変更履歴との比較(Compare with Tracked Changes...)】\n"
		u"InDesign の変更履歴(変更をトラック)が文書に記録した変更を、作者を問わず1件1行で、変更の前の文書と並べて一覧します。\n"
		u"・使い方: 文書を前面にして(または Set as Target で選んで)、パネルのフライアウトメニューから「Compare with Tracked Changes...」を選びます。先に何かを設定する必要はありません(Target・Source・モードはこの項目が決めます)。保存ダイアログで文書の写しの置き場所を決めると、その写しの中で変更履歴を全部却下し(＝変更の前の版。InDesign がそこで却下しない記録はステータス欄の not rejected in the copy=N で数えます)、写しを窓で開いて Source にし(パネルの Source の欄の末尾に (Track original) と出ます)、Track Changes モードで比較を始めます。キャンセルすると何も起きません\n"
		u"・一覧の右クリックで承認・却下できます: 変更の行＝Accept Change／Reject Change(一緒に選んだ変更の行すべて)、ストーリーの行＝その作成者のそのストーリーの変更、作成者の行＝その作成者の変更すべて、見出し(Tracked Changes)＝一覧の変更すべて。どれも Ctrl+Z 1回で戻り、一覧は読み直します。ほかに文書へ書き込むことはなく、InDesign の「変更をトラック」パネルもいつもどおり使えます\n"
		u"・一覧は 作者 → ストーリー → 変更 の3段です。作者ごとに、ストーリーエディターで InDesign が付ける色が付き、文書の中の印もその色になります(濃さは Marks opacity。InDesign が白で描く作者は Mark colour)。変更の行は、消えた文字を取り消し線で、入った文字をその後ろに出します: + 挿入、- 削除、≠ 置換、» 移動\n"
		u"・変更の行をクリックすると両方の窓がその場所へ動き、Story モードと同じくその文字を文書で選択します(文字ツールに切り替わります)。ステータス欄に 誰が・いつ・何をしたか が出ます。削除した文字はストーリーエディターにしか出ないので、削除と、非表示の条件の中の変更は、その場所にキャレットを置きます。一覧は取り消し・やり直しにも、InDesign の変更履歴パネルでの承認・却下にも追随します。それでも消えている変更は、飛ばずに「もう記録にない」と出ます(Refresh で読み直します)\n"
		u"・非表示の条件の中の変更は Hidden と出ます。ノートの中は読みません。脚注の中は InDesign が記録しません\n"
		u"・このモードではブックの比較は使えません(Before/After PDF 報告書は、どのモードとも同じく比較していないときに作れます)\n"
		u"\n"
		// ★Comparing PDFs (2026-10-14) - the English side's kKCMHint6Key, at the same seam.
		u"【PDF の比較(Set PDF as Target... / Set PDF as Source...)】\n"
		u"書き出した PDF 2つ、または文書とそこから書き出した PDF を、Pixel の比較で比べます。\n"
		u"・PDF 同士: パネルのフライアウトメニューの「Set PDF as Target...」で新しい方の PDF を、「Set PDF as Source...」で古い方の PDF を選び、Start Comparison を押します。"
		u"それぞれの PDF を、元の見開きの形に組んだ文書「ファイル名.pdf (KCM)」が窓で開き、印は Target の PDF の窓に出ます\n"
		u"・書き出しの検査: 文書を開き、その文書から書き出した PDF を「Set PDF as Source...」で選んで Start Comparison を押します。"
		u"PDF が文書とはっきり違って見える所(透明や画像が消えた・物が欠けた)に、文書の上で印が出ます。"
		u"描き方の小さな違い(文字の縁・色の丸め)には印を出さないので、細い線・小さな文字の1行・1文字の違い・わずかな色のずれも出ないことがあります\n"
		u"・ページは元の InDesign のページで組みます(ページを足した・消した・並べ替えた後でも同じページ同士)。"
		u"一部のページだけの PDF はそのページを比べ、残りをステータス欄で名指します。別の文書から書き出した PDF は断ります。ページの ID の無い PDF は順番で組みます。どちらで組んだかはステータス欄の2行目の頭に出ます\n"
		u"・同じ設定(同じプリセット)で書き出した PDF 同士なら、変えていない所に印は出ません\n"
		u"・当面は Pixel だけです(Story・Resources・Track Changes のモードでは Start を断ります)。文書に対して PDF を Target にしたとき・暗号化した PDF・壊れた PDF も断ります\n"
		u"・作った文書は保存しなくて構いません(閉じても保存を聞きません。チェックや猫の手を付けた後・Hide Unchanged Spreads を使った後は聞かれます)。"
		u"窓を閉じても PDF の選択は残り、次の Start でまた作ります。PDF を書き直すと、次の Start で作り直します\n"
		u"・Ignore Page Number Marker は PDF の中には効きません\n"
		u"\n"
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
		u"パネル下部の三角ボタンで開くと、Source版とTarget版で変更のあったストーリーを一覧します(Story比較モードで比較中のみ。Pixel比較モードは画素だけを比べ、ストーリーは比べません)。\n"
		u"・行の左に本文の先頭、狭い欄に記号を1つ表示"
		u"(+=Targetにだけある / -=Sourceにだけある / ==比べて同じ / ≠=違う)\n"
		u"・行をクリックすると、そのストーリーをTarget側とSource側の両方で表示します\n"
		u"・過去の文書からコピーして同じ場所に貼った枠は、ストーリーの ID が新しくなっても、そこにあった枠のストーリーと比べます(対応するページの上で、位置と大きさがまったく同じとき)。"
		u"その行をクリックすると、ステータス欄に組んだ Source のストーリーの番号が出ます\n"
		u"・以後、↑↓キーで行を移動できます(移動した先のストーリーを表示します)\n"
		u"・変更の行をクリックすると、その文字を Target で選択します(文字ツールに切り替わります)。キーボードは一覧に残るので、文書に打ち込むときは文書をクリックしてください\n"
		u"・文書に書き込むのは変更の行とストーリーの行の右クリックの項目で、どれも Ctrl+Z 1回で戻ります(一緒に選んだ行は1回)。取り込んだ文章が Source のときは、上の「ストーリーの往復」の項目です。"
		u"2つの文書の比較では、「Restore from Source」(Source の語を、ルビ・圏点・縦中横・割注ならその印を、Target の上に戻します。戻すと物が作り直されるところは上と同じく断ります)、"
		u"ストーリーの行の「Restore All Changes in This Story」(そのストーリーの語の変更を同じ形で1件ずつ)、戻した「=」の行の「Restore from Copy」(Target の元の語に戻します)、"
		u"「Match the Source」(Table の行で: 行・列・結合・ヘッダー／フッター行を Source の表と同じにし、構成が変わったセルの中身と設定(行の高さ・列の幅も)を Source から写します。表はそのままの表で、両側にあるセルには触りません(その文字の変更は Cell Text の行として別に出ます)。写した後に、文字・表そのものから読んだ高さと幅・INX に書き出したそれ以外のすべて、の3通りで読み直し、約束どおりでなければ何も変えずに理由を出します)。"
		u"Source は Task Start (Save)... の写しでも比較相手の文書でも Start が窓を開くので、常に目の前にあります(メモリに取った Task Start と取り込んだ文章には窓がありません)\n\n"
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
		u"・比較していなくても使え、Stop Comparison でも消えません(チェック印と同じ)\n"
		u"・画面には常に出ます。印刷とPDFには「Print comparison marks」がONのときだけ出ます。濃さは「Marks opacity 25% / 75%」に連動します\n"
		u"・肉球は文書のページに書き込まれます(置いた時点で自動。Undo/Redoで戻せ、文書を保存すれば次に開いたときも残ります)\n"
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

