//========================================================================================
//
//  KESCMChangeNav.h
//
//  「見るべきページ」を順に巡回するナビゲーション(パネルの ◀ Prev / Next ▶ ボタンの実体)。
//  対象は常に Target 文書(sDB)で、次の3種のいずれかのマークが付くページを、文書のページ順で巡る:
//    ① 変更あり(赤/青リング) = sEntries にキーがある
//    ② Added ページ(登録済み・囲み枠) = KESCMPageMapIsRegistered(sDB, uid)
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

#endif // __KESCMChangeNav_h__
