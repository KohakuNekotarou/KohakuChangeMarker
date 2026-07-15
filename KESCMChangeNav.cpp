//========================================================================================
//
//  KESCMChangeNav.cpp
//
//  「中身が変わったページ」を順に巡回する Next/Prev ナビ(KESCMChangeNav.h で宣言)。
//  KESCL の「検索ヒットを ScrollViewCenterTo で中央へ送る」動きと同じ発想で、対象は KESCM の
//  「見るべきページ」= Target(sDB)で内容変更マークが付くページ(sEntries)。
//  ★Added/Removed(登録ページ)・Overflow(未比較)はページ増減由来なので巡回対象に含めない
//    (2026-07-10 ユーザー指定)。
//
//  巡回リストは呼ばれるたびにその場で作り直す(再比較でマークが動いても常に最新に追従し、
//  index ではなく現在ページ UID を覚えておくことで位置を見失わない)。順序は文書のページ順そのもの
//  (KESCMCollectPageUIDs)。
//
//  移動は「対象ページの矩形中心 → ::InnerToPasteboardMatrix でペーストボード座標 →
//  KESCMQueryPanorama()->ScrollViewCenterTo()」。ズームは触らない(現在の倍率のまま中央へ)。
//  対象文書は常に Target なので、Target のレイアウトビュー(GetAllLayoutViews(db))をスクロールする
//  (Source を前面にして押しても背面の Target ビューが動く=ユーザー指定の「常に Target」)。
//
//  ★Source 側も連動: Target の対象ページに対応する Source ページ(KESCMSourcePageForTarget)へ
//  Source のビューも同時にスクロールする(背面のまま位置だけ)。ページの追加/削除で番号がズレていても
//  対応表(KESCMBuildPairing)が正しい相手を返すので正しく飛ぶ。Source に直接の相手が無い Added/
//  Overflow ページは、ページ順で最も近いペア済みページの相手(挿入位置の近傍)へ寄せる。
//  ★さらに Source の拡大率も Target に合わせる(Target の実効ズーム GetXScaleFactor(kTrue) を読み、
//  Source のビューへ MakeZoomCmd(kZoomToCmdBoss) で反映=KESCMPeek のビューポート同期と同じ手法)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IControlView.h"
#include "IPanorama.h"
#include "ILayoutViewUtils.h"	// GetAllLayoutViews(db)
#include "ILayoutUIUtils.h"		// MakeZoomCmd(kZoomToCmdBoss。他文書ビューのズームを合わせる公式経路)/GetFrontDocument
#include "IPanelControlData.h"
#include "IDocument.h"
#include "IPagesSubPanelController.h"	// ScrollPanelToSpread(page/spread UID 可と明記あり)
#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / IID_IPAGESSUBPANELCONTROLLER
#include "ICommand.h"			// ズームコマンド
#include "CmdUtils.h"			// ProcessCommand
#include "IGeometry.h"
#include "IDataBase.h"
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include "PMRect.h"
#include "PMPoint.h"			// PBPMPoint(= PMPoint の typedef)
#include "PMString.h"
#include "Utils.h"
#include "K2Vector.h"

#include <map>
#include <set>
#include <vector>

#include "KESCMCore.h"				// KESCMCollectPageUIDs / KESCMArmed* / KESCMSetStatus
#include "KESCMDrawEventHandler.h"	// sDB / sSrcDB / sEntries / KESCMQueryPanorama
#include "KESCMPageMap.h"			// KESCMBuildPairing / KESCMMapTargetToSource(Source 側連動スクロール用)
#include "KESCMThumbnailRefresh.h"	// KESCMGetVisiblePagesPanel(表示中 Pages パネル取得の共有ヘルパ)
#include "KESCMChangeNav.h"

// 直近に巡回した Target ページ UID(次/前の基準点)。index ではなく UID で持つことで、リストが
// 再構築されて中身が変わっても位置を追える。未 Start(DropAll)後はリストが空になるので実害はなく、
// 再 Start 後にこの UID が新リストに無ければ先頭/末尾から始め直す。
static UID sNavCurrent = kInvalidUID;

//----------------------------------------------------------------------------------------
// Target(sDB)で「見るべきページ」を文書のページ順に集める。
//   対象は「中身が変わったページ」= sEntries にキーがあるページのみ。
//   ★Added/Removed(登録ページ=緑「/」= KESCMPageMapIsRegistered)と Overflow(未比較=
//     KESCMBuildPairing の tOverflow)は巡回対象に含めない(2026-07-10: これらは既存ページの
//     内容変更ではなくページ増減由来なので、ユーザー指定で Prev/Next から除外)。
// 該当ページを KESCMCollectPageUIDs(sDB) の順で out に積む(ページ順なので重複なし)。
//----------------------------------------------------------------------------------------
static void KESCMBuildReviewList(std::vector<UID>& out)
{
	out.clear();
	IDataBase* targetDB = KESCMDrawEventHandler::sDB;
	if (targetDB == nil)
		return;

	std::vector<UID> flat;
	KESCMCollectPageUIDs(targetDB, flat);
	for (size_t i = 0; i < flat.size(); ++i)
	{
		const UID u = flat[i];
		if (KESCMDrawEventHandler::sEntries.find(u) != KESCMDrawEventHandler::sEntries.end())
			out.push_back(u);	// 中身が変わったページのみ
	}
}

