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
#include "KESCMThreadSafety.h"	// ★KESCMIsSameDoc(BG のクローン DB)/共有状態のロック

//========================================================================================
// ★★2026-08-15(第2段 Task 12B)= この入れ物は **main が書き、BG(PDF の非同期書き出し)の
//   描画パスが読む**ようになった。よって参照系・更新系の**このファイルにある全メソッド**で共有ロックを
//   取る(再帰ロックなので、既にロックしている描画ループから呼ばれても安全)。理由の全文は KESCMThreadSafety.h。
//   ⚠**例外が2つあり、どちらもヘッダー側にある**(2026-08-17 の不具合再検査 B4 で明文化):
//     ・IsEmpty()  … ヘッダー内 inline でロックを取らない。呼び手は KESCMPageCheckPruneToMarked
//                    (メインスレッド)1つだけなので実害は無いが、**BG から呼べる口ではない**。
//     ・GetMap()   … 素の map を渡す口なので、そもそも呼び手がロックする契約(ヘッダーに記載)。
//   ⇒ 「全メソッドが守っている」と読んで新しい呼び手を BG から足さないこと。
//========================================================================================

// db に対応するエントリ(ポインタで外したらファイル同一性で引き直す)。宣言側のコメントが理由。
KESCMDocUidSet::Map::const_iterator KESCMDocUidSet::FindDoc(IDataBase* db) const
{
	Map::const_iterator it = fMap.find(db);
	if (it != fMap.end())
		return it;						// メインスレッドの通常経路はここで即決(従来と同じコスト)
	for (it = fMap.begin(); it != fMap.end(); ++it)
	{
		if (KESCMIsSameDoc(it->first, db))
			return it;					// ★BG のクローン DB はここで拾う
	}
	return fMap.end();
}

//========================================================================================
// 参照系
//========================================================================================
bool16 KESCMDocUidSet::Contains(IDataBase* db, UID uid) const
{
	if (db == nil)
		return kFalse;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end() && it->second.count(uid) > 0) ? kTrue : kFalse;
}

bool16 KESCMDocUidSet::HasAny(IDataBase* db) const
{
	if (db == nil)
		return kFalse;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end() && !it->second.empty()) ? kTrue : kFalse;
}

int32 KESCMDocUidSet::CountIn(IDataBase* db) const
{
	if (db == nil)
		return 0;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	return (it != fMap.end()) ? (int32)it->second.size() : 0;
}

int32 KESCMDocUidSet::CountIn(IDataBase* db, const std::vector<UID>& uids) const
{
	if (db == nil)
		return 0;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	if (it == fMap.end())
		return 0;
	int32 n = 0;
	for (size_t i = 0; i < uids.size(); ++i)
	{
		if (it->second.count(uids[i]) > 0)
			++n;
	}
	return n;
}

bool16 KESCMDocUidSet::AnyNotIn(IDataBase* db, const std::vector<UID>& uids) const
{
	if (uids.empty())
		return kFalse;					// 問う対象が無い = 「入っていないもの」も無い
	if (db == nil)
		return kTrue;					// 集合が引けない = どれも入っていない(Contains が全部 kFalse を返す形)
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
	if (it == fMap.end())
		return kTrue;					// 同上(この文書には1件も登録が無い)
	for (size_t i = 0; i < uids.size(); ++i)
	{
		if (it->second.count(uids[i]) == 0)
			return kTrue;
	}
	return kFalse;
}

void KESCMDocUidSet::CollectInto(IDataBase* db, std::set<UID>& out) const
{
	if (db == nil)
		return;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::const_iterator it = FindDoc(db);
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
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	fMap[db].insert(uid);
}

void KESCMDocUidSet::Erase(IDataBase* db, UID uid)
{
	if (db == nil)
		return;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	Map::iterator it = fMap.find(db);
	if (it == fMap.end())
		return;
	it->second.erase(uid);
	if (it->second.empty())
		fMap.erase(it);
}

// (ClearDoc は 2026-08-17 の不具合再検査 B4 で削除: 唯一の呼び手 KESCMPageMapClearAll が呼び手ゼロだった。
//  1文書分の消去が要るときは Replace(db, 空) が同じ働きをする=エントリごと捨てる。)

void KESCMDocUidSet::ClearAllDocs()
{
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	fMap.clear();
}

void KESCMDocUidSet::Replace(IDataBase* db, const std::vector<UID>& uids)
{
	if (db == nil)
		return;
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
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
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
	if (uids.empty())
	{
		fMap.erase(db);
		return;
	}
	fMap[db] = uids;
}

void KESCMDocUidSet::PruneEmptyDocs()
{
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
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
	// ⚠この関数は「文書リストに居なければ閉じた」という推論なので、**BG では成り立たない**
	//   (BG は別 DB を見る)。呼び所の KESCMHandleDocsClosed が入口で
	//   IDThreading::IsMainThreadDomain() を見て弾いているので、ここへ BG から来ることはない
	//   (2026-08-15 第2段 Task 11C の修正)。★ここで二重に判定しない＝[[one-question-one-place]]。
	KESCMMarkStateLock lock(KESCMMarkStateMutex());
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
