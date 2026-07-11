//========================================================================================
//
//  KESCMPageCheck.h
//
//  「KESCM: Check」機能の入口。ページパネルのページを選択→右クリックのトグル「KESCM: Check」で、
//  そのページに「チェック済み」印を付け外しする。チェックすると、そのページの Pages パネル
//  サムネイル中央に青い ✓(ベクター線描画=フォント非依存)を表示する。
//
//  ・登録(KESCMPageMap の Added/Removed)とは完全に独立した別の集合(用途=ユーザーの任意の目印)。
//  ・Start 中(arm 済み)かつ選択文書が Target/Source のときだけ操作可(メニューはそれ以外グレーアウト)。
//  ・セッション内のみ(文書ファイルには保存しない=dirty にもならない)。Stop で全消去(忘れる)。
//  ・✓ は Pages パネルのサムネイル(描画イベントの isThumb 分岐)にのみ描き、レイアウトビューには出さない。
//
//========================================================================================
#ifndef __KESCMPageCheck_h__
#define __KESCMPageCheck_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <set>

class IActionStateList;
class IDataBase;

// ページパネル右クリックのトグル「KESCM: Check」の実行。選択ページのチェックを付け外しする
// (1つでも未チェックがあれば全チェック、全部チェック済みなら全解除)。結果はパネルのステータス行に
// 出す。トグルしたページのサムネイルは即更新して ✓ を反映する。実体は KESCMPageCheck.cpp。
void KESCMPageCheckToggleSelectedPages();

// 上のトグルのメニュー状態更新(kCustomEnabling)。listToUpdate の index 番目に、有効/無効
// (未 Start・第3文書・選択なしはグレー)と、チェック(全部チェック済み=✓/一部=中間)を設定する。
// KESCMActionComponent::UpdateActionStates から呼ぶ。
void KESCMPageCheckUpdateToggleState(IActionStateList* listToUpdate, int32 index);

// ドキュメントクローズ後の生存スイープ(KESCMHandleDocsClosed から呼ぶ)。閉じた文書のチェックを
// 状態だけ捨てる。★閉じた db は deref しない(FindDocByDataBase へのポインタ比較のみ)。
void KESCMPageCheckSweepClosedDocs();

// 全文書のチェックを丸ごと忘れる。Stop(KESCMDoClearMarks)で呼び、比較を解除したらチェックも
// 残さない。ポインタは触らず map を空にするだけ(deref なし=安全)。
void KESCMPageCheckClearAllDocs();

// 再比較後に呼ぶ。各文書のチェック済みページのうち、もうマーク(枠/「/」)を持たなくなったページの
// チェックを外す(ユーザー指定 2026-07-11:「枠が無くなったらチェックの記憶も外れる」)。マーク集合は
// KESCMCollectChangedPageUIDs で引く(db が比較対象=sDB/sSrcDB でなければ空=その文書のチェックは
// 全部外れる)。KESCMDoMarkChangesDoc の末尾(サムネイル更新の前)から呼ぶ。
void KESCMPageCheckPruneToMarked();

// pageUID(db内)がチェック済みか。db が nil、または該当文書のチェックが無ければ kFalse。
// 描画側(KESCMDrawEventHandler の isThumb 分岐)が ✓ を描くかの判定に使う。
bool16 KESCMPageCheckIsChecked(IDataBase* db, UID pageUID);

// db にチェック済みページが1つでもあるか(存在チェックのみ)。描画側の早期 return 判定
// (KESCMDrawEventHandler::HandleDrawEvent の anyMarkableContent)に使う。
bool16 KESCMPageCheckHasAny(IDataBase* db);

#endif // __KESCMPageCheck_h__
