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
#include "ITextUtils.h"				// GetPageUIDRef(KESCMFramePageUID の主経路)
#include "IHierarchy.h"				// GetOwnerPageUID へ渡す(KESCMFramePageUID のフォールバック)
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IMasterSpreadList.h"		// GetMasterSpreadCount / GetNthMasterSpreadUID(マスターページの収集)
#include "IBoolData.h"				// スプレッドの隠し状態(IID_IHIDESPREADBOOLDATA)の読み取り
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA(kSpreadBoss 上の IBoolData。docs の boss 一覧で裏取り済み)
#include "PMString.h"
#include "PMRect.h"
#include "IGeometryFacade.h"		// GetItemBounds(ページ矩形をペーストボード座標で。手本=SnapTracker.cpp:610-616)
// ★ビュー探索の3本は 2026-08-13 に KESCMViewLookup.cpp へ移した(model/UI 分割 第1段 Task 3)。
//   それだけが使っていた SDK インクルード(IControlView / IEventUtils / IWindow / IWindowUtils /
//   IDocumentPresentation / IPanelControlData / LayoutUIID / ILayoutViewUtils / ILayoutControlData /
//   K2Vector / PMPoint)も一緒に移っている。
//   ⇒ **このファイルはビュー系のヘッダーを1本も include しなくなった**(model 側に必要な条件の1つ)。
#include "ProgressBar.h"			// TaskProgressBar(重い比較の進捗バー＋キャンセル)
#include "ErrorUtils.h"				// PMSetGlobalErrorCode(キャンセル後にグローバルエラーを残さない)

#include <vector>
#include <set>
#include <map>						// KESCMDoMarkChangesDoc のペアリング map(間接includeに頼らず明示 2026-07-25)

#include "KESCMDrawEventHandler.h"   // 描画エンジン＋共有 static
#include "KESCMPeek.h"               // KESCMBaseScreenOpacity
#include "KESCMViewSync.h"           // KESCMInvalidateSyncCaches(2026-08-13 に KESCMPeek.h から移動)
#include "KESCMPageMap.h"            // KESCMBuildPairing(除外対応表)
#include "KESCMPageCheck.h"          // KESCMPageCheckClearAllDocs(Stop で✓を全消去)
#include "KESCMThumbnailRefresh.h"   // ★実験: 既表示サムネイルの再生成トライ(2026-07-06)
#include "KESCMChangeNav.h"          // KESCMResetNav(セッションを跨いだ巡回基準点の持ち越しを断つ)
#include "KESCMScrollMap.h"          // KESCMScrollMapInvalidateAll(比較後にスクロールバー地図を最新化)
#include "KESCMStoryStamp.h"         // ストーリーの変更カウンター(テキストが編集されたか＝画素比較には出せない情報)
#include "KESCMStoryList.h"          // 変更のあったストーリーの一覧(Story Edits セクションが読むモデル)
#include "KESCMStoryTree.h"          // KESCMStoryTreeRebuild(モデルを作り直したら画面も作り直す)
#include "KESCMStorySection.h"       // KESCMUpdateStorySectionLabel(見出しの件数)
#include "KESCMHideUnchanged.h"      // KESCMResetHideUnchanged(2026-08-13 に KESCMCore.h から移動)
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
// KESCMCollectMasterPageUIDs(KESCMCore.h で宣言)
//   マスタースプレッドのページを集める。上の KESCMCollectPageUIDs と対になるが、意図的に別関数。
//   ★マスタースプレッドは IMasterSpreadList の別管理で、ISpreadList には一度も現れない。
//   ★out をクリアしないので、通常ページの列の後ろへそのまま連結できる。
//========================================================================================
void KESCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<IMasterSpreadList> masterList(db, db->GetRootUID(), UseDefaultIID());
	if (masterList == nil)
		return;
	const int32 nm = masterList->GetMasterSpreadCount();
	for (int32 m = 0; m < nm; ++m)
	{
		const UID spreadUID = masterList->GetNthMasterSpreadUID(m);
		InterfacePtr<ISpread> spread(db, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();
		for (int32 p = 0; p < np; ++p)
			out.push_back(spread->GetNthPageUID(p));
	}
}

