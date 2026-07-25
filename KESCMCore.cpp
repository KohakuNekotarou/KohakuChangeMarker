//========================================================================================
//
//  KESCMCore.cpp
//
//  ChangeMarker の共有操作(KESCMCore.h で宣言)。KESCMScriptProvider.cpp から分離したもの。
//  スクリプトメソッドとパネルのウィジェットオブザーバが完全に同じ挙動を駆動できるよう、ただの関数に
//  してある。描画エンジン(KESCMDrawEventHandler)・peek モジュールへ委譲する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "ISession.h"			// GetExecutionContextSession(KESCMIsDocDBOpen)
#include "IActiveContext.h"		// GetContextDocument(KESCMActiveDoc)
#include "IApplication.h"		// QueryDocumentList(KESCMIsDocDBOpen)
#include "IDocumentList.h"		// FindDocByDataBase=生存確認のポインタ比較(KESCMIsDocDBOpen)
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "IControlView.h"
#include "IEventUtils.h"
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IBoolData.h"				// スプレッドの隠し状態(IID_IHIDESPREADBOOLDATA)の読み取り
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA(kSpreadBoss 上の IBoolData。docs の boss 一覧で裏取り済み)
#include "PMString.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"
#include "TransformUtils.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "ILayoutViewUtils.h"		// GetAllLayoutViews(KESCMFindDocDbForView)
#include "K2Vector.h"				// GetAllLayoutViews の out コンテナ(間接includeに頼らず明示)
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"
#include "LayoutUIID.h"				// kLayoutWidgetBoss / kLayoutSecondaryPanelWidgetID

#include <vector>
#include <set>
#include <map>						// KESCMDoMarkChangesDoc のペアリング map(間接includeに頼らず明示 2026-07-25)

#include "KESCMDrawEventHandler.h"   // 描画エンジン＋共有 static
#include "KESCMPeek.h"               // KESCMBaseScreenOpacity
#include "KESCMPageMap.h"            // KESCMBuildPairing(除外対応表)
#include "KESCMPageCheck.h"          // KESCMPageCheckClearAllDocs(Stop で✓を全消去)
#include "KESCMThumbnailRefresh.h"   // ★実験: 既表示サムネイルの再生成トライ(2026-07-06)
#include "KESCMChangeNav.h"          // KESCMResetNav(セッションを跨いだ巡回基準点の持ち越しを断つ)
#include "KESCMScrollMap.h"          // KESCMScrollMapInvalidateAll(比較後にスクロールバー地図を最新化)
#include "KESCMCore.h"

//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、スプレッド順・ページ順で平坦に集める。
//========================================================================================
void KESCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;
	const int32 ns = spreadList->GetSpreadCount();
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}

//========================================================================================
// マウス位置・ヒットテストの共有ヘルパ(peek と色サンプラが同じ流儀でカーソル位置を求める)。
//========================================================================================
bool16 KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY)
{
	outX = 0.0; outY = 0.0;
	if (view == nil)
		return kFalse;
	// マウス: 画面 → 窓 → コンテンツ(ペーストボード)座標。
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	outX = pt.X();
	outY = pt.Y();
	return kTrue;
}

// マウス下のレイアウトビューを求める(Split Window対応)。KESCMCore.h のコメント参照。
IControlView* KESCMQueryViewUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	InterfacePtr<IPanelControlData> hitPanelData(hitPres, UseDefaultIID());
	if (hitPanelData == nil)
		return nil;

	IControlView* primaryView = hitPanelData->FindWidget(kLayoutWidgetBoss);
	if (primaryView == nil)
		return nil;

	// primaryView は「グローバル→ウィンドウ座標への変換」にだけ使う(どの子ウィジェット経由でも同じ
	// ウィンドウ座標系になるため)。実際にマウス下にあるビューは FindWidget(windowPt) のヒットテストで
	// 特定する(キャンバス以外=ルーラ等に当たった場合は primaryView にフォールバック)。
	IControlView* hitView = primaryView;
	const PMPoint globalPM((PMReal)globalPt.x, (PMReal)globalPt.y);
	const PMPoint winPM = primaryView->GlobalToWindow(globalPM);
	SysPoint winPt;
	winPt.x = ::ToInt32(winPM.X());
	winPt.y = ::ToInt32(winPM.Y());

	IControlView* pointHit = hitPanelData->FindWidget(winPt);
	if (pointHit != nil &&
	    (pointHit->GetWidgetID() == kLayoutWidgetBoss || pointHit->GetWidgetID() == kLayoutSecondaryPanelWidgetID))
		hitView = pointHit;

	hitView->AddRef();	// QueryFrontView() と同じ「+1 ref、呼び出し側で Release」の契約に合わせる
	return hitView;
}