//----------------------------------------------------------------------------------------
// 文書 db の実効ズーム(拡大率)を読む。最初に見つかったレイアウトビューのパノラマの
// GetXScaleFactor(kTrue)(モニタPPI補正込みの実効スケール。1.0=100%)。ビューが無ければ -1。
// ★ビューポート同期(KESCMPeek)と同じ次元の値で、そのまま MakeZoomCmd に渡せる。
//----------------------------------------------------------------------------------------
static PMReal KESCMReadDocZoom(IDataBase* db)
{
	if (db == nil)
		return PMReal(-1.0);
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<IPanorama> pano(KESCMQueryPanorama(views[i]));
		if (pano != nil)
			return pano->GetXScaleFactor(kTrue);
	}
	return PMReal(-1.0);
}

//----------------------------------------------------------------------------------------
// 文書 db の全レイアウトビューを、pageUID の矩形中心が画面中央に来るようスクロールする。
//   applyZoom > 0 のときは、センタリングの前に各ビューの実効ズームを applyZoom に合わせる
//   (Source を Target の拡大率に合わせる用。ズームは UI のズーム欄と同じ公式コマンド
//   kZoomToCmdBoss=MakeZoomCmd 経由。★ILayoutViewUtils::ZoomLayoutViews 直呼びは他文書ビューに
//   効かないため不可=KESCMPeek のビューポート同期と同じ理由)。既に一致していれば触らない。
//   applyZoom <= 0 ならズームは変えない(従来どおり位置だけ)。1つでもスクロールできれば kTrue。
//----------------------------------------------------------------------------------------
static bool16 KESCMScrollDocToPage(IDataBase* db, UID pageUID, PMReal applyZoom = PMReal(-1.0))
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
	if (pageGeo == nil)
		return kFalse;

	// ページ inner 矩形の中心 → ペーストボード座標(ScrollViewCenterTo は PBPMPoint=ペーストボード)。
	PMRect box = pageGeo->GetPathBoundingBox();
	PMPoint center((box.Left() + box.Right()) / PMReal(2.0), (box.Top() + box.Bottom()) / PMReal(2.0));
	PMMatrix m = ::InnerToPasteboardMatrix(pageGeo);
	m.Transform(&center);

	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	bool16 any = kFalse;
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		IControlView* view = views[i];
		if (view == nil)
			continue;
		InterfacePtr<IPanorama> pano(KESCMQueryPanorama(view));
		if (pano == nil)
			continue;

		// ズーム合わせ(要求があり、かつ現状と食い違う時だけ)。ズーム後にセンタリングするので新しい
		// 倍率で正しく中央に来る(KESCMPeek のビューポート同期と同じ順序)。
		if (applyZoom > PMReal(0.0))
		{
			PMReal cur = pano->GetXScaleFactor(kTrue);
			PMReal diff = cur - applyZoom; if (diff < 0) diff = -diff;
			if (diff > PMReal(0.0001))
			{
				InterfacePtr<ICommand> zoomCmd(Utils<ILayoutUIUtils>()->MakeZoomCmd(view, applyZoom));
				if (zoomCmd != nil)
					CmdUtils::ProcessCommand(zoomCmd);
			}
		}

		pano->ScrollViewCenterTo(center, kTrue /*forceRedraw*/);
		any = kTrue;
	}
	return any;
}

