//========================================================================================
//
//  KESCMChangeNav.cpp
//
//  「中身が変わったページ」を順に巡回する Next/Prev ナビ(KESCMChangeNav.h で宣言)。
//  KESCL の「検索ヒットをパノラマの中央へ送る」動きと同じ発想で、対象は KESCM の
//  「見るべきページ」= Target(sDB)で内容変更マークが付くページ(sEntries)。
//  ★Added/Removed(登録ページ)・Overflow(未比較)はページ増減由来なので巡回対象に含めない
//    (2026-07-10 ユーザー指定)。
//
//  巡回リストは呼ばれるたびにその場で作り直す(再比較でマークが動いても常に最新に追従し、
//  index ではなく現在ページ UID を覚えておくことで位置を見失わない)。順序は文書のページ順そのもの
//  (KESCMCollectPageUIDs)。
//
//  移動は「対象ページの矩形をペーストボード座標で取り(Facade::IGeometryFacade::GetItemBounds)、その中心を
//  KESCMQueryPanorama()->ScrollContentLocationToFrameCenter() へ」。ズームは触らない(現在の倍率のまま中央へ)。
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
#include "ILayoutUIUtils.h"		// MakeZoomCmd(kZoomToCmdBoss。他文書ビューのズームを合わせる公式経路)
#include "IPanelControlData.h"
#include "IDocument.h"
#include "IPagesSubPanelController.h"	// ScrollPanelToSpread(page/spread UID 可と明記あり)
#include "PagesPanelID.h"		// kPagesPanelWidgetID / kLayoutPagesSubPanelWidgetID / IID_IPAGESSUBPANELCONTROLLER
#include "ICommand.h"			// ズームコマンド
#include "CmdUtils.h"			// ProcessCommand
#include "IGeometry.h"
#include "IDataBase.h"
#include "IHierarchy.h"			// GetSpreadUID(ページ→スプレッド。スクロール前の切替判定に使う)
#include "ILayoutControlData.h"	// GetSpreadRef(そのビューが今どのスプレッドを見ているか)/GetDocument
#include "ILayoutCmdData.h"		// kSetSpreadCmdBoss が要求するデータ(対象の文書とビュー)
#include "SpreadID.h"			// kSetSpreadCmdBoss(レイアウトビューにスプレッドを出すコマンド)
#include "ErrorUtils.h"			// PMSetGlobalErrorCode(切替に失敗しても後続コマンドを巻き添えにしない)
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "UIDList.h"			// SetItemList(切替先スプレッド)
#include "IPageList.h"			// GetPageString / kDefaultPageType(飛んだページ番号の表示ラベル "Page: 1" 等)
#include "IGeometryFacade.h"	// GetItemBounds(ページ矩形をペーストボード座標で。手本=SnapTracker.cpp:616)
#include "PMRect.h"
#include "PMPoint.h"			// PBPMPoint(= PMPoint の typedef)
#include "PMString.h"
#include "Utils.h"
#include "K2Vector.h"

#include <map>
#include <set>			// 通常スプレッドに載るページの集合(マスターページ上の overset を見分ける)
#include <vector>

#include "KESCMCore.h"				// KESCMCollectPageUIDs / KESCMArmed* / KESCMSetStatus
#include "KESCMDrawEventHandler.h"	// sDB / sSrcDB / sEntries / sOverset* / KESCMQueryPanorama
#include "KESCMOversetScan.h"		// KESCMOversetLoc(overset「+」箇所の位置)
#include "KESCMPageMap.h"			// KESCMBuildPairing(Source 側連動スクロールの対応表。2026-07-25 コメント現行化)
#include "KESCMThumbnailRefresh.h"	// KESCMGetVisiblePagesPanel(表示中 Pages パネル取得の共有ヘルパ)
#include "KESCMStoryList.h"			// KESCMStoryFirstFrameUID(Source 側で「同じストーリー」の先頭フレームを引く)
#include "KESCMChangeNav.h"

// 巡回の1ストップ。change=そのページの変更(枠)= ページ中心へスクロール / overset=あふれ「+」箇所=
// その pb 点へスクロール(KBS 流)。pageUID は並び順・ラベル・Source 連動に、pb は overset のときだけ有効。
struct KESCMNavStop
{
	UID			pageUID;
	bool16		isOverset;			// kFalse=変更(枠), kTrue=overset(「+」)
	PBPMPoint	pb;					// overset の「+」点(isOverset のときのみ有効)
	int32		oversetOrd;			// 同じページ内の overset ストップ通し番号(0始まり。リスト再構築後の同定用)
	int32		oversetCountOnPage;	// そのページの overset 件数(ラベルで (n) を出すか判定。1件なら番号なし)
	KESCMNavStop() : pageUID(kInvalidUID), isOverset(kFalse), oversetOrd(0), oversetCountOnPage(0) {}
};

// 直近に巡回したストップの同定情報。index ではなく内容(ページ+種別+ページ内序数)で持つことで、リストが
// 再構築されても位置を追える。対象が消えたら KESCMFindCurrentStop が -1 を返し先頭/末尾から始め直す。
static UID    sNavPageUID    = kInvalidUID;
static bool16 sNavIsOverset  = kFalse;
static int32  sNavOversetOrd = 0;

