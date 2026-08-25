//========================================================================================
//
//  KCMDocUidSet.h
//
//  「文書DB → ページUIDの集合」をセッション内だけ保持する共通の入れ物。
//
//  KCM は同じ形の状態を2つ持っている:
//    ・登録(Added/Removed = 比較相手なしページ) … KCMPageMap.cpp の sRegistered
//    ・チェック(✓)                              … KCMPageCheck.cpp の sChecked
//  用途は別物なので集合そのものは2つのままだが、「入れ物」と、それに対する定型操作
//  (生存スイープ / 全消去 / 所属判定 / 件数 / 一括置換)は1行違わず同じだった
//  (2026-08-06 ブロック9 監査 C-1)。その共通部分だけをここへ集約する。
//
//  規約:
//    ・文書ファイルには一切書かない(= 文書を dirty にしない)。Stop で全部忘れる。
//    ・★閉じた db は絶対に deref しない。生存スイープは IDocumentList::FindDocByDataBase への
//      ポインタ比較のみで判定する([[uidref-reuse-after-close]])。
//    ・空になった文書のエントリは即座に捨てる(スイープと「登録あり文書」の判定を軽く保つ)。
//      Insert と Replace(非空) がエントリを作り、Erase / Replace(空) が捨てる(全消去は ClearAllDocs)。
//
//========================================================================================
#ifndef __KCMDocUidSet_h__
#define __KCMDocUidSet_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <map>
#include <set>
#include <vector>

class IDataBase;

class KCMDocUidSet
{
public:
	typedef std::map<IDataBase*, std::set<UID> > Map;

	// uid が db の集合に入っているか。db が nil / 該当文書のエントリが無ければ kFalse。
	bool16 Contains(IDataBase* db, UID uid) const;

	// db の集合に1つでも入っているか(存在チェックのみ。描画側の早期 return 判定に使う)。
	bool16 HasAny(IDataBase* db) const;

	// db の集合の件数(無ければ 0)。
	int32 CountIn(IDataBase* db) const;

	// ★選択に対する2つの問い(2026-08-18 集約)。登録(PageMap)とチェック(PageCheck)が
	//   「選択ページのうち何枚が集合に入っているか」「1枚でも入っていないものがあるか」を
	//   1行違わず同じループで持っていたので、ここへ移した(2026-08-06 ブロック9 C-1 の続き)。
	//   ★Contains をページ数ぶん呼ぶのと違い、ロックと FindDoc は1回で済む
	//     ——ページごとにロックし直すと、途中で集合が変わったとき「全部/一部」の判定が
	//     食い違った状態のまま確定しうる。
	//   ⚠db が nil / エントリ無し = 「どれも入っていない」と同じ扱い(Contains と一致)。

	// uids のうち db の集合に入っている件数。
	int32 CountIn(IDataBase* db, const std::vector<UID>& uids) const;

	// uids の中に db の集合へ入っていないものが1つでもあるか(空なら kFalse)。
	bool16 AnyNotIn(IDataBase* db, const std::vector<UID>& uids) const;

	// db の集合に uid を足す(db が nil なら何もしない)。エントリが無ければ作る。
	void Insert(IDataBase* db, UID uid);

	// db の集合から uid を外す。空になったらエントリごと捨てる。
	void Erase(IDataBase* db, UID uid);

	// (ClearDoc(db)=1文書分だけ捨てる口は 2026-08-17 の不具合再検査 B4 で削除した。唯一の呼び手だった
	//  KCMPageMapClearAll 自身が呼び手ゼロだったため。「1文書分だけ忘れる」需要が出たら、そのとき
	//  呼び手と一緒に戻すこと ---- 呼び手のいない口は守られない約束になる。)

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
	// (例: KCMPageCheckPruneToMarked = 文書ごとにマーク集合を作って絞る)。
	// いじり終わったら PruneEmptyDocs() を呼び、空になったエントリを捨てること。
	Map&       GetMap()       { return fMap; }
	const Map& GetMap() const { return fMap; }

	// 空集合になっている文書のエントリを捨てる(GetMap() で直接いじった後の後始末)。
	void PruneEmptyDocs();

private:
	// ★★2026-08-15(第2段 Task 12B): db に対応するエントリを引く共通の口。
	//   fMap のキーは IDataBase* なので、**バックグラウンド(PDF の非同期書き出し)から
	//   クローン DB で引くと必ず外れる**(＝登録ページの緑「/」や ✓ が書き出しに出ない)。
	//   ⇒ ポインタで外したら**ファイル同一性(KCMIsSameDoc)で引き直す**。
	//   ⚠参照系(Contains/HasAny/CountIn/CollectInto)だけがこれを使う。**更新系は使わない**
	//     ——更新は必ずメインスレッドで、しかも「今その db に足す」意味なので、
	//     クローンのエントリを勝手に育てるとかえって取り違える。
	//   計算量: fMap の要素数＝開いている文書数(数個)。ポインタ一致で外れた時だけの線形探索。
	Map::const_iterator FindDoc(IDataBase* db) const;

	Map fMap;
};

#endif // __KCMDocUidSet_h__
