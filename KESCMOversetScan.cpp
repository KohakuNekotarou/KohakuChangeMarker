//========================================================================================
//
//  KESCMOversetScan.cpp
//
//  Find Overset の検出本体(KESCMOversetScan.h 参照)。アクティブ1文書の全ユーザーストーリーを走査し、
//  オーバーセット(あふれ)を含むページの UID を集める。overset の判定は Utils<ITextUtils>::IsOverset、
//  「+」を出しているフレーム(=最後の配置済みパーセルのフレーム)の特定は KBSOversetLocator.cpp の
//  ロジックをここにインライン複製した(KBS プラグインへの依存を持たないため)。フレーム→ページ UID の
//  変換は ITextUtils::GetPageUIDRef を主に、ILayoutUtils::GetOwnerPageUID をフォールバックに使い、
//  どちらも実ページ(kPageBoss)であることを検証してからページ集合に入れる(ペーストボードは自然に脱落)。
//  走査は読み取りのみ。窓なし文書でも dirty にしないよう IDataBase::SaveRestoreModifiedState で囲む。
//
//========================================================================================

#include "VCPlugInHeaders.h"

// オブジェクトモデル / テキスト / テーブル / レイアウト:
#include "IDataBase.h"			// GetRootUID / GetClass / SaveRestoreModifiedState
#include "IStoryList.h"			// GetUserAccessibleStoryCount / GetNthUserAccessibleStoryUID
#include "ITextModel.h"			// QueryFrameList / GetPrimaryStoryThreadSpan / QueryTextParcelList
#include "IFrameList.h"			// IsOverset の引数
#include "ITextParcelList.h"
#include "IParcelList.h"		// GetLastParcelKey / GetPreviousParcelKey / GetParcelFrameUID
#include "ITextUtils.h"			// IsOverset / GetPageUIDRef
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex(セルが押し出された時の anchor 登り)
#include "ILayoutUtils.h"		// GetOwnerPageUID(off-page なら spread UID を返す=ページ非所属判定)
#include "IHierarchy.h"
// テーブルセル単独あふれ(赤丸)の走査用:
#include "ITableModelList.h"		// story 内の全テーブル列挙(GetModelCount/QueryNthModel)
#include "ITableModel.h"			// const_iterator / GetGridID / begin/end
#include "ITextStoryThreadDict.h"	// QueryThread(gridID)(kTableModelBoss に載る)
#include "ITextStoryThread.h"		// GetTextStart(セルスレッド先頭 TextIndex)
#include "TableTypes.h"				// GridAddress / GridID(source/public/includes 配下)

// 一般:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "SpreadID.h"			// kPageBoss(実ページかの検証)
#include "Utils.h"
#include <set>

// プロジェクト内:
#include "KESCMOversetScan.h"


//========================================================================================
// このスレッド(pos が compose する parcel list)の「最後の配置済みパーセル」のフレーム UID を返す。
// 末尾から遡り、初めて有効フレーム(GetParcelFrameUID != kInvalidUID)を持つパーセルを見つける。
// 1つも配置済みが無ければ(全パーセルが未配置)kInvalidUID。KBSOversetLocator.cpp の LocateInThread 相当
// (ここでは outport 座標は不要なのでフレーム UID だけ返す簡略版)。
//========================================================================================
static UID KESCMLastPlacedFrameUID(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kInvalidUID;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kInvalidUID;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kInvalidUID;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		const UID frameUID = pl->GetParcelFrameUID(k);
		if (frameUID != kInvalidUID)
			return frameUID;	// この断片が「最後の配置済み」=「+」を出しているフレーム
		// frameUID == kInvalidUID の断片はそれ自体があふれ=さらに手前へ遡る
	}
	return kInvalidUID;
}


//========================================================================================
// あふれているスレッドの「+」を出しているフレーム UID を返す。まず pos 自身のスレッドを見て、配置済みが
// 無ければ(=セルが行ごとフレーム外へ押し出された等)テーブルアンカーを親スレッドへ登り、最初に配置済み
// フレームを持つ祖先の「+」を採る(KBSFindOversetLocator 相当)。非進行/深ネストは guard で止める。
//========================================================================================
static UID KESCMFindOversetFrameUID(ITextModel* textModel, TextIndex pos)
{
	UID frameUID = KESCMLastPlacedFrameUID(textModel, pos);
	if (frameUID != kInvalidUID)
		return frameUID;

	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		if (!Utils<ITableUtils>()->InsideTable(textModel, cur))
			break;
		const TextIndex up = Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur);
		if (up == cur)
			break;	// 進んでいない
		cur = up;
		frameUID = KESCMLastPlacedFrameUID(textModel, cur);
		if (frameUID != kInvalidUID)
			return frameUID;
	}
	return kInvalidUID;
}


//========================================================================================
// フレーム UID → そのフレームが載っているページ UID。どのページにも載らない(ペーストボード等)なら
// kInvalidUID。主経路=ITextUtils::GetPageUIDRef(textFrame 前提の purpose-built API)、フォールバック=
// IHierarchy 経由の ILayoutUtils::GetOwnerPageUID(ページ非所属なら spread UID を返す仕様)。どちらも
// 結果が実ページ(kPageBoss)であることを db->GetClass で検証してから返す(spread UID を誤って採らない)。
//========================================================================================
static UID KESCMFramePageUID(IDataBase* db, UID frameUID)
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


