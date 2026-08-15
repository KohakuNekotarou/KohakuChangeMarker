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
//  ★★2026-08-06 に対象を **2箇所** へ絞り(ユーザー指示)、2026-08-12 に **4箇所**、
//    2026-08-13 に **3箇所** になった:
//    ・How to Use...          … 初めて使う人への操作説明。KBS も「使い方の案内だけは日本語」という
//                               同じ線引きにしている(パネル/メニュー/ステータス行は英語のまま)
//    ・Hide Unchanged の確認   … 文書を変更する前の確認アラート。意味を取り違えると実害が出る
//    ・2ブックが揃わないとき    … Compare Books が出す警告
//    (Compare Books の**確認**は 2026-08-13 に英語へ戻した＝ユーザー指示「英語で良いです」)
//  ⚠★**線引きが1つ崩れている**: 2026-08-12 の版は「アラートで判断を求める文だけ日本語」で説明が
//    ついたが、上の確認だけが英語になった今、**同じ機能の2つのアラートが別の言語で答える**。
//    残った3つを貫く規則は「ユーザーが明示的に日本語だと言ったもの」であって、内容の性質ではない。
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

#include "KESCMID.h"

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

// 旧 KESCM_jaJP.fr が持っていた日本語(★2026-08-06 に About を外して2つ)。対のキーは KESCMID.h と
// enUS テーブルに健在。
namespace KESCMJa
{
	// ----- Hide Unchanged Spreads の確認(文書に変更を加えるため) -----
	const char16_t kHideConfirm[] = u"この機能はファイルに変更を加えます。構いませんか?";

}

#endif // __KESCMLoc_h__

// End, KESCMLoc.h.