// アクティブ(前面)文書とその db。KESCMCore.h のコメント参照(2026-07-25 重複解消で集約)。
IDocument* KESCMActiveDoc()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ac = session ? session->GetActiveContext() : nil;
	return ac ? ac->GetContextDocument() : nil;
}

IDataBase* KESCMActiveDocDB()
{
	IDocument* doc = KESCMActiveDoc();
	return doc ? ::GetUIDRef(doc).GetDataBase() : nil;
}

// view が db のレイアウトビュー群に含まれるか(1文書ぶんのポインタ照合)。下の2用途で共有する。
static bool16 KESCMViewBelongsToDb(IControlView* view, IDataBase* db)
{
	if (view == nil || db == nil)
		return kFalse;
	K2Vector<IControlView*> views;
	Utils<ILayoutViewUtils>()->GetAllLayoutViews(views, nil, db);	// 閉じた db なら空が返るだけ=安全
	for (int32 vi = 0; vi < (int32)views.size(); ++vi)
		if (views[vi] == view)
			return kTrue;
	return kFalse;
}

// ★直前にヒットした文書(2026-07-25 追補)。ホットパス最適化のためだけの「当たりを付ける」ヒント。
//   Sync Layout Views のスクロール追従とツールカーソルの黒/白判定は、同じビューについて連続で
//   (マウス移動のたび=数十回/秒)この関数を呼ぶ。毎回「全文書 × GetAllLayoutViews」を回すと
//   文書数ぶんの K2Vector 構築が積み上がるので、まず前回の db だけを試す。
//   ★誤りが混入しない作り: ヒントは「どの db から試すか」を決めるだけで、答えは必ず
//   KESCMViewBelongsToDb による実照合で確定する。外れたら従来どおり全走査へフォールバックする。
//   閉じた db が残っていても GetAllLayoutViews が空を返して外れるだけ(deref しない)。
static IDataBase* sLastViewHitDb = nil;

// 直前ヒントを捨てる(KESCMCore.h で宣言)。文書クローズ・arm 切替・同期 OFF から呼ぶ。
void KESCMForgetViewDbHint()
{
	sLastViewHitDb = nil;
}

// view がどの文書のレイアウトビューかをポインタ照合で特定する。KESCMCore.h のコメント参照。
// (2026-07-25: KESCMPeek.cpp の file-static から共有ヘルパへ移動。色サンプラの窓ガードでも使うため)
IDataBase* KESCMFindDocDbForView(IControlView* view)
{
	if (view == nil)
		return nil;

	// ①前回ヒットした文書を先に試す(連続呼び出しはほぼここで確定する)。
	if (sLastViewHitDb != nil && KESCMViewBelongsToDb(view, sLastViewHitDb))
		return sLastViewHitDb;

	// ②外れたら全文書を走査(従来どおり)。見つかった db を次回のヒントにする。
	ISession* session = GetExecutionContextSession();	// 終了処理中は nil になり得る(下の共通規約参照)
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 docCount = docList->GetDocCount();
	for (int32 d = 0; d < docCount; ++d)
	{
		IDocument* doc = docList->GetNthDoc(d);
		if (doc == nil)
			continue;
		IDataBase* db = ::GetUIDRef(doc).GetDataBase();
		if (db == sLastViewHitDb)
			continue;	// ①で試して外れている
		if (KESCMViewBelongsToDb(view, db))
		{
			sLastViewHitDb = db;
			return db;
		}
	}
	return nil;
}

