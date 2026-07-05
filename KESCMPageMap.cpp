//========================================================================================
//
//  KESCMPageMap.cpp
//
//  ページ対応(追加/削除ページ)モジュール。ページパネルでページを選択→右クリックのトグル
//  「KESCM: Register as Added/Removed Pages」で「比較相手なしページ」として登録/解除する。
//  アクティブ文書が Target なら「追加ページ」/Source なら「削除ページ」だが、概念はどちらも
//  同じ「比較相手なし」(対応表からの除外)なので、入れ物は文書DBごとの UID セット1種類。
//
//  - 選択の取得: ★Utils<ILayoutUIUtils>()->GetSelectedPages()(公式API、ILayoutUIUtils.h:183)。
//    bPagesOnly=kTrue でスプレッド選択も所属ページUIDへ展開、bIncludeMasters=kFalse でマスター
//    除外。本家実使用例=source/open の PageTransitionsPanelObserver.cpp:672。
//    ★旧実装の自前 IUIDListControlData 読み(kPagesPanelWidgetBoss 直上)は「ページアイコン選択」
//    しか拾えず、見開き(スプレッド)として選択されると空になり項目が出なかった(2026-07-05 実機)。
//    パネルには文書ページ用/マスター用のサブパネルが2つあり、選択の置き場は1本ではない。
//  - メニュー: KESCM.fr がページパネルのページ右クリックメニュー(内部名 RtMenuPagesPanel、
//    2026-07-05 実機確定)へトグル項目を追加している。内部名は非翻訳キーなので全ロケール共通。
//    チェック/有効無効/動的ラベルは kCustomEnabling → KESCMPageMapUpdateToggleState。
//  - 登録の保持: セッション内のみ(文書ファイルには保存しない=dirty にもならない)。文書クローズ時は
//    KESCMHandleDocsClosed からの KESCMPageMapSweepClosedDocs で状態だけ捨てる(deref なし)。
//  - ステップ2(2026-07-05): 除外対応表(KESCMBuildPairing/KESCMMapTargetToSource/
//    KESCMMapSourceToTarget)。登録済みページを平坦列から除いて残り同士を順番に対応させ、
//    比較(KESCMDoMarkChangesDoc)・peek旧版取得・スプレッド再比較・CMYK色サンプラ・
//    Hide UnchangedのSource側分類の5箇所が、素の平坦列 zip からこの対応表経由に置き換わった。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"
#include "IApplication.h"
#include "IDocument.h"
#include "IDataBase.h"
#include "IDocumentList.h"		// 生存スイープ(FindDocByDataBase へのポインタ比較のみ)
#include "IActionStateList.h"	// メニューの有効/チェック/動的ラベル(SetNthActionName)
#include "ILayoutUIUtils.h"		// GetFrontDocument / GetSelectedPages(ページパネル選択の公式取得)
#include "Utils.h"
#include "PersistUtils.h"		// ::GetDataBase(IDocument→IDataBase)
#include "UIDList.h"
#include "PMString.h"

#include <map>
#include <set>
#include <vector>

#include "KESCMCore.h"			// KESCMCollectPageUIDs / KESCMArmedTargetDB / KESCMArmedSourceDB / KESCMSetStatus
#include "KESCMPageMap.h"

// 登録済み「比較相手なしページ」: 文書DB → ページUIDの集合。セッション内のみ。
// 空になった文書のエントリは即座に消す(スイープと「登録あり文書」の判定を軽く保つ)。
static std::map<IDataBase*, std::set<UID> > sRegistered;

// ヘルパ: vector<UID> の線形 contains(選択は高々数十件なので set 化するまでもない)。
static bool16 KESCMVecContains(const std::vector<UID>& v, UID u)
{
	for (size_t k = 0; k < v.size(); ++k)
	{
		if (v[k] == u)
			return kTrue;
	}
	return kFalse;
}

//========================================================================================
// ヘルパ: ページパネルの選択を読む。outDB=選択が属する文書(=アクティブ文書)、outPages=文書の
// ページ列に実在する選択ページUID。有効なページが1つ以上あれば kTrue。
// 取得は公式 API Utils<ILayoutUIUtils>()->GetSelectedPages():
//   ・bIncludeMasters=kFalse … マスターページ/マスタースプレッドを除外(比較対象外)
//   ・bPagesOnly=kTrue …… 見開き全体の選択(パネル内部ではスプレッド扱い)も所属ページUIDへ展開
//   ・bCurrentPageOnly=kTrue はパネル非表示時のフォールバック規定(このメニューはパネルからしか
//     開けないため実質使われない)
// 返ったUIDは念のため文書の平坦ページ列(KESCMCollectPageUIDs)と突合し、重複も除去する。
//========================================================================================
static bool16 KESCMPageMapReadSelection(IDataBase*& outDB, std::vector<UID>& outPages)
{
	outDB = nil;
	outPages.clear();

	// ページパネルの表示対象=アクティブ(最前面)文書。その db を UIDList に仕込んで渡す契約
	// (ILayoutUIUtils.h:178 "UIDList must be set up with proper database")。
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
		if (KESCMVecContains(flat, u) && !KESCMVecContains(outPages, u))
			outPages.push_back(u);
	}
	if (outPages.empty())
		return kFalse;

	outDB = db;
	return kTrue;
}

