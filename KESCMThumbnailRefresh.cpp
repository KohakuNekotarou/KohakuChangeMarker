//========================================================================================
//
//  KESCMThumbnailRefresh.cpp
//
//  比較実行後に、Pages パネルの「既に表示済み」のサムネイルを再生成させる(KESCMThumbnailRefresh.h)。
//
//  ★2026-07-06 実機で切り分け完了。効く最小セットは次の2手だけ:
//    ④ IImageCacheMgr::Purge(...)  … 共有画像キャッシュを無効化する。★Pages パネルのサムネイルは
//       この共有キャッシュに載っている。これを外すとサムネイルは古いまま=枠が出ない(実機確認)。
//    ③ ForceRedraw                 … Purge 後に Pages パネル(と両サブパネル)を再描画させ、その場で
//       サムネイルを作り直させる。これを外すと Purge しても即時には反映されない(実機確認)。
//
//  ─────────────────────────────────────────────────────────────────────────────
//  ★2026-07-07 サムネイル点滅の低減(実機で確定):
//    従来の Purge(db) は「その文書の画像キャッシュ全部」を捨てるため、変更のないページの
//    サムネイルまで作り直され、Pages パネル全体が一瞬点滅していた。これを「変更ページ
//    (sEntries / sSrcPageToTarget のキー)だけを per-UID Purge」に絞る。変更のないサムネイルは
//    キャッシュが生き残るので点滅しない(変更ページだけ作り直され、そこは元々枠を描き直したい)。
//
//    ★キャッシュキーの特定(実機プローブ結果): Pages パネルのサムネイルは「ページ UID」で
//    キャッシュされている(page UID の Purge で解放バイト>0 / spread UID では 0)。よって
//    スプレッド単位の Purge は不要で、対象ページの UID を直接 Purge するだけでよい。
//
//    ★対象ページ集合には「変更リング(sEntries)」だけでなく「overflow="/"斜線(sOverflowT/sOverflowS)」も
//    含める(2026-07-08 実機で判明)。斜線は別集合なので、含めないと overflow ページのサムネイルが
//    Purge されず斜線が即時に出ない。
//
//    ★2026-07-08 Stop/クローズの取りこぼし修正(ハイブリッド化):
//    Stop(KESCMDoClearMarks)/クローズ(KESCMHandleDocsClosed)は DropAll 済みで db が sDB/sSrcDB でなく
//    なるため、変更ページ集合を引けず従来は Purge(db) 全体に落ちていた。ところが全体 Purge(db) 一発は
//    既存サムネイルの無効化として実機で効かず(枠が消えない)ことが判明。そこでフォールバックを
//    「全ページを列挙(KESCMCollectPageUIDs)して1ページずつ per-UID Purge」に変更する(proven な per-UID を
//    全ページに回す)。DropAll 済みで枠データが無いので作り直しはクリーン=枠が確実に消える。終端操作
//    なのでパネル全体が一瞬リフレッシュされても許容(START の点滅回避とは別扱い)。
//  ─────────────────────────────────────────────────────────────────────────────
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

#include <set>
#include <vector>

#include "KESCMThumbnailRefresh.h"
#include "KESCMDrawEventHandler.h"	// sEntries / sDB / sSrcDB / sSrcPageToTarget
#include "KESCMCore.h"				// KESCMCollectPageUIDs(全ページ列挙)

// サブパネル(Layout 用/Master 用)を1枚再描画する(③)。
static void KESCMForceRedrawSubPanel(IPanelControlData* pcd, const WidgetID& subPanelWID)
{
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(subPanelWID);
	if (subView != nil)
		subView->ForceRedraw(nil, kTrue);
}

