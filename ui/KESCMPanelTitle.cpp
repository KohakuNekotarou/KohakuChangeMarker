//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  パネルのタブ名。契約は KESCMPanelTitle.h。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager
#include "IControlView.h"		// パネルは control view ＝ GetPanelFromWidgetID が返すもの
#include "IPanelMgr.h"			// GetPanelFromWidgetID / GetPaletteRefContainingPanel
#include "ISession.h"

// General includes:
#include "PaletteRefUtils.h"	// SetPaletteLabel（タブ自身のラベル）
#include "PMString.h"
#include "Utils.h"				// Utils<IKESCMCompareFacade>()

// Project includes:
#include "KCMUIID.h"				// kKESCMPanelWidgetID
#include "KESCMBoundaryID.h"		// kKESCMDisplayName / KESCMCompareMode
#include "IKESCMCompareFacade.h"	// GetCompareMode（境界越しに model へ聞く）
#include "KESCMPanelTitle.h"

namespace
{

/** タブにラベルを置く。パネルが無い／パレットに入っていないときは何もしない。 */
void SetTabLabel(const PMString& label)
{
	// ⚠**終了処理中はセッションが既に無いことがある**（KESCMPanelAlpha.cpp が同じ理由で
	//   ポインタを変数に受けてから使う）。Restore() は Shutdown から呼ばれる＝ここが唯一
	//   teardown を通る入口なので、素直に nil を見る。
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;

	// 非所有（Get であって Query ではない）。パネルを一度も開いていなければ nil で、それが
	// 起動直後の通常の状態＝呼び手はいつでも撃ってよい。
	//
	// ★★**「見えているパネルだけ」の入口を使わない理由。** この plug-in の他所は
	//   `Utils<IPalettePanelUtils>()->QueryPanelByWidgetID` で取るが、あれは画面に出ていない
	//   パネルには nil を返す。**タブ名だけはそれで困る** ---- ラベルはパネルの中身ではなく
	//   パレットの持ち物で、パネルの中身が見えないときにも画面に残るから（最小化したパレットは
	//   まさに「タブの帯だけ」の状態）。その状態でもフライアウトからモードは変えられる。
	IControlView* panelView = panelMgr->GetPanelFromWidgetID(kKESCMPanelWidgetID);
	if (panelView == nil)
		return;

	// ラベルは**容器**の持ち物（通常のタブ付きパレットなら、タブを描く kTabPanelContainerType）。
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
	if (!container.IsValid())
		return;

	PaletteRefUtils::SetPaletteLabel(container, label, PaletteRefUtils::kTitle_PanelLabel);
}

}

void KESCMPanelTitle::Update()
{
	// ★区切りは素の ASCII ハイフン（KBS と同じ）。タブは狭く、全角ダッシュだと間延びして見える。
	//   ASCII に留めれば、BOM の無い .cpp で非 ASCII リテラルが CP932 に落ちる問題とも無縁になる
	//   ---- ただしこのファイルは日本語コメントを持つので BOM 付き（[[cpp-japanese-needs-bom]]）。
	PMString title(kKESCMDisplayName);
	title.Append(" - ");
	// ★短い方を採る（メニューは "Pixel Changes" / "Story Changes" だが、タブは幅が無い）。
	title.Append(Utils<IKESCMCompareFacade>()->GetCompareMode() == kKESCMModeStory ? "Story" : "Pixel");
	// ⚠パレットのラベルも**翻訳キーの候補**として扱われる＝訳せる印を落とさないと、
	//   文字列テーブルにたまたま同じキーがあったときに別の語へ差し替わる。
	title.SetTranslatable(kFalse);

	SetTabLabel(title);
}

void KESCMPanelTitle::Restore()
{
	PMString title(kKESCMDisplayName);
	title.SetTranslatable(kFalse);

	SetTabLabel(title);
}

// End, KESCMPanelTitle.cpp.
