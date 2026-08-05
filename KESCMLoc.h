//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuExtendScriptChangeMarker (KESCM)
//
//  実行時の日本語切替。jaJP 文字列テーブルは 2026-08-05 に撤去(ユーザー方針・KBS と同時):
//  全ロケールが enUS テーブルを読み、日本語を話す3箇所(How to Use / About / Hide Unchanged の
//  確認)だけをここで UI 言語判定して差し替える。判定は featureset ではなく UI 言語
//  (PMLocaleId は2軸を別々に持つ)なので、Roman エンジン+日本語 UI の環境でも日本語が出る。
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

// 旧 KESCM_jaJP.fr が持っていた日本語(3つだけ)。対のキーは KESCMID.h と enUS テーブルに健在。
namespace KESCMJa
{
	// ----- How to Use... (操作リファレンス。旧パネル説明文) -----
	const char16_t kHint[] =
		u"Kohaku Change Marker ツール(ツールボックス)を選び、レイアウト上で:\n"
		u"左ボタン長押し=\n　比較枠を表示(押している間/透明度はパネルの25%・75%)\n"
		u"左ボタン+Shift長押し=\n　元(旧版)を表示(押している間)\n"
		u"左ボタン+Shift+" kKESCMAltKeyName u"長押し=\n　元を50%で表示(押している間)\n"
		u"左ボタン+" kKESCMAltKeyName u"長押し=\n　クリック点の色情報(CMYK)をカーソルとパネルに表示\n\n"
		u"Shift・" kKESCMAltKeyName u"は押したままでも離してもOK\n\n"
		u"【ページの追加・削除への対応(Add / Remove)】\n"
		u"ページを追加・削除すると、それ以降のページ番号がズレて本来同じページ同士が比較されなくなり、"
		u"デザインが同じでも「すべて変更あり」と誤検知されることがあります。これを防ぐため、"
		u"追加・削除したページを比較対象から外して登録できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KESCM: Register as Added/Removed Pages」を選びます\n"
		u"・Target(新しい方)で登録=追加ページ / Source(古い方)で登録=削除ページ\n"
		u"・もう一度同じ操作で登録を解除\n"
		u"・登録したページは緑の斜線、ページ数の差で比較相手がいないページは赤の斜線で表示\n"
		u"・Start済みの状態で登録・解除すると、その場で自動的に比較し直します\n\n"
		u"【ページのチェック(KESCM: Check)】\n"
		u"確認が済んだページにチェック印を付けられます。Start中のみ使用でき、Stopで消えます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KESCM: Check」を選びます(もう一度で解除)\n"
		u"・チェック印を付けられるのは、比較枠や斜線などのマークが付いているページだけです(マークの無いページには付きません)\n"
		u"・チェック印と登録(Add / Remove)は保存・読込できます。パネルのフライアウトメニューから:\n"
		u"　「Save Check & Register」= 現在のチェック印と登録を専用ファイルに保存(保存先のパスを表示)\n"
		u"　「Load Check & Register」= 保存内容を読み込み、登録を適用して比較し直し、チェック印を復元(Start中のみ)\n"
		u"・保存はこのプラグイン専用のファイル(環境設定フォルダー)に書くだけで、InDesignの文書やワークスペースには一切書き込みません\n"
		u"・文書を別の場所へ移動したり別名保存したりして保存場所(パス)が変わると、保存したチェック印は復元できません\n\n"
		u"【ページ比較の更新(KESCM: Refresh Page Comparison)】\n"
		u"編集したページだけを比較し直せます。Start中のみ使用できます。\n"
		u"・使い方: ページパネルでページを選択して右クリックし、「KESCM: Refresh Page Comparison」を選びます\n"
		u"・選んだページの比較枠とサムネイルを最新の状態に更新します(TargetでもSourceでも可)\n\n"
		u"【オーバーセットの検出(Find Overset)】\n"
		u"比較とは別に、文字があふれた(オーバーセットの)ページを単独で探せます。\n"
		u"・フライアウトの「Find Overset」でON/OFF\n"
		u"・あふれのあるページをサムネイルに赤枠＋赤い「＋」、スクロールバー地図に赤帯で表示\n"
		u"・テキストフレームのあふれに加え、表のセル単独のあふれ(赤丸)も検出\n"
		u"・Prev/Nextであふれ箇所を巡回(比較中は各ページ 変更→あふれの順)\n"
		u"・編集後は「Refresh Overset」で走査し直し(Find OversetがONのときのみ)\n\n"
		u"比較枠の印刷をONにしても、ファイル＞書き出しのPDFには出ません。"
		u"PDFにしたい場合は、プリントでプリンターの選択でPDFを選んでください。\n\n"
		u"【注意】どのような問題が起こっても責任を取れません。ご利用は自己責任でお願いします。";

	// ----- About ボックス -----
	const char16_t kAboutBox[] =
		u"" kKESCMDisplayName u"、version " kKESCMVersion u"\n\n"
		u"Adobe InDesign C++ SDK プラグイン。\n"
		u"二つのドキュメントをページ単位でオフスクリーンにレンダリングしてピクセル比較し、"
		u"変化した箇所を画面上に赤い枠で重ねて表示します。表示は非永続なので、"
		u"ドキュメントには残りません。\n\n"
		u"本プラグインは KohakuNekotarou が、Anthropic の AI Claude（Claude Code）と協働して"
		u"設計・実装しました。\n\n"
		u"ソース: " kKESCMRepoURL;

	// ----- Hide Unchanged Spreads の確認(文書に変更を加えるため) -----
	const char16_t kHideConfirm[] = u"この機能はファイルに変更を加えます。構いませんか?";
}

#endif // __KESCMLoc_h__

// End, KESCMLoc.h.
