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
#include <set>

class IDataBase;

//----------------------------------------------------------------------------------------
// ページ単位トグル(Register / Check)が今どう見えるべきか。
//
// ★★2026-08-15(API 監査 B2 の A-2): **メニューに書き込むのは UI の仕事**なので、model は
//   「有効か」「全部か一部か」「どちらの役割の文書か」を答えるだけにした。
//   ⇒ `IActionStateList` が model 側の境界から消える。手本の `ICusCondTxtFacade` に
//     メニュー状態のメソッドが1つも無いのと同じ形。**ラベルの文字列も UI が持つ**(UI 文字列だから)。
//   ⚠この形が良いことは `IKESCMPageFlagsFacade.h` に自分で書いてあった——「返す形の方が良い分業
//     だが、第1段は挙動を変えない規則なので見送る ⇒ **第2段で model プラグインになったら見直す**」。
//     その第2段が終わったので実施した。
//
// ★Register と Check で同じ型を使う。答えの形が同じで、違うのは「何を数えるか」だけ。
//----------------------------------------------------------------------------------------
enum KESCMPageTick
{
	kKESCMPageTickNone = 0,		// 1つも付いていない
	kKESCMPageTickSome,			// 一部だけ付いている(中間チェック=kMultiSelectedAction)
	kKESCMPageTickAll			// 選択が全部付いている(チェック=kSelectedAction)
};

enum KESCMPageRole
{
	kKESCMPageRoleNone = 0,		// 比較していない、または第3の文書
	kKESCMPageRoleTarget,		// 比較の Target(新)側
	kKESCMPageRoleSource		// 比較の Source(旧)側
};

struct KESCMPageToggleState
{
	bool16			fEnabled;	// kFalse ならメニューはグレー(そのとき fTick / fRole は意味を持たない)
	KESCMPageTick	fTick;
	KESCMPageRole	fRole;		// Register のラベル出し分け用。Check は読まない

	KESCMPageToggleState()
		: fEnabled(kFalse), fTick(kKESCMPageTickNone), fRole(kKESCMPageRoleNone) {}
};

// ページパネルの選択ページを読む共通リーダー。outDB=選択が属する文書(=アクティブ/最前面文書)、
// outPages=文書のページ列に実在する選択ページUID(重複除去済み)。有効なページが1つ以上あれば kTrue。
// 取得は公式 API Utils<ILayoutUIUtils>()->GetSelectedPages(スプレッド選択もページUIDへ展開)。
// ★ページパネル右クリック3機能(Register/Check/Refresh)共通の唯一の読み口(2026-07-15 に3重コピーを統合)。
//   「選択ページ」の意味を変えるときは必ずここ1箇所で。実体は KESCMPageMap.cpp。
//
// ★includeMasters(2026-08-13) = マスターページも選択として読むか。3機能で答えが割れるので引数にした。
//   ・kTrue …… Check と Refresh。マスタースプレッドは 2026-08-11 から比較対象なので(名前で対応付け=
//               KESCMBuildMasterPairing)、マスターページにも枠が出る=✓ も部分再比較も意味を持つ。
//   ・kFalse … Register(既定)。**マスターに登録を許してはいけない**: マスターの対応付けは名前で行い、
//               登録による除外を一切見ない(KESCMBuildMasterPairing の契約)。通すと「登録はできるのに
//               比較は何も変わらない」メニューになる。
bool16 KESCMPageMapReadSelection(IDataBase*& outDB, std::vector<UID>& outPages, bool16 includeMasters = kFalse);

// ページパネル右クリックのトグル「KCM: Register as Added/Removed Pages」の実行。選択ページを
// 「比較相手なしページ」として登録/解除する(1つでも未登録があれば全登録、全部登録済みなら全解除)。
// 結果はパネルのステータス行に出す。実体は KESCMPageMap.cpp。
void KESCMPageMapToggleSelectedPages();

// 上のトグル(kCustomEnabling)が今どう見えるべきか。
//   ・fEnabled … ページパネルの選択に文書ページが1つも無い/未 Start/第3文書ならグレー
//   ・fTick …… 選択が全部登録済みなら All / 一部だけなら Some
//   ・fRole …… アクティブ文書が Target(=Added) か Source(=Removed) か
// ★**メニューには触らない**。SetNthActionState / SetNthActionName を呼ぶのは呼び手
//   (ui/KESCMActionComponent.cpp の UpdateActionStates)。
KESCMPageToggleState KESCMPageMapGetToggleState();