bool16 KESCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KESCMPageHit& out)
{
	out.spreadIndex = -1; out.spreadUID = kInvalidUID; out.numPages = 0;
	out.globalPageBase = 0; out.hitPageIndex = -1; out.hitPageUID = kInvalidUID;
	if (targetDB == nil)
		return kFalse;
	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;
	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(targetDB, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// ★隠しスプレッド(Hide Unchanged Spreads / ページパネルの Hide Spread)は当たり判定から除外する。
		//   隠すと表示中スプレッドが再配置されて座標が動くのに、隠れたスプレッドの旧座標が同じ場所に
		//   残ってマウスに先にヒットし、peek/再比較/色サンプラの新旧対応(平坦ページ番号)がずれるため。
		//   ページ数の加算(下の globalIndex += np)は続ける=平坦番号は「隠していない時と同じ元の番号」を
		//   維持し、旧ドキュメントの平坦ページ列との対応が崩れない。
		//   隠し状態は kSpreadBoss 上の IBoolData(IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む。
		InterfacePtr<IBoolData> hideFlag(targetDB, spreadUID, IID_IHIDESPREADBOOLDATA);
		if (hideFlag != nil && hideFlag->GetBool())
		{
			globalIndex += np;
			continue;
		}

		// マウスがこのスプレッドのいずれかのページ上にあるか?(最初に当たったページを採用)
		for (int32 p = 0; p < np; ++p)
		{
			const UID pageUID = spread->GetNthPageUID(p);
			InterfacePtr<IGeometry> geo(targetDB, pageUID, UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = geo->GetPathBoundingBox();
			PMMatrix m = ::InnerToPasteboardMatrix(geo);
			m.Transform(&bb);
			PMReal L = bb.Left(), R = bb.Right(), T = bb.Top(), B = bb.Bottom();
			if (L > R) { PMReal t = L; L = R; R = t; }
			if (T > B) { PMReal t = T; T = B; B = t; }
			if (mx >= L && mx <= R && my >= T && my <= B)
			{
				out.spreadIndex    = s;
				out.spreadUID      = spreadUID;
				out.numPages       = np;
				out.globalPageBase = globalIndex;
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				return kTrue;
			}
		}
		globalIndex += np;
	}
	return kFalse;
}

//========================================================================================
// 共有コア操作(KESCMCore.h で宣言)。
//
// 以前はスクリプトメソッド内にインラインで書かれていた本体。今はパネルのウィジェットオブザーバ
// (KESCMPanelObserver.cpp)が完全に同じ挙動を駆動できるよう、ただの(非 static)関数にしてある。
// この翻訳単位に置くのは意図的で、描画エンジン(KESCMDrawEventHandler)と file-local な peek 状態
// (sPeek*)へ直接アクセスできるようにするため。
//========================================================================================

ErrorCode KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// ★再比較の前に「今 枠/斜線が付いているページ」を控える(サムネイル取りこぼし対策)。再ペアリング
	//   (登録トグルでページ数差を無視した時など)で対応が1つズレると、overflow を抜けたページ(赤「/」が
	//   消える)や再ペアで変更なしに戻ったページ(リングが消える)が生じる。これらは再比較後の per-UID
	//   Purge 集合(=いずれも「今」の状態)には入らないため、旧集合を控えて後で一緒に Purge しないと
	//   古い枠/斜線がサムネイルに残る。
	//   列挙は KESCMCollectChangedPageUIDs に一本化(「何がマーク済みか」の定義を二重実装しない)。
	//   同関数は db が現在の sDB/sSrcDB と一致する時だけ集める=「前回比較が今回と同じ文書の時だけ
	//   旧 UID を拾う」ガード(UID は db 固有。別文書対への再 Start で誤 Purge しない)も兼ねる。
	std::set<UID> prevTargetMarked, prevSourceMarked;
	KESCMCollectChangedPageUIDs(targetDB, prevTargetMarked);
	KESCMCollectChangedPageUIDs(sourceDB, prevSourceMarked);

	// 差分再比較の可否。登録トグル専用(allowIncremental=kTrue)で、かつ前回比較と同じドキュメント対を
	// 対象にしていて前回ペアリングが残っている場合のみ差分にする。それ以外(Start・Ignore Page Number
	// マーカー切替・別文書対・前回ペアリング無し)は従来どおり全ページを再ラスタ化する。
	const bool16 doIncremental =
		allowIncremental &&
		KESCMDrawEventHandler::sDB == targetDB &&
		KESCMDrawEventHandler::sSrcDB == sourceDB &&
		!KESCMDrawEventHandler::sPrevPairTargetToSource.empty();

	// 再比較すると「どのスプレッドが変更なしか」の分類が古くなるため、「Hide Unchanged Spreads」で
	// 隠していたスプレッドは先に再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KESCMResetHideUnchanged(kTrue);

	// 両ドキュメントのページ対応を除外対応表(登録済み=比較相手なしページを除いた順番対応)で求める。
	// 差分・全再比較のどちらでも使い、末尾で次回差分用の前回ペアリング(sPrevPairTargetToSource)に記録する。
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	const size_t n = tPages.size();	// KESCMBuildPairing は既に短い方へ切り詰め済み(tPages/sPagesは同じ長さ)

	// 今回ペアリングの map 化(差分の O(1) 逆引き＋末尾の記録に使う)。
	std::map<UID, UID> newMap;
	for (size_t i = 0; i < n; ++i)
		newMap[tPages[i]] = sPages[i];

	// 比較は同期実行でページをラスタ化するため時間がかかる。ループ前に「Comparing changes...」を
	// パネルステータスへ出し、ForceRedraw で即時に描いてからループに入る(ブロック中も見えるようにする)。
	// 差分の場合はラスタ化枚数が少なく一瞬で終わるが、出しておいても害はない。
	{
		PMString busyMsg("Comparing changes...");
		busyMsg.SetTranslatable(kFalse);
		KESCMSetStatus(busyMsg, kTrue /*forceRedrawNow*/);
	}

	int32 changedCount = 0;
	if (doIncremental)
	{
		// 【差分再比較】前回ペアリング(oldMap)と今回(newMap)を突き合わせる。ペア不変のページは
		// MakeEntry を呼ばず前回のオーバーレイ(または「変化ゼロ=エントリ無し」)をそのまま再利用する。
		const std::map<UID, UID>& oldMap = KESCMDrawEventHandler::sPrevPairTargetToSource;

		// (1) 破棄: 前回ペアの target のうち、今回ペアが消えた/相手が変わったものはエントリを捨てる。
		//     MakeEntry は変化ゼロだと既存エントリを消さないので、相手が変わるページは先にここで消す。
		for (std::map<UID, UID>::const_iterator it = oldMap.begin(); it != oldMap.end(); ++it)
		{
			std::map<UID, UID>::const_iterator nit = newMap.find(it->first);
			if (nit == newMap.end() || nit->second != it->second)
				KESCMDrawEventHandler::DropOneEntry(it->first, it->second);
		}

		// (2) 再計算: 今回ペアの target のうち、前回ペアが無かった/相手が変わったものだけ MakeEntry。
		//     ペア不変ページは触らない(=前回結果を再利用=ラスタ化しない=ここが高速化の核)。
		for (size_t i = 0; i < n; ++i)
		{
			std::map<UID, UID>::const_iterator oit = oldMap.find(tPages[i]);
			if (oit == oldMap.end() || oit->second != sPages[i])
			{
				bool16 changed = kFalse;
				KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
			}
		}
		changedCount = (int32)KESCMDrawEventHandler::sEntries.size();	// 再利用分も含めた現在の変化ページ総数
	}
	else
	{
		// 【全再比較】ドキュメント単位の総入れ替え(Start・Ignore Page Number 切替・フォールバック)。
		KESCMDrawEventHandler::DropAll();
		KESCMDrawEventHandler::sDB = targetDB;
		// 対象文書を丸ごと入れ替えるので、変更ページ巡回(Next/Prev)の基準点も捨てる。旧文書のページ UID を
		// 持ち越すと、別文書での UID 偶然一致で誤った位置から巡回が始まるため(差分再比較の側は同一文書なので触らない)。
		KESCMResetNav();
		for (size_t i = 0; i < n; ++i)
		{
			bool16 changed = kFalse;
			KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
			if (changed) ++changedCount;
		}
	}

	// 今回のペアリングを次回の差分用に記録する(差分・全再比較のどちらの経路でも)。
	KESCMDrawEventHandler::sPrevPairTargetToSource.swap(newMap);

	// sSrcDB/対応表は MakeEntry が変化ページ登録時に埋めるが、変化ゼロでも db だけは明示しておく
	// (エントリが無ければ wantSrcMarks が空判定で落ちるので描画コストは増えない)。
	// ★「Show Marks on Source」の既定 ON はここでは立てない(2026-07-25 監査で移動): この関数は Start
	//   だけでなく登録トグルの差分再比較・Ignore Page Number 切替の再比較も通るため、ここで kTrue に
	//   戻すとユーザーが OFF にした直後の再比較で黙って ON に戻ってしまう。既定 ON へ戻すのは仕様どおり
	//   Start 経路(KESCMToggleStartStop)のみ。
	KESCMDrawEventHandler::sSrcDB = sourceDB;

	// overflow("/")キャッシュを今の対応表から作り直す。ここは Start・登録 Add/解除・Ignore 切替が
	// すべて通る唯一の再比較路なので、これらの操作後は描画側が最新の overflow を使う(描画のたびの
	// 全文書走査は EnsureOverflowCache 側で回避)。
	KESCMDrawEventHandler::RebuildOverflowCache();
	// ビューポート同期が持つ除外対応表キャッシュも同じ理由でここで捨てる(登録 Add/解除でペアが動く。
	// 2026-07-25 追補。呼び忘れても 250ms の TTL で追従するが、明示しておけば次の1通知から正しい)。
	KESCMInvalidateSyncCaches();

	// ★「KESCM: Check」の✓: 再比較で「マーク(枠/「/」)が無くなったページ」のチェックを忘れる
	//   (ユーザー指定 2026-07-11)。この後のサムネイル更新で、マークが消えたページは prevMarked 経由で
	//   purge され、リングも✓も無いクリーンなサムネイルに作り直される(チェックを先に外すのが肝)。
	//   ★必ず下の KESCMInvalidateDB より前に呼ぶ(2026-07-12 ユーザー報告の修正): ✓ はレイアウト
	//   ビューにも常時表示されるようになったので、Invalidate 後にチェックを外すと「✓ がまだある状態」
	//   でレイアウトが描き直されて古い ✓ が残る(サムネイルは prune 後に更新されるので消える=食い違い)。
	//   prune に必要なマーク集合(sEntries/登録/overflow)は直前の RebuildOverflowCache までで確定済み。
	KESCMPageCheckPruneToMarked();

	KESCMInvalidateDB(targetDB);
	if (sourceDB != targetDB)
		KESCMInvalidateDB(sourceDB);	// Source 側の常時枠を即反映

	// スクロールバー地図 strip のマークも最新化(Start/旧 Ctrl+ミドル再比較/登録トグルの全経路がここを通る)。
	KESCMScrollMapInvalidateAll();

	// ★サムネイル実験(2026-07-06): 既表示サムネイルの再生成を試みる(KESCMThumbnailRefresh)。
	// 従来 2026-07-05 に「文書の変更でしか無効化されない内部キャッシュがあり、InvalidatePageWidget/
	// InvalidateSpreadWidget・UpdatePagesPanel(bForcePurge)・ForceRedraw は全て不発」と確認済みだが、
	// 未検証だった IPendingUpdateController::Update()(保留更新の消化)と IImageCacheMgr::Purge(db) を
	// 合わせて叩いてみる(微かな望み)。効果が無ければこの1行と KESCMThumbnailRefresh.* を外すだけで戻せる。
	// (サムネイル自体への枠描画は sThumbExperiment 経由=描画エンジン側で ON。)詳細: memory
	// kescm-pages-panel-thumbnails。
	// Target/Source の2回とも Purge だけ行い、Pages パネルの ForceRedraw は最後の1回に畳む
	// (2026-07-25 監査: 同期 ForceRedraw の多重実行を削減)。
	KESCMTryRefreshPagesPanelThumbnails(targetDB, &prevTargetMarked, kFalse /*redrawNow*/);
	if (sourceDB != targetDB)
		KESCMTryRefreshPagesPanelThumbnails(sourceDB, &prevSourceMarked, kFalse /*redrawNow*/);
	KESCMForceRedrawPagesPanelNow();

	PMString report;
	report.SetTranslatable(kFalse);
	report.Append("marks start");
	report.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
	report.Append("pages compared="); report.AppendNumber((int32)n);
	report.Append(" changed="); report.AppendNumber(changedCount);
	outReport = report;

	// Prev/Next 間の現在位置表示(k/N・-)と Prev/Next ボタンの有効/無効を、確定した最新の変更ページ集合で
	// 作り直す。Start・差分再比較・登録(Add/Remove)・Check がすべてこの関数を通るので、Next/Prev を
	// 押さなくても集合の変化に即時追従する(ユーザー要望 2026-07-15。全再比較路では上で KESCMResetNav 済み
	// =未巡回扱いで "1/N")。
	KESCMRefreshNavPosition();
	return kSuccess;
}