//========================================================================================
// ページアイテムの UID → そのアイテムが載っているページ UID。どのページにも載らない
// (ペーストボード等)なら kInvalidUID。
//
// 道が2本あるのは、片方だけでは答えが出ないため。主経路 ITextUtils::GetPageUIDRef は
// テキストフレーム前提の purpose-built API、フォールバック IHierarchy + ILayoutUtils::
// GetOwnerPageUID は一般解。どちらの答えも kPageBoss で検証してから返す——GetOwnerPageUID は
// 「ページに載っていなければ spread の UID を返す」と契約に明記されており(ILayoutUtils.h:102-107)、
// 検証しないとその spread UID をページと取り違える。
//
// ★KBS は同じ場面で意図的に検証しない(KBSSearchEngine.cpp:799-804)。あちらはペーストボード上の
//   ヒットを "PB" と綴りたいから。KESCM は実ページか何も無いかの二択でよいので検証する。
//   両者は目的が違って割れているので、片方に合わせて「直して」はいけない。
//
// ★2026-08-09 に KESCMOversetScan.cpp の static からここへ移した。Story Edits の一覧が
//   「ストーリーの先頭フレームはどのページか」を同じ問いとして必要としたため(写すと割れる)。
//========================================================================================
UID KESCMFramePageUID(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return kInvalidUID;

	// 主経路: textFrame 前提の purpose-built API。
	const UIDRef pageRef = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID));
	const UID pageUID = pageRef.GetUID();
	if (pageUID != kInvalidUID && db->GetClass(pageUID) == kPageBoss)
		return pageUID;

	// フォールバック: IHierarchy → GetOwnerPageUID(off-page なら spread UID)。実ページのみ採用。
	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier != nil)
	{
		const UID owner = Utils<ILayoutUtils>()->GetOwnerPageUID(hier);
		if (owner != kInvalidUID && db->GetClass(owner) == kPageBoss)
			return owner;
	}
	return kInvalidUID;	// どのページにも載らない(ペーストボード等)=スキップ
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
			// ★ページ矩形をペーストボード座標で得るのは Facade の仕事(2026-08-06 ブロック12 監査で寄せた。
			//   ブロック4 はここを「TransformUtils の標準イディオム＝公式どおり」と判定していたが、
			//   Facade を見ていなかった＝ブロック10 で台帳ごと訂正済み。訂正の対象がここ)。
			//   手本 snapshot/SnapTracker.cpp:610-616 が**ページに対して**同じことをしている。
			//   ★上の nil 判定と下の入れ替えは残す: 「幾何を持つか」も「矩形が正規化済みか」も
			//   Facade は担保しない(旧実装がついでに担保していたぶん)。
			const PMRect bb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
				::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
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

/* KESCMRebuildStoryEdits
	Read both documents' story counters, work out which stories differ, and put the answer on screen.

	★★ONE PLACE, TWO CALLERS. The full comparison below calls it, and so does "KCM: Refresh Page
	Comparison" (KESCMPeek.cpp) - which does NOT go through KESCMDoMarkChangesDoc but re-compares the
	selected pages on its own. Written out twice, the two would drift; and the first thing that
	happened when only the comparison had it was that Refresh left the list showing the state before
	the edit (measured 2026-08-10). The nav position beside it is shared for exactly this reason.

	The list is rebuilt whole rather than patched, because stories do not divide up by page: one
	story can run across the pages that were refreshed and the pages that were not.

	★Reading the counters composes nothing, so this costs a walk of the story list and no more.
*/
void KESCMRebuildStoryEdits(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<KESCMStoryStamp> targetStamps;
	std::vector<KESCMStoryStamp> sourceStamps;
	KESCMStoryEdits::CollectStamps(targetDB, targetStamps);
	KESCMStoryEdits::CollectStamps(sourceDB, sourceStamps);

	// ⚠引数順は (source, target)。逆にすると「追加された」と「削除された」が入れ替わり、
	//   消えたストーリーが「追加」として数えられたうえで本当の追加が黙って落ちる。
	std::vector<KESCMStoryDiff> storyDiffs;
	KESCMStoryEdits::Compare(sourceStamps, targetStamps, storyDiffs);

	// 読むのは Target 側だけ(行はすべて Target に存在する=Compare の契約)。ページ順の並べ替えと
	// 本文先頭の取り出しは Build の中で完結する。
	KESCMStoryList::Build(targetDB, storyDiffs);

	// ★モデルを作ったら画面もその場で作り直し、見出しの件数も書き換える。パネルが閉じていても、
	//   セクションが畳まれていても呼んでよい(どちらも中で静かに諦める)＝「開いているか」を
	//   呼び手が知らなくて済む。
	// ★★件数はステータス行ではなく**見出し**に出す。ステータス欄は4行枠がすでに埋まっており、
	//   もう1行増えると failed=N がはみ出す(段階3の申し送り)。見出しなら、セクションを閉じた
	//   ままでも件数が読める。
	KESCMStoryTreeRebuild();
	KESCMUpdateStorySectionLabel();
}

