//========================================================================================
//
//  KESCMIconTip.cpp
//
//  パネルのイラスト(ON/OFF アイコン、kKESCMIconWidgetBoss)にホバーした時のツールチップ。
//  クリックすると開くのと同じ配布元URL(kKESCMRepoURL)をそのまま表示し、押すとそこへ飛ぶと
//  分かるようにする。
//
//  AbstractTip(source/public/libs/widgetbin/includes/AbstractTip.h)は使わない — その実体は
//  DV_WidgetBin.lib にあり、KESCM プロジェクトは WidgetBin.lib しかリンクしていないため未解決
//  シンボルになる。ITip を CPMUnknown 直下に自前実装すれば追加のライブラリ依存が要らない。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// インターフェイス:
#include "ITip.h"

// 一般:
#include "CPMUnknown.h"
#include "PMString.h"

// プロジェクト内:
#include "KESCMID.h"

class KESCMIconTip : public CPMUnknown<ITip>
{
public:
	KESCMIconTip(IPMUnknown* boss) : CPMUnknown<ITip>(boss) {}

	virtual PMString GetTipText(const PMPoint& mouseLocation);
	virtual bool16 UpdateToolTipOnMouseMove();
	virtual void SetTipText(const PMString tipText);
};

CREATE_PMINTERFACE(KESCMIconTip, kKESCMIconTipImpl)

PMString KESCMIconTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	PMString tip(kKESCMRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

bool16 KESCMIconTip::UpdateToolTipOnMouseMove()
{
	return kFalse;	// 同一ウィジェット内でマウス位置が変わってもチップ文言は変わらない。
}

void KESCMIconTip::SetTipText(const PMString /*tipText*/)
{
	// ITip.h の注記どおり、一般ケースでは未実装でよい(no-op)。
}

// KESCMIconTip.cpp 終わり。
