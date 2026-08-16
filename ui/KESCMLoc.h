//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuExtendScriptChangeMarker (KESCM)
//
//  実行時の日本語切替。jaJP 文字列テーブルは 2026-08-05 に撤去(ユーザー方針・KBS と同時):
//  全ロケールが enUS テーブルを読み、日本語を話す箇所だけをここで UI 言語判定して差し替える。
//  判定は featureset ではなく UI 言語(PMLocaleId は2軸を別々に持つ)なので、Roman エンジン+日本語 UI の
//  環境でも日本語が出る。
//
//  ★★製品全体で日本語を話すのは **3箇所**(2026-08-06 に2箇所へ絞り、08-12 に4箇所、08-13 に3箇所)。
//    ⚠★★**そのうち"この側"が持つのは2つ** ＝ 下の kHint(How to Use...)と kBookNoPair(2ブックが
//      揃わないとき)。残る1つ(Hide Unchanged の確認)は **source/KESCMLoc.h** にある
//      ＝2026-08-15(第2段 Task 6B-2)に、文字列を**使う側**へ分けたため
//      (Hide Unchanged は文書を変えるので model 側)。
//    ・How to Use...        … 初めて使う人への操作説明。KBS も「使い方の案内だけは日本語」という
//                             同じ線引きにしている(パネル/メニュー/ステータス行は英語のまま)
//    ・2ブックが揃わないとき … Compare Books が出す警告
//  ⚠★★**ファイルは分けたのに、この説明文は分けなかった**。2026-08-16 の監査 B-U1 まで、両側が
//    「自分は3箇所を持つ」と書き、それぞれ**自分が持っていない文字列**まで説明していた。
//    ⇒ **相方を読むこと。ここで数え直さないこと**(同じ判断を2か所に置かない)。
//  ★**線引きの規則**は「ユーザーが明示的に日本語だと言ったもの」であって、内容の性質ではない。
//    ⚠**同じ機能の2つのアラートが別の言語で答える**: Compare Books の**確認**は 2026-08-13 に
//      英語へ戻った(ユーザー指示「英語で良いです」)が、下の kBookNoPair は日本語のまま
//      ＝その指示が名指ししたのは確認アラートだけだったため。揃えるかは未判断。
//  ⚠**About は英語のみ**(2026-08-06 に「名前＋版数」の1行だけになったので、訳し分ける中身が無い)。
//
//  ***** このファイルは UTF-8 (BOM 付き) ***** — u"..." リテラルを日本語のまま読めるようにするため。
//  (BOM 無しだと MSVC が CP932 として誤読する。)
//
//========================================================================================

#ifndef __KESCMLoc_h__
#define __KESCMLoc_h__

#include <string>

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

#include "KCMUIID.h"

namespace KESCMLoc
{
	/** UI 言語は日本語か。 */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}

	/** 日本語 UI なら日本語テキスト、それ以外は enUS テーブルのキー翻訳。戻り値は完成済み
	    テキスト(untranslatable)。 */
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