// 文書の役割に応じた呼び名(ステータス行の文言用): Target=added/Source=removed/
// 未 arm・無関係な文書=総称 "added/removed"。
static const char* KESCMPageMapRoleWord(IDataBase* db)
{
	if (db != nil && db == KESCMArmedTargetDB())
		return "added";
	if (db != nil && db == KESCMArmedSourceDB())
		return "removed";
	return "added/removed";
}

//========================================================================================
// KESCMPageMapToggleSelectedPages(KESCMPageMap.h で宣言)
//   右クリックトグルの実行。選択ページに1つでも未登録があれば「全登録」、全部登録済みなら
//   「全解除」(チェック表示と対になる標準的なトグル動作)。結果と、その文書の登録合計を
//   パネルのステータス行に出す(パネルが隠れていてもセッションに残り、再表示時に見える)。
//========================================================================================
void KESCMPageMapToggleSelectedPages()
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageMapReadSelection(db, pages))
		return;		// メニューは kCustomEnabling で無効化済みのはずだが保険

	std::set<UID>& reg = sRegistered[db];

	bool16 anyUnregistered = kFalse;
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (reg.count(pages[i]) == 0)
		{
			anyUnregistered = kTrue;
			break;
		}
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	if (anyUnregistered)
	{
		for (size_t i = 0; i < pages.size(); ++i)
			reg.insert(pages[i]);
		msg.Append("Registered ");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" page(s) as ");
		msg.Append(KESCMPageMapRoleWord(db));
		msg.Append(" (no counterpart).");
	}
	else
	{
		for (size_t i = 0; i < pages.size(); ++i)
			reg.erase(pages[i]);
		msg.Append("Unregistered ");
		msg.AppendNumber((int32)pages.size());
		msg.Append(" ");
		msg.Append(KESCMPageMapRoleWord(db));
		msg.Append(" page(s).");
	}

	// 空になったらエントリごと捨てる。合計はその後に数える(解除で 0 なら "Total: 0")。
	if (reg.empty())
		sRegistered.erase(db);
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sRegistered.find(db);
	msg.Append(" Total in this document: ");
	msg.AppendNumber(it != sRegistered.end() ? (int32)it->second.size() : 0);

	// ★既に比較実行済み(Start後)なら、除外対応表が変わった分をその場で反映するため、Start と同じ
	// 全体再比較を自動で走らせる(実機確認: 比較後に登録を変えてもリアルタイムには反映されなかった
	// ため、2026-07-05 にこの自動再比較を追加)。Start 未実行なら何もしない(次の Start で自然に反映)。
	// KESCMDoMarkChangesDoc は Start 同様「Show Marks on Source」を既定 ON に戻す等の副作用も持つが、
	// これは手動で Start を押し直すのと同じ挙動なので許容する。
	if (KESCMIsArmed() && KESCMArmedTargetDB() != nil && KESCMArmedSourceDB() != nil)
	{
		PMString report;
		KESCMDoMarkChangesDoc(KESCMArmedTargetDB(), KESCMArmedSourceDB(), report);
		msg.AppendW(UTF32TextChar(0x0A));
		msg.Append(report);
	}

	KESCMSetStatus(msg);
}

//========================================================================================
// KESCMPageMapUpdateToggleState(KESCMPageMap.h で宣言)
//   kCustomEnabling のメニュー状態更新。KESCMActionComponent::UpdateActionStates から呼ばれる。
//   ・選択に文書ページが無い(選択なし/マスターのみ)→グレーアウト
//   ・選択が全部登録済み→チェック/一部だけ登録済み→中間チェック(kMultiSelectedAction=dash)
//   ・ラベルはアクティブ文書の役割で出し分け(SetNthActionName。IActionStateList.h:78 の
//     「状態でメニュー名を動的に変える」用途そのもの。dynamic menu の仕組みは不要)
//========================================================================================
void KESCMPageMapUpdateToggleState(IActionStateList* listToUpdate, int32 index)
{
	IDataBase* db = nil;
	std::vector<UID> pages;
	if (!KESCMPageMapReadSelection(db, pages))
	{
		listToUpdate->SetNthActionState(index, kDisabled_Unselected);
		return;
	}

	int32 regCount = 0;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sRegistered.find(db);
	if (it != sRegistered.end())
	{
		for (size_t i = 0; i < pages.size(); ++i)
		{
			if (it->second.count(pages[i]) > 0)
				++regCount;
		}
	}

	int16 state = kEnabledAction;
	if (regCount == (int32)pages.size())
		state |= kSelectedAction;			// 全部登録済み=チェック
	else if (regCount > 0)
		state |= kMultiSelectedAction;		// 一部だけ登録済み=中間チェック
	listToUpdate->SetNthActionState(index, state);

	// 動的ラベル(英語固定=パネルUIと同方針)。未 arm や第3文書では総称のまま。
	PMString name;
	if (db == KESCMArmedTargetDB())
		name = "KESCM: Register as Added Pages";
	else if (db == KESCMArmedSourceDB())
		name = "KESCM: Register as Removed Pages";
	else
		name = "KESCM: Register as Added/Removed Pages";
	name.SetTranslatable(kFalse);
	listToUpdate->SetNthActionName(index, name);
}

