//========================================================================================
//
//  KESCMPageMap.h
//
//  ページ対応(追加/削除ページ)モジュールの入口。新旧ドキュメントの平坦ページ対応から外れる
//  ページ(新版で追加された/旧版で削除された=どちらも「比較相手なし」)を、ユーザーがページ
//  パネルの右クリックトグルで登録/解除する。登録はセッション内のみ(文書ファイルには保存しない)。
//
//  ステップ1(2026-07-05): 登録トグル+状態保持+チェック表示まで。比較(除外対応表=残りページ
//  同士の順番ペアリング)への反映は次ステップ(経緯はメモリ kescm-page-offset-idea)。
//
//========================================================================================
#ifndef __KESCMPageMap_h__
#define __KESCMPageMap_h__

#include "BaseType.h"		// int32

class IActionStateList;

// ページパネル右クリックのトグル「KESCM: Register as Added/Removed Pages」の実行。選択ページを
// 「比較相手なしページ」として登録/解除する(1つでも未登録があれば全登録、全部登録済みなら全解除)。
// 結果はパネルのステータス行に出す。実体は KESCMPageMap.cpp。
void KESCMPageMapToggleSelectedPages();

// 上のトグルのメニュー状態更新(kCustomEnabling)。listToUpdate の index 番目に、
//   ・有効/無効(ページパネルの選択に文書ページが1つも無ければグレー)
//   ・チェック(選択が全部登録済み=✓/一部だけ=中間チェック)
//   ・動的ラベル(アクティブ文書が Target=Added/Source=Removed/それ以外=Added/Removed)
// を設定する。KESCMActionComponent::UpdateActionStates から呼ぶ。
void KESCMPageMapUpdateToggleState(IActionStateList* listToUpdate, int32 index);

// ドキュメントクローズ後の生存スイープ(KESCMHandleDocsClosed から呼ぶ)。閉じた文書の登録を
// 状態だけ捨てる。★閉じた db は deref しない(IDocumentList::FindDocByDataBase へのポインタ
// 比較のみ)。
void KESCMPageMapSweepClosedDocs();

#endif // __KESCMPageMap_h__
