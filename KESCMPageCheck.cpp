//========================================================================================
//
//  KESCMPageCheck.cpp
//
//  「KESCM: Check」機能(KESCMPageCheck.h 参照)。ページパネルでページを選択→右クリックの
//  トグル「KESCM: Check」で、そのページに「チェック済み」印を付け外しする。チェックしたページには
//  Pages パネルのサムネイル中央に青い ✓(ベクター線)を描く(描画は KESCMDrawEventHandler の
//  isThumb 分岐)。登録(KESCMPageMap)とは独立した別集合。セッション内のみ・Stop で全消去。
//
//  構造は KESCMPageMap.cpp を踏襲(選択取得=Utils<ILayoutUIUtils>()->GetSelectedPages、状態=
//  文書DBごとの UID セット、クローズスイープは deref なしのポインタ比較のみ)。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"
#include "IApplication.h"
#include "IDocument.h"
#include "IDataBase.h"
#include "IDocumentList.h"		// 生存スイープ(FindDocByDataBase へのポインタ比較のみ)
#include "IActionStateList.h"	// メニューの有効/チェック
#include "ILayoutUIUtils.h"		// GetFrontDocument / GetSelectedPages(ページパネル選択の公式取得)
#include "Utils.h"
#include "PersistUtils.h"		// ::GetDataBase(IDocument→IDataBase)
#include "UIDList.h"
#include "PMString.h"

#include <map>
#include <set>
#include <vector>

#include "KESCMCore.h"			// KESCMCollectPageUIDs / KESCMIsArmed / KESCMArmedTargetDB / KESCMArmedSourceDB / KESCMSetStatus
#include "KESCMPageCheck.h"
#include "KESCMThumbnailRefresh.h"	// KESCMRefreshThumbnailsForPages(トグルページの明示サムネイル更新)

// チェック済みページ: 文書DB → ページUIDの集合。セッション内のみ。空になった文書のエントリは即消す。
static std::map<IDataBase*, std::set<UID> > sChecked;

// ヘルパ: vector<UID> の線形 contains(選択は高々数十件)。
static bool16 KESCMCheckVecContains(const std::vector<UID>& v, UID u)
{
	for (size_t k = 0; k < v.size(); ++k)
	{
		if (v[k] == u)
			return kTrue;
	}
	return kFalse;
}

// ページパネルの選択を読む(KESCMPageMap.cpp の同名ヘルパと同じ流儀)。outDB=選択が属する文書
// (=アクティブ文書)、outPages=文書のページ列に実在する選択ページUID。有効なページが1つ以上あれば kTrue。
static bool16 KESCMPageCheckReadSelection(IDataBase*& outDB, std::vector<UID>& outPages)
{
	outDB = nil;
	outPages.clear();

	IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	IDataBase* db = (doc != nil) ? ::GetDataBase(doc) : nil;
	if (db == nil)
		return kFalse;

	UIDList sel(db);
	Utils<ILayoutUIUtils>()->GetSelectedPages(sel, kFalse /*masters除外*/, kTrue /*currentPageOnly*/, kTrue /*pagesOnly*/);

	std::vector<UID> flat;
	KESCMCollectPageUIDs(db, flat);
	const int32 n = sel.Length();
	for (int32 i = 0; i < n; ++i)
	{
		const UID u = sel[i];
		if (KESCMCheckVecContains(flat, u) && !KESCMCheckVecContains(outPages, u))
			outPages.push_back(u);
	}
	if (outPages.empty())
		return kFalse;

	outDB = db;
	return kTrue;
}

// そのページが現在「マーク付き」(=Pages パネルのサムネイルが確実に作り直されるページ)かを判定する。
// マーク付き = KESCM の変更リング(sEntries)/登録「/」/overflow「/」のいずれか。実体は
// KESCMCollectChangedPageUIDs(KESCMThumbnailRefresh.h。db が sDB/sSrcDB のときだけ true+集合を返す)。
// ★チェック(✓)は「マーク付きページ限定」にする(ユーザー指定 2026-07-11): マークの無いページは
//   サムネイルが作り直されず✓が乗らないため、そもそもチェックさせない/覚えない。
static bool16 KESCMPageIsMarked(IDataBase* db, UID pageUID)
{
	std::set<UID> marked;
	if (!KESCMCollectChangedPageUIDs(db, marked))
		return kFalse;
	return marked.count(pageUID) > 0 ? kTrue : kFalse;
}

// 選択ページのうち「マーク付き」のものだけを outMarked に残す。
static void KESCMFilterToMarked(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& outMarked)
{
	outMarked.clear();
	for (size_t i = 0; i < pages.size(); ++i)
		if (KESCMPageIsMarked(db, pages[i]))
			outMarked.push_back(pages[i]);
}