// 旧 KESCM_jaJP.fr が持っていた日本語のうち、**この側に来た2つ**。対のキー(kKESCMHintKey /
// kKESCMBookNoPairKey)は KCMUIID.h と KCMUI_enUS.fr に健在＝英語 UI はそちらを引く。
namespace KESCMJa
{
	// ----- How to Use... (操作リファレンス。旧パネル説明文) -----
	const char16_t kHint[] =
		u"Kohaku Change Marker ツール(ツールボックス、またはパネルのツールボタン)を選び、レイアウト上で:\n"
		u"左ボタン長押し=\n　比較枠を表示(押している間/透明度はパネルの25%・75%)\n"
		u"左ボタン+Shift長押し=\n　元(旧版)を表示(押している間)\n"
		u"左ボタン+Shift+" kKESCMAltKeyName u"長押し=\n　元を50%で表示(押している間)\n"
		u"左ボタン+" kKESCMAltKeyName u"長押し=\n　クリック点の色情報(CMYK)をカーソルとパネルに表示\n\n"
		u"Shift・" kKESCMAltKeyName u"は押したままでも離してもOK\n\n"
		u"【ページの追加・削除への対応(Add / Remove)】\n"
		u"ページを追加・削除すると、それ以降のページ番号がズレて本来同じページ同士が比較されなくなり、"
		u"デザインが同じでも「すべて変更あり」と誤検知されることがあります。これを防ぐため、"
		u"追加・削除したページを比較対象から外して登録できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KCM: Register as Added/Removed Pages」を選びます\n"
		u"・Target(新しい方)で登録=追加ページ / Source(古い方)で登録=削除ページ\n"
		u"・もう一度同じ操作で登録を解除\n"
		u"・登録したページは緑の斜線、ページ数の差で比較相手がいないページは赤の斜線で表示\n"
		u"・Start済みの状態で登録・解除すると、その場で自動的に比較し直します\n\n"
		u"【ページのチェック(KCM: Check)】\n"
		u"確認が済んだページにチェック印を付けられます。Start中のみ使用でき、Stopで消えます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KCM: Check」を選びます(もう一度で解除)\n"
		u"・チェック印を付けられるのは、比較枠や斜線などのマークが付いているページだけです(マークの無いページには付きません)\n"
		u"・チェック印と登録(Add / Remove)は保存・読込できます。パネルのフライアウトメニューから:\n"
		// ★メニュー名の「&」は「&&」に二重化(2026-08-06 再点検)。この本文は CAlert::ModalAlert に渡り、
		//   CAlert は単独の「&」をニーモニック扱いで食う([[ampersand-eaten-in-ui-strings]]。KBS と同じ対処)。
		u"　「Save Check && Register」= 現在のチェック印と登録を専用ファイルに保存(保存先のパスを表示)\n"
		u"　「Load Check && Register」= 保存内容を読み込み、登録を適用して比較し直し、チェック印を復元(Start中のみ)\n"
		u"・保存はこのプラグイン専用のファイル(環境設定フォルダー)に書くだけで、InDesignの文書やワークスペースには一切書き込みません\n"
		u"・文書を別の場所へ移動したり別名保存したりして保存場所(パス)が変わると、保存したチェック印は復元できません\n\n"
		u"【ページ比較の更新(KCM: Refresh Page Comparison)】\n"
		u"編集したページだけを比較し直せます。Start中のみ使用できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KCM: Refresh Page Comparison」を選びます\n"
		u"・選んだページの比較枠とサムネイルを最新の状態に更新します(TargetでもSourceでも可)\n\n"
		u"【オーバーセットの検出(Find Overset)】\n"
		u"比較とは別に、文字があふれた(オーバーセットの)ページを単独で探せます。\n"
		u"・フライアウトの「Find Overset」でON/OFF\n"
		u"・あふれのあるページをサムネイルに赤枠＋赤い「＋」、スクロールバー地図に赤帯で表示\n"
		u"・テキストフレームのあふれに加え、表のセル単独のあふれ(赤丸)も検出\n"
		u"・Prev/Nextであふれ箇所を巡回(比較中は各ページ 変更→あふれの順)\n"
		u"・編集後は「Refresh Overset」で走査し直し(Find OversetがONのときのみ)\n\n"
		u"【変更されたストーリーの一覧(Story Edits)】\n"
		u"パネル下部の三角ボタンで開くと、Source版とTarget版で変更のあったストーリーを一覧します(比較中のみ)。\n"
		u"・行の左に本文の先頭、右に変わった種類を表示(Text=文字 / Attr=属性 / Other=その他 / Added=Source側に無い)\n"
		u"・行をクリックすると、そのストーリーをTarget側とSource側の両方で表示します\n"
		u"・以後、↑↓キーで行を移動できます(移動した先のストーリーを表示します)\n"
		u"・ダブルクリックすると、そのストーリーの全文を選択します(文字ツールに切り替わり、キーボードは文書に戻ります)\n\n"
		u"比較枠の印刷をONにすると、プリントだけでなく、ファイル＞書き出しのPDFにも枠が出ます。\n"
		u"ただし書き出しの互換性は「Acrobat 5（PDF 1.4）」以上にしてください。\n\n"
		u"【注意】どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。";

	// (About の日本語は 2026-08-06 に廃止＝英語の1行「<名前> version x.y.z」だけになったので、
	//  訳し分ける中身が無い。呼び出し側 DoAbout も文字列キーを直接 CAlert へ渡す形に戻してある。)

	// (Compare Books の確認は 2026-08-12 にここへ来て 2026-08-13 に**出ていった**＝ユーザー指示
	//  「英語で良いです」。文面は enUS テーブルの kKESCMBookCompareConfirmKey だけになった。)

	// 2ブックを解決できなかったとき(通常はメニューが灰色なので到達しない)。
	// ⚠**上の相方が英語になったのに、こちらは日本語のまま**(2026-08-13 の指示は確認アラートだけを
	//   名指ししていた)。同じ機能の2つのアラートが別の言語で答える状態＝揃えるかは未判断。
	const char16_t kBookNoPair[] =
		u"ブックパネルで比較したいブックのタブを前面にして、もう1冊のブックも開いてください。";
}

#endif // __KESCMLoc_h__

// End, KESCMLoc.h.