//========================================================================================
// pos のスレッドが overset か。テーブルセルは ITextUtils::IsOverset(IFrameList*) の対象外
// (フレームリストを持たない)ため、スレッドの ITextParcelList の正式判定 GetIsOverset を使う。
// ★重要(2026-07-24 修正): 当初は GetParcelFrameUID(GetLastParcelKey())==kInvalidUID で判定していたが
// これは**セルに効かない**。ITextParcelList.h:705-713 が明記するとおり、テーブル等の複雑内容のパーセルは
// 「その TextParcelList 自体が overset にならずに」overset になり得る=セル領域パーセルは配置済み(frameUID
// 有効)なので kInvalidUID にならず取りこぼす。GetIsOverset()(ITextParcelList.h:116)は「最後の CR 以外の
// 内容がパーセルに composed されていなければ overset」という正式判定で、「最後の CR だけ」は overset 扱い
// しない(InDesign の赤丸の実挙動・DOM cell.overflows と一致)。
//========================================================================================
static bool16 KESCMThreadIsOverset(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	return tpl->GetIsOverset();		// スレッド単位の正式な overset 判定
}


//========================================================================================
// story 内の全テーブルの全セルを走査し、単独あふれ（赤丸。親フレームは非あふれ）のセルが載るページ UID を
// out に足す。セルテキストは親と同一 ITextModel の別スレッド（より大きな TextIndex）＝プライマリの
// IsOverset では拾えないので、各セルの先頭 TextIndex を StoryThreadDict 経路（SnpAccessTableContent 実証）で
// 取り、KESCMThreadIsOverset で判定→あふれなら KESCMFindOversetFrameUID+KESCMFramePageUID でページ追加。
//   取得: story(UIDRef,kTextStoryBoss)→ITableModelList（deprecated だが現役。SnpIterTableStories 実証）。
//         各 ITableModel の const_iterator でアンカーセルを回し、GetGridID→dict->QueryThread→GetTextStart。
//   ★ネスト表: ITableModelList が story 内の全テーブル（入れ子含む）を返す前提。ヘッダーでは包含が保証
//     されていない＝実機で取りこぼしが出たら QueryCellContentBoss から子 ITableModelList への再帰を足す。
//   ★性能: コストは Σ(rows×cols)。大きな表で重い（Find Overset はオンデマンドなので許容）。安価な
//     「表にあふれセルが在るか」の事前判定 API は SDK に無い（確認済み）。
//========================================================================================
static void KESCMCollectOversetCells(IDataBase* db, const UIDRef& storyRef, ITextModel* textModel, std::set<UID>& out)
{
	if (db == nil || textModel == nil)
		return;
	InterfacePtr<ITableModelList> tableList(storyRef, UseDefaultIID());
	if (tableList == nil)
		return;

	const int32 nTables = tableList->GetModelCount();
	for (int32 t = 0; t < nTables; ++t)
	{
		InterfacePtr<ITableModel> tableModel(tableList->QueryNthModel(t));	// ref+1
		if (tableModel == nil)
			continue;
		InterfacePtr<ITextStoryThreadDict> dict(tableModel, UseDefaultIID());	// dict は kTableModelBoss に載る
		if (dict == nil)
			continue;

		for (ITableModel::const_iterator it(tableModel->begin()), end(tableModel->end()); it != end; ++it)
		{
			const GridAddress ga = *it;					// アンカーセル（マージセルも1回）
			const GridID gridID = tableModel->GetGridID(ga);
			InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));	// ref+1
			if (thread == nil)
				continue;
			int32 span = 0;
			const TextIndex cellPos = thread->GetTextStart(&span);	// セルスレッド先頭 TextIndex
			if (span <= 0)
				continue;								// 空セルはあふれない
			if (!KESCMThreadIsOverset(textModel, cellPos))
				continue;
			const UID frameUID = KESCMFindOversetFrameUID(textModel, cellPos);
			const UID pageUID = KESCMFramePageUID(db, frameUID);
			if (pageUID != kInvalidUID)
				out.insert(pageUID);
		}
	}
}


//========================================================================================
// KESCMCollectOversetPages(KESCMOversetScan.h で宣言)
//========================================================================================
void KESCMCollectOversetPages(IDataBase* db, std::set<UID>& outPages)
{
	if (db == nil)
		return;

	// 読み取りのみ。走査で lazy recompose 等が起きても文書を dirty にしない(KBS/KESCL と同じ作法)。
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return;

	const int32 n = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < n; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			continue;

		// (1) プライマリスレッドのあふれ(通常フレームの赤「+」)。purpose-built の IsOverset で判定し、
		//     あふれていれば末尾位置から「最後の配置済みフレーム」→ページ。span<=0(空)は対象外。
		//     ★ここで continue しないこと: 親が非あふれでもテーブルのセルは単独であふれ得る((2)で拾う)。
		InterfacePtr<IFrameList> frameList(textModel->QueryFrameList());
		if (frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
		{
			const int32 span = textModel->GetPrimaryStoryThreadSpan();
			if (span > 0)
			{
				const UID frameUID = KESCMFindOversetFrameUID(textModel, span - 1);
				const UID pageUID = KESCMFramePageUID(db, frameUID);
				if (pageUID != kInvalidUID)
					outPages.insert(pageUID);	// kInvalidUID はペーストボードのみ=スキップ(仕様通り)
			}
		}

		// (2) テーブルのセル単独あふれ(赤丸)。親スレッドの IsOverset では拾えないため、全テーブル・全セルの
		//     スレッド先頭 TextIndex を個別に overset 判定する(親が非あふれのストーリーでも必ず実行する)。
		KESCMCollectOversetCells(db, storyRef, textModel, outPages);
	}
}

// KESCMOversetScan.cpp 終わり。