//========================================================================================
// KESCMPageCheckToggleSelectedPages(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> selPages;
	if (!KESCMPageCheckReadSelection(db, selPages))
		return;		// メニューは kCustomEnabling で無効化済みのはずだが保険

	// チェックは「比較を Start 中(arm 済み)」かつ「選択文書が Target/Source」のときだけ可能。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
		return;

	// ★マーク付きページだけを対象にする(マークの無いページは✓が乗らないので無視)。
	std::vector<UID> pages;
	KESCMFilterToMarked(db, selPages, pages);
	if (pages.empty())
		return;		// 選択にマーク付きページが無い=何もしない(メニューも無効のはず)

	std::set<UID>& chk = sChecked[db];

	bool16 anyUnchecked = kFalse;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (chk.count(pages[i]) == 0)
		{
			anyUnchecked = kTrue;
			break;
		}
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	if (anyUnchecked)
	{
		for (size_t i = 0; i < pages.size(); ++i)
			chk.insert(pages[i]);
		msg.Append("check +");
		msg.AppendNumber((int32)pages.size());
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			chk.erase(pages[i]);
		msg.Append("check -");
		msg.AppendNumber((int32)pages.size());
	}

	// 空になったらエントリごと捨てる。合計はその後に数える。
	if (chk.empty())
		sChecked.erase(db);
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	msg.Append(", total ");
	msg.AppendNumber(it != sChecked.end() ? (int32)it->second.size() : 0);

	// トグルしたページ(マーク付きに限定済み=サムネイルが確実に作り直される)のサムネイルを即更新して
	// ✓ を反映する(比較には影響しないので再比較は不要)。
	KESCMRefreshThumbnailsForPages(db, pages);

	KESCMSetStatus(msg);
}

//========================================================================================
// KESCMPageCheckUpdateToggleState(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckUpdateToggleState(IActionStateList* listToUpdate, int32 index)
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageCheckReadSelection(db, pages))
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	// Start 中かつ選択文書が Target/Source のときだけ有効。それ以外はグレーアウト。
	if (!KESCMIsArmed() || (db != KESCMArmedTargetDB() && db != KESCMArmedSourceDB()))
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	// ★選択にマーク付き(枠/「/」の付く=サムネイルが作り直される)ページが無ければ無効化する
	//   (枠の無いページでは「チェック」を出さない=ユーザー指定 2026-07-11)。
	std::vector<UID> marked;
	KESCMFilterToMarked(db, pages, marked);
	if (marked.empty())
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	int32 chkCount = 0;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	if (it != sChecked.end())
	{
		for (size_t i = 0; i < marked.size(); ++i)
		{
			if (it->second.count(marked[i]) > 0)
				++chkCount;
		}
	}

	int16 state = kEnabledAction;
	if (chkCount == (int32)marked.size())
		state |= kSelectedAction;			// マーク付き選択が全部チェック済み=✓
	else if (chkCount > 0)
		state |= kMultiSelectedAction;		// 一部だけチェック済み=中間チェック
	listToUpdate->SetNthActionState(index, state);
}

//========================================================================================
// KESCMPageCheckSweepClosedDocs(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckSweepClosedDocs()
{
	if (sChecked.empty())
		return;

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	std::map<IDataBase*, std::set<UID> >::iterator it = sChecked.begin();
	while (it != sChecked.end())
	{
		if (docList->FindDocByDataBase(it->first) == nil)
			sChecked.erase(it++);	// 閉じた文書: 状態だけ捨てる(deref なし)
		else
			++it;
	}
}

//========================================================================================
// KESCMPageCheckClearAllDocs(KESCMPageCheck.h で宣言)
//========================================================================================
void KESCMPageCheckClearAllDocs()
{
	sChecked.clear();
}

//========================================================================================
// KESCMPageCheckPruneToMarked(KESCMPageCheck.h で宣言)
//   再比較後、各文書のチェックを「今もマーク付き」のページだけに絞る(マークが消えたページの
//   チェックは忘れる)。KESCMCollectChangedPageUIDs は db が sDB/sSrcDB のときだけ現在のマーク集合を
//   返す(それ以外は空=その db の全チェックが外れる)。ポインタは deref しない。
//========================================================================================
void KESCMPageCheckPruneToMarked()
{
	if (sChecked.empty())
		return;
	std::map<IDataBase*, std::set<UID> >::iterator it = sChecked.begin();
	while (it != sChecked.end())
	{
		std::set<UID> marked;
		KESCMCollectChangedPageUIDs(it->first, marked);		// db が比較対象でなければ空
		std::set<UID>& chk = it->second;
		for (std::set<UID>::iterator c = chk.begin(); c != chk.end(); )
		{
			if (marked.count(*c) == 0)
				chk.erase(c++);		// もうマークが無いページ=チェックを忘れる
			else
				++c;
		}
		if (chk.empty())
			sChecked.erase(it++);
		else
			++it;
	}
}

//========================================================================================
// KESCMPageCheckIsChecked(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckIsChecked(IDataBase* db, UID pageUID)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	return (it != sChecked.end() && it->second.count(pageUID) > 0) ? kTrue : kFalse;
}

//========================================================================================
// KESCMPageCheckHasAny(KESCMPageCheck.h で宣言)
//========================================================================================
bool16 KESCMPageCheckHasAny(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sChecked.find(db);
	return (it != sChecked.end() && !it->second.empty()) ? kTrue : kFalse;
}

// KESCMPageCheck.cpp 終わり。
