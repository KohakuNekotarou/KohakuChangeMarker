//========================================================================================
//
//  KESCMThumbnailRefresh.cpp
//
//  比較実行後に、Pages パネルの「既に表示済み」のサムネイルを再生成させる(KESCMThumbnailRefresh.h)。
//
//  ★2026-07-06 実機で切り分け完了。効く最小セットは次の2手だけ:
//    ④ IImageCacheMgr::Purge(db)  … 共有画像キャッシュを無効化する。★Pages パネルのサムネイルは
//       この共有キャッシュに載っている(2026-07-05 に「内部専用キャッシュで公開APIでは無効化不可」と
//       していた前提は誤りだった)。これを外すとサムネイルは古いまま=枠が出ない(実機確認)。
//    ③ ForceRedraw               … Purge 後に Pages パネル(と両サブパネル)を再描画させ、その場で
//       サムネイルを作り直させる。これを外すと Purge しても即時には反映されない(実機確認)。
//  試して不要と分かって外した手(履歴): ① InvalidateSpreadWidget + UpdatePagesPanel(bForcePurge)、
//    ② IPendingUpdateController::Update()。詳細な切り分け経過は memory kescm-pages-panel-thumbnails。
//
//  ※サムネイル自体への枠描画は描画エンジン側(KESCMDrawEventHandler::sThumbExperiment=既定ON)。
//    この関数はあくまで「既表示分を作り直させる」トリガー。安全に何度でも呼べる。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IApplication.h"
#include "IPanelMgr.h"
#include "IPanelControlData.h"
#include "IControlView.h"
#include "IDataBase.h"
#include "IWorkspace.h"
#include "IImageCacheMgr.h"

#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / kMasterPagesSubPanelWidgetID
#include "ImageID.h"			// IID_IIMAGECACHEMGR
#include "Utils.h"

#include "KESCMThumbnailRefresh.h"

// サブパネル(Layout 用/Master 用)を1枚再描画する(③)。
static void KESCMForceRedrawSubPanel(IPanelControlData* pcd, const WidgetID& subPanelWID)
{
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(subPanelWID);
	if (subView != nil)
		subView->ForceRedraw(nil, kTrue);
}

void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db)
{
	if (db == nil)
		return;

	// ④ 共有画像キャッシュを無効化(パネルの有無に関わらず先に実行)。これで db のサムネイル/プレビューの
	//    キャッシュ済みビットマップが捨てられ、次の描画で作り直される。ワークスペースは実効セッションから。
	{
		InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
		InterfacePtr<IImageCacheMgr> cacheMgr(ws, IID_IIMAGECACHEMGR);
		if (cacheMgr != nil)
			cacheMgr->Purge(db);
	}

	// ③ Pages パネルが表示されていれば、その場で再描画させて(Purge 済みの)サムネイルを作り直させる。
	//    隠れていれば触る先が無い(次に開いたとき自然に作られる)。
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;
	IControlView* panel = panelMgr->GetVisiblePanel(kPagesPanelWidgetID);
	if (panel == nil)
		return;
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());

	KESCMForceRedrawSubPanel(pcd, kLayoutPagesSubPanelWidgetID);
	KESCMForceRedrawSubPanel(pcd, kMasterPagesSubPanelWidgetID);
	panel->ForceRedraw(nil, kTrue);
}

// KESCMThumbnailRefresh.cpp 終わり。
