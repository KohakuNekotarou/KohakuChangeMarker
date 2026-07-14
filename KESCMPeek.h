//========================================================================================
//
//  KESCMPeek.h
//
//  ツール(左ボタン)の「peek(覗き)」。修飾キー＋ツール左ボタンを押している間だけ、カーソル下スプレッドの旧版を
//  表示する(または再比較する)。離すと元に戻す。peek 状態(arm 済みの target/source DB、押下中フラグ)と
//  イベントウォッチャ／起動サービスを所有する。ここで公開するのは KESCMBaseScreenOpacity だけで、
//  arm/disarm/状態アクセサは KESCMCore.h にある。
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "PMReal.h"
#include "CursorSpec.h"		// CreateCursorBitmapProc(Alt+左 CMYK のカスタムカーソル)

// 常時表示マークの画面上の「基準」不透明度。印刷設定から決まる(印刷ON => 選択不透明度25%/75%、印刷OFF => 1.0)。
// peek を離したときの経路と KESCMDoSetPrintMarks が使う。実体は KESCMPeek.cpp。
PMReal KESCMBaseScreenOpacity();

// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中に左ボタンを押している間だけ、押下時の修飾キーで
// 選んだ動作を行う。Begin=押下(押下時の修飾キー状態を渡す)、End=解放。実体は
// KESCMPeek.cpp(peek の file-local 状態と描画状態にアクセスできる唯一の場所)。KESCMTracker.cpp から呼ぶ。
// (由来: いずれも旧・中ボタン＋修飾キーのジェスチャをツールの左ボタンへ移植したもの。)
//   ・修飾なし        = マーク一時表示(reveal) / Hold to Hide 反転(常時表示の枠を押下中だけ隠す)
//   ・Shift           = 旧版べた載せ peek 100%(旧・中ボタン Shift+ミドル)
//   ・Shift+Alt       = 旧版べた載せ peek 50%(旧・中ボタン Shift+Alt+ミドル)
//   ・Alt(単独)       = クリック点の CMYK 生値を新/旧サンプリングしステータス行へ(旧・中ボタン Shift+Ctrl+Alt+ミドル)
//   ・Ctrl(cmd)含む  = 未対応。何もしない(再比較はページ右クリックメニュー/パネル操作はフライアウトへ移行済み)。
void KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown);
void KESCMTrackerRevealEnd();

// Alt+左「色比較」のカスタムカーソル(CMYK をカーソル自身に描く)。KESCMTracker.cpp が使う:
// BeginTracking で KESCMTrackerRevealBegin 後、Pending が立っていれば
// ChangeModalCursor(CursorSpec(KESCMTrackerCmykCursorProc(), ...)) を呼ぶ。実体は KESCMPeek.cpp。
bool16 KESCMTrackerHasPendingCmykCursor();
CreateCursorBitmapProc KESCMTrackerCmykCursorProc();

// ドラッグ中の CMYK ライブ更新。トラッカーの ContinueTracking(マウス移動)から呼ぶ。現在のマウス位置で
// CMYK を再サンプルし(スロットル付き=連続ラスタ化で重くならないように)、値が変わったら
// sCmykCursorText を更新して kTrue を返す。呼び出し側はそのとき ChangeModalCursor でカーソルを描き直す。
// Alt+左 CMYK モード中(Pending)でなければ、またはスロットル中/ページ外/値不変なら kFalse。実体は KESCMPeek.cpp。
bool16 KESCMTrackerUpdateCmykDrag();

// ページパネルのページ右クリック「KESCM: Refresh Page Comparison」の実体。選択ページの比較を再検出して
// 枠/サムネイルを更新する(旧 Ctrl+ミドルのスプレッド再比較を移設。2026-07-13)。arm 済み(Start 後)かつ
// 前面文書が Target/Source のときだけ動く。outPages=処理ページ数 / outChanged=うち変化ページ数(いずれも
// nil 可)。戻り=1ページ以上処理したか。実体は KESCMPeek.cpp。KESCMActionComponent.cpp から呼ぶ。
bool16 KESCMRefreshComparisonForSelectedPages(int32* outPages, int32* outChanged);

// 上記メニューの有効/無効判定(KESCMActionComponent.cpp の UpdateActionStates 用)。arm 済みかつ前面文書が
// Target/Source なら kTrue。実体は KESCMPeek.cpp。
bool16 KESCMRefreshComparisonAvailable();

#endif // __KESCMPeek_h__
