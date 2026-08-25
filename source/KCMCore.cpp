//========================================================================================
//
//  KCMCore.cpp
//
//  ChangeMarker の共有操作(KCMCore.h で宣言)。KCMScriptProvider.cpp から分離したもの。
//  スクリプトメソッドとパネルのウィジェットオブザーバが完全に同じ挙動を駆動できるよう、ただの関数に
//  してある。描画エンジン(KCMDrawEventHandler)・peek モジュールへ委譲する。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"
#include "ISession.h"			// GetExecutionContextSession(KCMIsDocDBOpen)
#include "IActiveContext.h"		// GetContextDocument(KCMActiveDoc)
#include "IApplication.h"		// QueryDocumentList(KCMIsDocDBOpen)
#include "IDocumentList.h"		// FindDocByDataBase=生存確認のポインタ比較(KCMIsDocDBOpen)
#include "IDataBase.h"
#include "IDocument.h"
#include "ILayoutUtils.h"
#include "ITextUtils.h"				// GetPageUIDRef(KCMFramePageUID の主経路)
#include "IHierarchy.h"				// GetOwnerPageUID へ渡す(KCMFramePageUID のフォールバック)
#include "IGeometry.h"
#include "ISpread.h"
#include "ISpreadList.h"
#include "IMasterSpreadList.h"		// GetMasterSpreadCount / GetNthMasterSpreadUID(マスターページの収集)
#include "IPageList.h"				// ★GetPageCount / GetNthPageUID(文書の平坦なページ列。2026-08-16 の監査 B3・A-3)
#include "IBoolData.h"				// スプレッドの隠し状態(IID_IHIDESPREADBOOLDATA)の読み取り
#include "SpreadID.h"				// IID_IHIDESPREADBOOLDATA(kSpreadBoss 上の IBoolData。docs の boss 一覧で裏取り済み)
#include "PMString.h"
#include "PMRect.h"
#include "PMPoint.h"				// PMRect::PointIn に渡す点(間接includeに頼らず明示 2026-08-16)
#include "IGeometryFacade.h"		// GetItemBounds(ページ矩形をペーストボード座標で。手本=SnapTracker.cpp:610-616)
// ★ビュー探索の3本は 2026-08-13 に KCMViewLookup.cpp へ移した(model/UI 分割 第1段 Task 3)。
//   それだけが使っていた SDK インクルード(IControlView / IEventUtils / IWindow / IWindowUtils /
//   IDocumentPresentation / IPanelControlData / LayoutUIID / ILayoutViewUtils / ILayoutControlData /
//   K2Vector / PMPoint)も一緒に移っている。
//   ⇒ **このファイルはビュー系のヘッダーを1本も include しなくなった**(model 側に必要な条件の1つ)。
#include "ProgressBar.h"			// TaskProgressBar(重い比較の進捗バー＋キャンセル)
#include "ErrorUtils.h"				// PMSetGlobalErrorCode(キャンセル後にグローバルエラーを残さない)

#include <vector>
#include <set>
#include <map>						// KCMDoMarkChangesDoc のペアリング map(間接includeに頼らず明示 2026-07-25)

#include "KCMDrawEventHandler.h"   // 描画エンジン＋共有 static
#include "KCMThreadSafety.h"       // ★マーク色を変えたときリング画像のキャッシュを畳むのに entry を走査する
#include "KCMPeek.h"               // KCMBaseScreenOpacity
#include "KCMPageMap.h"            // KCMBuildPairing(除外対応表) / KCMPageMapCollectRegistered
#include "KCMPageCheck.h"          // KCMPageCheckClearAllDocs(Stop で✓を全消去)
#include "KCMStoryStamp.h"         // ストーリーの変更カウンター(テキストが編集されたか＝画素比較には出せない情報)
#include "KCMStoryList.h"          // 変更のあったストーリーの一覧(Story Edits セクションが読むモデル)
#include "KCMStoryDiffRun.h"       // Story モードで「どこがどう変わったか」を行に付ける(2026-08-20)
#include "KCMHideUnchanged.h"      // KCMResetHideUnchanged(2026-08-13 に KCMCore.h から移動)
// ★★2026-08-13(Task 10): **UI 側ヘッダー6本の include を落とした** ---- KCMViewSync /
//   KCMThumbnailRefresh / KCMChangeNav / KCMScrollMap / KCMStoryTree / KCMStorySection。
//   比較の後始末のうち「画面を作り直す」部分は全部 KCMNotify*() の通知になり、このファイルは
//   **何が変わったかを言うだけ**になった。⇒ 比較エンジンから UI への依存はゼロ。
#include "KCMCore.h"
#include "KCMID.h"			// kKCM*Message(通知の ID。Task 10 で使い始めた)
#include "KCMModelNotify.h"	// KCMNotifyStatus / KCMNotifyDocs - the model tells the UI, it never calls it

//========================================================================================
// ヘルパ: ドキュメント内の全ページUIDを、文書のページ順(平坦)で集める。
//
// ★★2026-08-16(API 監査 B3・A-3)= **ISpreadList → ISpread の2重ループから IPageList へ寄せた。**
//   `IPageList.h:71-74` が自分でこう名指ししている ---- 「caches commonly needed information about
//   pages in the document. All the information is computed only when needed. It is ***much* more
//   efficient to use this than to compute the same information from other sources**」。
//   旧実装(スプレッドを回してページを拾う)は、まさにその "other sources" だった。
//
// ★**寄せてよいと決めた根拠は実測**(2026-08-16)。ヘッダーの契約は "does not include master pages"
//   (`:81`)までしか言わず、**隠しスプレッドの扱いを書いていない**(`GetPageIndex` にだけ
//   `includePagesOfHiddenSpread` がある＝`:104`)。KCM の平坦ページ番号は「隠していない時と同じ番号」
//   であることが新旧対応の土台なので、**Hide Unchanged で2スプレッドを隠した状態のまま件数と UID 列を
//   全数突き合わせ**た ⇒ `[pl=4 walk=4 SAME-ORDER]`＝**隠しページも含み、順序も2重ループと完全に同じ**。
//   全文＝docs/ai-notes/kescm-api-audit-b3-2026-08-16.md
//
// ⚠**マスターページを含まない性質は変わらない**(`:81` が明記)。呼び手のうち比較(下の
//   KCMDoMarkChangesDoc)・TSV・Prev/Next の3つが「マスターは別に足す」と書いてその性質に依存して
//   いるが、寄せても前提は保たれる(**根拠が ISpreadList から IPageList の契約に移っただけ**)。
// ⚠**out はクリアしない**(呼び手が通常ページの列の後ろへマスターを連結する使い方)。
//========================================================================================
void KCMCollectPageUIDs(IDataBase* db, std::vector<UID>& out)
{
	if (db == nil)
		return;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return;
	const int32 n = pageList->GetPageCount();
	if (n <= 0)
		return;
	out.reserve(out.size() + (size_t)n);
	for (int32 i = 0; i < n; ++i)
		out.push_back(pageList->GetNthPageUID(i));
}