// 文書の生存確認(KESCMCore.h で宣言)。★閉じた db は deref 禁止=IDocumentList への
// ポインタ比較のみ。旧 KESCMActionComponent.cpp の static を共有化したもの(2026-07-10)。
// ★session の nil ガードは必須(2026-07-25 追補): この関数は KESCMScrollMapView::Draw と遅延サムネイル
//   idle task から呼ばれ、どちらもアプリ終了のティアダウン中に発火し得る。session が解体済みの
//   環境(特に Mac の Cocoa 解体順)で無ガード deref すると crash-on-quit になる。
//   引けない=解体が進んでいる → 「開いていない」と答えるのが安全側。
bool16 KESCMIsDocDBOpen(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(db) != nil) ? kTrue : kFalse;
}

// アプリが終了処理中(kQuitting=QuitCmd の Terminate 後 / kShuttingDown=イベントループ停止後)なら kTrue。
// quit の close-all フェーズ(ユーザーが保存確認をキャンセルできる段階)はまだ kRunning なので kFalse のまま
// =通常クローズと同じフルクリーンアップが走る。ここが kTrue の間はウィンドウ/パネルの解体順が
// プラットフォーム依存(特に Mac の Cocoa 解体順は Windows と異なる)のため、widget 操作・再描画・
// idle task 予約などの UI 仕事をしてはならない(2026-07-15 終了堅牢化)。
bool16 KESCMAppIsQuitting()
{
	// ★session 自体も nil ガード(2026-07-25 監査で追加): 終了保護そのものの関数が無ガード deref では
	//   本末転倒。session すら引けない=解体が進んでいる、として終了中扱いに倒す。
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kTrue;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return kTrue;	// アプリすら引けない=解体が進んでいる。安全側(終了中扱い)に倒す
	const IApplication::ApplicationStateType st = app->GetApplicationState();
	return (st == IApplication::kQuitting || st == IApplication::kShuttingDown) ? kTrue : kFalse;
}

