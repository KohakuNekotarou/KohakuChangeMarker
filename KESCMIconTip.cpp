//========================================================================================
//
//  KESCMIconTip.cpp
//
//  パネルのイラスト(ON/OFF アイコン、kKESCMIconWidgetBoss)にホバーした時のツールチップ。
//  クリックすると開くのと同じ配布元URL(kKESCMRepoURL)をそのまま表示し、押すとそこへ飛ぶと
//  分かるようにする。
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

// 一般:
#include "PMString.h"

// プロジェクト内:
#include "KESCMID.h"

/** パネルのイラストのツールチップ: クリックの飛び先そのもの(配布元URL)を出す。 */
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
	// URL は翻訳する語句ではない(同じ理由で文字列テーブルにも置いていない)。
	PMString tip(kKESCMRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

// KESCMIconTip.cpp 終わり。
