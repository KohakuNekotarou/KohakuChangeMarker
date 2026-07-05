//========================================================================================
//
//  KESCMPageMap.h
//
//  ページ対応(追加/削除ページ)モジュールの入口。新旧ドキュメントの平坦ページ対応から外れる
//  ページ(新版で追加された/旧版で削除された=どちらも「比較相手なし」)を、ユーザーがページ
//  パネルの右クリックトグルで登録/解除する。登録はセッション内のみ(文書ファイルには保存しない)。
//
//  ステップ1(2026-07-05): 登録トグル+状態保持+チェック表示。
//  ステップ2(2026-07-05): 除外対応表(登録済みページを除いた順番ペアリング)。KESCMCollectPageUIDs
//  の素の平坦列を直接zipしていた5箇所(KESCMDoMarkChangesDoc/peek旧版取得/スプレッド再比較/
//  CMYK色サンプラ/Hide UnchangedのSource側分類)を、ここの KESCMBuildPairing / KESCMMapTargetToSource /
//  KESCMMapSourceToTarget に置き換え済み(経緯はメモリ kescm-page-offset-idea)。
//
//========================================================================================
#ifndef __KESCMPageMap_h__
#define __KESCMPageMap_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <vector>

class IActionStateList;
class IDataBase;

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

// pageUID(db内)が「比較相手なし」として登録済みか。db が nil、または該当文書の登録が無ければ kFalse。
bool16 KESCMPageMapIsRegistered(IDataBase* db, UID pageUID);

// db に登録済み(比較相手なし)ページが1つでもあるか(存在チェックのみ)。描画側の早期 return 判定
// (KESCMDrawEventHandler::HandleDrawEvent の wantMarks/wantSrcMarks)に使う。
bool16 KESCMPageMapHasAnyRegistered(IDataBase* db);

// 除外対応表: targetDB/sourceDB それぞれの平坦ページ列(KESCMCollectPageUIDs)から登録済み
// (比較相手なし)ページを除き、残り同士を順番に対応させる。outTargetPages[i] と outSourcePages[i] が
// 対応ペア(短い方の長さに切り詰め済み=2つの配列は同じ長さになる)。targetDB/sourceDB が nil なら
// 両方とも空にして戻る。実体は KESCMPageMap.cpp。
void KESCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages);

// targetPageUID(targetDB内)に対応する sourceDB 側のページを1つ求める(内部で KESCMBuildPairing を
// 使う)。targetPageUID 自身が登録済み(除外対象)か、対応表の範囲外(対応相手なし)なら kFalse で
// outSourcePageUID は不定。
bool16 KESCMMapTargetToSource(IDataBase* targetDB, IDataBase* sourceDB,
	UID targetPageUID, UID& outSourcePageUID);

// 逆方向: sourcePageUID(sourceDB内)に対応する targetDB 側のページを1つ求める。
bool16 KESCMMapSourceToTarget(IDataBase* targetDB, IDataBase* sourceDB,
	UID sourcePageUID, UID& outTargetPageUID);

#endif // __KESCMPageMap_h__