// db が非nilなら、その IDocument のビューを再描画する。呼び出し側(パネル操作時の「今アクティブな
// 文書」)と「実際にマークが描かれている対象文書」が異なる(例: Source や無関係な第3文書が前面の
// 状態で Stop や印刷マーク切替を行った)場合でも、両方を確実に再描画するために使う共有ヘルパ。
void KESCMInvalidateDB(IDataBase* db)
{
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}

void KESCMDoClearMarks(IDataBase* db)
{
	// マーク(=「変更なし」判定の根拠)が消えるので、「Hide Unchanged Spreads」で隠していた
	// スプレッドも再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KESCMResetHideUnchanged(kTrue);

	// DropAll() で sDB が nil になる前に、実際にマークが描かれていた文書を控えておく。呼び出し側の
	// db(=操作時のアクティブ文書)が前面で Source や無関係な第3文書に切り替わっていても、対象文書の
	// 枠が即座に消えるようにするため(タイル表示等で対象文書が同時に見えている場合に効く)。
	// Source 側の常時枠(Show Marks on Source)も同様に、消える前の db を控えて後で再描画する。
	IDataBase* markedDB = KESCMDrawEventHandler::sDB;
	IDataBase* srcDB    = KESCMDrawEventHandler::sSrcDB;

	// ★登録(Added/Removedページ)も Stop で丸ごと忘れる(ユーザー指定 2026-07-11:「Stop すると
	// Add/Remove の登録は解除する」)。登録は arm 済みのとき Target/Source にしか作れないので実質この2文書
	// だが、取りこぼしの無いよう全文書分を一括クリアする(Target/Source の組み合わせを変えて再 Start した
	// 時に古い登録が紛れ込むのも防ぐ。2026-07-05 の per-db クリアを全体クリアへ拡張)。
	KESCMPageMapClearAllDocs();

	// ★「KESCM: Check」の✓も Stop で丸ごと忘れる(ユーザー指定: Start 中限定・Stop で消去)。
	KESCMPageCheckClearAllDocs();

	KESCMDrawEventHandler::DropAll();
	KESCMDrawEventHandler::DropAllOrig();	// 旧版べた載せのキャッシュも解放(メモリ開放)

	KESCMInvalidateDB(markedDB);
	if (db != markedDB)
		KESCMInvalidateDB(db);
	if (srcDB != markedDB && srcDB != db)
		KESCMInvalidateDB(srcDB);			// Source 側の常時枠も即座に消す

	// ★Pages パネルのサムネイルからも枠/斜線を消す。KESCMInvalidateDB(=InvalidateViews)はレイアウト
	// ビューだけを無効化し、サムネイルの共有画像キャッシュには届かない。Start 側(比較実行後)が枠を
	// 付けるのと対称に、Stop でも Purge+ForceRedraw でクリーンなサムネイルへ作り直させる。DropAll 済みで
	// マーク対象が無い状態なので、再生成される isThumb 描画は早期 return し枠は描かれない。
	// 2文書とも Purge だけ行い、ForceRedraw は最後の1回に畳む(2026-07-25 監査: 多重実行の削減)。
	KESCMTryRefreshPagesPanelThumbnails(markedDB, nil, kFalse /*redrawNow*/);
	if (srcDB != nil && srcDB != markedDB)
		KESCMTryRefreshPagesPanelThumbnails(srcDB, nil, kFalse /*redrawNow*/);
	KESCMForceRedrawPagesPanelNow();

	// 変更ページ巡回(Next/Prev)の基準点も忘れる(次の比較へ持ち越さない)。
	KESCMResetNav();
	// Stop で sDB は nil(DropAll 済み)なので、位置表示は空・Prev/Next ボタンは無効へ戻る。
	KESCMRefreshNavPosition();
}

void KESCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
{
	KESCMDrawEventHandler::sPrintMarks = printFlag;
	KESCMDrawEventHandler::sMarkOpacity25 = opacity25Flag;
	// 常時表示(画面)の不透明度を印刷設定に合わせて即反映。
	KESCMDrawEventHandler::sMarkScreenOpacity = KESCMBaseScreenOpacity();

	// 実際にマークが描かれている対象文書(sDB)を優先して再描画する。呼び出し側 db(=アクティブ文書)が
	// それと異なっていても(Source や無関係な第3文書が前面の状態で操作した場合)、対象文書の見た目が
	// 即座に更新されるようにするため。Start 前(sDB==nil)は従来どおり db のみ再描画する。
	// Source 側の常時枠(Show Marks on Source)は 25%/75% 選択に連動するので、Source も再描画する。
	KESCMInvalidateDB(KESCMDrawEventHandler::sDB);
	if (db != KESCMDrawEventHandler::sDB)
		KESCMInvalidateDB(db);
	if (KESCMDrawEventHandler::sSrcDB != KESCMDrawEventHandler::sDB && KESCMDrawEventHandler::sSrcDB != db)
		KESCMInvalidateDB(KESCMDrawEventHandler::sSrcDB);
}

// 現在の印刷マーク設定を返す(パネル再表示時の状態復元に使用)。
bool16 KESCMGetPrintMarks()
{
	return KESCMDrawEventHandler::sPrintMarks;
}

bool16 KESCMGetMarkOpacity25()
{
	return KESCMDrawEventHandler::sMarkOpacity25;
}