//----------------------------------------------------------------------------------------
// 巡回する文書。比較 Start 中は Target(sDB)。未 Start でも Find Overset ON ならその走査文書(sOversetDB)。
// どちらでもなければ nil(巡回対象なし)。
//----------------------------------------------------------------------------------------
static IDataBase* KESCMNavDoc()
{
	if (KESCMDrawEventHandler::sDB != nil)
		return KESCMDrawEventHandler::sDB;
	if (KESCMDrawEventHandler::sOversetOn && KESCMDrawEventHandler::sOversetDB != nil)
		return KESCMDrawEventHandler::sOversetDB;
	return nil;
}

//----------------------------------------------------------------------------------------
// 巡回ストップ列を文書のページ順に組む。各ページで「まず変更(枠)→ 次に overset の各「+」箇所」の順
// (ユーザー指定 2026-07-24)。
//   ・変更ストップ = 比較 Start 中(navDB==sDB)かつ sEntries にあるページ。ページ中心へ。
//     ★Added/Removed・Overflow(ページ増減由来)は従来どおり含めない(2026-07-10)。
//   ・overset ストップ = Find Overset ON かつ sOversetDB==navDB。そのページに載る「+」を箇所ごとに1つ
//     (sOversetLocs の並び=走査順)。KBS 流に「+」pb 点へ。
//   比較と overset が別文書のとき(navDB==Target、overset は別文書)は overset を混ぜない(そのページ順の
//   意味が崩れるため。overset 単独時はその文書を navDB として overset だけを巡る)。
//----------------------------------------------------------------------------------------
// pageUID に載る overset「+」箇所を走査順にストップとして足す(ページ内の枝番と件数もここで振る)。
// ★通常ページ用のループとマスターページ等の追い足しの両方から呼ぶので、枝番の付け方が2箇所に
//   分かれないようここへ切り出してある。
static void KESCMAppendOversetStopsForPage(UID pageUID, std::vector<KESCMNavStop>& out)
{
	std::vector<size_t> onPage;
	for (size_t j = 0; j < KESCMDrawEventHandler::sOversetLocs.size(); ++j)
		if (KESCMDrawEventHandler::sOversetLocs[j].pageUID == pageUID)
			onPage.push_back(j);
	const int32 cnt = (int32)onPage.size();
	for (int32 k = 0; k < cnt; ++k)
	{
		const KESCMOversetLoc& loc = KESCMDrawEventHandler::sOversetLocs[onPage[k]];
		KESCMNavStop s; s.pageUID = pageUID; s.isOverset = kTrue; s.pb = loc.pb;
		s.oversetOrd = k; s.oversetCountOnPage = cnt;
		out.push_back(s);
	}
}

static void KESCMBuildStops(std::vector<KESCMNavStop>& out)
{
	out.clear();
	IDataBase* navDB = KESCMNavDoc();
	if (navDB == nil)
		return;
	const bool16 changeHere  = (KESCMDrawEventHandler::sDB == navDB);	// 変更(枠)を混ぜるのは比較 Target のときだけ
	const bool16 oversetHere = (KESCMDrawEventHandler::sOversetOn && KESCMDrawEventHandler::sOversetDB == navDB);

	std::vector<UID> flat;
	KESCMCollectPageUIDs(navDB, flat);
	for (size_t i = 0; i < flat.size(); ++i)
	{
		const UID u = flat[i];
		// 1) そのページの変更(枠)= ページ中心。
		if (changeHere && KESCMDrawEventHandler::sEntries.find(u) != KESCMDrawEventHandler::sEntries.end())
		{
			KESCMNavStop s; s.pageUID = u; s.isOverset = kFalse;
			out.push_back(s);
		}
		// 2) そのページの overset「+」箇所(走査順に1つずつ)。
		if (oversetHere)
			KESCMAppendOversetStopsForPage(u, out);
	}

	// 3) ★通常スプレッドに載っていない overset を末尾に足す(2026-08-06 ユーザー報告「マスターの
	//    オーバーセット、見つけますがボタンが押せない」の修正)。
	//    ★★KESCMCollectPageUIDs が回すのは **ISpreadList = 通常スプレッドだけ**で、マスタースプレッドは
	//    IMasterSpreadList の別管理なので上のループには一度も現れない。その結果、マスターページ上の
	//    あふれは検出できていて(サムネイルの「+」は出る)、それでもストップ列から丸ごと落ち、他に
	//    巡回対象が無ければ **Prev/Next が無効のまま**になっていた＝「見つかるのに飛べない」。
	//    ★KESCMCollectPageUIDs 自体は変えない: あれは比較のページ対応(KESCMBuildPairing)でも使う共有
	//    ヘルパで、マスターページを混ぜると**比較する対象そのものが変わる**。ここで足すのが正しい。
	//    順序は「通常ページを全部回った後」＝ページ順の意味を壊さない。
	if (oversetHere)
	{
		const std::set<UID> covered(flat.begin(), flat.end());
		std::vector<UID> extra;		// 走査順・重複なし
		for (size_t j = 0; j < KESCMDrawEventHandler::sOversetLocs.size(); ++j)
		{
			const UID pu = KESCMDrawEventHandler::sOversetLocs[j].pageUID;
			if (covered.find(pu) != covered.end())
				continue;			// 通常ページ=上のループで拾い済み
			bool16 already = kFalse;
			for (size_t e = 0; e < extra.size() && !already; ++e)
				if (extra[e] == pu)
					already = kTrue;
			if (!already)
				extra.push_back(pu);
		}
		for (size_t e = 0; e < extra.size(); ++e)
			KESCMAppendOversetStopsForPage(extra[e], out);
	}
}

