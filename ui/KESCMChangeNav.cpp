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
#include "ErrorUtils.h"			// PMSetGlobalErrorCode / GlobalErrorStatePreserver
								//   (切替やズームに失敗しても後続コマンドを巻き添えにせず、外へも出さない)
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

#include "KESCMUIShared.h"	// panel / status line / nav readout / tool button (split from KESCMCore.h on 2026-08-13)
#include "KESCMViewSync.h"			// KESCMGetLayoutSync(同期 ON なら連動スクロールを任せる。2026-08-13 に KESCMCore.h から移動)
#include "IKESCMCompareFacade.h"		// GetActiveDocDB(2026-08-14 Task 16 で Facade 経由へ)
#include "IKESCMMarkData.h"			// 比較結果の読み取り(変更ページ・変化セル数・overset 箇所)。2026-08-13 Task 12
#include "KESCMViewLookup.h"		// KESCMQueryPanorama(同 Task 12 に KESCMDrawEventHandler.h から移動)
#include "KESCMOversetScan.h"		// KESCMOversetLoc(overset「+」箇所の位置)
                                    // ＋ GetPagePairing(Source 側連動スクロールの対応表。2026-08-13 Task 13 で
                                    //   KESCMPageMap.h から IKESCMMarkData 経由へ)
#include "KESCMThumbnailRefresh.h"	// KESCMGetVisiblePagesPanel(表示中 Pages パネル取得の共有ヘルパ)
#include "IKESCMStoryEditsFacade.h"	// GetFirstFrameUID(Source 側で「同じストーリー」の先頭フレームを引く)／
									// GetStoryStartPoint(本文の書き出し位置)。2026-08-13 Task 14 で Facade 経由へ
#include "KESCMStoryNav.h"			// Story Changes モードのストップ列(=一覧の葉)と、その飛び方。2026-08-24
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

	// ★★★Story Changes モードのストップ(2026-08-24)。上の2種が指すのは**ページ**だが、こちらが
	//   指すのは **Story Edits 一覧の葉**＝1つの編集(または子を持たない行そのもの)。
	//   ⚠**pageUID は使わない**(kInvalidUID のまま)＝どのページに着くかは飛んでみるまで決まらない
	//     (連結ストーリーの後ろの編集は、そのストーリーの先頭フレームのページではない
	//     ＝KESCMStoryJumpToChange が GetStoryFrameAt で解決する)。∴ Story ストップは
	//     ページを前提にした処理 ---- 隠しページ判定・KESCMStopLabel・KESCMSyncCompanionViews ----
	//     を**1つも通さない**(下の KESCMGoto の分岐)。
	bool16		isStory;
	int32		storyRow;			// KESCMStoryList の行番号(Facade に渡す語彙)
	int32		storyChange;		// その行の何番目の変更か。**-1 = 子を持たない行そのもの**
	UID			storyUID;			// 同定用＝「どのストーリーか」(下の sNavStoryUID 参照)

	KESCMNavStop() : pageUID(kInvalidUID), isOverset(kFalse), oversetOrd(0), oversetCountOnPage(0),
					 isStory(kFalse), storyRow(-1), storyChange(-1), storyUID(kInvalidUID) {}
};

// 直近に巡回したストップの同定情報。index ではなく内容(ページ+種別+ページ内序数)で持つことで、リストが
// 再構築されても位置を追える。対象が消えたら KESCMFindCurrentStop が -1 を返し先頭/末尾から始め直す。
static UID    sNavPageUID    = kInvalidUID;
static bool16 sNavIsOverset  = kFalse;
static int32  sNavOversetOrd = 0;

// Story ストップの基準点(2026-08-24)。**行番号ではなくストーリーで覚える**のは上とまったく同じ理由＝
// 「Refresh Story Comparison」はその行の子を作り直し、次の比較は一覧ごと作り直す。ストーリーで覚えて
// おけば、子が増減しても同じ編集を指し続け、その編集が消えていれば見つからず先頭/末尾から始まる。
static bool16 sNavIsStory     = kFalse;
static UID    sNavStoryUID    = kInvalidUID;
static int32  sNavStoryRow    = -1;
static int32  sNavStoryChange = -1;

// ★★「入口に立っている」＝上の基準点が指すストップへ**まだ行っていない**(2026-08-24)。
//   子のある親行を選んだときだけ立つ ---- あの行は巡回対象では無い(KESCMStoryNav.h)ので、
//   代わりに**その最初の子の入口**に立たせる。次の Next はそのストップ「へ」行き(進めない)、
//   Prev は1つ前へ行く。⇒ **Start 直後に「1/N」と出て、Next で1番目へ行く**のと同じ規則で、
//   実際そちらも「基準点がまだ無い(cur<0)」という同じ形で表現されている。
static bool16 sNavStoryAtEntry = kFalse;

