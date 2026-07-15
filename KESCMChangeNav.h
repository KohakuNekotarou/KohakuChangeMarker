//========================================================================================
//
//  KESCMChangeNav.h
//
//  「見るべきページ」を順に巡回するナビゲーション(パネルの ◀ Prev / Next ▶ ボタンの実体)。
//  対象は常に Target 文書(sDB)で、次の3種のいずれかのマークが付くページを、文書のページ順で巡る:
//    ① 変更あり(赤/青リング) = sEntries にキーがある
//    ② Added ページ(登録済み・緑「/」斜線) = KESCMPageMapIsRegistered(sDB, uid)
//    ③ Overflow(未比較・"/"斜線) = KESCMBuildPairing の tOverflow
//  現在のズームは変えず、対象ページの中心をレイアウトビューの中央へスクロールするだけ(選択はしない)。
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
