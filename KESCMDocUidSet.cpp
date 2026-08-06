//========================================================================================
//
//  KESCMDocUidSet.cpp
//
//  「文書DB → ページUIDの集合」の共通実装(KESCMDocUidSet.h 参照)。登録(Added/Removed)と
//  チェック(✓)が同じ形の入れ物と定型操作を持っていたので、そこだけを集約したもの
//  (2026-08-06 ブロック9 監査 C-1)。集合そのものは呼び出し側が2つ別々に持つ。
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ISession.h"
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocumentList.h"		// 生存スイープ(FindDocByDataBase へのポインタ比較のみ)

#include "KESCMDocUidSet.h"

//========================================================================================
// 参照系
//========================================================================================
bool16 KESCMDocUidSet::Contains(IDataBase* db, UID uid) const
{
	if (db == nil)
		return kFalse;
	Map::const_iterator it = fMap.find(db);
	return (it != fMap.end() && it->second.count(uid) > 0) ? kTrue : kFalse;
}

bool16 KESCMDocUidSet::HasAny(IDataBase* db) const
{
	if (db == nil)
		return kFalse;
	Map::const_iterator it = fMap.find(db);
	return (it != fMap.end() && !it->second.empty()) ? kTrue : kFalse;
}

int32 KESCMDocUidSet::CountIn(IDataBase* db) const
{
	if (db == nil)
		return 0;
	Map::const_iterator it = fMap.find(db);
	return (it != fMap.end()) ? (int32)it->second.size() : 0;
}

void KESCMDocUidSet::CollectInto(IDataBase* db, std::set<UID>& out) const
{
	if (db == nil)
		return;
	Map::const_iterator it = fMap.find(db);
	if (it == fMap.end())
		return;
	out.insert(it->second.begin(), it->second.end());
}

//========================================================================================
// 更新系(★空になったエントリは即座に捨てる = KESCMDocUidSet.h の規約)
//========================================================================================
void KESCMDocUidSet::Insert(IDataBase* db, UID uid)
{
	if (db == nil)
		return;
	fMap[db].insert(uid);
}

void KESCMDocUidSet::Erase(IDataBase* db, UID uid)
{
	if (db == nil)
		return;
	Map::iterator it = fMap.find(db);
	if (it == fMap.end())
		return;
	it->second.erase(uid);
	if (it->second.empty())
		fMap.erase(it);
}

void KESCMDocUidSet::ClearDoc(IDataBase* db)
{
	if (db == nil)
		return;
	fMap.erase(db);
}

void KESCMDocUidSet::ClearAllDocs()
{
	fMap.clear();
}

void KESCMDocUidSet::Replace(IDataBase* db, const std::vector<UID>& uids)
{
	if (db == nil)
		return;
	if (uids.empty())
	{
		fMap.erase(db);
		return;
	}
	std::set<UID>& s = fMap[db];
	s.clear();
	for (size_t i = 0; i < uids.size(); ++i)
		s.insert(uids[i]);
}

void KESCMDocUidSet::Replace(IDataBase* db, const std::set<UID>& uids)
{
	if (db == nil)
		return;
	if (uids.empty())
	{
		fMap.erase(db);
		return;
	}
	fMap[db] = uids;
}

void KESCMDocUidSet::PruneEmptyDocs()
{
	Map::iterator it = fMap.begin();
	while (it != fMap.end())
	{
		if (it->second.empty())
			fMap.erase(it++);
		else
			++it;
	}
}

//========================================================================================
// 生存スイープ
//   ドキュメントクローズ直後に呼ぶ(呼び所 = KESCMHandleDocsClosed)。閉じた文書のエントリを
//   状態だけ捨てる。★閉じた db は FindDocByDataBase へのポインタ比較のみで、絶対に deref しない
//   (KESCM の他のクローズ後片付けと同じ流儀)。こまめに捨てることで、閉じた文書とアドレス再利用の
//   新文書を取り違える余地も最小化する([[uidref-reuse-after-close]])。
//========================================================================================
void KESCMDocUidSet::SweepClosedDocs()
{
	if (fMap.empty())
		return;

	// クローズ掃除は終了シーケンス中にも来得るので session を nil ガード(2026-07-25 に KESCM 全体で統一)。
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;

	Map::iterator it = fMap.begin();
	while (it != fMap.end())
	{
		if (docList->FindDocByDataBase(it->first) == nil)
			fMap.erase(it++);	// 閉じた文書: 状態だけ捨てる(deref なし)
		else
			++it;
	}
}

// KESCMDocUidSet.cpp 終わり。