// リスト内で「今の基準ストップ」の index を返す(見つからなければ -1)。
static int32 KESCMFindCurrentStop(const std::vector<KESCMNavStop>& stops)
{
	for (size_t i = 0; i < stops.size(); ++i)
	{
		if (stops[i].pageUID == sNavPageUID && stops[i].isOverset == sNavIsOverset &&
			(!stops[i].isOverset || stops[i].oversetOrd == sNavOversetOrd))
			return (int32)i;
	}
	return -1;
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
// スクロールの前に、対象のスプレッドをビューに出す。
//
// ★★itemUID は**ページでもページアイテムでもよい**。IHierarchy::GetSpreadUID は「この階層ノードの
//   スプレッド」を返す契約(IHierarchy.h:177-181)で、ページに限った話ではない。∴ ページに載っていない
//   (ペーストボード上の)フレームでも、正しいスプレッドへ切り替えてから測れる。
//   ⚠この一般性は Story Edits の行ジャンプ(2026-08-10)で必要になった。ページを渡す従来の呼び手
//   (KESCMScrollDocToItemCenter)の挙動は1つも変わらない。
//
// ★★ペーストボード点へのスクロールは「そのビューが既にその点のスプレッドを映している」ことを
//   前提にしている。別のスプレッドを見ているビューにとって、その座標は別の場所か、どこでもない。
//   ★マスタースプレッドでこれが露骨に出る: 通常スプレッドの連続したペーストボードに含まれないので、
//   スクロールをいくらしても絶対に届かず、空のペーストボードに着地する。
//   ⚠KBS が 2026-08-05 に実測で踏んだ不具合とまったく同じ形(あちらは行の locator は "PA" と正しいのに
//   クリックすると何も無い場所へ飛んだ)。手当ても同じ＝KBSJump.cpp:280 EnsureSpreadInView の移植。
//
// ★★判定は「マスターかどうか」ではなく「違うスプレッドかどうか」。Adobe 自身がそう書いている
//   (snapshot/SnapTracker.cpp:224 は ::GetUIDRef(spread) と ILayoutControlData::GetSpreadRef() を
//   比べ、違えば無条件にコマンドを出す。マスターの特例はどこにも無い)。KBS は当初これを
//   「マスターのときだけ」に絞って書いたが、ユーザー指摘で公式どおり無条件へ直した経緯がある。
//
// ★★呼んだ「後」に座標を読むこと(SnapTracker.cpp:234-235 "Re-calculate the starting point")。
//   下の KESCMScrollDocToPage は幾何を読む前にここを通す。overset の「+」点だけは例外で、
//   スキャン時に ::InnerToPasteboardMatrix で確定した**ビュー非依存**の座標なので切替後も有効
//   (再スキャンせずに使ってよいのはそのため)。
//
// コマンドは kSetSpreadCmdBoss + ILayoutCmdData(SnapTracker.cpp:390-413 が完全な実例)。
// ★KESCM は KBS と違い「その文書の全レイアウトビュー」を対象にする(Split Window・複数窓でも
//   スクロール先が揃うように。既存の KESCMScrollDocToPBPoint と同じ範囲)。
// 取れないビューは黙って飛ばす=そのビューは従来どおりスクロールだけになり、悪化はしない。
//----------------------------------------------------------------------------------------
static void KESCMEnsureSpreadInView(IDataBase* db, UID itemUID)
{
	if (db == nil || itemUID == kInvalidUID)
		return;

	InterfacePtr<IHierarchy> itemHier(db, itemUID, UseDefaultIID());
	if (itemHier == nil)
		return;
	const UID spreadUID = itemHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return;

	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);
	for (int32 i = 0; i < (int32)views.size(); ++i)
	{
		if (views[i] == nil)
			continue;
		InterfacePtr<ILayoutControlData> layout(views[i], UseDefaultIID());
		if (layout == nil)
			continue;
		if (layout->GetSpreadRef().GetUID() == spreadUID)
			continue;	// もう映している=通常のケース。いちばん安い出口

		// コマンドはビューを名指しするので、その「ビュー自身の文書」を渡す(KBS/SnapTracker と同じ)。
		IDocument* const viewDoc = layout->GetDocument();
		if (viewDoc == nil)
			continue;
		// UID は出どころの db の外では意味を持たない。同じはずだが確かめてから渡す。
		if (::GetDataBase(viewDoc) != db)
			continue;

		InterfacePtr<ICommand> setSpreadCmd(CmdUtils::CreateCommand(kSetSpreadCmdBoss));
		if (setSpreadCmd == nil)
			continue;
		InterfacePtr<ILayoutCmdData> cmdData(setSpreadCmd, UseDefaultIID());
		if (cmdData == nil)
			continue;
		cmdData->Set(::GetUIDRef(viewDoc), layout);
		setSpreadCmd->SetItemList(UIDList(db, spreadUID));
		if (CmdUtils::ProcessCommand(setSpreadCmd) != kSuccess)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// スクロールは続行。後続コマンドを巻き添えにしない
	}
}

