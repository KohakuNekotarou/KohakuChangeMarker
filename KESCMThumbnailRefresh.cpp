//========================================================================================
//
//  KESCMThumbnailRefresh.cpp
//
//  ★実験: Pages パネルの既表示サムネイルの再生成を試みる(KESCMThumbnailRefresh.h 参照)。
//  試す手(いずれも公開API・失敗しても無害):
//   ① IPagesSubPanelController(Layout/Master 両サブパネル): 全スプレッドを InvalidateSpreadWidget →
//      UpdatePagesPanel(bForceUpdateAll=kTrue, bForcePurge=kTrue) で全ページタブ再構築を予約。
//   ② IPendingUpdateController::Update()(サブパネルが持てば): 描画直前の保留更新をその場で消化させる。
//      ★これが過去に「pending は 1 になるが消化されない」だった部分の消化役の可能性(未検証)。
//   ③ サブパネル/パネルの ForceRedraw。
//   ④ IImageCacheMgr::Purge(db)(共有画像キャッシュ。Pages サムネイルが使う確証は無いが長い望みで叩く)。
//  ①③は過去に単体では不発。②④は今回追加。うまくいかなければ呼び出し側の1行を消すだけで撤去できる。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IApplication.h"
#include "IPanelMgr.h"
#include "IPanelControlData.h"
#include "IControlView.h"
#include "IDataBase.h"
#include "ISpreadList.h"
#include "IWorkspace.h"
#include "IPagesSubPanelController.h"
#include "IPendingUpdateController.h"
#include "IImageCacheMgr.h"

#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / kMasterPagesSubPanelWidgetID / IID_IPAGESSUBPANELCONTROLLER
#include "ImageID.h"			// IID_IIMAGECACHEMGR
#include "UIDRef.h"
#include "Utils.h"

#include "KESCMThumbnailRefresh.h"

// 1つのサブパネル(Layout 用/Master 用)に対して ①②③ を試す。
static void KESCMTryRefreshOneSubPanel(IPanelControlData* pcd, const WidgetID& subPanelWID, IDataBase* db)
{
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(subPanelWID);
	if (subView == nil)
		return;

	// ① コントローラ経由: 全スプレッドを無効化 → 全再構築(bForcePurge)を予約。
	InterfacePtr<IPagesSubPanelController> ctrl(subView, UseDefaultIID());
	if (ctrl != nil)
	{
		InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
		if (spreadList != nil)
		{
			const int32 ns = spreadList->GetSpreadCount();
			for (int32 s = 0; s < ns; ++s)
				ctrl->InvalidateSpreadWidget(UIDRef(db, spreadList->GetNthSpreadUID(s)));
		}
		ctrl->UpdatePagesPanel(kTrue /*bForceUpdateAll*/, kTrue /*bForcePurge*/,
			IPagesSubPanelController::kNoReasonSpecified);
	}

	// ② 保留更新の消化(このサブパネルが IPendingUpdateController を持てば)。過去に不発だった
	//    「pending=1 のまま消化されない」の消化役かもしれない(未検証)。
	InterfacePtr<IPendingUpdateController> pending(subView, UseDefaultIID());
	if (pending != nil)
	{
		pending->SetPendingUpdate(kTrue);
		pending->Update();
	}

	// ③ 念のため再描画。
	subView->ForceRedraw(nil, kTrue);
}

void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db)
{
	if (db == nil)
		return;

	// ④ 共有画像キャッシュの Purge(パネルの有無に関わらず先に試す)。Pages サムネイルがこの
	//    キャッシュを使う確証は無いので長い望み。ワークスペースは実効セッションから取る。
	{
		InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
		InterfacePtr<IImageCacheMgr> cacheMgr(ws, IID_IIMAGECACHEMGR);
		if (cacheMgr != nil)
			cacheMgr->Purge(db);
	}

	// ①②③: 表示中の Pages パネルがある時だけ(隠れていれば触る先が無い)。
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
	if (pcd == nil)
		return;

	KESCMTryRefreshOneSubPanel(pcd, kLayoutPagesSubPanelWidgetID, db);
	KESCMTryRefreshOneSubPanel(pcd, kMasterPagesSubPanelWidgetID, db);

	// パネル全体も再描画。
	panel->ForceRedraw(nil, kTrue);
}

// KESCMThumbnailRefresh.cpp 終わり。