//----------------------------------------------------------------------------------------
// 巡回する文書。比較 Start 中は Target(sDB)。未 Start でも Find Overset ON ならその走査文書(sOversetDB)。
// どちらでもなければ nil(巡回対象なし)。
//----------------------------------------------------------------------------------------
static IDataBase* KESCMNavDoc()
{
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());
	if (marks->GetMarkedTargetDB() != nil)
		return marks->GetMarkedTargetDB();
	if (marks->GetOversetOn() && marks->GetOversetDB() != nil)
		return marks->GetOversetDB();
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
// ★locs は呼び手が1回だけ引いた「今の overset 箇所」の写し(2026-08-13 Task 12)。境界の向こうから
//   毎ページ引き直すと同じものを何度もコピーすることになるので、走査の間ずっと使い回す。
static void KESCMAppendOversetStopsForPage(UID pageUID, const std::vector<KESCMOversetLoc>& locs,
										   std::vector<KESCMNavStop>& out)
{
	std::vector<size_t> onPage;
	for (size_t j = 0; j < locs.size(); ++j)
		if (locs[j].pageUID == pageUID)
			onPage.push_back(j);
	const int32 cnt = (int32)onPage.size();
	for (int32 k = 0; k < cnt; ++k)
	{
		const KESCMOversetLoc& loc = locs[onPage[k]];
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
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());

	// ★★★Story Changes モードには「変更(枠)」のストップが1つも無い ---- ページを1枚もラスタ化しない
	//   ので sEntries が空のまま(KESCMCore.cpp の `toRaster.clear()`)。代わりに巡るのは
	//   **Story Edits 一覧の葉**(2026-08-24。規則は KESCMStoryNav.h)。
	//   ⚠`== kKESCMModeStory` と書くのは意図＝`!= kKESCMModePixel` と書くと、将来「枠を作らない3つ目の
	//     モード」が増えたときに**黙ってこちらへ流れ込む**(KESCMPeek.cpp:175 が同じ用心を書いている)。
	const bool16 storyMode   = (Utils<IKESCMCompareFacade>()->GetCompareMode() == kKESCMModeStory);
	const bool16 changeHere  = (!storyMode && marks->GetMarkedTargetDB() == navDB);	// 変更(枠)を混ぜるのは比較 Target のときだけ
	const bool16 oversetHere = (marks->GetOversetOn() && marks->GetOversetDB() == navDB);

	// overset 箇所は3か所から引くので、ここで1回だけ写しを取る(下の3つのブロックが使い回す)。
	std::vector<KESCMOversetLoc> locs;
	if (oversetHere)
		marks->GetOversetLocations(locs);

	// 0) ★Story の葉を**先に**並べる(2026-08-24)。
	//    ⚠**ページ順に混ぜない。** 一覧は「ページ順 → 削除された行だけ後ろへ」という独自の並びを持って
	//      いる(KESCMStoryList::Build)ので、ページ単位で割り込ませると**画面に見えている順と Prev/Next の
	//      順が食い違う**。押す人は一覧を見ながら押すのだから、そちらに合わせる。
	//    ★あふれ「+」は従来どおりこの後ろに続く＝**Story モードでも Find Overset は使えるまま**。
	//      OFF なら「k/N」の N は一覧の編集の数そのものになる。
	//    ★条件が `marks->GetMarkedTargetDB() == navDB` なのは changeHere と同じ問い＝一覧は比較が作った
	//      ものなので、巡回文書が比較の Target のときだけ意味を持つ(あふれ単独走査の文書では出さない)。
	if (storyMode && marks->GetMarkedTargetDB() == navDB)
	{
		std::vector<KESCMStoryNavStop> storyStops;
		KESCMBuildStoryNavStops(storyStops);
		for (size_t i = 0; i < storyStops.size(); ++i)
		{
			KESCMNavStop s;
			s.isStory     = kTrue;
			s.storyRow    = storyStops[i].fRow;
			s.storyChange = storyStops[i].fChange;
			s.storyUID    = storyStops[i].fStoryUID;
			out.push_back(s);
		}
	}

	// ★marks は上で InterfacePtr に引いてあるので、そのまま使う(Utils.h:74-80。2026-08-17 の
	//   API 監査 B-U8＝同じ関数の中で InterfacePtr と直呼びが混在していた)。
	std::vector<UID> flat;
	marks->GetAllPageUIDs(navDB, flat);
	for (size_t i = 0; i < flat.size(); ++i)
	{
		const UID u = flat[i];
		// 1) そのページの変更(枠)= ページ中心。
		if (changeHere && marks->HasEntryForPage(u))
		{
			KESCMNavStop s; s.pageUID = u; s.isOverset = kFalse;
			out.push_back(s);
		}
		// 2) そのページの overset「+」箇所(走査順に1つずつ)。
		if (oversetHere)
			KESCMAppendOversetStopsForPage(u, locs, out);
	}

	// ★★KESCMCollectPageUIDs が返すのは **文書の通常ページだけ**で、マスタースプレッドは
	//    IMasterSpreadList の別管理なので上のループには一度も現れない。以下でマスターを追い足す。
	//    (2026-08-16 に中身が ISpreadList の2重ループから **IPageList** へ移ったが、マスターを
	//     含まないのは契約＝`IPageList.h:81` "does not include master pages"。前提は不変。)
	//    ★KESCMCollectPageUIDs 自体は変えない: あれは比較のページ対応(KESCMBuildPairing)でも使う共有
	//    ヘルパで、マスターページを混ぜると**比較する対象そのものが変わる**。ここで足すのが正しい。
	//    順序は「通常ページを全部回った後」＝ページ順の意味を壊さない。
	std::set<UID> covered(flat.begin(), flat.end());

	// 3) ★マスタースプレッドのページ(2026-08-06=overset / 2026-08-11=変更枠)。
	//    overset は 2026-08-06 のユーザー報告「マスターのオーバーセット、見つけますがボタンが押せない」
	//    の修正で足した(検出はできていてサムネイルの「+」も出るのに、ストップ列から丸ごと落ちていた)。
	//    ★変更(枠)を 2026-08-11 に同じ場所へ足した: マスターを比較対象に加えた結果、同じ形の
	//    「枠は出るのに Prev/Next で飛べない」が起きうるため。
	//    ★通常ページのループと同じく「1ページにつき [枠 → overset...]」の順に足す(あふれだけを別に
	//    まとめると、マスターが複数あるとき枠とあふれが離れて並ぶ)。
	if (changeHere || oversetHere)
	{
		std::vector<UID> masters;
		marks->GetMasterPageUIDs(navDB, masters);	// ★上で引いた InterfacePtr を使う(Utils.h:74-80)
		for (size_t i = 0; i < masters.size(); ++i)
		{
			const UID u = masters[i];
			if (covered.find(u) != covered.end())
				continue;			// 上のループで拾い済み(マスターがそこに出ることは無いが、二重に足さない)
			covered.insert(u);
			if (changeHere && marks->HasEntryForPage(u))
			{
				KESCMNavStop s; s.pageUID = u; s.isOverset = kFalse;
				out.push_back(s);
			}
			if (oversetHere)
				KESCMAppendOversetStopsForPage(u, locs, out);
		}
	}

	// 4) それでも残る overset(通常ページにもマスターページにも属さないページ)を末尾に足す。
	//    ★2026-08-11 にマスターを 3) で拾うようになった後も残す安全網: ここが空になる保証は
	//    「ページ UID は通常スプレッドかマスタースプレッドのどちらかに属する」という前提に頼るが、
	//    その前提はこちらのコードでは担保できない。落ちるより出す。
	if (oversetHere)
	{
		std::vector<UID> extra;		// 走査順・重複なし
		for (size_t j = 0; j < locs.size(); ++j)
		{
			const UID pu = locs[j].pageUID;
			if (covered.find(pu) != covered.end())
				continue;			// 通常ページ/マスターページ=上で拾い済み
			bool16 already = kFalse;
			for (size_t e = 0; e < extra.size() && !already; ++e)
				if (extra[e] == pu)
					already = kTrue;
			if (!already)
				extra.push_back(pu);
		}
		for (size_t e = 0; e < extra.size(); ++e)
			KESCMAppendOversetStopsForPage(extra[e], locs, out);
	}
}