// この db の「サムネイルを作り直したいページ UID 集合」を outPages へ集める。
//   Target(db==sDB)   : sEntries(変更リング)のキー ＋ sOverflowT(overflow="/"斜線)
//   Source(db==sSrcDB): sSrcPageToTarget(変更リング)のキー ＋ sOverflowS(overflow="/"斜線)
// どちらでもなければ kFalse(=集合を引けない)。
//   ★斜線「/」は変更リング(sEntries)とは別集合(sOverflowT/sOverflowS)なので、これを含めないと
//     overflow ページのサムネイルが Purge されず、斜線が即時に出ない(2026-07-08 実機で判明)。
//     overflow キャッシュが現在のペア(sDB,sSrcDB)用に作られている時だけ加える(別文書用の UID を
//     誤って Purge しないための安全ガード)。
static bool16 KESCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages)
{
	const bool16 overflowCacheMatches =
		(KESCMDrawEventHandler::sOverflowCacheDB == KESCMDrawEventHandler::sDB &&
		 KESCMDrawEventHandler::sOverflowCacheSrcDB == KESCMDrawEventHandler::sSrcDB);

	if (db != nil && db == KESCMDrawEventHandler::sDB)
	{
		for (std::map<UID, KESCMOverlayEntry*>::iterator it = KESCMDrawEventHandler::sEntries.begin();
			 it != KESCMDrawEventHandler::sEntries.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KESCMDrawEventHandler::sOverflowT.begin(), KESCMDrawEventHandler::sOverflowT.end());
		return kTrue;
	}
	if (db != nil && db == KESCMDrawEventHandler::sSrcDB)
	{
		for (std::map<UID, UID>::iterator it = KESCMDrawEventHandler::sSrcPageToTarget.begin();
			 it != KESCMDrawEventHandler::sSrcPageToTarget.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KESCMDrawEventHandler::sOverflowS.begin(), KESCMDrawEventHandler::sOverflowS.end());
		return kTrue;
	}
	return kFalse;
}

void KESCMTryRefreshPagesPanelThumbnails(IDataBase* db)
{
	if (db == nil)
		return;

	// ④ 共有画像キャッシュを無効化(パネルの有無に関わらず先に実行)。ワークスペースは実効セッションから。
	{
		InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
		InterfacePtr<IImageCacheMgr> cacheMgr(ws, IID_IIMAGECACHEMGR);
		if (cacheMgr != nil)
		{
			std::set<UID> changedPages;
			if (KESCMCollectChangedPageUIDs(db, changedPages))
			{
				// ── 比較中の db(START/再比較) ──
				// ★変更ページ＋overflow斜線ページ(ページ UID)だけを狙って Purge。変更のないページの
				//   サムネイルはキャッシュが生き残り点滅しない。サムネイルはページ UID でキャッシュ
				//   されている(2026-07-07 実機プローブで確定)。集合が空(変更ゼロ)なら何も purge せず、
				//   下の ForceRedraw だけ行う(無害)。
				for (std::set<UID>::iterator it = changedPages.begin(); it != changedPages.end(); ++it)
				{
					UIDRef pageRef(db, *it);
					cacheMgr->Purge(pageRef, IImageCacheMgr::kWildCard);
				}
			}
			else
			{
				// ── Stop/クローズ後(DropAll 済みで db が比較対象でなくなった) ──
				// ★全ページを列挙して1ページずつ per-UID Purge する。全体 Purge(db) 一発は既存サムネイルの
				//   無効化として実機で効かなかった(枠が消えない)ため、proven な per-UID を全ページに回す。
				//   DropAll 済み=枠データが無いので、作り直されるサムネイルはクリーン(枠が消える)。
				//   終端操作なのでパネル全体が一瞬リフレッシュされても許容。
				std::vector<UID> allPages;
				KESCMCollectPageUIDs(db, allPages);
				for (std::vector<UID>::iterator it = allPages.begin(); it != allPages.end(); ++it)
				{
					UIDRef pageRef(db, *it);
					cacheMgr->Purge(pageRef, IImageCacheMgr::kWildCard);
				}
			}
		}
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