// ドキュメントクローズ後の生存スイープ(KESCMHandleDocsClosed から呼ぶ)。閉じた文書の登録を
// 状態だけ捨てる。★閉じた db は deref しない(IDocumentList::FindDocByDataBase へのポインタ
// 比較のみ)。
void KESCMPageMapSweepClosedDocs();

// db の登録(比較相手なしページ)を全部消す。db が nil、または登録が無ければ何もしない。
// Stop(KESCMDoClearMarks)から呼ぶ他、将来のフライアウト「Clear Registered Pages」でも使う想定。
void KESCMPageMapClearAll(IDataBase* db);

// 全文書の登録を丸ごと忘れる。Stop で比較を解除したら Add/Remove の登録も残さないために使う
// (ユーザー指定 2026-07-11)。ポインタは触らず map を空にするだけ。
void KESCMPageMapClearAllDocs();

// pageUID(db内)が「比較相手なし」として登録済みか。db が nil、または該当文書の登録が無ければ kFalse。
bool16 KESCMPageMapIsRegistered(IDataBase* db, UID pageUID);

// db に登録済み(比較相手なし)ページが1つでもあるか(存在チェックのみ)。描画側の早期 return 判定
// (KESCMDrawEventHandler::HandleDrawEvent の wantMarks/wantSrcMarks)に使う。
bool16 KESCMPageMapHasAnyRegistered(IDataBase* db);

// db に登録済み(Added/Removed=緑「/」)のページ UID をすべて out に追加する。Pages パネルサムネイルの
// per-UID Purge 対象に登録ページを含めるために使う(登録ページは sEntries/overflow とは別管理のため、
// これを含めないと緑「/」がサムネイルに即時反映されない)。db が nil か登録が無ければ何もしない。
void KESCMPageMapCollectRegistered(IDataBase* db, std::set<UID>& out);

// db の登録集合を pages で丸ごと置き換える(LOAD 用。「Load Check & Register」から呼ぶ)。sRegistered を
// 書き換えるだけで再比較/サムネイル更新はしない(呼び出し側が両文書を set 後に一度だけ再比較する)。
// pages が空ならその文書の登録を消す。実体は KESCMPageMap.cpp。
void KESCMPageMapReplaceRegistered(IDataBase* db, const std::vector<UID>& pages);

// 除外対応表: targetDB/sourceDB それぞれの平坦ページ列(KESCMCollectPageUIDs)から登録済み
// (比較相手なし)ページを除き、残り同士を順番に対応させる。outTargetPages[i] と outSourcePages[i] が
// 対応ペア(短い方の長さに切り詰め済み=2つの配列は同じ長さになる)。targetDB/sourceDB が nil なら
// 両方とも空にして戻る。
// outOverflowTargetPages/outOverflowSourcePages(任意、nil可)には、登録されていないのに文書間の
// ページ数差で対応表からあふれた側のページを入れる(ページ数が多い方の db にだけ入る。少ない方は空)。
// 実体は KESCMPageMap.cpp。
void KESCMBuildPairing(IDataBase* targetDB, IDataBase* sourceDB,
	std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages,
	std::vector<UID>* outOverflowTargetPages = nil, std::vector<UID>* outOverflowSourcePages = nil);

// マスタースプレッドどうしを**名前で**対応付け、一致したスプレッドのページを順に組む(2026-08-11)。
// outTargetPages / outSourcePages は同じ長さで、i 番目どうしが比較する組(呼び出し時に clear する)。
// ★上の KESCMBuildPairing(通常ページ)とは対応の規則そのものが違うので、別関数にしてある:
//   通常ページは「順番」対応だが、マスターは名前(A-親ページ 等)で対応させる。マスターは追加や
//   並べ替えが起きても名前で一意に指せるのに対し、順番で組むと片方にマスターが1つ増えただけで
//   別のマスターどうしを比べてしまうため。
// ★片方にしか無い名前は組まない(相手なし=比較しない)。ページ数が違う組は短い方に切り詰める。
// 実体は KESCMPageMap.cpp。
void KESCMBuildMasterPairing(IDataBase* targetDB, IDataBase* sourceDB,
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