//========================================================================================
// KCMCollectChangedPageUIDs(KCMCore.h で宣言)
//   db が現在の比較対象(sDB/sSrcDB)なら「今マークが出得るページ UID」(変更リング + overflow「/」+
//   登録「/」)を outPages へ足して kTrue。比較対象でなければ何もせず kFalse。
//   ★「何がマーク済みか」の定義はこの1箇所に集約する。マークの種類を増やす時はここへ足せば、
//     再比較前の退避(KCMDoMarkChangesDoc)とサムネイルの Purge(UI 側)の両方が自動で追随する。
//
// ★★2026-08-13 に KCMThumbnailRefresh.cpp からここへ移した(model/UI 分割 第1段 Task 10)。
//   置いてあったファイル名は「サムネイル更新」だが、**中身は純粋に model の問い**——読むのは
//   sEntries / overflow キャッシュ / 登録ページだけで、widget にも view にも一切触らない。
//   UI 側ファイルに置いたままだと、これを呼ぶだけの model 側3ファイル(このファイル・PageCheck・
//   PageMap)が UI ヘッダーを include し続けることになっていた。
//   ⇒ **これは通知で切る逆流ではなく、宣言の置き場所の誤りだった**(逆流台帳 §2-1)。
//   ⚠「UI ヘッダーを include している」だけでは逆流と断定できない、という実例そのもの。
//
// ⚠★★**なぜここは共有状態(sEntries ほか)をロックを取らずに全走査してよいのか**(2026-08-16・
//   API 監査 B4 で明文化)。KCMThreadSafety.h:86-93 の契約は「main が書き、**BG(PDF の非同期
//   書き出し)が描画で読む**から守る」であって、**この関数はその BG 側ではない**——呼び手を
//   全数数えると4つとも**メインスレッド**しかない:
//     ・このファイルの KCMCollectCheckablePageUIDs(すぐ下)＝Pixel モードのときだけ素通しする。
//       その先の呼び手は KCMPageCheck.cpp の3か所(トグル/メニュー状態/剪定)と Load の復元で、
//       ★**剪定だけは呼び手が既にロック済み**(KCMPageCheckPruneToMarked)
//     ・このファイルの再比較前の旧集合の退避(KCMDoMarkChangesDoc)
//     ・KCMFacades.cpp の IKCMMarkData 経由(＝UI から)
//   書き手も main だけなので、同一スレッド内では走査中に書き換わらない。⇒ ロックは要らない。
//   ★★**これは「今の呼び手の性質」であって構造ではない。** BG から呼ぶ経路を1つ足したら、
//     その瞬間に**解放済みメモリの読み取り**になる(sEntries は生ポインタの map で DropAll が
//     delete する)。**新しい呼び手を足すときは、それがどのスレッドで走るかを先に決めること。**
//========================================================================================
bool16 KCMCollectChangedPageUIDs(IDataBase* db, std::set<UID>& outPages)
{
	const bool16 overflowCacheMatches =
		(KCMDrawEventHandler::sOverflowCacheDB == KCMDrawEventHandler::sDB &&
		 KCMDrawEventHandler::sOverflowCacheSrcDB == KCMDrawEventHandler::sSrcDB);

	if (db != nil && db == KCMDrawEventHandler::sDB)
	{
		for (std::map<UID, KCMOverlayEntry*>::iterator it = KCMDrawEventHandler::sEntries.begin();
			 it != KCMDrawEventHandler::sEntries.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KCMDrawEventHandler::sOverflowT.begin(), KCMDrawEventHandler::sOverflowT.end());
		// ★登録ページ(Added=緑「/」)も含める。sEntries/overflow とは別集合なので、含めないと
		//   再比較時に登録ページのサムネイルが Purge されず緑「/」が即時に出ない(START 時も同様)。
		KCMPageMapCollectRegistered(db, outPages);
		return kTrue;
	}
	if (db != nil && db == KCMDrawEventHandler::sSrcDB)
	{
		for (std::map<UID, UID>::iterator it = KCMDrawEventHandler::sSrcPageToTarget.begin();
			 it != KCMDrawEventHandler::sSrcPageToTarget.end(); ++it)
			outPages.insert(it->first);
		if (overflowCacheMatches)
			outPages.insert(KCMDrawEventHandler::sOverflowS.begin(), KCMDrawEventHandler::sOverflowS.end());
		// ★登録ページ(Removed=緑「/」)も含める(上と同じ理由)。
		KCMPageMapCollectRegistered(db, outPages);
		return kTrue;
	}
	return kFalse;
}

//========================================================================================
// KCMCollectCheckablePageUIDs(KCMCore.h で宣言。理由と経緯は宣言側のコメント)
//   ★上の KCMCollectChangedPageUIDs のすぐ下に置いてあるのは、**2つが紛らわしいほど近い問いだから**。
//     片方を直すときにもう片方が目に入る位置に居させる。
//   ⚠**ロックを取らないのは上と同じ理由**(呼び手が全部メインスレッド。上の長い注記を参照)。
//     Story 分岐が読むのはポインタ2つとモードの enum 1つだけなので、そもそも走査すらしない。
//========================================================================================
bool16 KCMCollectCheckablePageUIDs(IDataBase* db, KCMCheckablePages& out)
{
	out.fAllPages = kFalse;
	out.fPages.clear();

	if (KCMGetCompareMode() != kKCMModeStory)
	{
		// Pixel = マークの付いたページだけ。★対象文書かの判定も向こうが持つ(比較対象でなければ kFalse)。
		if (!KCMCollectChangedPageUIDs(db, out.fPages))
			return kFalse;
		// ★★2026-08-24(ユーザー要望): **マスターページは、差が無くても常に ✓ を付けられる。**
		//   Pixel の規則「枠/「/」の付いたページだけ」(2026-07-11 のユーザー指定)はそのままで、
		//   マスターだけを例外にする。理由＝Story モードでは全ページに付けられるので**マスターにも
		//   付けられるのに、Pixel に切り替えた瞬間に付けられなくなる**という食い違いが出ていた。
		//   ⚠通常ページの規則は変えない(「変更が無いページに ✓ は要らない」は今も有効)。
		std::vector<UID> masters;
		KCMCollectMasterPageUIDs(db, masters);
		out.fPages.insert(masters.begin(), masters.end());
		return kTrue;
	}

	// Story モード = 比較中の2文書なら全ページ。★対象文書の判定は上の関数と同じ2つのポインタで行う。
	if (db == nil || (db != KCMDrawEventHandler::sDB && db != KCMDrawEventHandler::sSrcDB))
		return kFalse;

	out.fAllPages = kTrue;
	return kTrue;
}

//========================================================================================
// KCMCollectMasterPageUIDs(KCMCore.h で宣言)
//   マスタースプレッドのページを集める。上の KCMCollectPageUIDs と対になるが、意図的に別関数。
//   ★マスタースプレッドは IMasterSpreadList の別管理で、ISpreadList には一度も現れない。
//   ★out をクリアしないので、通常ページの列の後ろへそのまま連結できる。
//========================================================================================
void KCMCollectMasterPageUIDs(IDataBase* db, std::vector<UID>& out)
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
// KCMIsPageOnHiddenSpread(KCMCore.h で宣言) — そのページのスプレッドは隠されているか。
//
// ★2026-08-18(不具合再検査 B10 の2周目)に新設。KCM 内の隠し判定5か所は全部「ISpreadList を
//   回りながらそのスプレッドを見る」形で、**ページ UID から聞く**問いはここが初めて。
// ページ → スプレッドは IHierarchy::GetSpreadUID(この階層ノードのスプレッドを返す契約で、ページ
// 限定ではない)。KCMChangedPagesTSV の MasterPageDisplay / KCMPeek / KCMChangeNav と同じ聞き方。
// ⚠マスターページを渡しても kFalse で返る＝呼び手は場合分け不要。根拠は**このファイルの
//   KCMFindPageUnderMouse がマスターを走査する段の但し書き**（2026-08-16 の監査で明文化）＝
//   「マスタースプレッドを隠す機能は InDesign に無く、IID_IHIDESPREADBOOLDATA は kSpreadBoss 上の
//   通常スプレッドの話」。SpreadID.h の include 注記も「kSpreadBoss 上の IBoolData(docs の boss 一覧で
//   裏取り済み)」と書いている。⇒ Query が nil でも、取れても kFalse でも、どちらでも同じ答えになる。
// ⚠★★2026-08-19(不具合再検査 B-U8)訂正＝ここは「**この関数自身がマスターページで呼ばれる経路は
//   今は無い**（TSV のマスターループは通していない）。上は将来渡されたときの契約であって実測ではない」と
//   書いてあったが、**書いた日(2026-08-18)には既に経路があった**:
//     ・Prev/Next の巡回 … KCMBuildStops は **マスタースプレッドのページをストップに足す**
//       (2026-08-06 に overset、2026-08-11 に変更枠)。その pageUID がそのまま
//       KCMGoto と KCMStopLabel からここへ渡る(ui/KCMChangeNav.cpp)。
//     ・Story Edits の行 … マスター上のフレームの行なら ui/KCMStoryJump.cpp からも渡る。
//   ⇒ **契約(マスターは kFalse)は正しく、動作も正しい**。誤っていたのは「経路が無い」という全数宣言で、
//     TSV(＝この命題を書いた回の担当ファイル)しか数えていなかった。
//   ★[[verify-claims-in-comments]] §13 の再演＝**機能はブロックに属するが、命題は属さない。**
//     全数を書くときだけは、担当ファイルの外へ grep を広げる。
//========================================================================================
bool16 KCMIsPageOnHiddenSpread(IDataBase* db, UID pageUID)
{
	if (db == nil || pageUID == kInvalidUID)
		return kFalse;
	InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
	if (pageHier == nil)
		return kFalse;
	const UID spreadUID = pageHier->GetSpreadUID();
	if (spreadUID == kInvalidUID)
		return kFalse;
	// 隠し状態は kSpreadBoss 上の IBoolData(IID_IHIDESPREADBOOLDATA、kTrue=隠し中)で読む。
	InterfacePtr<IBoolData> hideFlag(db, spreadUID, IID_IHIDESPREADBOOLDATA);
	return (hideFlag != nil && hideFlag->GetBool()) ? kTrue : kFalse;
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
//   ヒットを "PB" と綴りたいから。KCM は実ページか何も無いかの二択でよいので検証する。
//   両者は目的が違って割れているので、片方に合わせて「直して」はいけない。
//
// ★2026-08-09 に KCMOversetScan.cpp の static からここへ移した。Story Edits の一覧が
//   「ストーリーの先頭フレームはどのページか」を同じ問いとして必要としたため(写すと割れる)。
//========================================================================================
UID KCMFramePageUID(IDataBase* db, UID frameUID)
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

// アクティブ(前面)文書とその db。KCMCore.h のコメント参照(2026-07-25 重複解消で集約)。
IDocument* KCMActiveDoc()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ac = session ? session->GetActiveContext() : nil;
	return ac ? ac->GetContextDocument() : nil;
}