//----------------------------------------------------------------------------------------
// 文書 db の全レイアウトビューを、pbPoint(ペーストボード座標)が画面中央に来るようスクロールする。
//   applyZoom > 0 のときは、センタリングの前に各ビューの実効ズームを applyZoom に合わせる
//   (Source を Target の拡大率に合わせる用。ズームは UI のズーム欄と同じ公式コマンド
//   kZoomToCmdBoss=MakeZoomCmd 経由。★ILayoutViewUtils::ZoomLayoutViews 直呼びは他文書ビューに
//   効かないため不可=KESCMPeek のビューポート同期と同じ理由)。既に一致していれば触らない。
//   applyZoom <= 0 ならズームは変えない(従来どおり位置だけ)。1つでもスクロールできれば kTrue。
//----------------------------------------------------------------------------------------
static bool16 KESCMScrollDocToPBPoint(IDataBase* db, const PBPMPoint& pbPoint, PMReal applyZoom = PMReal(-1.0))
{
	if (db == nil)
		return kFalse;
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
			const PMReal cur = pano->GetXScaleFactor(kTrue);
			if (abs(cur - applyZoom) > PMReal(0.0001))
			{
				InterfacePtr<ICommand> zoomCmd(Utils<ILayoutUIUtils>()->MakeZoomCmd(view, applyZoom));
				if (zoomCmd == nil || CmdUtils::ProcessCommand(zoomCmd) != kSuccess)
				{
					// ★ズームは飛び先表示の便宜で、失敗してもスクロール自体は続けてよい。ただしエラー状態を
					//   持ち越すと後続コマンドが巻き添えで失敗する([[command-sequence-rollback-on-error]])ので
					//   掃除して続行(KESCMEnsureSpreadInView の失敗時と同じ作法。2026-08-06 再点検)。
					ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				}
			}
		}

		// ★名称は新しい方(2026-08-06 ブロック10 監査で寄せた)。IPanorama.h:141-145 が旧 ScrollViewCenterTo を
		//   「An obsolete name … New code should call ScrollContentLocationToFrameCenter … this function
		//   will go away in a future release」と明記している。中身は同じ(:135-138 の inline が旧名を呼ぶ)。
		//   ★KESCMPeek.cpp は既に新名称(:1010 ほか)＝このファイルだけが取り残されていた。
		pano->ScrollContentLocationToFrameCenter(pbPoint, kTrue /*forceRedraw*/);
		any = kTrue;
	}
	return any;
}