//----------------------------------------------------------------------------------------
// Target のあるページに対して、Source 側で表示すべきページ UID を決める。
//   ・通常のペア済みページ … 対応表(KESCMBuildPairing)で正しい Source ページを直接引く。
//     ★ページの追加/削除で番号がズレていても、対応表は登録済みページを除いて残りを順番対応させて
//     いるので、ズレを吸収した「本来の相手」が返る(ここが増減対応の肝)。
//   ・Added / Overflow ページ(Target 側にしか無い=Source に直接の相手が無い) … ページ順で最も
//     近いペア済みページの Source 相手へ寄せる(同じ距離なら前方=挿入位置の手前を優先)。これで
//     「この追加ページが旧版のどのあたりに入ったか」の近傍が Source ビューに出る。
//   見つからなければ kInvalidUID(呼び出し側は Source を動かさない)。
//----------------------------------------------------------------------------------------
static UID KESCMSourcePageForTarget(IDataBase* targetDB, IDataBase* sourceDB, UID targetPageUID)
{
	if (targetDB == nil || sourceDB == nil)
		return kInvalidUID;

	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);	// ペア済み(登録除外・ズレ吸収済み)

	// 直接の相手(Target→Source)を探す。
	std::map<UID, UID> t2s;
	for (size_t i = 0; i < tPages.size(); ++i)
	{
		if (tPages[i] == targetPageUID)
			return sPages[i];
		t2s[tPages[i]] = sPages[i];
	}

	// 相手なし(Added/Overflow)。ページ順で最も近いペア済みページの Source 相手へ寄せる。
	std::vector<UID> flat;
	KESCMCollectPageUIDs(targetDB, flat);
	int32 idx = -1;
	for (size_t i = 0; i < flat.size(); ++i)
		if (flat[i] == targetPageUID) { idx = (int32)i; break; }
	if (idx < 0)
		return kInvalidUID;

	const int32 n = (int32)flat.size();
	for (int32 d = 1; d < n; ++d)
	{
		if (idx - d >= 0)	// 前方(挿入位置の手前)を優先
		{
			std::map<UID, UID>::const_iterator it = t2s.find(flat[idx - d]);
			if (it != t2s.end())
				return it->second;
		}
		if (idx + d < n)
		{
			std::map<UID, UID>::const_iterator it = t2s.find(flat[idx + d]);
			if (it != t2s.end())
				return it->second;
		}
	}
	return kInvalidUID;
}

//----------------------------------------------------------------------------------------
// Pages パネルも対象ページのスプレッドへ連動スクロールする(ベストエフォート)。
//   ★Pages パネルは「前面(アクティブ)の文書」のページ一覧を表示するので、前面文書が db と
//   一致する時だけ動かす(Source が前面のまま Next/Prev を押した場合、パネルは Source の一覧を
//   表示中=Target のページ UID を渡しても意味がないので何もしない)。
//   経路は KESCMThumbnailRefresh と同じ IPanelMgr→GetVisiblePanel(kPagesPanelWidgetID)→
//   FindWidget(kLayoutPagesSubPanelWidgetID)→IPagesSubPanelController。ScrollPanelToSpread は
//   ヘッダー注記により page UID をそのまま渡してよい(「spread or page uid」)。
//----------------------------------------------------------------------------------------
static void KESCMScrollPagesPanelToPage(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return;

	// 前面文書のページ一覧を表示中か(違えば触らない)。
	IDocument* front = Utils<ILayoutUIUtils>()->GetFrontDocument();
	if (front == nil || ::GetDataBase(front) != db)
		return;

	// パネル取得は共有ヘルパ(KESCMThumbnailRefresh.h)に一本化(2026-07-10)。
	IControlView* panel = KESCMGetVisiblePagesPanel();
	if (panel == nil)
		return;	// パネル非表示なら何もしない(次に開いたときは通常表示でよい)
	InterfacePtr<IPanelControlData> pcd(panel, UseDefaultIID());
	if (pcd == nil)
		return;
	IControlView* subView = pcd->FindWidget(kLayoutPagesSubPanelWidgetID);
	if (subView == nil)
		return;
	InterfacePtr<IPagesSubPanelController> ctrl(subView, UseDefaultIID());
	if (ctrl != nil)
		ctrl->ScrollPanelToSpread(UIDRef(db, pageUID));
}

