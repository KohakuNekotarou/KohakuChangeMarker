//========================================================================================
//
//  KESCMChangeNav.h
//
//  「見るべき箇所」を順に巡回するナビゲーション(パネルの ◀ Prev / Next ▶ ボタンの実体)。
//  巡回対象(2026-07-25 コメント現行化。実装は KESCMChangeNav.cpp の KESCMBuildStops が正):
//    ① 変更あり(赤/青リング)のページ = sEntries にキーがある(比較 Start 中)
//    ② overset「+」箇所 = Find Overset の sOversetLocs(未 Start でも単独巡回可。1箇所=1ストップ)
//  ※Added/Removed(登録・緑「/」)と Overflow(赤「/」)は巡回対象に含めない(2026-07-10 ユーザー指定)。
//  変更ページはズームを変えずページ中心へ、overset は「+」点へスクロールするだけ(選択はしない)。
//
//========================================================================================

#ifndef __KESCMChangeNav_h__
#define __KESCMChangeNav_h__

#include "BaseType.h"	// bool16
#include "OMTypes.h"	// UID

class IDataBase;
class IControlView;

// view が spreadUID を映していなければ、公式コマンド kSetSpreadCmdBoss で切り替える(2026-08-11 に
// 1ビュー単位で括り出して公開)。既に映していれば何もしないので、何度呼んでも安い。
// ★スクロールだけでは別スプレッド(とくにマスタースプレッド)へは届かない＝空のペーストボードに
//   着地する。「違うスプレッドなら切り替える」は公式の作法(手本 SnapTracker.cpp:224 に特例なし)。
// ★Prev/Next(この .cpp 内)と、レイアウトビュー同期(KESCMPeek.cpp)の両方が呼ぶ＝同じ判断を
//   2か所に書かないため([[one-question-one-place]])。
// 戻り値: 実際に切り替えたら kTrue(既に映していた・失敗した場合は kFalse)。
bool16 KESCMEnsureViewShowsSpread(IControlView* view, IDataBase* db, UID spreadUID);

// 次/前の「見るべきページ」へレイアウトビューをスクロールする。未 Start(sDB==nil)や対象0件のときは
// スクロールせず、パネルのステータス行にその旨を出すだけ(安全に何度でも呼べる)。
void KESCMGotoNextChange();
void KESCMGotoPrevChange();

// 巡回の基準点(直近ページ)を忘れる。比較の Start(全再比較=対象文書入れ替え)と Stop で呼ぶ。
// ★UID はデータベース単位なので、別文書で再 Start したときに旧文書のページ UID が偶然一致して
// 誤った位置から巡回が始まるのを防ぐ(セッションを跨いだ基準点の持ち越しを断つ)。
void KESCMResetNav();

// Prev/Next の間の現在位置表示を「今の変更ページ集合＋巡回基準点」から作り直してパネルへ送る。
// KESCL の UpdateNavWidgets と同じ発想で、Next/Prev を押さなくても状態変化に追従させるために、
// 変更ページ集合が変わり得るすべての契機から呼ぶ(Start/差分再比較/登録/Check=KESCMDoMarkChangesDoc、
// スプレッド再比較=KESCMRefreshComparisonForSelectedPages、Stop=KESCMDoClearMarks、パネル更新=
// KESCMApplyPanelInfo)。表示規則:
//   ・未 Start(比較なし)          → 空
//   ・Start 済み・変更ページ 0 件  → "/"
//   ・Start 済み・N 件(未巡回)     → "1/N"(Start 直後に即表示)
//   ・k 番目を巡回中               → "k/N"
void KESCMRefreshNavPosition();

// Story Edits の行から呼ぶジャンプ: そのストーリーの先頭フレームを画面中央に出す。
//   ・frameUID のスプレッドを先に出すので、別スプレッドでもマスターでもペーストボードでも届く
//   ・★★Source 窓も連れて行くが、合わせるのは**ページではなく同じストーリー**(storyUID)＝2つの版で
//     そのストーリーが違う場所にあっても、両方の窓が同じストーリーを映す。Prev/Next が対応表で
//     ページを引くのとは意図して違う(2026-08-10 ユーザー指摘)。Source に無いストーリー(Added)なら
//     Target だけが動く。⚠「Sync Layout Views」が ON のときは Source を手動で動かさない
//     (Sync が Target のスクロールを運ぶので二重になる)
//   ・Pages パネルは両側とも追随する(pageUID がその解決に要る。kInvalidUID＝ページに載っていない
//     フレームなら Pages パネルは動かない)
//   ・★Prev/Next の巡回位置「k/N」には影響しない(別の動線なので基準点を動かさない)
// 戻り値: 1つでもビューをスクロールできたら kTrue。実体は KESCMChangeNav.cpp。
bool16 KESCMGotoStoryFrame(IDataBase* db, UID frameUID, UID pageUID, UID storyUID);

#endif // __KESCMChangeNav_h__
