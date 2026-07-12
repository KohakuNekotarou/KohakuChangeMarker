//========================================================================================
//
//  KESCMPeek.h
//
//  ミドルボタンの「peek(覗き)」。修飾キー＋ミドルを押している間だけ、カーソル下スプレッドの旧版を
//  表示する(または再比較する)。離すと元に戻す。peek 状態(arm 済みの target/source DB、押下中フラグ)と
//  イベントウォッチャ／起動サービスを所有する。ここで公開するのは KESCMBaseScreenOpacity だけで、
//  arm/disarm/状態アクセサは KESCMCore.h にある。
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "PMReal.h"

// 常時表示マークの画面上の「基準」不透明度。印刷設定から決まる(印刷ON => 選択不透明度25%/75%、印刷OFF => 1.0)。
// peek を離したときの経路と KESCMDoSetPrintMarks が使う。実体は KESCMPeek.cpp。
PMReal KESCMBaseScreenOpacity();

// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中に左ボタンを押している間だけ、中ボタンの
// 「修飾なし押下」と同じマーク一時表示(reveal)を行う。Begin=押下、End=解放。実体は KESCMPeek.cpp
// (peek の file-local 状態と描画状態にアクセスできる唯一の場所)。KESCMTracker.cpp から呼ぶ。
// ★Step 1: 修飾なしの reveal のみ。Shift/Ctrl/Alt 別ジェスチャや Hold to Hide 反転は Step 2 以降。
void KESCMTrackerRevealBegin();
void KESCMTrackerRevealEnd();

#endif // __KESCMPeek_h__