//========================================================================================
// KESCMPageMapSweepClosedDocs(KESCMPageMap.h で宣言)
//   ドキュメントクローズ直後の生存スイープ(呼び所=KESCMHandleDocsClosed)。閉じた文書の登録を
//   状態だけ捨てる。★閉じた db は FindDocByDataBase へのポインタ比較のみで、絶対に deref しない
//   (KESCM の他のクローズ後片付けと同じ流儀)。こまめに捨てることで、閉じた文書とアドレス再利用の
//   新文書を取り違える余地も最小化する。
//========================================================================================
void KESCMPageMapSweepClosedDocs()
{
	if (sRegistered.empty())
		return;

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	std::map<IDataBase*, std::set<UID> >::iterator it = sRegistered.begin();
	while (it != sRegistered.end())
	{
		if (docList->FindDocByDataBase(it->first) == nil)
			sRegistered.erase(it++);	// 閉じた文書: 状態だけ捨てる(deref なし)
		else
			++it;
	}
}

//========================================================================================
// KESCMPageMapIsRegistered(KESCMPageMap.h で宣言)
//========================================================================================
bool16 KESCMPageMapIsRegistered(IDataBase* db, UID pageUID)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sRegistered.find(db);
	return (it != sRegistered.end() && it->second.count(pageUID) > 0) ? kTrue : kFalse;
}

//========================================================================================
// KESCMPageMapHasAnyRegistered(KESCMPageMap.h で宣言)
//========================================================================================
bool16 KESCMPageMapHasAnyRegistered(IDataBase* db)
{
	if (db == nil)
		return kFalse;
	std::map<IDataBase*, std::set<UID> >::const_iterator it = sRegistered.find(db);
	return (it != sRegistered.end() && !it->second.empty()) ? kTrue : kFalse;
}

//========================================================================================
// KESCMBuildPairing(KESCMPageMap.h で宣言)
//   targetDB/sourceDB の平坦ページ列(KESCMCollectPageUIDs)から、それぞれ登録済み(比較相手なし)
//   ページを除き、残り同士を順番に対応させる。従来(ステップ1以前)は素の平坦列を直接 zip していたが、
//   追加/削除ページが登録されていれば、そのページを飛ばして残りを詰めて対応させる。
//========================================================================================
void KESCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
{
	outTargetPages.clear();
	outSourcePages.clear();
	if (targetDB == nil || sourceDB == nil)
		return;

	std::vector<UID> tFlat, sFlat;
	KESCMCollectPageUIDs(targetDB, tFlat);
	KESCMCollectPageUIDs(sourceDB, sFlat);

	std::vector<UID> tFiltered, sFiltered;
	tFiltered.reserve(tFlat.size());
	sFiltered.reserve(sFlat.size());
	for (size_t i = 0; i < tFlat.size(); ++i)
		if (!KESCMPageMapIsRegistered(targetDB, tFlat[i]))
			tFiltered.push_back(tFlat[i]);
	for (size_t i = 0; i < sFlat.size(); ++i)
		if (!KESCMPageMapIsRegistered(sourceDB, sFlat[i]))
			sFiltered.push_back(sFlat[i]);

	const size_t n = (tFiltered.size() < sFiltered.size()) ? tFiltered.size() : sFiltered.size();
	outTargetPages.assign(tFiltered.begin(), tFiltered.begin() + n);
	outSourcePages.assign(sFiltered.begin(), sFiltered.begin() + n);
}

//========================================================================================
// KESCMMapTargetToSource / KESCMMapSourceToTarget(KESCMPageMap.h で宣言)
//   1ページ単位の対応変換。内部で KESCMBuildPairing を呼んで対応表を作り、探しているページを
//   線形探索で引く(ページ数は高々数百なので毎回作り直しても軽い。呼び出し側は既に1スプレッド分
//   =数ページの粒度でしか呼ばないため実測コストも小さい)。
//========================================================================================
bool16 KESCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID)
{
	outSourcePageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < tPages.size(); ++i)
	{
		if (tPages[i] == targetPageUID)
		{
			outSourcePageUID = sPages[i];
			return kTrue;
		}
	}
	return kFalse;
}

bool16 KESCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID)
{
	outTargetPageUID = kInvalidUID;
	std::vector<UID> tPages, sPages;
	KESCMBuildPairing(targetDB, sourceDB, tPages, sPages);
	for (size_t i = 0; i < sPages.size(); ++i)
	{
		if (sPages[i] == sourcePageUID)
		{
			outTargetPageUID = tPages[i];
			return kTrue;
		}
	}
	return kFalse;
}

// KESCMPageMap.cpp 終わり。
