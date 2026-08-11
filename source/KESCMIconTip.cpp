//========================================================================================
//
//  KESCMIconTip.cpp
//
//  kKESCMIconWidgetBoss を載せた widget にホバーした時のツールチップ。★この boss はパネルの
//  **3つの widget** で共有されているので、出す文言は WidgetID で分ける(2026-08-07):
//    ・イラスト(ON/OFF アイコン)      → 配布元URL(kKESCMRepoURL)。クリックの飛び先そのものを
//                                       表示して、押すとそこへ飛ぶと分かるようにする。
//    ・ツール切替ボタン(+42)          → ツールボックスと同じツール名(kKESCMToolStringKey)。
//                                       ★同じ文字列キーを KESCMTool::Init の SetName も使っている
//                                       ので、ツールボックスのツールチップと必ず一致する。
//
//  ***** なぜ AbstractTip を継承するのか *****
//
//  AbstractTip(source/public/libs/widgetbin/includes/AbstractTip.h)は、製品コードのツールチップが
//  例外なく継承している基底(linksui/LinkInfoIconTip.cpp:35 ほか13件)。外部プラグインの例も
//  customconditionaltextui/CusCondTxtUIIconTip.cpp:42 が同じ形で、★ITip を CPMUnknown 直下に
//  実装しているコードは SDK 全体に1つも無い。
//
//  この基底が、このクラスが意見を持たない2つを供給するので、書くのは GetTipText だけで済む:
//    UpdateToolTipOnMouseMove - 基底は return kFalse(AbstractTip.cpp:45-48)。ITip.h:44-50 は
//                               この API を ID_DEPRECATED で囲っている。
//    SetTipText               - 基底は空実装(AbstractTip.h:56)。ITip.h:51-53 が「一般ケースでは
//                               未実装」と明記している。
//
//  実体は DV_WidgetBin.lib にある。★かつてはそれが「使わない理由」だったが、KESCM は 2026-07-11 に
//  自前描画ビュー(KESCMScrollMap の DVControlView)のため全4構成で同ライブラリをリンクしており
//  (_buildproj/README.md:18-20)、基底のコストはゼロ。2026-08-06 の監査(ブロック8 A-1)で、
//  「リンクしていないから使えない」という当時のコメントが既に事実に反していたことが判り、寄せた。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "AbstractTip.h"		// 製品のツールチップが全部継承している基底
#include "IControlView.h"		// GetWidgetID(★自分がどの widget に載っているかを聞く)

// 一般:
#include "PMString.h"

// プロジェクト内:
#include "KESCMID.h"

/** kKESCMIconWidgetBoss のツールチップ: イラストなら飛び先(配布元URL)、ツール切替ボタンならツール名。 */
class KESCMIconTip : public AbstractTip
{
public:
	KESCMIconTip(IPMUnknown* boss);
	virtual ~KESCMIconTip();

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)

KESCMIconTip::KESCMIconTip(IPMUnknown* boss) : AbstractTip(boss)
{
}

KESCMIconTip::~KESCMIconTip()
{
}

PMString KESCMIconTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	// ★どの widget に載っているかで文言を変える(2026-08-07)。ITip と widget は**同じ boss の別
	//   インターフェイス**なので、自分自身に IControlView を聞けば WidgetID が分かる
	//   (親を辿る必要は無い＝1段で済む)。
	InterfacePtr<IControlView> cv(this, UseDefaultIID());
	if (cv != nil && cv->GetWidgetID() == kKESCMToolButtonWidgetID)
	{
		// ★ツールボックスのツール名と**同じ文字列キー**を返す(KESCMTool::Init の SetName と同一)。
		//   翻訳フラグは落とさない ＝ 文字列テーブルのキーとして解決させる。
		return PMString(kKESCMToolStringKey);
	}

	// URL は翻訳する語句ではない(同じ理由で文字列テーブルにも置いていない)。
	PMString tip(kKESCMRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

// KESCMIconTip.cpp 終わり。