// リスト内で「今の基準ストップ」の index を返す(見つからなければ -1)。
static int32 KESCMFindCurrentStop(const std::vector<KESCMNavStop>& stops)
{
	for (size_t i = 0; i < stops.size(); ++i)
	{
		// ★★種別が違えば見るまでもなく別物。⚠**ここで分けないとページ側の条件が Story ストップにも
		//   当たる**＝Story ストップの pageUID は kInvalidUID のままなので、「ページの取れないストップ」
		//   どうしが取り違う(基準点が初期値のときはどちらも kInvalidUID)。
		if (stops[i].isStory != sNavIsStory)
			continue;

		if (stops[i].isStory)
		{
			// ★**ストーリー・行・編集の3つが揃って初めて同じストップ**(2026-08-25 の再検査で
			//   storyRow を足した)。
			// ⚠★★★**足した理由は誤っていた。同日中に裏を取って撤回した。** 「版どうしでない2文書では
			//   Target 側の行と Source 側の削除行の UID が衝突しうる」と書いたが、**衝突は起きない** ----
			//   `KESCMStoryStamp.h:110-111` が Added を「**Source 側にこの UID のストーリーが無い**」、
			//   Removed を「**Target 側に無い**」と定義しており、**同じ UID が両側にあれば必ずペアになる**
			//   ＝どちらの行にもならない。⇒ **一覧の中で同じ UID が2行に現れることは無い。**
			// ★**それでも3つ見るままにしてある**＝(a)UID の一意性は上のペアリングの実装に依存しており、
			//   ここはその契約を知らずに済むほうがよい (b)行番号は Facade に渡す語彙そのもので、
			//   どのみち持っている (c)**どれかがずれたら「見つからない」＝先頭から始まる**＝安全側に倒れる。
			// ★**行番号を混ぜても壊れない**＝行の並びが変わるのは**新しい比較のとき**だけで、そのときは
			//   KESCMResetNav が基準点ごと捨てる。「Refresh Story Comparison」は1行の子を作り直すだけで
			//   並びを変えない(IKESCMStoryEditsFacade::RefreshRow が明記)。
			if (stops[i].storyUID == sNavStoryUID && stops[i].storyRow == sNavStoryRow &&
				stops[i].storyChange == sNavStoryChange)
				return (int32)i;
			continue;
		}

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
//   クリックすると何も無い場所へ飛んだ)。手当ても同じ＝**KBS の `KBSJump.cpp` の `EnsureSpreadInView`** の移植。
//   ⚠2026-08-19(不具合再検査 B-U8)訂正＝ここは "KBSJump.cpp:280" と行番号で引いていた。**書いた日
//     (KBS の初出コミット `6ccdf1a`)は :280 ちょうどで当たっていた**が、今は :348 へ動いている(+68)。
//     ★**他リポジトリを指す参照は sha でも救えない**(こちらの git では検算できない)⇒**関数名で引く**。
//
// ★★判定は「マスターかどうか」ではなく「違うスプレッドかどうか」。Adobe 自身がそう書いている
//   (snapshot/SnapTracker.cpp:224 は ::GetUIDRef(spread) と ILayoutControlData::GetSpreadRef() を
//   比べ、違えば無条件にコマンドを出す。マスターの特例はどこにも無い)。KBS は当初これを
//   「マスターのときだけ」に絞って書いたが、ユーザー指摘で公式どおり無条件へ直した経緯がある。
//
// ★★呼んだ「後」に座標を読むこと(SnapTracker.cpp:234-235 "Re-calculate the starting point")。
//   下の KESCMScrollDocToItemCenter は幾何を読む前にここを通す。overset の「+」点だけは例外で、
//   スキャン時に ::InnerToPasteboardMatrix で確定した**ビュー非依存**の座標なので切替後も有効
//   (再スキャンせずに使ってよいのはそのため)。
//
// コマンドは kSetSpreadCmdBoss + ILayoutCmdData(SnapTracker.cpp:390-413 が完全な実例)。
// ★KESCM は KBS と違い「その文書の全レイアウトビュー」を対象にする(Split Window・複数窓でも
//   スクロール先が揃うように。既存の KESCMScrollDocToPBPoint と同じ範囲)。
// 取れないビューは黙って飛ばす=そのビューは従来どおりスクロールだけになり、悪化はしない。
//----------------------------------------------------------------------------------------
// KESCMEnsureViewShowsSpread(KESCMChangeNav.h で宣言) — 1つのビューぶん。
// ★1ビュー単位で括り出してある(2026-08-11)。同期経路(KESCMViewSync.cpp。分割前は KESCMPeek.cpp)も「このビューを相手の
//   マスタースプレッドへ移す」ために同じ判断を要るようになったため＝判断の置き場は1つ
//   ([[one-question-one-place]])。下の KESCMEnsureSpreadInView は db の全ビューにこれを配るだけ。
bool16 KESCMEnsureViewShowsSpread(IControlView* view, IDataBase* db, UID spreadUID)
{
	if (view == nil || db == nil || spreadUID == kInvalidUID)
		return kFalse;
	InterfacePtr<ILayoutControlData> layout(view, UseDefaultIID());
	if (layout == nil)
		return kFalse;
	if (layout->GetSpreadRef().GetUID() == spreadUID)
		return kFalse;	// もう映している=通常のケース。いちばん安い出口

	// コマンドはビューを名指しするので、その「ビュー自身の文書」を渡す(KBS/SnapTracker と同じ)。
	IDocument* const viewDoc = layout->GetDocument();
	if (viewDoc == nil)
		return kFalse;
	// UID は出どころの db の外では意味を持たない。同じはずだが確かめてから渡す。
	if (::GetDataBase(viewDoc) != db)
		return kFalse;

	// ★失敗したスプレッド切替をこの関数の外へ出さない(2026-08-17 の API 監査 B-U7)。下の
	//   PMSetGlobalErrorCode(kSuccess) だけだと**入る前に立っていたエラーまで消す**。公式の口は
	//   ErrorUtils.h:118。KESCM では BookCompare / BookOpen / HideUnchanged(B10)が採用済みで、
	//   ここと KESCMViewSync のズームだけが取り残されていた(全数3箇所)。
	//   ★早期 return を全部抜けた後＝実際にコマンドを出す直前に作る(上の3つの出口は何も壊さない)。
	GlobalErrorStatePreserver setSpreadErrorState;

	InterfacePtr<ICommand> setSpreadCmd(CmdUtils::CreateCommand(kSetSpreadCmdBoss));
	if (setSpreadCmd == nil)
		return kFalse;
	InterfacePtr<ILayoutCmdData> cmdData(setSpreadCmd, UseDefaultIID());
	if (cmdData == nil)
		return kFalse;
	cmdData->Set(::GetUIDRef(viewDoc), layout);
	setSpreadCmd->SetItemList(UIDList(db, spreadUID));
	if (CmdUtils::ProcessCommand(setSpreadCmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// スクロールは続行。後続コマンドを巻き添えにしない
		return kFalse;
	}
	return kTrue;
}

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
		KESCMEnsureViewShowsSpread(views[i], db, spreadUID);	// nil ビューは中で弾く
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

	// ★失敗したズームをこの関数の外へ出さない(2026-08-17 の API 監査 B-U7。理由と全数は
	//   KESCMEnsureViewShowsSpread の同じ宣言を見よ)。⚠保存はコンストラクタで必ず起きる＝
	//   applyZoom <= 0 のときも作られるが、その経路はコマンドを1本も出さずエラー状態を動かさないので、
	//   デストラクタの復元は同じ値を書き戻すだけになる。
	GlobalErrorStatePreserver scrollZoomErrorState;

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
		//   ⚠2026-08-17 訂正(API 監査 B-U8): 旧記述は「★KESCMPeek.cpp は既に新名称(:1010 ほか)」だったが、
		//   **KESCMPeek.cpp はそもそもこの API を1回も呼んでいない**(全数 Grep)。
		//   ⚠2026-08-19(不具合再検査 B-U8)＝ここには「906行しかなく」と**行数**を根拠に書いてあったが、
		//     実測 968 行へ増えていた。★**行数は「何行目を指せるか」の根拠にしか使えず、しかも黙って腐る**
		//     ので落とした——**「1回も呼んでいない」の方は Grep で何度でも引き直せる**。
		//   分割で同期エンジンが移ったため＝新名称を使っているもう1つは **KESCMViewSync.cpp の
		//   KESCMSyncOtherDocViewportsTo(末尾の ScrollContentLocationToFrameCenter)**
		//   (ほかに KESCMStoryJump.cpp が説明として引用)。★行番号でよそのファイルを指す引用は、
		//   そのファイルが分割・改名されると黙って嘘になる ---- B-U6 で同型を2件直したのに続く3件目。
		//   ⚠★★2026-08-18(不具合再検査 B-U2)＝**この一文自身がその通りになった**。ここには
		//     "KESCMViewSync.cpp:681" と書いてあり、書いた 2026-08-17(B-U8)の時点では**その行がまさに
		//     当の呼び**だったが、翌日には :686 へずれていた。⇒ **警告を書くだけでは足りない。警告が
		//     付いている当の引用を、名前で引き直すところまでやる。**
		pano->ScrollContentLocationToFrameCenter(pbPoint, kTrue /*forceRedraw*/);
		any = kTrue;
	}
	return any;
}

//----------------------------------------------------------------------------------------
// 文書 db の全レイアウトビューを、itemUID の矩形中心が画面中央に来るようスクロールする。
// ★itemUID は**ページでもページアイテムでもよい**(GetItemBounds はどちらにも答える)。
//   ⚠2026-08-19(不具合再検査 B-U8)訂正＝「呼び手は2つ」と書いてあったが、**実測3つ**:
//     ①Prev/Next の巡回(KESCMGoto)      … ページを渡す
//     ②Source 側の連動(KESCMSyncCompanionViews) … 対応表で引いた Source のページを渡す ←数え落ち
//     ③Story Edits の行ジャンプ(KESCMScrollDocToStoryStart のフォールバック) … フレームを渡す(2026-08-10)
//   ★渡す物の**種類**は2つ(ページ／フレーム)で、そこは正しかった＝**「何を数えているか」を言い直すと
//     ずれが見える**([[verify-claims-in-comments]] §24)。
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
	Utils<IKESCMMarkData>()->GetPagePairing(targetDB, sourceDB, tPages, sPages);	// ペア済み(登録除外・ズレ吸収済み)

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
	Utils<IKESCMMarkData>()->GetAllPageUIDs(targetDB, flat);
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
	if (Utils<IKESCMCompareFacade>()->GetActiveDocDB() != db)
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
	// ★2026-08-17 追記(API 監査 B-U8): **第2引数 includePagesOfHiddenSpread の既定 kTrue に依存している。**
	//   ここで欲しいのは「マスターではない=通常ページか」であって、隠れているかどうかは関係ない
	//   ---- Hide Unchanged で隠れているページも通常ページなので kTrue が正しい。⚠ここを kFalse にすると
	//   「隠したページへは Pages パネルが連動しない」に化ける(B7 A-2 で Story Edits の並び順が同じ引数の
	//   既定に依存していたのと同型＝**依存していること自体を書いておく**)。
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
// (セクション込み・**ページパネルの番号**)。
// ★★★2026-08-18(不具合再検査 B10 の2周目)＝第7 bIncludePagesOfHiddenSpread を kFalse から **kTrue** へ。
//   InDesign はページ番号を2つ持っており(実機で実測)、kFalse は「ページに刷られる実ノンブル」の側＝
//   隠しスプレッドを飛ばして数える番号だった。ラベルは「次に見るべきページはどれか」を人に見せる所で、
//   受け取った人はページパネルで探す ---- ∴ ページパネルと同じ番号(kTrue)で綴る。
//   ★TSV(KESCMChangedPagesTSV.cpp の PageDisplay)と Story Edits(KESCMStoryJump.cpp の PageLabel)も
//     同じ日に同じ理由で揃えた。**この3つは「同じページを人にどう綴るか」という1つの問い**なので、
//     どれか1つだけ直すと Hide 中にパネルと書き出しが食い違う([[one-question-one-place]])。
//   ⚠ノンブル除外矩形(KESCMPageNumberMarker.cpp)だけは kFalse のままで正しい＝あちらは実際に刷られる
//     数字のインク範囲を測る用途。用途が違うので揃えてはいけない。
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
		pageList->GetPageString(stop.pageUID, &numStr, kTrue, kFalse, kDefaultPageType, kTrue, kTrue);
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
		int32 changedCells = 0, totalCells = 0;
		if (Utils<IKESCMMarkData>()->GetChangeCells(stop.pageUID, changedCells, totalCells))
		{
			const PMString ratio = KESCMFormatChangeRatio(changedCells, totalCells);
			if (ratio.NumUTF16TextChars() > 0)
			{
				label.Append(", Change ");	// 何の % なのか分かるようにラベルを付ける("Page: 3, Change 12%")
				label.Append(ratio);
			}
		}
	}

	// ★★2026-08-18(不具合再検査 B10 の2周目・ユーザー指定): **隠れているスプレッドのページは
	//   スクロールで行けない**（実測＝押しても `activePage` が動かない）。**ストップは巡回に残したまま、
	//   「なぜ画面が動かないのか」をここで言う。**
	//   ⚠**この但し書きは同日の kTrue 化とセットで要る**: 以前はここが実ノンブル基準(kFalse)で、
	//     隠れたページは番号を持たず "Page: #" と出ていた ---- 異常な見た目そのものが「行けない」の
	//     合図になっていた。ページパネルの番号にした結果**普通の "Page: 2" に見えるようになった**ので、
	//     行けない理由を明示しないと「押しても何も起きない」だけが残る。
	//   ★ステータス行は全ロケール英語（KESCMID.h の表示方針。日本語で出すのは How to Use と
	//     Hide Unchanged の確認アラートの2つだけ）。
	//   ★印の綴りは TSV の Page 列と同じ "(Hide)"＝同じ状態を2通りに綴らない
	//     （KESCMChangedPagesTSV.cpp の PageDisplay）。
	if (db != nil && Utils<IKESCMMarkData>()->IsPageOnHiddenSpread(db, stop.pageUID))
		label.Append(" (Hide)");

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
	IDataBase* sourceDB = Utils<IKESCMMarkData>()->GetMarkedSourceDB();
	if (sourceDB != nil && sourceDB != navDB)
	{
		const UID srcPage = KESCMSourcePageForTarget(navDB, sourceDB, pageUID);
		if (srcPage != kInvalidUID)
		{
			// ★Sync layout views が ON のときは Source の「ビュー」スクロールをしない。理由: Sync オブザーバ
			//   (KESCMViewSync.cpp。2026-08-13 の分割で KESCMPeek.cpp から移った)が navDB(Target)の
			//   スクロールをページオフセット込みで Source へ自動ミラーする。
			//   ここで Source を手動スクロールすると、その変化が Sync により Target へ逆ミラーされ、overset の
			//   「+」スクロールがページ中心に打ち消される(2026-07-24 ユーザー発見: sync OFF なら overset に飛ぶ)。
			//   sync ON では Target のスクロール(呼び手がやった分)を Sync が Source へ伝える。
			//   Pages パネルの連動は Sync の対象外なので、そちらは sync の有無に関わらず行う。
			// ★★2026-08-18(B10 の2周目): Source 側も**隠れているページへは動かさない**（Target と同じ
			//   理由＝スプレッドは切り替わらないのにスクロールだけ効いて見当違いへ寄る）。Hide Unchanged は
			//   両文書の対応スプレッドを隠すので、Target が隠れていれば相手も隠れているのが普通。
			//   ⚠Pages パネルの連動（下）は隠れていても行う＝「どのページか」は見せる。
			if (!KESCMGetLayoutSync() &&
			    !Utils<IKESCMMarkData>()->IsPageOnHiddenSpread(sourceDB, srcPage))
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
//   中心は本文の途中で、読みたいのは書き出しの方。点の算出は IKESCMStoryEditsFacade::GetStoryStartPoint
//   (実体は KESCMStoryList.cpp の KESCMStoryStartPoint)
//   (＝overset の「+」を出す KESCMLastPlacedOutport の鏡像。同じ3つの座標系を同じ順に通る)。
// ★点へ寄せる手順も overset とまったく同じ＝**先にスプレッドを出してから** pb 点へ。pb 点への
//   スクロールは「そのビューが既にそのスプレッドを映している」ことが前提だから(上の
//   KESCMEnsureSpreadInView の説明)。
// ★まだ1度も組まれていない等で点が採れなければ、フレームの中心へ落とす(2026-08-10 以前の動き)。
//   outFrame には実際に着地したフレームを返す ---- Pages パネルの連動がページを引くのに使う。
//
// ★★★focusIndex を渡すと「ストーリーの書き出し」ではなく**その文字**へ寄せる(ユーザー要望 2026-08-22
//   「変更された部分の一番最初の部分がレイアウトビューの真ん中に移動して欲しい」)。長いストーリーの
//   後ろの方が変わっているとき、書き出しへ飛ぶのは**そのストーリーを指しているだけで変更を指していない**。
//   点はキャレット(選択のときに立つ縦線)の位置＝`GetStoryPointAt`。
//   ⚠**採れなかったときは黙って書き出しへ落ちる**＝overset・未配置・未組版はいずれも「その文字は今どこにも
//     出ていない」であって、行そのものは正しい。⇒ 何も動かないより、ストーリーを見せる方がよい。
//----------------------------------------------------------------------------------------
static bool16 KESCMScrollDocToStoryStart(IDataBase* db, UID storyUID, UID fallbackFrameUID,
	UID& outFrame, PMReal applyZoom = PMReal(-1.0), TextIndex focusIndex = kInvalidTextIndex)
{
	if (focusIndex != kInvalidTextIndex)
	{
		PBPMPoint focusPb;
		if (Utils<IKESCMStoryEditsFacade>()->GetStoryPointAt(db, storyUID, focusIndex, focusPb))
		{
			// ★★スプレッドを出すのに使うフレームは**呼び手が渡したもの**＝変更箇所を含むフレームで
			//   なければならない。**ペーストボード座標はスプレッドごと**なので、別のスプレッドを出した
			//   まま点へ寄せると「少しずれる」ではなく**別のページに着く**。
			//   ⚠★★★2026-08-22(不具合再検査)＝**この但し書きは、書かれた時点で片方の呼び手でしか
			//     成立していなかった**。新側(KESCMStoryJumpToChange)は解決済みのフレームを渡していたが、
			//     旧側(下の Source 分岐)は `GetFirstFrameUID`＝**ストーリーの先頭フレーム**を渡していた
			//     ⇒ 連結ストーリーの後ろの方が変わっていると、旧版の窓だけ無関係な場所へ飛んでいた。
			//     **今は両方の呼び手が `GetStoryFrameAt` で同じ問いを出す**([[one-question-one-place]])。
			outFrame = fallbackFrameUID;
			KESCMEnsureSpreadInView(db, fallbackFrameUID);
			return KESCMScrollDocToPBPoint(db, focusPb, applyZoom);
		}
		// 採れなければ下の「書き出しへ」に落ちる(フォールバックは1本にまとめてある)。
	}

	UID startFrame = kInvalidUID;
	PBPMPoint startPb;
	if (Utils<IKESCMStoryEditsFacade>()->GetStoryStartPoint(db, storyUID, startFrame, startPb))
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
	{
		next = (dir > 0) ? 0 : (int32)stops.size() - 1;
		// ⚠**入口フラグも落とす**(2026-08-25 の再検査)＝基準点そのものが消えた(その行を Refresh して
		//   子が無くなった等)のに、「どのストップの入口か」だけが残るのは意味を成さない。
		//   今は下の分岐が cur>=0 のときしか読まないので実害は出ないが、**読まれないから正しい状態**を
		//   置いておくと、次に条件が1つ変わった日に壊れる。
		sNavStoryAtEntry = kFalse;
	}
	else if (sNavStoryAtEntry && stops[cur].isStory)
	{
		// ★★★「入口に立っている」＝**子のある親行を選んだ**状態(2026-08-24 ユーザー決定)。基準点は
		//   その行の最初の子を指しているが、**まだそこへは行っていない** ---- 行のクリックが飛んだ先は
		//   ストーリーの書き出しで、中の最初の変更ではないから。
		//   ⇒ **Next はそのストップ「へ」行く**(進めない)＝親を選んで Next を押した人が、中の最初の
		//     1件を飛ばされない。**Prev は1つ前のストップへ**(入口の手前へ出る)。
		//   ★これは上の `cur < 0`(まだ一度も巡っていない＝Start 直後の「1/N」)とまったく同じ考え方で、
		//     違いは「行を選んだので、どのストップの入口かが分かっている」ことだけ。
		next = (dir > 0) ? cur : cur - 1;
		if (next < 0) next = (int32)stops.size() - 1;	// 先頭の入口で「前」→末尾へ折り返し
	}
	else
	{
		next = cur + dir;
		if (next < 0)                        next = (int32)stops.size() - 1;	// 先頭で「前」→末尾へ折り返し
		else if (next >= (int32)stops.size()) next = 0;						// 末尾で「次」→先頭へ折り返し
	}
	const KESCMNavStop& stop = stops[next];

	// ★★★Story Changes モードのストップ(2026-08-24)＝**この先のページ処理を1つも通さない。**
	//   飛び方も、マークも、メッセージ欄の中身も、**一覧の行をクリックしたときとまったく同じ実装**を
	//   呼ぶ(KESCMStoryNav.cpp → KESCMStoryJump.cpp)。⇒ ユーザー指定「StoryEdit の行を選択したのと
	//   同じ挙動」は、**ここで作り直さないこと**によってしか保てない([[one-question-one-place]])。
	//   ⚠**下の KESCMStopLabel と KESCMSyncCompanionViews は呼ばない**:
	//     ①ラベル(`Page: 3, Change 12%`)は画素比較の変化セル数の割合で、Story には分母が無い。しかも
	//       メッセージ欄はジャンプ側が既に埋めている(変更なら旧側の本文、行なら `Page: 3`)ので、
	//       ここで書くと**それを上書きして消す**。
	//     ②Source 窓と Pages パネルの追随は KESCMGotoStoryFrame が中でやっており、しかも
	//       **ページではなく「同じストーリー」に**合わせる(2026-08-10 のユーザー指摘)。ページで
	//       合わせるあちらを重ねると、まさに見せたい「動いたストーリー」を見失う。
	//   ★隠しスプレッドの扱いも持ち込まない ---- KESCMGotoStoryFrame が「レイアウトは動かさず Pages
	//     パネルだけ合わせて kTrue を返す」と既に決めている(2026-08-18・同じ判断を2か所に書かない)。
	//   ★★**基準ストップはジャンプの成否に関わらず進める。** 未配置のストーリーの行は「行けない」と
	//     自分で言うが、それでもストップではある ---- 進めないと**そこで詰まって次へ行けない**。
	//     ページ側が「スクロールできなければ進めない」のは、あちらの失敗が「そのページが今は無い」＝
	//     リストの作り直しで消える類だから。
	if (stop.isStory)
	{
		KESCMStoryNavStop storyStop;
		storyStop.fRow      = stop.storyRow;
		storyStop.fChange   = stop.storyChange;
		storyStop.fStoryUID = stop.storyUID;
		KESCMGotoStoryNavStop(storyStop);

		// ★★**基準点はここで置かない。** 置くのはジャンプ側(KESCMStoryJump.cpp → KESCMNoteStoryStop)
		//   で、**行のクリックも矢印キーの歩きもそこを通る** ---- ここでも置くと、同じ「今どこに
		//   立っているか」を2か所が別々に決めることになる([[one-question-one-place]])。
		//   ⚠あちらは**行が実在すると分かった時点**で置くので、飛べなかった行(未配置のストーリー・
		//     隠しページ)でも基準は進む＝**そこで詰まらない**。
		KESCMRefreshNavPosition();		// 「k/N」とボタンの有効/無効(ページ側の出口と同じ締め方)
		return;
	}

	// overset は「+」点へ(KBS 流)、変更はページ中心へスクロール。
	// ★基準ストップ(sNav*)の更新はスクロール成功後(2026-07-25 監査で移動): 失敗時に基準だけ先へ進むと、
	//   位置表示「k/N」が古いまま・次回の巡回起点も移動済み、という不整合が残るため。
	// ★overset 経路は pb 点へ直接スクロールするので、ここでスプレッドを出しておく(ページ中心経路は
	//   KESCMScrollDocToItemCenter の中で同じことをしている)。これが無いとマスタースプレッド上の
	//   あふれに飛べない=空のペーストボードに着地する(2026-08-06 ユーザー指摘。KBS と同じ手当て)。
	// ★★★2026-08-18(不具合再検査 B10 の2周目・ユーザー指定): **隠れているスプレッドのページへは
	//   レイアウトビューを動かさない。**
	//   ⚠実測＝隠しページのストップへ飛ぶと、**スプレッドは切り替わらないのにスクロールだけ効く**
	//     （kSetSpreadCmdBoss は隠しスプレッドを出せないので、今映しているスプレッドのまま、隠れた
	//     ページのペーストボード座標へ寄ってしまう）＝**画面が見当違いの場所へ動く**。
	//     「行けないなら動かない」ほうが、押した人の予想と合う。
	//   ★**ストップ自体は巡回に残す**（ユーザー指定「Prev などには入る」）＝基準の更新・位置表示
	//     「k/N」・ステータス行のラベル（末尾に "(Hide)"）・**Pages パネルの連動**は下でそのまま行う。
	//     ⇒ 画面は動かないが、「どのページが変わっているか」はパネルとステータス行で分かる。
	const bool16 stopHidden = Utils<IKESCMMarkData>()->IsPageOnHiddenSpread(navDB, stop.pageUID);
	bool16 ok = kTrue;		// 隠れているときは「スクロールしなかった」を成功として扱う(下の早期 return を避ける)
	if (!stopHidden)
	{
		if (stop.isOverset)
			KESCMEnsureSpreadInView(navDB, stop.pageUID);
		ok = stop.isOverset ? KESCMScrollDocToPBPoint(navDB, stop.pb)
		                    : KESCMScrollDocToItemCenter(navDB, stop.pageUID);
	}
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
	// ページ側へ戻ってきた＝Story の基準点は種別ごと無効になる(上の分岐と対)。
	// ⚠**入口フラグも一緒に落とす**(2026-08-25 の再検査)＝これを残すと、Story ストップの入口に立った
	//   まま Prev であふれ箇所へ抜けたときに kTrue が居座る。今は `sNavIsStory` が偽なので読まれずに
	//   済んでいるが、**「読まれないから正しい」は次に条件が1つ変わった日に崩れる**。
	sNavIsStory      = kFalse;
	sNavStoryAtEntry = kFalse;

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
bool16 KESCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID,
	TextIndex focusIndex, TextIndex sourceFocusIndex)
{
	// ★この関数だけで4回聞くので InterfacePtr に1回受ける(Utils.h:74-80。2026-08-17 の API 監査 B-U8)。
	// ★2026-08-18(B10 の2周目)に**関数の先頭へ移した**＝すぐ下の隠しページ判定がこれを使うため。
	InterfacePtr<IKESCMMarkData> marks(Utils<IKESCMMarkData>().QueryUtilInterface());

	// ★★★2026-08-18(不具合再検査 B10 の2周目): **隠れているスプレッドのページへはレイアウトビューを
	//   動かさない** ---- Prev/Next(KESCMGoto)とまったく同じ判断。あちらだけ直して**ここを直さないと、
	//   同じ「隠れたページへ行こうとする」が、パネルのボタンでは動かず・Story Edits の行では見当違いの
	//   場所へ動く**という食い違いになる([[one-question-one-place]])。
	//   ★Pages パネルの連動は行う＝「どのページか」は見せる。行のラベル(KESCMStoryJump.cpp の PageLabel)
	//     が末尾に "(Hide)" を出すので、動かなかった理由はユーザーに伝わる。
	//   ★戻り値は kTrue＝「行けなかった」ではなく「**行かないと決めた**」。呼び手
	//     (KESCMStoryJumpToRow)は kFalse を "Could not scroll." と綴るので、ここで kFalse を返すと
	//     失敗でないものを失敗として報告してしまう。
	if (marks->IsPageOnHiddenSpread(db, pageUID))
	{
		KESCMScrollPagesPanelToPage(db, pageUID);
		return kTrue;
	}

	// 変更箇所が指定されていればその文字へ、無ければストーリーの書き出しへ(フレームの中心ではない。
	// 上の KESCMScrollDocToStoryStart 参照)。
	UID landedFrame = kInvalidUID;
	if (!KESCMScrollDocToStoryStart(db, storyUID, frameUID, landedFrame, PMReal(-1.0), focusIndex))
		return kFalse;

	// Pages パネルも、**実際に着地したフレーム**のページへ(ページに載っていないなら中で何もしない)。
	// ★行が覚えている pageUID ではなく着地側から引く: 先頭フレームにパーセルが1つも配置されていない
	//   ときは、着地するのは次のフレーム＝別のページのことがある。表示と実際がずれない方を採る。
	KESCMScrollPagesPanelToPage(db, (landedFrame != kInvalidUID) ? marks->GetFramePageUID(db, landedFrame) : pageUID);

	// ***** Source 側も連れて行く。ただし合わせるのは「ページ」ではなく「ストーリー」。*****
	//
	// ★★ここが Prev/Next(KESCMSyncCompanionViews)と違うところ。あちらが指しているのはページなので
	//   対応表でページを引けば足りるが、この行が指しているのは**ストーリー**で、**同じストーリーが
	//   2つの版で違う場所にあることがある**(2026-08-10 ユーザー指摘。レイアウトが変われば当然そうなる)。
	//   ∴ Source でも同じ story UID の先頭フレームを引き、それを中心に出す ---- ページ番号を経由すると、
	//   まさにこの機能が見せたい「動いたストーリー」を見失う。
	// ★UID で引き当てられる根拠は、この機能全体が乗っているのと同じ前提＝**別名保存では story UID が
	//   引き継がれる**(KESCMStoryStamp.h の "WHY TWO VERSIONS CAN BE MATCHED AT ALL" に実測済み。
	//   ⚠2026-08-17 訂正＝旧「:36-38」は挿入で腐った行番号で、実体は10行下だった)。
	//   Source に無いストーリー(=Added の行)は
	//   kInvalidUID が返るので、そのときは Target だけが動く。
	IDataBase* sourceDB = marks->GetMarkedSourceDB();
	if (sourceDB != nil && sourceDB != db && storyUID != kInvalidUID)
	{
		// ⚠★★旧側にも dirty ガードが要る(2026-08-22)。sourceFocusIndex を渡すと、その文字の位置と
		//   その文字を載せているフレームを出すために**旧文書の組版**が最新化されることがあり、
		//   組版は文書を汚す(IKESCMStoryEditsFacade::GetStoryPointAt / GetStoryFrameAt)。
		//   新側のガードは呼び手が持っているが、**旧文書に触るのはこの関数だけなので、ここが持つ**。
		//   ★焦点を渡さない経路(親のストーリー行)では組版は起きないので、このガードは何もしない。
		IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

		UID srcFrame = Utils<IKESCMStoryEditsFacade>()->GetFirstFrameUID(sourceDB, storyUID);

		// ★★★**旧側も「変更箇所を含むフレーム」を引く**(2026-08-22 の不具合再検査)。
		//   上の GetFirstFrameUID が返すのは**ストーリーの先頭フレーム**で、行が指しているのが
		//   ストーリーそのもの(親の行)ならそれで正しい ---- が、**変更箇所へ寄せるときにそれで
		//   スプレッドを決めると、連結ストーリーの後ろの方の変更で別のスプレッドを出してしまう**。
		//   ペーストボード座標はスプレッドごとなので、着地は「少しずれる」ではなく別のページになる。
		//   ⇒ 新側(KESCMStoryJumpToChange)とまったく同じ問いを、同じ口へ出す。
		//   ⚠採れなければ先頭フレームのまま＝従来どおりの動きに落ちる(overset・未配置・比較後に
		//     旧文書が短くなった場合はいずれもここへ来る)。
		if (sourceFocusIndex != kInvalidTextIndex)
		{
			const UID srcFocusFrame =
				Utils<IKESCMStoryEditsFacade>()->GetStoryFrameAt(sourceDB, storyUID, sourceFocusIndex);
			if (srcFocusFrame != kInvalidUID)
				srcFrame = srcFocusFrame;
		}

		if (srcFrame != kInvalidUID)
		{
			// ★Sync layout views が ON のときは Source を手動で動かさない ---- Sync のオブザーバ
			//   (KESCMViewSync.cpp)が、すぐ上でやった Target のスクロールを Source へ既にミラーしている。
			//   ここでも動かすと二重になり、しかもその変化が Sync 経由で Target へ逆ミラーされて、
			//   出したはずのフレームが押し戻される(2026-07-24 に overset で実際に起きた形)。
			//   ⚠ ON のとき Source が映すのは「Target と同じ座標」なので、ストーリーの位置がずれて
			//   いれば厳密には別の場所になる。それでも Sync の約束(2つの窓を同じ座標で並べる)の方が
			//   ユーザーの明示的な指定なので、そちらを優先する。KESCMSyncCompanionViews と同じ判断。
			if (!KESCMGetLayoutSync())
			{
				// ★Target とまったく同じ寄せ方(ズームも Target に合わせる)。
				// ★★★**旧側も「対応する文字」まで寄せる**(2026-08-22)＝sourceFocusIndex が
				//   Change::fSourceStart。これで KESCMID.h の増分⑬にある「⚠まだ第1段＝旧側の窓は
				//   『同じストーリー』までで、対応する文字までは寄せていない」が解消する。
				//   ⚠**新旧で文字位置は違う**ので、Target の focusIndex を使い回してはいけない
				//     (旧版で同じ番号の文字はまったく別の場所にある)。差分が両側の位置を出しているので、
				//     使うのはそちら＝[[one-question-one-place]] の逆で、**別の問いには別の答え**。
				//   ★★2026-08-22＝**挿入でも旧側の位置が来る**。以前は呼び手が kInvalidTextIndex を
				//     渡していた（「旧側に指す場所が無い」という理由）が、それは**文字**の話で
				//     **場所**の話ではなかった＝新しい語が入った隙間は旧版にちゃんとある。今は
				//     fSourceStart（空範囲の開始＝キャレットの位置）が来るので、ここは何も分岐しない。
				UID srcLanded = kInvalidUID;
				KESCMScrollDocToStoryStart(sourceDB, storyUID, srcFrame, srcLanded, KESCMReadDocZoom(db),
										   sourceFocusIndex);
				if (srcLanded != kInvalidUID)
					srcFrame = srcLanded;
			}

			// Pages パネルは Sync の対象外なので ON/OFF に関わらず追随させる(Source が前面のときだけ
			// 中で効く。Target が前面なら上の呼び出しの方が効いている)。
			KESCMScrollPagesPanelToPage(sourceDB, marks->GetFramePageUID(sourceDB, srcFrame));
		}
	}

	// ★巡回の基準点(sNavPageUID 等)は動かさない。行ジャンプは Prev/Next とは別の動線で、ここで基準を
	//   書き換えると「Next を押したら一覧で飛んだ場所の次から始まる」という、どちらの機能の説明にも
	//   出てこない挙動になる(2026-08-10 の設計判断)。
	return kTrue;
}

//========================================================================================
// KESCMNoteStoryStop(KESCMChangeNav.h で宣言)
//   ★一覧の行へ「今立った」ことを巡回位置へ反映する。呼び手はジャンプ関数の中ただ1つで、
//     クリック・矢印キー・Prev/Next の**全部がそこを通る**(規則と理由はヘッダー)。
//========================================================================================
void KESCMNoteStoryStop(int32 rowIndex, int32 changeIndex)
{
	if (rowIndex < 0)
		return;

	// ⚠Pixel モードの巡回対象はページで、一覧の行はその列に居ない ---- 触ると「行をクリックしたら
	//   ページの巡回位置が飛ぶ」ことになる。あちらの「行ジャンプは基準点を動かさない」は据え置き。
	if (Utils<IKESCMCompareFacade>()->GetCompareMode() != kKESCMModeStory)
		return;

	// 行の実在と storyUID、そして子の数を聞く(3つとも同じ facade なので1回引く)。
	InterfacePtr<IKESCMStoryEditsFacade> edits(Utils<IKESCMStoryEditsFacade>().QueryUtilInterface());
	if (edits == nil)
		return;

	IKESCMStoryEditsFacade::Row row;
	if (!edits->GetRow(rowIndex, row))
		return;		// 一覧が作り直された直後にクリックが届いた: その行はもう無い

	sNavIsStory    = kTrue;
	sNavStoryUID   = row.fStoryUID;
	sNavStoryRow   = rowIndex;		// ★UID と対で同定する(理由は KESCMFindCurrentStop の説明。
									//   ⚠そこに書いた「UID が衝突しうる」は誤りで、同日中に撤回した)
	sNavPageUID    = kInvalidUID;	// ページ側の基準は持ち越さない(種別で分かれるので値も残さない)
	sNavIsOverset  = kFalse;
	sNavOversetOrd = 0;

	if (changeIndex >= 0)
	{
		sNavStoryChange   = changeIndex;
		sNavStoryAtEntry  = kFalse;		// その変更そのものに立っている
	}
	else
	{
		// 行そのものを選んだ。★子があるならこの行はストップでは無い(KESCMStoryNav.h)ので、
		//   **その最初の子の入口**に立つ ---- 表示はその子の番号、Next を押すとそこへ行く。
		//   子が無ければ行そのものがストップなので、普通に立つ。
		const int32 changeCount = edits->GetChangeCount(rowIndex);
		sNavStoryChange  = (changeCount > 0) ? 0 : -1;
		sNavStoryAtEntry = (changeCount > 0) ? kTrue : kFalse;
	}

	// ★★表示も作り直す。**行のクリックと矢印キーは、これ以外に「k/N」を書き換える経路を持たない**
	//   ---- 基準点だけ動かして表示を置き去りにすると、パネルが自分と食い違う。
	//   ⚠Prev/Next は自分の出口でも呼ぶので二重になるが、**今の状態から作り直すだけ**なので同じ値。
	KESCMRefreshNavPosition();
}

// 巡回の基準点を忘れる(KESCMChangeNav.h)。次回の Next/Prev はリストの先頭/末尾から始まる。
// ★表示更新はしない(基準点を落とすだけ): これは比較の総入れ替え(Start)の途中でも呼ばれるため、
//   位置表示は呼び出し側(KESCMDoMarkChangesDoc 末尾 / KESCMDoClearMarks)が確定後に
//   KESCMRefreshNavPosition で一括更新する。
void KESCMResetNav()
{
	sNavPageUID = kInvalidUID; sNavIsOverset = kFalse; sNavOversetOrd = 0;
	// ★Story 側も同じ理由で捨てる(2026-08-24)＝一覧は比較のたびに丸ごと作り直されるので、前の比較の
	//   ストーリーも編集の番号も意味を持たない。⚠ここを足し忘れると、別の文書対で再 Start したときに
	//   **偶然 UID が一致した行から巡回が始まる**(ページ UID について上の説明が言っているのと同じ形)。
	sNavIsStory = kFalse; sNavStoryUID = kInvalidUID; sNavStoryRow = -1; sNavStoryChange = -1;
	sNavStoryAtEntry = kFalse;
}

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
