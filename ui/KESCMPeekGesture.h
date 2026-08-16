//========================================================================================
//
//  KESCMPeekGesture.h
//
//  What the mouse gesture means and what it starts: classifying the modifier combination
//  under the tool, beginning and ending the reveal, and the deferred UI cleanup that runs
//  once after a batch document close.
//
//  Split out of KESCMPeek.cpp on 2026-08-13. Behaviour unchanged. UI side: it reads the
//  pressed state of the tool, which lives in the UI and is invisible to the model.
//
//  NOTE: the close handling here is the UI half only. Its model twin is
//  KESCMHandleDocsClosed() in KESCMPeek.cpp, which drops tracking state for documents that
//  are gone. Both listen to the same close notification for different purposes -- do not
//  merge them.
//
//========================================================================================

#ifndef __KESCMPeekGesture_h__
#define __KESCMPeekGesture_h__

#include "BaseType.h"

// 修飾キー→ジェスチャの分類の結果。
// ★2026-08-14(第1段 Task 16)に KESCMPeek.h(model 側)から移した。定義はあちらに在ったが、
//   **参照していたのはこのファイルと KESCMPeekGesture.cpp と KESCMTracker.cpp の3つだけ**で、
//   KESCMPeek.cpp は一度も使っていなかった＝置き場所が model 側である理由が無かった。
//   これで UI 側から KESCMPeek.h を include する必要が消える(第1段の完了条件1)。
enum KESCMGesture
{
	kKESCMGestureNone = 0,	// Ctrl(cmd)または Mac の Control を含む=未割当(何もしない)
	kKESCMGestureReveal,	// 修飾なし: マーク一時表示(reveal) / Hold to Hide の temp-hide
	kKESCMGesturePeek100,	// Shift: 旧版べた載せ peek 100%
	kKESCMGesturePeek50,	// Shift+Alt(Mac: Shift+Option): 旧版べた載せ peek 50%
	kKESCMGestureCmyk		// Alt 単独(Mac: Option 単独): CMYK 色サンプリング(カーソル表示)
};

// 修飾キー→ジェスチャの分類。★割当の定義はこの1本だけ(2026-07-15 に3箇所の独立判定を統合):
// KESCMTracker.cpp の BeginTracking(CMYK を先に発動させるかの判定)・RevealBegin の分岐・
// Hold to Hide の temp-hide 判定がすべてこれを使う。ジェスチャ割当を変えるときは
// KESCMClassifyGesture(KESCMPeekGesture.cpp)だけを直す。
// ★macCtrlDown(= IEvent::MacCtrlDown。Windows では常に kFalse)は「未割当」に倒す(2026-07-25 追補 Mac 対応):
//   macOS の Control+クリックは OS/アプリが副ボタン(コンテキストメニュー)として扱う標準ジェスチャなので、
//   もし左ボタン押下として届いても KESCM が reveal を横取りしないようにする。cmdDown(Mac の Command)を
//   未割当にしているのと同じ趣旨。既定引数 kFalse なので Windows 側の呼び出しは影響を受けない。
KESCMGesture	KESCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);

// トラッカー(左ボタン)用の共有入口。KESCM ツール選択中に左ボタンを押している間だけ、押下時の修飾キーで
// 選んだ動作を行う。Begin=押下(押下時の修飾キー状態を渡す)、End=解放。KESCMTracker.cpp から呼ぶ。
// (由来: いずれも旧・中ボタン＋修飾キーのジェスチャをツールの左ボタンへ移植したもの。)
//   ・修飾なし        = マーク一時表示(reveal) / Hold to Hide 反転(常時表示の枠を押下中だけ隠す)
//   ・Shift           = 旧版べた載せ peek 100%(旧・中ボタン Shift+ミドル)
//   ・Shift+Alt       = 旧版べた載せ peek 50%(旧・中ボタン Shift+Alt+ミドル)
//   ・Alt(単独)       = クリック点の CMYK 生値を新/旧サンプリングしステータス行へ(旧・中ボタン Shift+Ctrl+Alt+ミドル)
//   ・Ctrl(cmd)含む  = 未対応。何もしない(再比較はページ右クリックメニュー/パネル操作はフライアウトへ移行済み)。
//   ・Mac の Control  = 未対応(上の macCtrlDown 参照)。
// ★キー名の対応(SDK の IEvent が吸収する): OptionAltKeyDown = Win の Alt / Mac の Option、
//   CmdKeyDown = Win の Ctrl / Mac の Command。よって上表の "Alt" は Mac では Option になる。
void			KESCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown = kFalse);
void			KESCMTrackerRevealEnd();

// Subscribe to the batch-close completion notification so the deferred UI cleanup (strip
// removal, thumbnail regeneration, panel refresh) runs once instead of once per document.
// Called at startup.
void			KESCMAttachDocsClosedObserver();

// Forget that a press is showing anything. Called from arm, disarm and the close sweep, which
// all reset the same two flags. (Split out on 2026-08-13: those three callers live on the
// model side and cannot see this file's statics.)
void			KESCMResetPeekGestureState();

// Is a batch close running right now? Reads the session flag the application's Links UI keeps
// (IID_IKFILESCLOSING); kFalse when that flag cannot be read, which restores the pre-2026-07-27
// behaviour of cleaning up once per document.
//
// The close sweep asks this to decide whether to clean the UI now or hand it to
// KESCMDeferCloseUi below.
bool16			KESCMBatchCloseInProgress();

// Hold the UI half of the close cleanup until the batch close finishes. The close sweep calls
// this when it has dropped its state but must not touch widgets yet.
void			KESCMDeferCloseUi();

// Shutdown: drop the pending flag so nothing is left booked. Called from
// KESCMUIStartup::Shutdown.
void			KESCMPeekGestureShutdown();

#endif // __KESCMPeekGesture_h__
