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

#endif // __KESCMChangeNav_h__