IDataBase* KCMActiveDocDB()
{
	IDocument* doc = KCMActiveDoc();
	return doc ? ::GetUIDRef(doc).GetDataBase() : nil;
}

bool16 KCMFindPageUnderMouse(IDataBase* targetDB, PMReal mx, PMReal my, KCMPageHit& out,
                               UID onlySpreadUID)
{
	out.spreadIndex = -1; out.spreadUID = kInvalidUID; out.numPages = 0;
	out.globalPageBase = 0; out.hitPageIndex = -1; out.hitPageUID = kInvalidUID;
	out.isMaster = kFalse;
	if (targetDB == nil)
		return kFalse;
	InterfacePtr<ISpreadList> spreadList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return kFalse;
	const int32 ns = spreadList->GetSpreadCount();
	int32 globalIndex = 0;
	const PMPoint pt(mx, my);	// 聞く相手は PMRect::PointIn(下の2箇所で使う)

	// ★★★2026-08-19(不具合再検査 B-U6): 絞り込みは**種別(マスター/通常)の一段だけ**にする。
	//   2026-08-16〜2026-08-19 は下の2つのループがどちらも「**表示中スプレッド以外は全部落とす**」
	//   形で、**通常スプレッド同士まで落としていた**。⇒ ユーザー報告(2026-08-19)＝
	//   「**選択されているスプレッド/ページの上でしか CMYK も Shift+ の peek も効かない。
	//     他のページへカーソルを持っていくと `---` になる**」。
	//   ★**曖昧なのはマスター⇔通常の間だけ**(そこだけがペーストボード座標で重なる＝KCMCore.h の実測)。
	//     **通常スプレッド同士は重ならない**——その証拠は、2026-08-16 に絞りを入れるまで
	//     **この関数はずっと通常スプレッドを全走査していて、通常同士の取り違えは一度も出ていない**こと。
	//   ∴ 表示中がマスターなら「通常を見ない＋そのマスターだけ」、表示中が通常なら
	//     「マスターを見ない＋**通常は全部見る**」。kInvalidUID(絞りなし)は従来どおり全走査。
	//   ⚠**Query は1回増える**(2026-08-19 に自分で書いた「増えない」を訂正)。旧は
	//     **通常スプレッドでヒットした時点で return するのでマスター一覧を引かなかった**——
	//     引くのは通常で外れた時だけだった。新は種別の判定に要るので**常に引く**。
	//     ⇒ この関数はマウスが動くたび通るが、増えるのは同じ DB のルート UID への Query 1回で、
	//       既にやっている ISpreadList + スプレッドごとの ISpread + ページごとの IGeometry に比べれば誤差。
	//       **速さより「絞りの単位を1か所で決める」ことを採った**([[one-question-one-place]])。
	//   ★判定の形は UI 側の先例 KCMScrollMap.cpp の KCMIsMasterSpread に合わせた
	//     (IMasterSpreadList::GetMasterSpreadIndex は「マスターでない UID を渡したときの戻り」が
	//      ヘッダーに書かれていないので使わない、という同じ理由)。
	InterfacePtr<IMasterSpreadList> mList(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	const int32 nm = (mList != nil) ? mList->GetMasterSpreadCount() : 0;
	bool16 viewingMaster = kFalse;
	if (onlySpreadUID != kInvalidUID)
	{
		for (int32 m = 0; m < nm; ++m)
		{
			if (mList->GetNthMasterSpreadUID(m) == onlySpreadUID)
			{
				viewingMaster = kTrue;
				break;
			}
		}
	}
	for (int32 s = 0; s < ns; ++s)
	{
		const UID spreadUID = spreadList->GetNthSpreadUID(s);
		InterfacePtr<ISpread> spread(targetDB, spreadUID, UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 np = spread->GetNumPages();

		// ★★2026-08-16 → 2026-08-19 改訂: **表示中がマスターのときだけ**通常スプレッドを見送る
		//   (理由は KCMCore.h＝マスターと通常はペーストボード座標で重なる)。
		//   ⚠**旧実装はここが `spreadUID != onlySpreadUID` で、通常スプレッド同士まで落としていた**
		//     ＝「選択中のスプレッド以外では CMYK も peek も効かない」の原因(関数冒頭の但し書き)。
		//   ⚠**globalIndex の加算は続ける**＝平坦ページ番号は「絞り込みの有無で変わらない」。
		if (onlySpreadUID != kInvalidUID && viewingMaster)
		{
			globalIndex += np;
			continue;
		}

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

		// ★★2026-08-16(API 監査 B3・A-2): まず**スプレッドの箱**で落とす。
		//   手本＝snapshot/SnapTracker.cpp:599-600(「点はこのスプレッドの上か」を GetPagesBounds + PointIn で聞く)。
		//   当たらないスプレッドのページ実測がまるごと消える——この関数は peek と色サンプラが
		//   **マウスが動くたびに**通る。
		//   ⚠**globalIndex の加算はここでも必ず続ける**（平坦ページ番号は「隠していない/外していない時と
		//     同じ番号」でないと、旧ドキュメントの平坦ページ列と対応が取れない）。
		//   ⚠聞いているのは「ページの上か」なので **GetPagesBounds**（ページだけ）。ペーストボードに置いた
		//     アイテムまで含める GetPagesAndItemsBounds は KBS のあふれマーカーの用途で、ここでは広すぎる。
		PMRect spreadBounds = spread->GetPagesBounds(Transform::PasteboardCoordinates());
		spreadBounds.Normalize();
		if (!spreadBounds.PointIn(pt))
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
			PMRect bb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
				::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
			// ★2026-08-16(B3・A-2): 内包判定も公式へ＝**PMRect::Normalize()**(PMRect.h:622)＋
			//   **PMRect::PointIn()**(:814-816 = 閉区間・PMReal の epsilon 比較)。手書きの入れ替え＋4項比較と
			//   **まったく同じ判定**で、行が減る。手本＝SnapTracker.cpp:616-617。
			//   ⚠**Normalize は落とさない**——PointIn は left<=right / top<=bottom を前提にした素の比較なので、
			//     非正規化の箱を渡すと**常に kFalse**(＝ページが1枚も当たらなくなる)。旧実装が手で入れ替えて
			//     いた担保がこれで、Facade は矩形が正規化済みだとは担保しない。
			bb.Normalize();
			if (bb.PointIn(pt))
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

	// ★★2026-08-16: **マスタースプレッドも当たり判定に入れる**（ユーザー報告＝「マスターページでは
	//   peek も CMYK も出ない。違いの枠は出ているのに」）。比較そのものは 2026-08-11 からマスターを
	//   扱っている（名前対応＝KCMBuildMasterPairing）のに、**マウス下のページを探すこの関数だけが
	//   ISpreadList＝通常スプレッドしか見ていなかった**。
	//
	// ⚠**なぜ「通常を全部見てから」なのか**＝★**順序で正しくなるのではない**（ここが要）。
	//   **2つの矩形は重なる**（2026-08-16 実測＝マスタースプレッドを表示したまま絞りなしで走査すると
	//   通常ページに当たった）ので、**通常を先に見てもマスターを先に見ても、片方を表示中は必ず誤る**。
	//   正しさを担保しているのは順序ではなく onlySpreadUID の絞り込み（理由の全文は KCMCore.h）。
	//   ∴ 通常を先に置く意味は「絞りを渡さない呼び手(kInvalidUID)に従来と同じ答えを返す」ことだけ。
	//
	// ⚠**隠しスプレッドの除外は入れない**——マスタースプレッドを隠す機能は InDesign に無く、
	//   IID_IHIDESPREADBOOLDATA は kSpreadBoss 上の通常スプレッドの話。
	// ⚠**globalIndex は加算しない**——マスターは平坦ページ列（IPageList）に居ないので番号を持たない。
	// ★mList / nm は関数冒頭で引いてある(種別の判定に要るため)。⚠そのぶん、通常スプレッドで
	//   ヒットする普通の経路でも一覧を1回引くようになった(旧はここまで来なければ引かなかった)。
	for (int32 m = 0; m < nm; ++m)
	{
		const UID msUID = mList->GetNthMasterSpreadUID(m);
		// ★2026-08-19: 表示中が**通常**スプレッドならマスターは一切見ない/表示中が**マスター**なら
		//   その表示中のマスターだけを見る(平坦番号はマスターには無いので加算も無い)。
		if (onlySpreadUID != kInvalidUID && (!viewingMaster || msUID != onlySpreadUID))
			continue;
		InterfacePtr<ISpread> ms(targetDB, msUID, UseDefaultIID());
		if (ms == nil)
			continue;
		const int32 mp = ms->GetNumPages();

		// 通常スプレッドと同じ2段（スプレッドの箱で足切り → ページごとに内包判定）。
		PMRect msBounds = ms->GetPagesBounds(Transform::PasteboardCoordinates());
		msBounds.Normalize();
		if (!msBounds.PointIn(pt))
			continue;

		for (int32 p = 0; p < mp; ++p)
		{
			const UID pageUID = ms->GetNthPageUID(p);
			InterfacePtr<IGeometry> geo(targetDB, pageUID, UseDefaultIID());
			if (geo == nil)
				continue;
			PMRect bb = Utils<Facade::IGeometryFacade>()->GetItemBounds(
				::GetUIDRef(geo), Transform::PasteboardCoordinates(), Geometry::PathBounds());
			bb.Normalize();
			if (bb.PointIn(pt))
			{
				out.spreadIndex    = -1;			// マスターはスプレッドリストに居ない
				out.spreadUID      = msUID;
				out.numPages       = mp;
				out.globalPageBase = -1;			// 平坦ページ番号を持たない
				out.hitPageIndex   = p;
				out.hitPageUID     = pageUID;
				out.isMaster       = kTrue;
				return kTrue;
			}
		}
	}
	return kFalse;
}

//========================================================================================
// 共有コア操作(KCMCore.h で宣言)。
//
// 以前はスクリプトメソッド内にインラインで書かれていた本体。今はパネルのウィジェットオブザーバ
// (KCMPanelObserver.cpp)が完全に同じ挙動を駆動できるよう、ただの(非 static)関数にしてある。
// この翻訳単位に置くのは意図的で、描画エンジン(KCMDrawEventHandler)と file-local な peek 状態
// (sPeek*)へ直接アクセスできるようにするため。
//========================================================================================

/* KCMRebuildStoryEdits
	Read both documents' story counters, work out which stories differ, and put the answer on screen.

	★★ONE PLACE, TWO CALLERS. The full comparison below calls it, and so does "Refresh Page
	Comparison" (KCMPeek.cpp) - which does NOT go through KCMDoMarkChangesDoc but re-compares the
	selected pages on its own. Written out twice, the two would drift; and the first thing that
	happened when only the comparison had it was that Refresh left the list showing the state before
	the edit (measured 2026-08-10). The nav position beside it is shared for exactly this reason.

	The list is rebuilt whole rather than patched, because stories do not divide up by page: one
	story can run across the pages that were refreshed and the pages that were not.

	★Reading the counters composes nothing, so this costs a walk of the story list and no more.
*/
void KCMRebuildStoryEdits(IDataBase* targetDB, IDataBase* sourceDB)
{
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<KCMStoryStamp> targetStamps;
	std::vector<KCMStoryStamp> sourceStamps;
	KCMStoryEdits::CollectStamps(targetDB, targetStamps);
	KCMStoryEdits::CollectStamps(sourceDB, sourceStamps);

	// ⚠引数順は (source, target)。逆にすると「追加された」と「削除された」が入れ替わり、
	//   消えたストーリーが「追加」として数えられたうえで本当の追加が黙って落ちる。
	std::vector<KCMStoryDiff> storyDiffs;
	KCMStoryEdits::Compare(sourceStamps, targetStamps, storyDiffs);

	// ★★2026-08-21: **両方の文書を渡す**。以前は「読むのは Target 側だけ（行はすべて Target に
	//   存在する＝Compare の契約）」だったが、**削除されたストーリーの行**は Target に無いので
	//   Source から読む（本文・先頭フレーム・ページ）。どちらから読むかは行の fKinds が決め、
	//   Build の中で完結する（ページ順の並べ替えと本文先頭の取り出しも従来どおり中で完結）。
	KCMStoryList::Build(targetDB, sourceDB, storyDiffs);

	// ★★Story モードのときだけ、行に「どこがどう変わったか」を付ける(2026-08-20)。
	//   カウンターが答えられるのは「このストーリーは変わった」までで、その先＝どの語がどう
	//   変わったかは本文を突き合わせないと出ない。Pixel モードでは呼ばない＝行は子を持たず、
	//   一覧はこれまでどおりの平らな見た目のままになる。
	//   ⚠**Build の後**でなければならない。変更は行を「並べ替え済みの何番目か」で名指しするので、
	//     並びが決まる前に走らせると別の行に付く。
	if (KCMGetCompareMode() == kKCMModeStory)
		KCMStoryDiffRun::Run(targetDB, sourceDB);

	// ★★2026-08-22: **書式だけが動いた行を一覧から落とす**（ユーザー指定「属性の変更は無視」）。
	//   カウンターが答えるのは「同じではない」までなので、フォント・色・スタイル・表の罫線を変えた
	//   だけのストーリーもここまでは行になっている。
	//   ★★**残るのは「テキストの変更」と「ルビ」だけ**（2026-08-23 ユーザー決定＝「ストーリーモードの
	//     StoryEdit にでるのは、テキストの変更と、ルビだけで」）。⚠**圏点は 2026-08-22 に一度入って
	//     同月 23 日に取りやめた**＝比較そのものを止めてあるので、圏点だけが動いたストーリーは
	//     フォントだけ変えた行と同じくここで落ちる。
	//   ⚠**必ずこの位置**＝Build と Run の**後**。前に置くと、Story モードでルビだけ変えた行が
	//     「Attr しか動いていない行」に見えたまま、差分がそのルビを見つける直前に落ちる。
	//   ★判定そのものは `KCMStoryRowFilter.h`（モードを見ない・InDesign の外で検査してある）。
	//   ⚠Pixel モードでは差分を走らせないので、**ルビだけの変更はここで落ちる**
	//     （2026-08-22 ユーザー判断＝Pixel では諦めて Text の変更だけ出す）。
	KCMStoryList::DropRowsWithNoContentChange();

	// ★モデルを作ったら画面もその場で作り直し、見出しの件数も書き換える。パネルが閉じていても、
	//   セクションが畳まれていても呼んでよい(どちらも中で静かに諦める)＝「開いているか」を
	//   呼び手が知らなくて済む。
	// ★★件数はステータス行ではなく**見出し**に出す。ステータス欄は4行枠がすでに埋まっており、
	//   もう1行増えると failed=N がはみ出す(段階3の申し送り)。見出しなら、セクションを閉じた
	//   ままでも件数が読める。
	// ★2026-08-13(Task 10): ツリーと見出しを直接呼ぶのをやめ、通知1本にした。model は「一覧を作り
	//   直した」とだけ言い、それを画面のどこへどう出すかは UI が決める。
	KCMNotify(kKCMStoryEditsRebuiltMessage);
}

ErrorCode KCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport, bool16 allowIncremental)
{
	if (targetDB == nil || sourceDB == nil)
		return kFailure;

	// ★全ページのラスタ化は未組版ストーリーの lazy recompose を誘発し得る=「聞くだけで組む→組めば
	//   dirty になる」(KCMOversetScan.cpp の (0) と同じ理屈)。KCM は「モデルを書き換えない・dirty に
	//   しない」が設計の核なので、入る前が clean なら出るとき clean へ戻す(2026-08-06 再点検)。
	IDataBase::SaveRestoreModifiedState targetDirtyGuard(targetDB);
	IDataBase::SaveRestoreModifiedState sourceDirtyGuard(sourceDB);

	// ★再比較の前に「今 枠/斜線が付いているページ」を控える(サムネイル取りこぼし対策)。再ペアリング
	//   (登録トグルでページ数差を無視した時など)で対応が1つズレると、overflow を抜けたページ(赤「/」が
	//   消える)や再ペアで変更なしに戻ったページ(リングが消える)が生じる。これらは再比較後の per-UID
	//   Purge 集合(=いずれも「今」の状態)には入らないため、旧集合を控えて後で一緒に Purge しないと
	//   古い枠/斜線がサムネイルに残る。
	//   列挙は KCMCollectChangedPageUIDs に一本化(「何がマーク済みか」の定義を二重実装しない)。
	//   同関数は db が現在の sDB/sSrcDB と一致する時だけ集める=「前回比較が今回と同じ文書の時だけ
	//   旧 UID を拾う」ガード(UID は db 固有。別文書対への再 Start で誤 Purge しない)も兼ねる。
	//
	// ★★2026-08-13(Task 10): **この退避は今は取っていない。** サムネイルの Purge は UI 側へ移り、
	//   旧集合を渡す道が無くなった。代わりに UI は**全ページ**を Purge する ---- 取りこぼしは原理的に
	//   起きず、ページ数ぶん遅くなるだけ(KCMThumbnailRefresh.h の KCMPurgeAllPageThumbs)。
	// ⚠★★2026-08-16(API 監査 B5)訂正: ここに書いてあった理由「**通知は ClassID しか運べない**」は
	//   **誤りだった**——ISubject::Change の第3引数 changedBy で運べる(2026-08-15 の監査 B2 で判明)。
	//   ★**正しい理由は「載せる物が手元に無い」**: 要るのは「再比較の**前**に枠が付いていたページ」で、
	//     それを知るにはこの退避を復活させる必要がある(＝今も未実施)。∴ 全ページ Purge のままで正しい。
	//   ★対照＝**部分再比較(KCMRefreshComparisonCore)は載せている**。あちらは触るページを先に
	//     決めてから回るので、集合が最初から手元にある。
	//   ⚠**Task 12 で IKCMMarkData が入ったら、ここで退避を取り直して絞り込みへ戻すこと。**
	//     上の段落は、そのとき何をなぜ集めていたかの記録として残してある。

	// 差分再比較の可否。登録トグル専用(allowIncremental=kTrue)で、かつ前回比較と同じドキュメント対を
	// 対象にしていて前回ペアリングが残っている場合のみ差分にする。それ以外(Start・Ignore Page Number
	// マーカー切替・別文書対・前回ペアリング無し)は従来どおり全ページを再ラスタ化する。
	const bool16 doIncremental =
		allowIncremental &&
		KCMDrawEventHandler::sDB == targetDB &&
		KCMDrawEventHandler::sSrcDB == sourceDB &&
		!KCMDrawEventHandler::sPrevPairTargetToSource.empty();

	// 再比較すると「どのスプレッドが変更なしか」の分類が古くなるため、「Hide Unchanged Spreads」で
	// 隠していたスプレッドは先に再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KCMResetHideUnchanged(kTrue);

	// 両ドキュメントのページ対応を除外対応表(登録済み=比較相手なしページを除いた順番対応)で求める。
	// 差分・全再比較のどちらでも使い、末尾で次回差分用の前回ペアリング(sPrevPairTargetToSource)に記録する。
	std::vector<UID> tPages, sPages;
	KCMBuildPairing(targetDB, sourceDB, tPages, sPages);

	// ★マスタースプレッドのページを後ろに連結する(2026-08-11)。従来はマスターが一度も比較されて
	// いなかった(KCMCollectPageUIDs にマスターが入らないため。マスターに出ていた枠はあふれ「+」だけ)。
	// ⚠2026-08-16 に KCMCollectPageUIDs の中身が ISpreadList の2重ループから IPageList へ移ったが、
	//   **マスターを含まないことは変わらない**(`IPageList.h:81` が契約として明記)＝この連結は今も要る。
	// ★★連結するだけでよい理由: この後の比較ループ・進捗バーの総数・差分再比較のキャッシュ
	//   (sPrevPairTargetToSource)・Source 側の対応表(sSrcPageToTarget)は、すべて tPages/sPages の
	//   添字で回っている。MakeEntry はページの UIDRef しか見ない(中身が通常ページかマスターページかを
	//   気にしない)ので、ここに足すだけで全部が乗る。2026-08-11 に実機で実証済み。
	// ★KCMBuildPairing 自体には足さない: あれの契約は「通常ページの除外対応表」で、TSV 出力など
	//   他の呼び手も居る。連結は呼び出し側の責任にする。
	{
		std::vector<UID> tMaster, sMaster;
		KCMBuildMasterPairing(targetDB, sourceDB, tMaster, sMaster);
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
		KCMNotifyStatus(busyMsg, kTrue /*forceRedrawNow*/);
	}

	// ★これから実際にラスタ化するページ(tPages/sPages の添字)を先に確定する。進捗バーの総数に使うほか、
	//   差分側は「対象かどうか」の判定を1回で済ませられる(以前は判定とラスタ化が同じループにあった)。
	std::vector<size_t> toRaster;
	if (doIncremental)
	{
		// 【差分再比較】前回ペアリング(oldMap)と今回(newMap)を突き合わせる。ペア不変のページは
		// MakeEntry を呼ばず前回のオーバーレイ(または「変化ゼロ=エントリ無し」)をそのまま再利用する。
		const std::map<UID, UID>& oldMap = KCMDrawEventHandler::sPrevPairTargetToSource;

		// (1) 破棄: 前回ペアの target のうち、今回ペアが消えた/相手が変わったものはエントリを捨てる。
		//     MakeEntry は変化ゼロだと既存エントリを消さないので、相手が変わるページは先にここで消す。
		for (std::map<UID, UID>::const_iterator it = oldMap.begin(); it != oldMap.end(); ++it)
		{
			std::map<UID, UID>::const_iterator nit = newMap.find(it->first);
			if (nit == newMap.end() || nit->second != it->second)
				KCMDrawEventHandler::DropOneEntry(it->first, it->second);
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
		KCMDrawEventHandler::DropAll();
		KCMDrawEventHandler::sDB = targetDB;
		// 対象文書を丸ごと入れ替えるので、変更ページ巡回(Next/Prev)の基準点も捨てる。旧文書のページ UID を
		// 持ち越すと、別文書での UID 偶然一致で誤った位置から巡回が始まるため(差分再比較の側は同一文書なので触らない)。
		// ★2026-08-13(Task 10): 基準点は UI 側が持つ状態なので、ここでは捨てられない。末尾の通知に
		//   navReset として乗せ、UI に捨てさせる。条件はこの else に居ること＝`!doIncremental` そのもの。
		toRaster.reserve(n);
		for (size_t i = 0; i < n; ++i)
			toRaster.push_back(i);
	}

	// ★★★**Story モードではページを1枚もラスタ化しない**(2026-08-20)。
	//
	//   ここまでは両モードで同じ道を通る。それが要るからで、飛ばしてよいものは1つも無い:
	//     ・ページ対応表(tPages/sPages)   … peek(旧版を覗く)と元ノンブルのバッジが乗っている
	//     ・overflow キャッシュ            … 「/」の付くページ
	//     ・DropAll / sDB の差し替え       … 前のモードで付いた枠をここで捨てる
	//   違うのは「対応の付いたページを1枚ずつ描いて画素を比べるか」だけなので、その入力である
	//   toRaster を空にする。ループが0回になり、進捗バーも出ない(rasterCount=0)。
	//
	//   ⚠**ここで分岐する**のは、上の2つの分岐(差分/全再比較)がどちらも「どのページを比べるか」を
	//     決める仕事で、モードはその後段の「そもそも比べるか」だから。中に混ぜると、差分側と全再比較側の
	//     両方に同じ条件を書くことになる。
	if (KCMGetCompareMode() == kKCMModeStory)
		toRaster.clear();

	// ★重い比較には進捗バーとキャンセルを出す(2026-07-27)。総数は「これから実際にラスタ化する枚数」
	//   (差分なら再計算するページだけ)。タイトルは KCM の他の文言と同じく英語固定＝翻訳キー扱いを
	//   避けるため SetTranslatable(kFalse)。
	// ★★showImmediate(第3引数)は「時間がかかったら自動で出す」ではない。kFalse(既定)は「出さない」で、
	//   100 ページの比較でも一度も現れなかった(2026-07-27 実機で判明)。→ 出す/出さないは自前のしきい値
	//   kKCMProgressBarMinPages(KCMConstants.h。経緯もそこに記載)で決める。登録トグルによる数ページの
	//   差分再比較ではバーを出さず、本格的な比較では必ず出る。
	const int32 rasterCount = (int32)toRaster.size();
	const bool8 showBar = (rasterCount >= kKCMProgressBarMinPages) ? kTrue : kFalse;
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
			KCMDrawEventHandler::MakeEntry(UIDRef(targetDB, tPages[i]), UIDRef(sourceDB, sPages[i]), changed);
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
		//   ※Refresh 経路(KCMPeek.cpp)は「そこまで更新した分を残す」設計で、最後に押されても
		//     失うものが無い(ステータスに "- cancelled" と出るだけ)ため、あちらは現状のままでよい。
		if (k + 1 < toRaster.size() && progress.WasCancelled(kFalse))
		{
			cancelled = kTrue;
			break;
		}
	}
	// 差分では、今回ラスタ化しなかったページ(前回結果の再利用分)も現在の変化ページ数に含める。
	if (doIncremental && !cancelled)
		changedCount = (int32)KCMDrawEventHandler::sEntries.size();

	if (cancelled)
	{
		// ★キャンセル: 「比較済みページと未比較ページの混在」を残さない。マークを全部捨てて
		//   「比較していない」状態へ戻す(変更が無いのか、まだ見ていないのかが区別できない画面を作らない)。
		//   前回ペアリングも DropAll が捨てるので、次の比較は必ず全ページを見直す(差分で取りこぼさない)。
		//   ここでペアリング(newMap)を記録しないのが肝。
		KCMDrawEventHandler::DropAll();
		changedCount = 0;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// 中断で立った可能性のあるエラーを持ち越さない
		// ★★2026-08-17(不具合再検査 B3): **ここで Story Edits の一覧を捨てる必要は無い。**
		//   一度は「マークは全部消えるのに一覧だけ前回の比較のまま残り、もう比較していない2文書の
		//   差分を指したままクリックで飛べる行が並ぶ」と読んで KCMStoryList::Clear() を足したが、
		//   **呼び手を4つとも開いたら成立しなかった**——この関数が kFailure(=キャンセル)を返したとき:
		//     ・Start(KCMComparisonRun.cpp:152)              … arm しない。そこへ来る前は必ず未 arm
		//       ＝一覧は空(ブック比較の「Start Change Marker」＝KCMBookOpen.cpp の
		//       KCMBookStartComparisonForRow も、比較を始める前に先に Stop する)
		//       (⚠旧引用 ":477" は**38行ずれて別の関数の入口**を指していた＝2026-08-18・不具合再検査
		//        B-U5 の2周目。★**B-U3 がこの4件を検算して「外れていたのは1件だけ」と書いた後で、
		//        同じ日の B-U5 1周目があちらのファイルに +58 行入れて腐らせた**——検算した参照は
		//        「検算した時点で当たっていた」だけで、**指される側が編集されれば黙って外れる**。
		//        ⇒ 関数名へ。名前は行の挿入では動かない。)
		//     ・登録トグル(KCMPageMap.cpp:242)               … KCMToggleStartStop() で Stop へ戻す
		//     ・Load Check & Register(KCMPageCheck.cpp の KCMPageCheckLoadFromFile)… 同上
		//       (⚠旧引用 ":824" は12行ずれて別の関数の中を指していた＝2026-08-18・不具合再検査 B-U3。
		//        **同じ4件のうち外れていたのはこれ1つで、他の3件は当たっていた**。)
		//     ・Ignore トグル(ui/KCMActionComponent.cpp:401) … 同上
		//   ⇒ **4つとも Stop へ戻す**ので、Stop(KCMDoClearMarks)の KCMStoryList::Clear() が必ず走る。
		//   ⚠★**この関数の中だけを読むと「一覧が残る」ように見える**(後始末が呼び手側にあるため)。
		//     次に同じ疑いを持ったらここを読むこと。実測＝30ページの再比較を進捗バーでキャンセルし、
		//     見出しが "Story Edits (3)" → "Story Edits" へ戻ることを確認(2026-08-17)。
	}
	else
	{
		// 今回のペアリングを次回の差分用に記録する(差分・全再比較のどちらの経路でも)。
		KCMDrawEventHandler::sPrevPairTargetToSource.swap(newMap);

		// sSrcDB/対応表は MakeEntry が変化ページ登録時に埋めるが、変化ゼロでも db だけは明示しておく
		// (エントリが無ければ wantSrcMarks が空判定で落ちるので描画コストは増えない)。
		// ★「Always Show Marks on Source」の既定 ON はここでは立てない(2026-07-25 監査で移動): この関数は Start
		//   だけでなく登録トグルの差分再比較・Ignore Page Number 切替の再比較も通るため、ここで kTrue に
		//   戻すとユーザーが OFF にした直後の再比較で黙って ON に戻ってしまう。既定 ON へ戻すのは仕様どおり
		//   Start 経路(KCMToggleStartStop)のみ。
		// ⚠★2026-08-17(不具合再検査 B3 の2周目): **ここにマーク集合のロックは要らない。**
		//   MakeEntry 側の同じ代入はロックの中にあるが、あちらが守っているのは隣の
		//   sSrcPageToTarget(std::map=挿入で木を回す)で、sSrcDB はそのスコープに同居しているだけ。
		//   ポインタ1個の代入は、読み手(描画)が新旧どちらの値を見ても壊れない
		//   ---- 古ければ Source 枠が出ない、新しければ出る、それだけ。
		//   ★この但し書きが無かったため、2026-08-17 の再検査でここを一度「ロック漏れ」と誤診した。
		KCMDrawEventHandler::sSrcDB = sourceDB;
	}

	// ★★2026-08-17(不具合再検査 B3 の2周目): **ラスタ化に失敗したページがあったときも、
	//   そこで立った可能性のあるエラーを持ち越さない。** 上のキャンセル分岐と同じ理由で、
	//   同じ扱いに揃える(以前はキャンセルのときだけ落としていた＝同じ問いに2つの答え)。
	//   ★失敗は report の failed=N として利用者へ伝えるので、**エラー状態で伝える必要は無い**。
	//     立てたまま返すと、呼び手が次に投げるコマンドが巻き添えで失敗する
	//     (CmdUtils.h:72-77 の protective shutdown / シーケンスなら丸ごと巻き戻る)。
	//   ⚠**失敗の中身は2種類**で、エラーを立て得るのは後者だけ:
	//     ①ページサイズ不一致(wth!=wsh) …… ラスタ化自体は成功しているので何も立たない
	//     ②SnapshotUtilsEx::Draw の失敗・OOM …… 立て得る(SDK 内部なので確かめる術が無い)
	//   ⇒ **測れない側に安全側で倒す**。落としてよい根拠＝この関数は失敗を戻り値では区別せず
	//     (kFailure を返すのはキャンセルのときだけ)、失敗ページは report で報告し切っている。
	if (failedCount > 0)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	// overflow("/")キャッシュを今の対応表から作り直す。ここは Start・登録 Add/解除・Ignore 切替が
	// すべて通る唯一の再比較路なので、これらの操作後は描画側が最新の overflow を使う(描画のたびの
	// 全文書走査は EnsureOverflowCache 側で回避)。
	KCMDrawEventHandler::RebuildOverflowCache();
	// ビューポート同期が持つ除外対応表キャッシュも同じ理由で捨てる(登録 Add/解除でペアが動く。
	// 2026-07-25 追補)。★2026-08-13(Task 10): キャッシュは UI 側(KCMViewSync)の持ち物なので、
	// 末尾の kKCMMarksRebuiltMessage を受けた UI が捨てる。

	// ★「Check」の✓: 再比較で「マーク(枠/「/」)が無くなったページ」のチェックを忘れる
	//   (ユーザー指定 2026-07-11)。この後のサムネイル更新で、マークが消えたページは prevMarked 経由で
	//   purge され、リングも✓も無いクリーンなサムネイルに作り直される(チェックを先に外すのが肝)。
	//   ★必ず下の KCMInvalidateDB より前に呼ぶ(2026-07-12 ユーザー報告の修正): ✓ はレイアウト
	//   ビューにも常時表示されるようになったので、Invalidate 後にチェックを外すと「✓ がまだある状態」
	//   でレイアウトが描き直されて古い ✓ が残る(サムネイルは prune 後に更新されるので消える=食い違い)。
	//   prune に必要なマーク集合(sEntries/登録/overflow)は直前の RebuildOverflowCache までで確定済み。
	KCMPageCheckPruneToMarked();

	KCMInvalidateDB(targetDB);
	if (sourceDB != targetDB)
		KCMInvalidateDB(sourceDB);	// Source 側の常時枠を即反映

	// スクロールバー地図 strip のマークも最新化(Start/旧 Ctrl+ミドル再比較/登録トグルの全経路がここを通る)
	// ---- ★2026-08-13(Task 10): strip も UI。末尾の通知を受けた UI が注入と描き直しをする。

	// ★Pages パネルのサムネイルの作り直しも UI の仕事＝末尾の通知に含めた(2026-08-13・Task 10)。
	//   何をどう叩けば既表示のサムネイルが作り直されるか(IImageCacheMgr::Purge をページ UID 単位で
	//   → Pages パネルを ForceRedraw)という 2026-07-06 の切り分けの結果は KCMThumbnailRefresh.* に
	//   そのまま残っている。ここが知っている必要はもう無い。

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

		// ★Story モードでは「ページを何枚比べたか」は報告しない——1枚も比べていないので、
		//   "pages compared=100 changed=0" は嘘ではないが、読んだ人に「100ページ比べて差が
		//   無かった」と伝わる。数えたものを言う。
		const bool16 storyMode = (KCMGetCompareMode() == kKCMModeStory);
		if (!storyMode)
		{
			report.Append("pages compared="); report.AppendNumber((int32)n);
			report.Append(" changed="); report.AppendNumber(changedCount);
			// ★比較できなかったページは隠さない(そのページは「枠が無い=変更なし」とは限らない)。
			if (failedCount > 0)
			{
				report.Append(" failed="); report.AppendNumber(failedCount);
			}
		}

		// ★Story Edits の一覧。画素比較が答えるのは「このページは違って見える」までで、
		//   「テキストが変わったのか、レイアウトだけ動いたのか」は区別できない。両者は補い合う
		//   ——ストーリーが無変更でもページは動きうるし、ページが同じに見えてもテキストは変わりうる。
		KCMRebuildStoryEdits(targetDB, sourceDB);

		// ★Story モードの報告は**この後**でしか作れない。件数が出るのは一覧を作り終えてから。
		if (storyMode)
		{
			const int32 storyCount = KCMStoryList::GetRowCount();
			int32 editCount = 0;
			for (int32 i = 0; i < storyCount; ++i)
			{
				const KCMStoryRow* row = KCMStoryList::GetRow(i);
				if (row != nil)
					editCount += static_cast<int32>(row->fChanges.size());
			}

			report.Append("stories changed="); report.AppendNumber(storyCount);
			report.Append(" edits="); report.AppendNumber(editCount);

			// ★**差が「0 件」なのと「出せなかった」のは違う**。カウンターが動いたのに本文の差が
			//   1つも出ないのは、書式だけの変更・比較できなかった(相手が居ない/違いすぎる/長さが
			//   合わない)のどれか。黙って 0 と出すと「テキストは変わっていない」と読めてしまう。
			if (storyCount > 0 && editCount == 0)
				report.Append(" (no text differences located)");
		}
	}
	outReport = report;

	// ★★ここまでで model 側の仕事は終わり。**画面の作り直しは通知1本にまとめて UI に任せる**
	//   (2026-08-13・Task 10)＝ビュー同期キャッシュの破棄・strip の注入と描き直し・Pages パネルの
	//   サムネイル・パネルの表示・Prev/Next の位置。順序は受け手(KCMModelChangeObserver)が持つ。
	//
	// ⚠**navReset は `!doIncremental`**。Prev/Next 間の現在位置は「確定した最新の変更ページ集合」で
	//   作り直され(Start・差分再比較・登録 Add/Remove・Check がすべてここを通るので、押さなくても
	//   集合の変化に即時追従する＝ユーザー要望 2026-07-15)、**全再比較のときだけ基準点も捨てて**
	//   未巡回扱いの "1/N" に戻す。差分再比較で捨てると、ページを1つ登録するたびに巡回が先頭へ
	//   戻ってしまう。
	// ⚠ キャンセルされた場合も投げる ---- 途中まで作られたマークと、消えたマークの両方が画面に
	//   反映されなければならない(この関数は cancelled でも同じ後始末をしていた)。
	//
	// ⚠★★2026-08-17(不具合再検査 B3 の2周目)＝**「キャンセル × 差分再比較」だけは
	//   navReset が kFalse になる。** マークは DropAll で全部消えたのに、巡回の基準点は残る
	//   ---- ここだけ読むと「もう無いページを指したまま Prev/Next が始まる」ように見える。
	//   ★**成立しない。呼び手を4つとも開くと、全部その後で Stop へ戻る**(kFailure を返すため)＝
	//     Stop が navReset=kTrue の Cleared を投げ直すので、基準点はそこで必ず捨てられる。
	//     内訳は上のキャンセル分岐に書いた4つと同じ(Start は arm しない・残り3つは
	//     KCMToggleStartStop() で Stop)。
	//   ⇒ **この式を `!doIncremental && !cancelled` へ変える必要は無い。** 同じ後始末を
	//     2か所ですると、片方だけ直したときに必ずずれる([[one-question-one-place]])。
	KCMNotifyDocs(kKCMMarksRebuiltMessage, targetDB, sourceDB, !doIncremental);
	// (★2026-08-20: ここに「透明マネージャに聞き直させる」通知があったが **外した**。
	//  ⚠**一覧は文書側のデータで `.indd` に永続する**(実測＝比較して保存した文書を開き直すと残っており、
	//    開くだけでは再検証されない)。比較のあいだずっと載せておくと、ユーザーが保存した瞬間に
	//    **根拠のない記録が焼き付く** ---- KCM を持たない人がその文書を開いても残る。
	//  ⇒ 載せるのは**書き出し／印刷のあいだだけ**にした＝KCMRingAdornment.cpp の 5) 節。
	//    フラットナが要るのはその2つの出力のときだけで、画面にもサムネイルにも一覧は要らない。)
	// ★キャンセルは kFailure で返す。Start 経路(KCMToggleStartStop)はこの戻り値を見て arm するかどうかを
	//   決めるので、ここを常に kSuccess にすると「キャンセルしたのに arm され、メニューが Stop のまま」
	//   になる(2026-07-27 実機で発生)。
	return cancelled ? kFailure : kSuccess;
}

// 文書の生存確認(KCMCore.h で宣言)。★閉じた db は deref 禁止=IDocumentList への
// ポインタ比較のみ。旧 KCMActionComponent.cpp の static を共有化したもの(2026-07-10)。
// ★session の nil ガードは必須(2026-07-25 追補): この関数は KCMScrollMapView::Draw と遅延サムネイル
//   idle task から呼ばれ、どちらもアプリ終了のティアダウン中に発火し得る。session が解体済みの
//   環境(特に Mac の Cocoa 解体順)で無ガード deref すると crash-on-quit になる。
//   引けない=解体が進んでいる → 「開いていない」と答えるのが安全側。
bool16 KCMIsDocDBOpen(IDataBase* db)
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
bool16 KCMAppIsQuitting()
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
void KCMInvalidateDB(IDataBase* db)
{
	if (db == nil)
		return;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}

void KCMDoClearMarks(IDataBase* db)
{
	// マーク(=「変更なし」判定の根拠)が消えるので、「Hide Unchanged Spreads」で隠していた
	// スプレッドも再表示してトグルを OFF に戻す(何も隠していなければ何もしない)。
	KCMResetHideUnchanged(kTrue);

	// DropAll() で sDB が nil になる前に、実際にマークが描かれていた文書を控えておく。呼び出し側の
	// db(=操作時のアクティブ文書)が前面で Source や無関係な第3文書に切り替わっていても、対象文書の
	// 枠が即座に消えるようにするため(タイル表示等で対象文書が同時に見えている場合に効く)。
	// Source 側の常時枠(Always Show Marks on Source)も同様に、消える前の db を控えて後で再描画する。
	IDataBase* markedDB = KCMDrawEventHandler::sDB;
	IDataBase* srcDB    = KCMDrawEventHandler::sSrcDB;

	// ★登録(Added/Removedページ)も Stop で丸ごと忘れる(ユーザー指定 2026-07-11:「Stop すると
	// Add/Remove の登録は解除する」)。登録は arm 済みのとき Target/Source にしか作れないので実質この2文書
	// だが、取りこぼしの無いよう全文書分を一括クリアする(Target/Source の組み合わせを変えて再 Start した
	// 時に古い登録が紛れ込むのも防ぐ。2026-07-05 の per-db クリアを全体クリアへ拡張)。
	KCMPageMapClearAllDocs();

	// ★「Check」の✓も Stop で丸ごと忘れる(ユーザー指定: Start 中限定・Stop で消去)。
	KCMPageCheckClearAllDocs();

	KCMDrawEventHandler::DropAll();
	KCMDrawEventHandler::DropAllOrig();	// 旧版べた載せのキャッシュも解放(メモリ開放)

	KCMInvalidateDB(markedDB);
	if (db != markedDB)
		KCMInvalidateDB(db);
	if (srcDB != markedDB && srcDB != db)
		KCMInvalidateDB(srcDB);			// Source 側の常時枠も即座に消す

	// ★Stop の後始末のうち**画面側は通知1本**にまとめた(2026-08-13・Task 10)＝strip の撤去・
	//   Pages パネルのサムネイルの作り直し・パネルの表示・Prev/Next の基準点と位置。
	//   (サムネイルの共有画像キャッシュは KCMInvalidateDB=InvalidateViews では届かないので、
	//    Start 側と対称に Purge+ForceRedraw が要る ---- その手順は UI 側が持っている。DropAll 済みで
	//    マーク対象が無いため、作り直される isThumb 描画は早期 return し枠は描かれない。)
	//
	// ⚠★**掃除する2文書は通知に載せなければならない。** ここへ来るまでに DropAll 済みで
	//   sDB/sSrcDB は nil ＝ UI が KCMArmedTargetDB() を聞いても答えは返らず、どの文書の
	//   サムネイルを作り直せばよいか分からない。Rebuilt と違って**聞けない**のがこちら。
	// ★navReset=kTrue ＝ Stop では巡回の基準点を次の比較へ持ち越さない。
	KCMNotifyDocs(kKCMMarksClearedMessage, markedDB, srcDB, kTrue /*navReset*/);
	// (★2026-08-20: 対になる「降ろす」通知もここから外した。理由は上の再比較側と同じ。
	//  ⚠★★**外す前のここは、実は一度も効いていなかった** ---- 上げも下げも同じ
	//    `kXPC_MayHaveAddedSomeXP` を送っており、**この種別は増える方向にしか効かない**
	//    (A/B 実測＝同じ文書に `MayHaveAdded` で 1->1 / `kXPC_RemovedSomeXP` で 1->0)。
	//    旧コメントは「対称に呼ぶこと」と正しく書いてあり、呼び出しも対称だったが、
	//    **意味が対称ではなかった**＝[[one-question-one-place]] の裏返しで、
	//    **どちらにも使える1本の関数にしたことで方向が引数から消えていた**。
	//    ⇒ 今は KCMSetItemXPState() が方向を引数で受け取る。)

	// ★Story Edits の一覧も同じく忘れる。次の比較まで残しておくと、もう比較していない2文書の
	//   差分を指したまま**クリックすれば飛べてしまう**行が並ぶことになる(ジャンプは段階4)。
	// ★見出しは括弧つきの件数を落として "Story Edits" に戻る ---- 見出しの文言は
	//   KCMUpdateStorySectionLabel が arm 状態を見て決めるので、model は「一覧が変わった」と
	//   言うだけでよい(2026-08-13・Task 10 で通知化)。
	KCMStoryList::Clear();
	KCMNotify(kKCMStoryEditsRebuiltMessage);
}

void KCMDoSetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
{
	KCMDrawEventHandler::sPrintMarks = printFlag;
	KCMDrawEventHandler::sMarkOpacity25 = opacity25Flag;
	// 常時表示(画面)の不透明度を印刷設定に合わせて即反映。
	KCMDrawEventHandler::sMarkScreenOpacity = KCMBaseScreenOpacity();

	// 実際にマークが描かれている対象文書(sDB)を優先して再描画する。呼び出し側 db(=アクティブ文書)が
	// それと異なっていても(Source や無関係な第3文書が前面の状態で操作した場合)、対象文書の見た目が
	// 即座に更新されるようにするため。Start 前(sDB==nil)は従来どおり db のみ再描画する。
	// Source 側の常時枠(Always Show Marks on Source)は 25%/75% 選択に連動するので、Source も再描画する。
	KCMInvalidateDB(KCMDrawEventHandler::sDB);
	if (db != KCMDrawEventHandler::sDB)
		KCMInvalidateDB(db);
	if (KCMDrawEventHandler::sSrcDB != KCMDrawEventHandler::sDB && KCMDrawEventHandler::sSrcDB != db)
		KCMInvalidateDB(KCMDrawEventHandler::sSrcDB);

	// (★2026-08-20: ここにあった透明マネージャへの通知3本も外した。理由は上の2か所と同じで、
	//  **載せるのは書き出し／印刷のあいだだけ**にしたため＝KCMRingAdornment.cpp の 5) 節。
	//  ★このトグルは「出力にマークを出すか」を変えるので**申告の答えそのものを変える**が、
	//    その答えを聞きに来るのは出力のときだけなので、ここで先回りして一覧を触る必要が無い。
	//  ⇒ 上の再描画(画面の更新)だけがこの関数の仕事に戻った。)
}

// 現在の印刷マーク設定を返す(パネル再表示時の状態復元に使用)。
bool16 KCMGetPrintMarks()
{
	return KCMDrawEventHandler::sPrintMarks;
}

bool16 KCMGetMarkOpacity25()
{
	return KCMDrawEventHandler::sMarkOpacity25;
}

// マークの色(赤/シアン)を設定する。
// ★★2026-08-24: **背景による自動切り替えを廃止し、フライアウトで選ぶようにした**(ユーザー判断
//   「ユーザーが選べばいいので」)。Pixel の枠も Story の色地も、描くときに
//   KCMDrawEventHandler::SelectedMarkColor() を通るので、この旗1つで両方に効く。
void KCMDoSetMarkColor(bool16 cyan, IDataBase* db)
{
	if (KCMDrawEventHandler::sMarkColorCyan == cyan)
		return;						// 同じ色を選び直しただけ。作り直しも再描画も要らない

	KCMDrawEventHandler::sMarkColorCyan = cyan;

	// ⚠★★★リング画像はキャッシュで、**半径が変わったときだけ**作り直される(BuildRing の呼び口が
	//   `R != e->lastRadius` で守られている)。⇒ 色を変えただけでは作り直されず、**古い色のまま
	//   残る**。ここで「未描画」に戻して、次の描画で作り直させる。
	//   ★Story の色地はこの手当てが要らない ---- あちらは Draw のたびに色を読むので、再描画するだけで
	//     新しい色になる。**同じ設定でも、キャッシュを持つ側と持たない側で必要な後始末が違う。**
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		for (std::map<UID, KCMOverlayEntry*>::iterator it = KCMDrawEventHandler::sEntries.begin();
		     it != KCMDrawEventHandler::sEntries.end(); ++it)
			if (it->second != nil)
				it->second->lastRadius = -1;	// -1 = 未描画(KCMOverlayEntry の既定値と同じ)
	}

	// 再描画は KCMDoSetPrintMarks と同じ範囲(対象・アクティブ・Source の3つ)。
	KCMInvalidateDB(KCMDrawEventHandler::sDB);
	if (db != KCMDrawEventHandler::sDB)
		KCMInvalidateDB(db);
	if (KCMDrawEventHandler::sSrcDB != KCMDrawEventHandler::sDB && KCMDrawEventHandler::sSrcDB != db)
		KCMInvalidateDB(KCMDrawEventHandler::sSrcDB);
}

bool16 KCMGetMarkColorCyan()
{
	return KCMDrawEventHandler::sMarkColorCyan;
}

//----------------------------------------------------------------------------------------
// 比較モード(2026-08-20)
//----------------------------------------------------------------------------------------
// ★**セッション全体の設定で、文書ごとではない**。だから db を引数に取らない。印刷マーク
//   (sPrintMarks)が KCMDrawEventHandler の static に在るのは「描画の設定」だからで、こちらは
//   「比較の設定」なので比較を持っているこの翻訳単位に置く。
//
// ⚠**BG スレッドからも読まれる**(Story モードでは枠を描かないので、描画イベントがこれを見る)。
//   書くのはメニュー操作＝メインスレッドだけで、読みは enum 1つ分＝[[model-plugin-thread-safety]]
//   の言う「BG は別 db を見るが同じ static を共有する」型のうち、共有していて**正しい**ほう
//   (どのスレッドから見ても同じモードでなければならない)。
static KCMCompareMode sCompareMode = kKCMModePixel;

KCMCompareMode KCMGetCompareMode()
{
	return sCompareMode;
}

void KCMSetCompareMode(KCMCompareMode mode)
{
	sCompareMode = mode;
}
