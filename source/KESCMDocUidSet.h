//========================================================================================
//
//  KESCMDocUidSet.h
//
//  「文書DB → ページUIDの集合」をセッション内だけ保持する共通の入れ物。
//
//  KESCM は同じ形の状態を2つ持っている:
//    ・登録(Added/Removed = 比較相手なしページ) … KESCMPageMap.cpp の sRegistered
//    ・チェック(✓)                              … KESCMPageCheck.cpp の sChecked
//  用途は別物なので集合そのものは2つのままだが、「入れ物」と、それに対する定型操作
//  (生存スイープ / 全消去 / 所属判定 / 件数 / 一括置換)は1行違わず同じだった
//  (2026-08-06 ブロック9 監査 C-1)。その共通部分だけをここへ集約する。
//
//  規約:
//    ・文書ファイルには一切書かない(= 文書を dirty にしない)。Stop で全部忘れる。
//    ・★閉じた db は絶対に deref しない。生存スイープは IDocumentList::FindDocByDataBase への
//      ポインタ比較のみで判定する([[uidref-reuse-after-close]])。
//    ・空になった文書のエントリは即座に捨てる(スイープと「登録あり文書」の判定を軽く保つ)。
//      Insert と Replace(非空) がエントリを作り、Erase / ClearDoc / Replace(空) が捨てる。
//
//========================================================================================
#ifndef __KESCMDocUidSet_h__
#define __KESCMDocUidSet_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <map>
#include <set>
#include <vector>

class IDataBase;

class KESCMDocUidSet
{
public:
	typedef std::map<IDataBase*, std::set<UID> > Map;

	// uid が db の集合に入っているか。db が nil / 該当文書のエントリが無ければ kFalse。
	bool16 Contains(IDataBase* db, UID uid) const;

	// db の集合に1つでも入っているか(存在チェックのみ。描画側の早期 return 判定に使う)。
	bool16 HasAny(IDataBase* db) const;

	// db の集合の件数(無ければ 0)。
	int32 CountIn(IDataBase* db) const;

	// db の集合に uid を足す(db が nil なら何もしない)。エントリが無ければ作る。
	void Insert(IDataBase* db, UID uid);

	// db の集合から uid を外す。空になったらエントリごと捨てる。
	void Erase(IDataBase* db, UID uid);

	// db のエントリを丸ごと捨てる(db が nil / エントリが無ければ何もしない)。
	void ClearDoc(IDataBase* db);

	// 全文書ぶんを忘れる。★ポインタは触らず map を空にするだけ(deref なし = 安全)。
	void ClearAllDocs();

	// db の集合を uids で丸ごと置き換える(LOAD 用の setter)。uids が空ならエントリを捨てる。
	void Replace(IDataBase* db, const std::vector<UID>& uids);
	void Replace(IDataBase* db, const std::set<UID>& uids);

	// db の集合を out に足し込む(★out はクリアしない = 既存の集合へ合流させる使い方)。
	void CollectInto(IDataBase* db, std::set<UID>& out) const;

	// ドキュメントクローズ後の生存スイープ。閉じた文書のエントリを状態だけ捨てる。
	// ★閉じた db は deref しない(FindDocByDataBase へのポインタ比較のみ)。
	// クローズ掃除は終了シーケンス中にも来得るので session/app/docList を nil ガードする。
	void SweepClosedDocs();

	// 1文書も持っていないか(スイープ等の早期 return に使う)。
	bool16 IsEmpty() const { return fMap.empty() ? kTrue : kFalse; }

	// ★一括剪定のように集合そのものを直接いじる必要がある処理のための口
	// (例: KESCMPageCheckPruneToMarked = 文書ごとにマーク集合を作って絞る)。
	// いじり終わったら PruneEmptyDocs() を呼び、空になったエントリを捨てること。
	Map&       GetMap()       { return fMap; }
	const Map& GetMap() const { return fMap; }

	// 空集合になっている文書のエントリを捨てる(GetMap() で直接いじった後の後始末)。
	void PruneEmptyDocs();

private:
	Map fMap;
};

#endif // __KESCMDocUidSet_h__
