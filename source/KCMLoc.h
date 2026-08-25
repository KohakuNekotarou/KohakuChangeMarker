//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuExtendScriptChangeMarker (KCM)
//
//  実行時の日本語切替。jaJP 文字列テーブルは 2026-08-05 に撤去(ユーザー方針・KBS と同時):
//  全ロケールが enUS テーブルを読み、日本語を話す箇所だけをここで UI 言語判定して差し替える。
//  判定は featureset ではなく UI 言語(PMLocaleId は2軸を別々に持つ)なので、Roman エンジン+日本語 UI の
//  環境でも日本語が出る。
//
//  ★★製品全体で日本語を話すのは **3箇所**(2026-08-06 に2箇所へ絞り、08-12 に4箇所、08-13 に3箇所)。
//    ⚠★★**そのうち"この側"が持つのは1つだけ** ＝ 下の kHideConfirm(Hide Unchanged の確認)。
//      残る2つ(How to Use... ／ 2ブックが揃わないとき)は **ui/KCMLoc.h** にある
//      ＝2026-08-15(第2段 Task 6B-2)に、文字列を**使う側**へ分けたため
//      (Hide Unchanged は文書を変えるので model 側に残った)。
//    ・Hide Unchanged の確認 … 文書を変更する前の確認アラート。意味を取り違えると実害が出る
//  ⚠★★**ファイルは分けたのに、この説明文は分けなかった**。2026-08-16 の監査 B-U1 まで、両側が
//    「自分は3箇所を持つ」と書き、それぞれ**自分が持っていない文字列**まで説明していた。
//    ⇒ **相方を読むこと。ここで数え直さないこと**(同じ判断を2か所に置かない)。
//  ★**線引きの規則**は「ユーザーが明示的に日本語だと言ったもの」であって、内容の性質ではない
//    (Compare Books の**確認**は 2026-08-12 に日本語になり、08-13 に英語へ戻った＝「英語で良いです」)。
//  ⚠**About は英語のみ**(2026-08-06 に「名前＋版数」の1行だけになったので、訳し分ける中身が無い)。
//
//  ***** このファイルは UTF-8 (BOM 付き) ***** — u"..." リテラルを日本語のまま読めるようにするため。
//  (BOM 無しだと MSVC が CP932 として誤読する。)
//
//========================================================================================

#ifndef __KCMLoc_h__
#define __KCMLoc_h__

#include <string>

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

#include "KCMID.h"

namespace KCMLoc
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

// 旧 KCM_jaJP.fr が持っていた日本語のうち、**この側に残った1つ**。対のキー
// (kKCMHideConfirmKey)は KCMID.h と KCM_enUS.fr に健在＝英語 UI はそちらを引く。
namespace KCMJa
{
	// ----- Hide Unchanged Spreads の確認(文書に変更を加えるため) -----
	const char16_t kHideConfirm[] = u"この機能はファイルに変更を加えます。構いませんか?";

}

#endif // __KCMLoc_h__

// End, KCMLoc.h.