//----------------------------------------------------------------------------------------
// 文書 db の全レイアウトビューを、itemUID の矩形中心が画面中央に来るようスクロールする。
// ★itemUID は**ページでもページアイテムでもよい**(GetItemBounds はどちらにも答える)。呼び手は2つ＝
//   Prev/Next の巡回はページを渡し(従来どおり)、Story Edits の行ジャンプはストーリーの先頭フレームを
//   渡す(2026-08-10)。
// 上の pb 版へ委譲(inner 中心 → ペーストボード変換)。
//----------------------------------------------------------------------------------------
static bool16 KESCMScrollDocToItemCenter(IDataBase* db, UID itemUID, PMReal applyZoom = PMReal(-1.0))
{
	if (db == nil || itemUID == kInvalidUID)
		return kFalse;

	// ★先にスプレッドを出す。マスタースプレッド上のページは、これが無いとスクロールでは届かない
	//   (上の KESCMEnsureSpreadInView の説明を参照)。★幾何を読むのはこの後(切替前の座標は当てにしない
	//   ＝SnapTracker.cpp:234-235 と同じ順序)。
	KESCMEnsureSpreadInView(db, itemUID);

	InterfacePtr<IGeometry> itemGeo(db, itemUID, UseDefaultIID());
	if (itemGeo == nil)
		return kFalse;	// 幾何を持たない UID=これは測れない(呼び出し側は「動かせなかった」と出す)

	// ★矩形をペーストボード座標で得るのは Facade の仕事(2026-08-06 ブロック10 監査で寄せた)。
	//   手本 snapshot/SnapTracker.cpp:610-616 が**ページに対して**まったく同じことをしている＝
	//   IGeometry を Query して nil を弾き、その ::GetUIDRef を GetItemBounds に渡す。旧実装は
	//   「GetPathBoundingBox + ::InnerToPasteboardMatrix + 自前 Transform」で同じ答えを組んでいた。
	//   ★上の nil 判定は残す: 旧実装が「ついでに担保していたこと」(この UID は本当に幾何を持つ)を
	//   Facade は担保しない。手本も同じ順序で書いている。
	//   ⚠BoundsKind は PathBounds(＝旧 GetPathBoundingBox と同じ意味)。手本は OuterStrokeBounds だが、
	//   ページに線幅は無く、フレームの線は矩形の四辺に対称に付くので**どちらでも中心の座標は変わらない**
	//   ——意味の合う方を採る。
	const PMRect box = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		::GetUIDRef(itemGeo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
	return KESCMScrollDocToPBPoint(db,
		PBPMPoint((box.Left() + box.Right()) / PMReal(2.0), (box.Top() + box.Bottom()) / PMReal(2.0)),
		applyZoom);
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
//   ★Pages パネルは「アクティブ文書」のページ一覧を表示するので、アクティブ文書が db と
//   一致する時だけ動かす(Source がアクティブのまま Next/Prev を押した場合、パネルは Source の一覧を
//   表示中=Target のページ UID を渡しても意味がないので何もしない)。
//   経路は KESCMThumbnailRefresh と同じ IPanelMgr→GetVisiblePanel(kPagesPanelWidgetID)→
//   FindWidget(kLayoutPagesSubPanelWidgetID)→IPagesSubPanelController。ScrollPanelToSpread は
//   ヘッダー注記により page UID をそのまま渡してよい(「spread or page uid」)。
//----------------------------------------------------------------------------------------
static void KESCMScrollPagesPanelToPage(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return;

	// アクティブ文書のページ一覧を表示中か(違えば触らない)。
	// ★db は KESCMActiveDocDB()(=IActiveContext::GetContextDocument)で引く(2026-08-06 ブロック9 監査 A-1)。
	//   「Pages パネルが今どの文書を見せているか」は KESCMPageMapReadSelection と同じ問いなので、
	//   同じ口で聞く([[one-question-one-place]])。旧実装の GetFrontDocument() は契約が
	//   「frontmost *layout* presentation の文書」(ILayoutUIUtils.h:95-98)で、アクティブ文書と食い違い得る。
	if (KESCMActiveDocDB() != db)
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
	// ★マスターページはレイアウト側サブパネル(kLayoutPagesSubPanelWidgetID)の管轄外なので渡さない
	//   (2026-08-06 追補。マスター overset へのジャンプ自体はスプレッド切替で成立し、パネル連動だけ
	//   諦める)。通常ページかどうかは IPageList への所属(GetPageIndex>=0)で判定する(IPageList.h:97-104)。
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil || pageList->GetPageIndex(pageUID) < 0)
		return;
	InterfacePtr<IPagesSubPanelController> ctrl(subView, UseDefaultIID());
	if (ctrl != nil)
		ctrl->ScrollPanelToSpread(UIDRef(db, pageUID));
}

//----------------------------------------------------------------------------------------
// 変更セル数 changed / ページ全体のセル数 total から、表示用の割合文字列を作る。
//   1% 以上            → 整数     例 "12%"
//   0.05% 以上 1% 未満 → 小数1桁  例 "0.4%"
//   0.05% 未満         → "<0.1%"  (小数1桁に丸めると "0.0%" になり「変更なし」と誤読されるため)
// total<=0 / changed<=0 なら空文字列を返す(呼び出し側は何も足さない)。
// ★小数点は自前で "." を足す: PMString の実数書式はロケールで小数点が "," になり得るため、
//   千分率を整数演算で出してから桁を組み、表記を固定する(ロケール差の入り込む余地を作らない)。
//----------------------------------------------------------------------------------------
static PMString KESCMFormatChangeRatio(int32 changed, int32 total)
{
	PMString out; out.SetTranslatable(kFalse);
	if (total <= 0 || changed <= 0)
		return out;

	// 千分率(0.1% 単位・四捨五入)。changed <= total なので 1000 以下。int64 で計算するので桁あふれしない。
	const int32 permille = (int32)(((int64)changed * 1000 + total / 2) / total);

	if (permille >= 10)			// 1% 以上: 整数の % を別途四捨五入して出す
		out.AppendNumber((int32)(((int64)changed * 100 + total / 2) / total));
	else if (permille >= 1)		// 0.05% 以上 1% 未満: 小数1桁("0" + "." + 1桁)
	{
		out.AppendNumber(permille / 10);
		out.Append(".");
		out.AppendNumber(permille % 10);
	}
	else						// 0.05% 未満: 丸めると 0 になるので下限表記
		out.Append("<0.1");

	out.Append("%");
	return out;
}

//----------------------------------------------------------------------------------------
// 飛んだ先のラベルを組む(パネルのメッセージ欄に出す)。★2026-07-27 に "P1" 形式から改めた(ユーザー指定)。
//   変更(枠)           = "Page: <番号>, Change <割合>" 例: Page: 1, Change 12% / Page: 4, Change 0.4%
//   overset(1件のページ)  = "Page: <番号> Overset"      例: Page: 1 Overset
//   overset(複数のページ) = "Page: <番号> (n) Overset"  例: Page: 1 (1) Overset / Page: 1 (2) Overset  (n=1始まり)
// ★2026-07-29 に割合の区切りを半角スペースから ", Change " へ変更(ユーザー指定)＝何の % なのか読んで分かる
//   ようにするため。overset 側は既に "Overset" の語が付いているので従来どおりスペース区切りのまま。
// (n)・Overset は半角スペース1つ区切り。ページ番号は IPageList::GetPageString
// (セクション込み・現在の表示番号)。
// ★割合は変更ストップにだけ付ける(overset は「あふれ」であって変更量とは無関係)。エントリが引けない等で
//   値が作れないときは付けない=従来どおりのラベル。仕様は docs/ai-notes/kescm-change-ratio.md。
//----------------------------------------------------------------------------------------
static PMString KESCMStopLabel(IDataBase* db, const KESCMNavStop& stop)
{
	PMString label; label.SetTranslatable(kFalse);
	label.Append("Page: ");

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	PMString numStr; numStr.SetTranslatable(kFalse);
	if (pageList != nil)
		pageList->GetPageString(stop.pageUID, &numStr, kTrue, kFalse, kDefaultPageType, kTrue, kFalse);
	if (numStr.NumUTF16TextChars() > 0)
		label.Append(numStr);
	else if (stop.isOverset)
		label.Append("Master");	// マスターページの overset 用の受け皿(2026-08-06 追補)。「?」より事情が伝わる語。
								// ⚠実機ではここへ来ない: GetPageString はマスターページにもプレフィックス
								//   を返すので(実測 2026-08-06、日本語版で "Page: A Overset" と出た)、上の
								//   numStr が空にならない。★残してあるのは保険であって、期待値ではない
								//   ——テストで "Master" を期待して書かないこと。
	else
		label.Append("?");	// 番号が取れないページ(通常は起きない)

	if (stop.isOverset)
	{
		if (stop.oversetCountOnPage > 1)	// 同ページに複数あるときだけ (n) を付ける(1始まり)
		{
			label.Append(" (");			// 番号と枝番が詰まって読みにくいので空ける("Page: 1 (2)")
			label.AppendNumber(stop.oversetOrd + 1);
			label.Append(")");
		}
		label.Append(" Overset");	// 半角スペース + Overset
	}
	else
	{
		// 変更ストップ: そのページの変更の割合を足す(例 "Page: 3, Change 12%")。分子=比較時に数えた変化セル数、
		// 分母=そのページの低解像度セル数(= w * h。エントリの画像寸法がそのまま分母)。
		std::map<UID, KESCMOverlayEntry*>::const_iterator it = KESCMDrawEventHandler::sEntries.find(stop.pageUID);
		if (it != KESCMDrawEventHandler::sEntries.end() && it->second != nil)
		{
			const PMString ratio = KESCMFormatChangeRatio(it->second->changedCells, it->second->w * it->second->h);
			if (ratio.NumUTF16TextChars() > 0)
			{
				label.Append(", Change ");	// 何の % なのか分かるようにラベルを付ける("Page: 3, Change 12%")
				label.Append(ratio);
			}
		}
	}
	return label;
}

//----------------------------------------------------------------------------------------
// Target 側の移動が済んだ後、周りのビューを追随させる(Pages パネルと Source 窓)。
//
// ★呼び手は Prev/Next の巡回(KESCMGoto)だけ。⚠**Story Edits の行ジャンプはここを通らない**
//   ---- あちらは Target しか動かさない(2026-08-10 ユーザー決定。Source も見たいときは
//   「Sync Layout Views」を使う、という切り分け)。関数として分けたままにしてあるのは、ズーム合わせ・
//   Sync ON のときの除外・対応表でのページ解決の3つが「連れて行くとはどういうことか」の答えで、
//   巡回本体に混ぜると読めなくなるため。
// ★pageUID が kInvalidUID(ペーストボード上のフレーム)なら寄せる先が決められないので、Source も
//   Pages パネルも動かさない。Target 側の移動は呼び手が済ませてあるので、それだけが成立する。
//----------------------------------------------------------------------------------------
static void KESCMSyncCompanionViews(IDataBase* navDB, UID pageUID)
{
	if (navDB == nil || pageUID == kInvalidUID)
		return;

	// Pages パネルも対象ページへ連動スクロール(前面文書が navDB のときだけ。ベストエフォート)。
	KESCMScrollPagesPanelToPage(navDB, pageUID);

	// Source 側も対応ページへ連動スクロール(比較 Start 中のみ=sSrcDB 非nil。背面のまま位置だけ)。
	// ページの追加/削除でズレていても対応表で正しい相手へ、相手が無い Added/Overflow は近傍へ寄せる。
	// ★Source の拡大率も Target に合わせる。overset ストップでも「そのページ」の対応 Source ページへ寄せる。
	// ベストエフォート: Source ビューが無い/相手が引けなくても navDB 側の移動は成立させる。
	IDataBase* sourceDB = KESCMDrawEventHandler::sSrcDB;
	if (sourceDB != nil && sourceDB != navDB)
	{
		const UID srcPage = KESCMSourcePageForTarget(navDB, sourceDB, pageUID);
		if (srcPage != kInvalidUID)
		{
			// ★Sync layout views が ON のときは Source の「ビュー」スクロールをしない。理由: Sync オブザーバ
			//   (KESCMPeek.cpp)が navDB(Target)のスクロールをページオフセット込みで Source へ自動ミラーする。
			//   ここで Source を手動スクロールすると、その変化が Sync により Target へ逆ミラーされ、overset の
			//   「+」スクロールがページ中心に打ち消される(2026-07-24 ユーザー発見: sync OFF なら overset に飛ぶ)。
			//   sync ON では Target のスクロール(呼び手がやった分)を Sync が Source へ伝える。
			//   Pages パネルの連動は Sync の対象外なので、そちらは sync の有無に関わらず行う。
			if (!KESCMGetLayoutSync())
			{
				const PMReal targetZoom = KESCMReadDocZoom(navDB);	// 実効ズーム(<=0 ならズームは変えない)
				KESCMScrollDocToItemCenter(sourceDB, srcPage, targetZoom);
			}
			// Source が前面の場合、Pages パネルは Source の一覧を表示しているので、そちらの対応ページへ
			// 連動スクロール(ヘルパー内の前面一致ガードにより、navDB 前面ならこの呼び出しは何もしない)。
			KESCMScrollPagesPanelToPage(sourceDB, srcPage);
		}
	}
}

//----------------------------------------------------------------------------------------
// 文書 db のビューを、storyUID の「**一番最初**」が画面中央に来るようスクロールする。
//
// ★★飛び先はフレームの中心ではなく**本文の書き出し**(ユーザー決定 2026-08-10)。背の高いフレームでは
//   中心は本文の途中で、読みたいのは書き出しの方。点の算出は KESCMStoryStartPoint
//   (＝overset の「+」を出す KESCMLastPlacedOutport の鏡像。同じ3つの座標系を同じ順に通る)。
// ★点へ寄せる手順も overset とまったく同じ＝**先にスプレッドを出してから** pb 点へ。pb 点への
//   スクロールは「そのビューが既にそのスプレッドを映している」ことが前提だから(上の
//   KESCMEnsureSpreadInView の説明)。
// ★まだ1度も組まれていない等で点が採れなければ、フレームの中心へ落とす(2026-08-10 以前の動き)。
//   outFrame には実際に着地したフレームを返す ---- Pages パネルの連動がページを引くのに使う。
//----------------------------------------------------------------------------------------
static bool16 KESCMScrollDocToStoryStart(IDataBase* db, UID storyUID, UID fallbackFrameUID,
	UID& outFrame, PMReal applyZoom = PMReal(-1.0))
{
	UID startFrame = kInvalidUID;
	PBPMPoint startPb;
	if (KESCMStoryStartPoint(db, storyUID, startFrame, startPb))
	{
		outFrame = startFrame;
		KESCMEnsureSpreadInView(db, startFrame);
		return KESCMScrollDocToPBPoint(db, startPb, applyZoom);
	}

	outFrame = fallbackFrameUID;
	return KESCMScrollDocToItemCenter(db, fallbackFrameUID, applyZoom);
}

//----------------------------------------------------------------------------------------
// 巡回本体(dir=+1 で次、-1 で前)。端は折り返す。位置表示「3/12」は Prev/Next 間のウィジェットへ、
// 飛んだページラベル「Page: 1, Change 12%」等はメッセージ欄へ。ストップは「変更(枠)=ページ中心」または
// 「overset=「+」pb 点(KBS 流)」。
//----------------------------------------------------------------------------------------
static void KESCMGoto(int32 dir)
{
	IDataBase* navDB = KESCMNavDoc();
	if (navDB == nil)
	{
		PMString s("Start a comparison or run Find Overset first."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		return;
	}

	std::vector<KESCMNavStop> stops;
	KESCMBuildStops(stops);
	if (stops.empty())
	{
		PMString s("Nothing to review."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		KESCMRefreshNavPosition();	// 巡回対象なし: 位置は "/"・Prev/Next は無効化(通常はボタン無効で来ない)
		return;
	}

	// 現在位置を内容で探す。見つからなければ(初回/前回ストップが消えた)、次=先頭・前=末尾から。
	int32 cur = KESCMFindCurrentStop(stops);
	int32 next;
	if (cur < 0)
		next = (dir > 0) ? 0 : (int32)stops.size() - 1;
	else
	{
		next = cur + dir;
		if (next < 0)                        next = (int32)stops.size() - 1;	// 先頭で「前」→末尾へ折り返し
		else if (next >= (int32)stops.size()) next = 0;						// 末尾で「次」→先頭へ折り返し
	}
	const KESCMNavStop& stop = stops[next];

	// overset は「+」点へ(KBS 流)、変更はページ中心へスクロール。
	// ★基準ストップ(sNav*)の更新はスクロール成功後(2026-07-25 監査で移動): 失敗時に基準だけ先へ進むと、
	//   位置表示「k/N」が古いまま・次回の巡回起点も移動済み、という不整合が残るため。
	// ★overset 経路は pb 点へ直接スクロールするので、ここでスプレッドを出しておく(ページ中心経路は
	//   KESCMScrollDocToPage の中で同じことをしている)。これが無いとマスタースプレッド上の
	//   あふれに飛べない=空のペーストボードに着地する(2026-08-06 ユーザー指摘。KBS と同じ手当て)。
	if (stop.isOverset)
		KESCMEnsureSpreadInView(navDB, stop.pageUID);
	const bool16 ok = stop.isOverset ? KESCMScrollDocToPBPoint(navDB, stop.pb)
	                                 : KESCMScrollDocToItemCenter(navDB, stop.pageUID);
	if (!ok)
	{
		PMString s("Could not scroll."); s.SetTranslatable(kFalse);
		KESCMSetStatus(s);
		KESCMRefreshNavPosition();	// 表示とボタン状態は「移動しなかった現状」で作り直す
		return;
	}
	sNavPageUID    = stop.pageUID;
	sNavIsOverset  = stop.isOverset;
	sNavOversetOrd = stop.oversetOrd;

	// 飛んだ先をメッセージ欄へ(例 "Page: 1, Change 12%" / "Page: 1 Overset" / "Page: 1 (2) Overset"
	// =KESCMStopLabel 参照。2026-08-06 現行化: 旧 "P1" 表記は 2026-07-27 に廃止済み)。
	// 位置 k/N は別ウィジェット(下の RefreshNavPosition)。
	KESCMSetStatus(KESCMStopLabel(navDB, stop));

	// 周りのビュー(Pages パネル・Source 窓)を追随させる。★Story Edits の行ジャンプと同じ関数を通る。
	KESCMSyncCompanionViews(navDB, stop.pageUID);

	// 現在位置は Prev/Next の間の専用ウィジェット(KESCL 風「3/12」)へ。基準ストップは上で更新済みなので、
	// 共通関数で今のストップ列から「k/N」を作り直す(値の組み立てとボタン有効/無効を1箇所に集約)。
	KESCMRefreshNavPosition();
}

//========================================================================================
// KESCMGotoNextChange / KESCMGotoPrevChange(KESCMChangeNav.h で宣言)
//========================================================================================
void KESCMGotoNextChange() { KESCMGoto(+1); }
void KESCMGotoPrevChange() { KESCMGoto(-1); }

//========================================================================================
// KESCMGotoStoryFrame(KESCMChangeNav.h で宣言)
//========================================================================================
bool16 KESCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID)
{
	// ストーリーの書き出しへ(フレームの中心ではない。上の KESCMScrollDocToStoryStart 参照)。
	UID landedFrame = kInvalidUID;
	if (!KESCMScrollDocToStoryStart(db, storyUID, frameUID, landedFrame))
		return kFalse;

	// Pages パネルも、**実際に着地したフレーム**のページへ(ページに載っていないなら中で何もしない)。
	// ★行が覚えている pageUID ではなく着地側から引く: 先頭フレームにパーセルが1つも配置されていない
	//   ときは、着地するのは次のフレーム＝別のページのことがある。表示と実際がずれない方を採る。
	KESCMScrollPagesPanelToPage(db, (landedFrame != kInvalidUID) ? KESCMFramePageUID(db, landedFrame) : pageUID);

	// ***** Source 側も連れて行く。ただし合わせるのは「ページ」ではなく「ストーリー」。*****
	//
	// ★★ここが Prev/Next(KESCMSyncCompanionViews)と違うところ。あちらが指しているのはページなので
	//   対応表でページを引けば足りるが、この行が指しているのは**ストーリー**で、**同じストーリーが
	//   2つの版で違う場所にあることがある**(2026-08-10 ユーザー指摘。レイアウトが変われば当然そうなる)。
	//   ∴ Source でも同じ story UID の先頭フレームを引き、それを中心に出す ---- ページ番号を経由すると、
	//   まさにこの機能が見せたい「動いたストーリー」を見失う。
	// ★UID で引き当てられる根拠は、この機能全体が乗っているのと同じ前提＝**別名保存では story UID が
	//   引き継がれる**(KESCMStoryStamp.h:36-38 に実測済み)。Source に無いストーリー(=Added の行)は
	//   kInvalidUID が返るので、そのときは Target だけが動く。
	IDataBase* sourceDB = KESCMDrawEventHandler::sSrcDB;
	if (sourceDB != nil && sourceDB != db && storyUID != kInvalidUID)
	{
		UID srcFrame = KESCMStoryFirstFrameUID(sourceDB, storyUID);
		if (srcFrame != kInvalidUID)
		{
			// ★Sync layout views が ON のときは Source を手動で動かさない ---- Sync のオブザーバ
			//   (KESCMPeek.cpp)が、すぐ上でやった Target のスクロールを Source へ既にミラーしている。
			//   ここでも動かすと二重になり、しかもその変化が Sync 経由で Target へ逆ミラーされて、
			//   出したはずのフレームが押し戻される(2026-07-24 に overset で実際に起きた形)。
			//   ⚠ ON のとき Source が映すのは「Target と同じ座標」なので、ストーリーの位置がずれて
			//   いれば厳密には別の場所になる。それでも Sync の約束(2つの窓を同じ座標で並べる)の方が
			//   ユーザーの明示的な指定なので、そちらを優先する。KESCMSyncCompanionViews と同じ判断。
			if (!KESCMGetLayoutSync())
			{
				// ★Target とまったく同じ寄せ方＝ストーリーの書き出しへ(ズームも Target に合わせる)。
				UID srcLanded = kInvalidUID;
				KESCMScrollDocToStoryStart(sourceDB, storyUID, srcFrame, srcLanded, KESCMReadDocZoom(db));
				if (srcLanded != kInvalidUID)
					srcFrame = srcLanded;
			}

			// Pages パネルは Sync の対象外なので ON/OFF に関わらず追随させる(Source が前面のときだけ
			// 中で効く。Target が前面なら上の呼び出しの方が効いている)。
			KESCMScrollPagesPanelToPage(sourceDB, KESCMFramePageUID(sourceDB, srcFrame));
		}
	}

	// ★巡回の基準点(sNavPageUID 等)は動かさない。行ジャンプは Prev/Next とは別の動線で、ここで基準を
	//   書き換えると「Next を押したら一覧で飛んだ場所の次から始まる」という、どちらの機能の説明にも
	//   出てこない挙動になる(2026-08-10 の設計判断)。
	return kTrue;
}

// 巡回の基準点を忘れる(KESCMChangeNav.h)。次回の Next/Prev はリストの先頭/末尾から始まる。
// ★表示更新はしない(基準点を落とすだけ): これは比較の総入れ替え(Start)の途中でも呼ばれるため、
//   位置表示は呼び出し側(KESCMDoMarkChangesDoc 末尾 / KESCMDoClearMarks)が確定後に
//   KESCMRefreshNavPosition で一括更新する。
void KESCMResetNav() { sNavPageUID = kInvalidUID; sNavIsOverset = kFalse; sNavOversetOrd = 0; }

// KESCMChangeNav.h 参照。今のストップ列(変更+overset 箇所)＋基準ストップから Prev/Next 間の位置表示を
// 作り直し、Prev/Next ボタンの有効/無効もあわせて更新する(値組み立てとボタン状態を1箇所に集約=KESCL の
// UpdateNavWidgets と同じ発想)。表示規則: 未Start&overset無し→空 / 対象0件→"/" / N件→"k/N"。
void KESCMRefreshNavPosition()
{
	PMString text; text.SetTranslatable(kFalse);
	bool16 navEnabled = kFalse;

	IDataBase* navDB = KESCMNavDoc();	// 比較 Start 中 or Find Overset ON のとき非nil
	if (navDB != nil)
	{
		std::vector<KESCMNavStop> stops;
		KESCMBuildStops(stops);
		if (stops.empty())
		{
			text.Append("/");	// 対象0件: 巡回対象なし → "/"・ボタン無効(ユーザー指定 2026-07-15)
		}
		else
		{
			// 基準ストップの現在位置(1始まり)。まだ巡回していない(列内に無い)ときは先頭扱いで "1/N"。
			const int32 cur = KESCMFindCurrentStop(stops);
			const int32 shown = (cur < 0) ? 1 : (cur + 1);
			text.AppendNumber(shown);
			text.Append("/");
			text.AppendNumber((int32)stops.size());
			navEnabled = kTrue;	// 巡回対象あり → Prev/Next 有効
		}
	}
	// 未 Start かつ overset 無し(navDB==nil): text は空・navEnabled=false(位置欄クリア・ボタン無効)

	KESCMSetNavPosition(text, navEnabled);
}

// KESCMChangeNav.cpp 終わり。
