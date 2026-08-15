//========================================================================================
//
//  KESCMOversetApply.cpp
//
//  overset 走査の「適用」(KESCMActionComponent.cpp から分離。2026-08-13 の model/UI 分割
//  第1段 Task 2)。走査した結果(位置列とページ集合)をエンジンの状態へ入れ、それを見ている表示
//  (Pages パネルのサムネイル・スクロールバー地図・Prev/Next の巡回)を更新する。
//
//  ★分離では関数の中身を1行も変えていない。変えたのは「どのファイルに座るか」と「誰から見えるか」だけ。
//    KESCMOversetScanTargetDB は static を外した——呼び手3つ(Find Overset / Refresh Overset の
//    メニュー処理と UpdateActionStates)が KESCMActionComponent.cpp に残るため。
//
//  model 側。⚠この時点では逆流を3本持っている(サムネイル・地図・Prev/Next)。Task 8/10 で通知へ反転する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル:
#include "ISession.h"				// GetExecutionContextSession(走査前の文書生存確認)
#include "IApplication.h"
#include "IDocumentList.h"			// FindDocByDataBase(閉じた db を deref しないための生存確認)
#include <vector>
#include <set>

// プロジェクト内:
#include "KESCMOversetApply.h"
#include "KESCMCore.h"				// KESCMActiveDocDB / KESCMInvalidateDB
#include "KESCMDrawEventHandler.h"	// sOversetOn/sOversetDB/sOversetPages/sOversetLocs/sDB(走査状態)
#include "KESCMOversetScan.h"		// KESCMCollectOversetLocations / KESCMOversetLoc(検出そのもの)
#include "KESCMID.h"				// kKESCMOversetRescannedMessage
#include "KESCMModelNotify.h"		// KESCMNotifyDocs(model は UI を呼ばない ---- 知らせるだけ)
// ★2026-08-13(Task 10): UI 側ヘッダー3本(KESCMThumbnailRefresh / KESCMScrollMap / KESCMChangeNav)の
//   include を落とした。あふれ走査の結果をどう見せるかは、通知を受けた UI が決める。

/* KESCMApplyOversetForDoc(KESCMOversetApply.h で宣言) — db を走査して overset を反映する共有処理。
   Find Overset(ON)/Refresh Overset(armed時Target)/Start(overset ON時) から呼ぶ。前回と別文書なら
   前の文書のサムネイル目印を消す。ステータス行は呼び出し側が用途別に出す。 */
void KESCMApplyOversetForDoc(IDataBase* db)
{
	if (db == nil)
		return;

	// ★最終ライン防御(2026-07-24): 大半の呼び出し(DoFindOverset/DoRefreshOverset/Start 経路)は
	//   その場解決した生きた db を渡すが、Stop 経路だけは保存済みの sOversetDB を渡す。クローズ
	//   responder が漏れて閉じた文書のポインタが来た場合に、下の走査(KESCMCollectOversetLocations)で
	//   解放済み IDataBase を deref しないよう、ここで一度だけ生存確認する(FindDocByDataBase への
	//   ポインタ比較のみ=deref しない。KESCM 全体の共通規約)。死んでいたら何もせず戻る。
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return;

	// 前回の走査対象を控える(別文書へ移ったら、前の文書の目印を消す必要がある)。
	// ★2026-08-13(Task 10): **旧ページ集合はもう控えていない。** サムネイルの Purge は UI 側へ移り、
	//   通知では集合を運べないので、UI は対象文書の全ページを Purge する(理由と戻し方は
	//   KESCMThumbnailRefresh.h の KESCMPurgeAllPageThumbs)。同一文書で「あふれが解消したページ」の
	//   ＋を消すのも、全ページ Purge なら取りこぼしようがない。
	IDataBase* prevDB = KESCMDrawEventHandler::sOversetDB;

	// db を走査して overset 位置(＋点)とページを集める。
	std::vector<KESCMOversetLoc> locs;
	KESCMCollectOversetLocations(db, locs);
	std::set<UID> pages;
	for (size_t i = 0; i < locs.size(); ++i)
		pages.insert(locs[i].pageUID);

	KESCMDrawEventHandler::sOversetOn = kTrue;
	KESCMDrawEventHandler::sOversetDB = db;
	KESCMDrawEventHandler::sOversetPages.swap(pages);
	KESCMDrawEventHandler::sOversetLocs.swap(locs);

	// ★走査対象の文書が変わったか。変わったなら巡回の基準点も捨てる(2026-08-06 再点検)。「同じ作りの
	//   文書は UID まで同じ」(KESCMChangeNav.h)なので、前の文書の基準点を持ち越すと UID の偶然一致で
	//   途中から巡回が始まり得る。別文書へ移るのは未 arm(アクティブ文書走査)のときだけ=比較の巡回には
	//   影響しない。⚠**同一文書での再走査では捨てない** ---- あふれを1つ直すたびに巡回が先頭へ戻る。
	const bool16 movedToAnotherDoc = (prevDB != nil && prevDB != db);

	// レイアウトビューの無効化は model の仕事(描画データを持っているのはこちら)。
	KESCMInvalidateDB(db);
	if (movedToAnotherDoc)
		KESCMInvalidateDB(prevDB);

	// ★★2026-08-13(Task 10): ここから先 ---- Pages パネルのサムネイル・スクロールバー地図の注入と
	//   描き直し・Prev/Next の対象と位置 ---- は**すべて UI の持ち物**なので、通知1本にまとめた。
	//   docA=今の走査対象／docB=直前の走査対象(別文書へ移ったときだけ。同じ文書なら nil を渡す＝
	//   UI は1文書ぶんだけ作り直す)。
	KESCMNotifyDocs(kKESCMOversetRescannedMessage,
	                db,
	                movedToAnotherDoc ? prevDB : nil,
	                movedToAnotherDoc /*navReset*/);
}

// 比較中なら overset の走査対象は必ず比較 Target 文書(sDB)にする(変更(枠)と overset を同じ文書で
// Prev/Next 巡回できるように=nav の navDB と一致させる)。未 Start ならアクティブ文書。
IDataBase* KESCMOversetScanTargetDB()
{
	if (KESCMDrawEventHandler::sDB != nil)
		return KESCMDrawEventHandler::sDB;	// = KESCMNavDoc() の armed 分岐と同じ
	return KESCMActiveDocDB();
}

// KESCMOversetApply.cpp 終わり。