//----------------------------------------------------------------------------------------
// 巡回本体(dir=+1 で次、-1 で前)。端は折り返す。ステータス行に「3 / 12」と現在位置を出す。
//----------------------------------------------------------------------------------------
static void KESCMGoto(int32 dir)
{
	IDataBase* targetDB = KESCMDrawEventHandler::sDB;
	if (targetDB == nil)
	{
		PMString s("Start a comparison first."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return;
	}

	std::vector<UID> list;
	KESCMBuildReviewList(list);
	if (list.empty())
	{
		PMString s("No changed pages."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		KESCMRefreshNavPosition();	// 巡回対象なし: 位置は "/"・Prev/Next は無効化(通常はボタン無効で来ない)
		return;
	}

	// 現在位置を UID で探す。見つからなければ(初回/前回ページが消えた)、次=先頭・前=末尾から。
	int32 cur = -1;
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i] == sNavCurrent) { cur = (int32)i; break; }
	}

	int32 next;
	if (cur < 0)
		next = (dir > 0) ? 0 : (int32)list.size() - 1;
	else
	{
		next = cur + dir;
		if (next < 0)                       next = (int32)list.size() - 1;	// 先頭で「前」→末尾へ折り返し
		else if (next >= (int32)list.size()) next = 0;						// 末尾で「次」→先頭へ折り返し
	}
	sNavCurrent = list[next];

	if (!KESCMScrollDocToPage(targetDB, sNavCurrent))
	{
		PMString s("Could not scroll to the page."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return;
	}

	// Pages パネルも対象ページへ連動スクロール(前面文書が Target のときだけ。ベストエフォート)。
	KESCMScrollPagesPanelToPage(targetDB, sNavCurrent);

	// Source 側も対応ページへ連動スクロール(背面のまま位置だけ動かす。前面には出さない)。
	// ページの追加/削除でズレていても対応表で正しい相手へ、相手が無い Added/Overflow は近傍へ寄せる。
	// ★Source の拡大率も Target に合わせる(Target の実効ズームを読み、Source のビューへ MakeZoomCmd で反映)。
	// ベストエフォート: Source ビューが無い/相手が引けなくても Target 側の移動は成立させる。
	IDataBase* sourceDB = KESCMDrawEventHandler::sSrcDB;
	if (sourceDB != nil && sourceDB != targetDB)
	{
		const UID srcPage = KESCMSourcePageForTarget(targetDB, sourceDB, sNavCurrent);
		if (srcPage != kInvalidUID)
		{
			const PMReal targetZoom = KESCMReadDocZoom(targetDB);	// 実効ズーム(<=0 ならズームは変えない)
			KESCMScrollDocToPage(sourceDB, srcPage, targetZoom);
			// Source が前面の場合、Pages パネルは Source の一覧を表示しているので、そちらの対応ページへ
			// 連動スクロール(ヘルパー内の前面一致ガードにより、Target 前面ならこの呼び出しは何もしない=
			// 上の Target 側呼び出しと排他で、前面の文書に合った側だけがパネルを動かす)。
			KESCMScrollPagesPanelToPage(sourceDB, srcPage);
		}
	}

	// 現在位置は Prev/Next の間の専用ウィジェット(KESCL 風「3/12」)へ。ステータス行(メッセージ欄)には
	// 出さない(2026-07-15 ユーザー指定: 位置はボタン間へ移設)。基準点(sNavCurrent)は上で更新済みなので、
	// 共通関数で今の集合から「k/N」を作り直す(値の組み立てとボタン有効/無効を1箇所に集約)。
	KESCMRefreshNavPosition();
}

//========================================================================================
// KESCMGotoNextChange / KESCMGotoPrevChange(KESCMChangeNav.h で宣言)
//========================================================================================
void KESCMGotoNextChange() { KESCMGoto(+1); }
void KESCMGotoPrevChange() { KESCMGoto(-1); }

// 巡回の基準点を忘れる(KESCMChangeNav.h)。次回の Next/Prev はリストの先頭/末尾から始まる。
// ★表示更新はしない(基準点を落とすだけ): これは比較の総入れ替え(Start)の途中でも呼ばれるため、
//   位置表示は呼び出し側(KESCMDoMarkChangesDoc 末尾 / KESCMDoClearMarks)が確定後に
//   KESCMRefreshNavPosition で一括更新する。
void KESCMResetNav() { sNavCurrent = kInvalidUID; }

// KESCMChangeNav.h 参照。今の変更ページ集合＋基準点(sNavCurrent)から Prev/Next 間の位置表示を作り直し、
// Prev/Next ボタンの有効/無効もあわせて更新する(値組み立てとボタン状態を1箇所に集約=KESCL の
// UpdateNavWidgets と同じ発想)。表示規則は KESCMChangeNav.h のコメント参照。
void KESCMRefreshNavPosition()
{
	IDataBase* targetDB = KESCMDrawEventHandler::sDB;	// 比較中のみ非nil(Stop の DropAll で nil)

	PMString text; text.SetTranslatable(kFalse);
	bool16 navEnabled = kFalse;

	if (targetDB != nil)	// Start 済み(比較中)
	{
		std::vector<UID> list;
		KESCMBuildReviewList(list);
		if (list.empty())
		{
			text.Append("/");	// 変更ページ0件: 巡回対象なし → "/"・ボタン無効(ユーザー指定 2026-07-15)
		}
		else
		{
			// 基準点の現在位置(1始まり)。まだ巡回していない(集合内に無い)ときは先頭扱いで "1/N"。
			int32 cur = -1;
			for (size_t i = 0; i < list.size(); ++i)
				if (list[i] == sNavCurrent) { cur = (int32)i; break; }
			const int32 shown = (cur < 0) ? 1 : (cur + 1);
			text.AppendNumber(shown);
			text.Append("/");
			text.AppendNumber((int32)list.size());
			navEnabled = kTrue;	// 巡回対象あり → Prev/Next 有効
		}
	}
	// 未 Start(targetDB==nil): text は空・navEnabled=false(位置欄クリア・ボタン無効)

	KESCMSetNavPosition(text, navEnabled);
}

// KESCMChangeNav.cpp 終わり。