ErrorCode KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// ★全ページのラスタ化は未組版ストーリーの lazy recompose を誘発し得る=「聞くだけで組む→組めば
	//   dirty になる」(KESCMOversetScan.cpp の (0) と同じ理屈)。KESCM は「モデルを書き換えない・dirty に
	//   しない」が設計の核なので、入る前が clean なら出るとき clean へ戻す(2026-08-06 再点検)。
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

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

	// ★マスタースプレッドのページを後ろに連結する(2026-08-11)。従来はマスターが一度も比較されて
	// いなかった(KESCMCollectPageUIDs が ISpreadList=通常スプレッドしか回さないため。マスターに
	// 出ていた枠はあふれ「+」だけ)。
	// ★★連結するだけでよい理由: この後の比較ループ・進捗バーの総数・差分再比較のキャッシュ
	//   (sPrevPairTargetToSource)・Source 側の対応表(sSrcPageToTarget)は、すべて tPages/sPages の
	//   添字で回っている。MakeEntry はページの UIDRef しか見ない(中身が通常ページかマスターページかを
	//   気にしない)ので、ここに足すだけで全部が乗る。2026-08-11 に実機で実証済み。
	// ★KESCMBuildPairing 自体には足さない: あれの契約は「通常ページの除外対応表」で、TSV 出力など
	//   他の呼び手も居る。連結は呼び出し側の責任にする。
	{
		std::vector<UID> tMaster, sMaster;
		KESCMBuildMasterPairing(targetDB, sourceDB, tMaster, sMaster);
		tPages.insert(tPages.end(), tMaster.begin(), tMaster.end());
		sPages.insert(sPages.end(), sMaster.begin(), sMaster.end());
	}

	const size_t n = tPages.size();	// 各 Build 関数が短い方へ切り詰め済み(tPages/sPagesは同じ長さ)

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

	// ★これから実際にラスタ化するページ(tPages/sPages の添字)を先に確定する。進捗バーの総数に使うほか、
	//   差分側は「対象かどうか」の判定を1回で済ませられる(以前は判定とラスタ化が同じループにあった)。
	std::vector<size_t> toRaster;
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

		// (2) 再計算対象: 今回ペアの target のうち、前回ペアが無かった/相手が変わったものだけ。
		//     ペア不変ページは触らない(=前回結果を再利用=ラスタ化しない=ここが高速化の核)。
		for (size_t i = 0; i < n; ++i)
		{
			std::map<UID, UID>::const_iterator oit = oldMap.find(tPages[i]);
			if (oit == oldMap.end() || oit->second != sPages[i])
				toRaster.push_back(i);
		}
	}
	else
	{
		// 【全再比較】ドキュメント単位の総入れ替え(Start・Ignore Page Number 切替・フォールバック)。
		KESCMDrawEventHandler::DropAll();
		KESCMDrawEventHandler::sDB = targetDB;
		// 対象文書を丸ごと入れ替えるので、変更ページ巡回(Next/Prev)の基準点も捨てる。旧文書のページ UID を
		// 持ち越すと、別文書での UID 偶然一致で誤った位置から巡回が始まるため(差分再比較の側は同一文書なので触らない)。
		KESCMResetNav();
		toRaster.reserve(n);
		for (size_t i = 0; i < n; ++i)
			toRaster.push_back(i);
	}

	// ★重い比較には進捗バーとキャンセルを出す(2026-07-27)。総数は「これから実際にラスタ化する枚数」
	//   (差分なら再計算するページだけ)。タイトルは KESCM の他の文言と同じく英語固定＝翻訳キー扱いを
	//   避けるため SetTranslatable(kFalse)。
	// ★★showImmediate(第3引数)は「時間がかかったら自動で出す」ではない。kFalse(既定)は「出さない」で、
	//   100 ページの比較でも一度も現れなかった(2026-07-27 実機で判明)。→ 出す/出さないは自前のしきい値
	//   kKESCMProgressBarMinPages(KESCMConstants.h。経緯もそこに記載)で決める。登録トグルによる数ページの
	//   差分再比較ではバーを出さず、本格的な比較では必ず出る。
	const int32 rasterCount = (int32)toRaster.size();
	const bool8 showBar = (rasterCount >= kKESCMProgressBarMinPages) ? kTrue : kFalse;
	PMString barTitle(rasterCount == 1 ? "Comparing 1 page..." : "Comparing pages...");
	barTitle.SetTranslatable(kFalse);
	TaskProgressBar progress(barTitle, rasterCount, showBar);
	progress.DisableChildProgressBars(kTrue);	// ラスタ化の内部処理が自分のバーを出すのを抑える

	bool16 cancelled = kFalse;
	int32 changedCount = 0;
	int32 failedCount = 0;
	for (size_t k = 0; k < toRaster.size(); ++k)
	{
		const size_t i = toRaster[k];

		PMString item("Page ");
		item.AppendNumber((int32)(k + 1));
		item.Append(" / ");
		item.AppendNumber(rasterCount);
		item.SetTranslatable(kFalse);	// 数値入りなので翻訳対象にしない
		progress.DoTask(item);			// ★1件進める(前の1件の完了もここで反映される)

		bool16 changed = kFalse;
		const ErrorCode mkErr =
			KESCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
		if (mkErr != kSuccess)
		{
			// ★比較できなかったページ(ページサイズ不一致のペア・ラスタ化失敗・OOM)を「変更なし」と
			//   混同しない(2026-08-06 再点検)。今回ペアリングから外す=次回は差分再比較でも必ず比較し直す
			//   (載せたままだと「ペア不変=前回結果を再利用」と判定され、失敗が「比較済み・差なし」の
			//   見た目で固定化される)。件数は下の report に failed=N で出す。
			newMap.erase(tPages[i]);
			++failedCount;
		}
		else if (changed)
			++changedCount;

		// ★キャンセル判定は「1ページを比較し終えた安全な場所」で行う(WasCancelled はイベントを回すので、
		//   ラスタ化の途中では見ない)。引数 kFalse = グローバルエラー状態を立てない(立てると後続の
		//   コマンドが巻き添えで失敗する)。
		// ★★最後のページの後では見ない(2026-07-30 の監査で修正)。この経路のキャンセルは
		//   「全マークを破棄して Stop へ戻す」なので、全ページ終わった直後に押されたのを拾うと
		//   **完了している比較を丸ごと捨ててしまう**(100 ページ比較した後なら全部やり直しになる)。
		//   残りが無い＝もう中断する余地が無いので、判定自体が無意味。
		//   ※Refresh 経路(KESCMPeek.cpp)は「そこまで更新した分を残す」設計で、最後に押されても
		//     失うものが無い(ステータスに "- cancelled" と出るだけ)ため、あちらは現状のままでよい。
		if (k + 1 < toRaster.size() && progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			break;
		}
	}
	// 差分では、今回ラスタ化しなかったページ(前回結果の再利用分)も現在の変化ページ数に含める。
	if (doIncremental && !cancelled)
		changedCount = (int32)KESCMDrawEventHandler::sEntries.size();

	if (cancelled)
	{
		// ★キャンセル: 「比較済みページと未比較ページの混在」を残さない。マークを全部捨てて
		//   「比較していない」状態へ戻す(変更が無いのか、まだ見ていないのかが区別できない画面を作らない)。
		//   前回ペアリングも DropAll が捨てるので、次の比較は必ず全ページを見直す(差分で取りこぼさない)。
		//   ここでペアリング(newMap)を記録しないのが肝。
		KESCMDrawEventHandler::DropAll();
		changedCount = 0;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// 中断で立った可能性のあるエラーを持ち越さない
	}
	else
	{
		// 今回のペアリングを次回の差分用に記録する(差分・全再比較のどちらの経路でも)。
		KESCMDrawEventHandler::sPrevPairTargetToSource.swap(newMap);

		// sSrcDB/対応表は MakeEntry が変化ページ登録時に埋めるが、変化ゼロでも db だけは明示しておく
		// (エントリが無ければ wantSrcMarks が空判定で落ちるので描画コストは増えない)。
		// ★「Show Marks on Source」の既定 ON はここでは立てない(2026-07-25 監査で移動): この関数は Start
		//   だけでなく登録トグルの差分再比較・Ignore Page Number 切替の再比較も通るため、ここで kTrue に
		//   戻すとユーザーが OFF にした直後の再比較で黙って ON に戻ってしまう。既定 ON へ戻すのは仕様どおり
		//   Start 経路(KESCMToggleStartStop)のみ。
		KESCMDrawEventHandler::sSrcDB = sourceDB;
	}

	// overflow("/")キャッシュを今の対応表から作り直す。ここは Start・登録 Add/解除・Ignore 切替が
	// すべて通る唯一の再比較路なので、これらの操作後は描画側が最新の overflow を使う(描画のたびの
	// 全文書走査は EnsureOverflowCache 側で回避)。
	KESCMDrawEventHandler::RebuildOverflowCache();
	// ビューポート同期が持つ除外対応表キャッシュも同じ理由でここで捨てる(登録 Add/解除でペアが動く。
	// 2026-07-25 追補。呼び忘れても 250ms の TTL で追従するが、明示しておけば次の1通知から正しい)。
	KESCMInvalidateSyncCaches();

	// ★「KCM: Check」の✓: 再比較で「マーク(枠/「/」)が無くなったページ」のチェックを忘れる
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
	if (cancelled)
	{
		// キャンセルしたことと、その結果マークが無くなったことの両方を出す(枠が消えた理由が分かるように)。
		report.Append("comparison cancelled");
		report.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
		report.Append("marks cleared");
	}
	else
	{
		report.Append("marks start");
		report.AppendW(UTF32TextChar(0x0A));	// 改行 → 2行目へ
		report.Append("pages compared="); report.AppendNumber((int32)n);
		report.Append(" changed="); report.AppendNumber(changedCount);
		// ★比較できなかったページは隠さない(そのページは「枠が無い=変更なし」とは限らない)。
		if (failedCount > 0)
		{
			report.Append(" failed="); report.AppendNumber(failedCount);
		}

		// ★Story Edits の一覧。画素比較が答えるのは「このページは違って見える」までで、
		//   「テキストが変わったのか、レイアウトだけ動いたのか」は区別できない。両者は補い合う
		//   ——ストーリーが無変更でもページは動きうるし、ページが同じに見えてもテキストは変わりうる。
		KESCMRebuildStoryEdits(targetDB, sourceDB);
	}
	outReport = report;

	// Prev/Next 間の現在位置表示(k/N・-)と Prev/Next ボタンの有効/無効を、確定した最新の変更ページ集合で
	// 作り直す。Start・差分再比較・登録(Add/Remove)・Check がすべてこの関数を通るので、Next/Prev を
	// 押さなくても集合の変化に即時追従する(ユーザー要望 2026-07-15。全再比較路では上で KESCMResetNav 済み
	// =未巡回扱いで "1/N")。
	KESCMRefreshNavPosition();
	// ★キャンセルは kFailure で返す。Start 経路(KESCMToggleStartStop)はこの戻り値を見て arm するかどうかを
	//   決めるので、ここを常に kSuccess にすると「キャンセルしたのに arm され、メニューが Stop のまま」
	//   になる(2026-07-27 実機で発生)。
	return cancelled ? kFailure : kSuccess;
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

	// ★「KCM: Check」の✓も Stop で丸ごと忘れる(ユーザー指定: Start 中限定・Stop で消去)。
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

	// ★Story Edits の一覧も同じく忘れる。次の比較まで残しておくと、もう比較していない2文書の
	//   差分を指したまま**クリックすれば飛べてしまう**行が並ぶことになる(ジャンプは段階4)。
	// ★見出しは括弧つきの件数を落として "Story Edits" に戻る ---- KESCMUpdateStorySectionLabel が
	//   arm 状態を見て決めるので、ここは順番に呼ぶだけでよい。
	KESCMStoryList::Clear();
	KESCMStoryTreeRebuild();
	KESCMUpdateStorySectionLabel();
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
