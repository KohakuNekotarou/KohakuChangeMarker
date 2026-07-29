//========================================================================================
//
//  KESCMPanelAlpha.h
//
//  パネルのフライアウト「Translucent Panel」トグルの実体。
//
//  ★Windows 専用。Win32 の SetLayeredWindowAttributes でパネルの窓に alpha をかける
//    (Mac では下の3本は残るが KESCMApplyPanelTranslucency が何もしない)。
//  ★効くのは「フローティング中」のときだけ。ドッキング中のパネルはメインフレームの子窓に
//    なるため単独では透かせない(その場合は何もしない=フラグだけ立つ)。
//
//  技術的根拠(実測の全記録) = docs/ai-notes/win32-window-transparency.md
//                             memory/win32-window-alpha-transparency.md
//
//========================================================================================

#ifndef __KESCMPanelAlpha_h__
#define __KESCMPanelAlpha_h__

#include "BaseType.h"

// トグルの現在状態(★既定 OFF)。
bool16	KESCMGetPanelTranslucent();

// トグル状態を設定する。★フラグを更新するだけで窓には触らない
// (起動時の設定復元ではパネルがまだ存在しないため、適用と分離してある)。
void	KESCMSetPanelTranslucent(bool16 on);

// 現在のフラグをパネルの窓へ反映する。
//  - パネルが見つからない / ドッキング中 のときは何もしない(エラーにしない)
//  - 呼ぶ場所: メニュー押下時(KESCMActionComponent.cpp)と パネルの AutoAttach 時・
//    ドッキング切り替え時(KESCMPanelObserver.cpp)
//  - 返り値: 実際に窓へ alpha を設定できたら kTrue。パネルが無い/ドッキング中/Mac なら kFalse
//    (メニュー押下時のステータス文言を「効いた」「ドッキング中なので効かない」で分けるために使う)
bool16	KESCMApplyPanelTranslucency();

// パネルの表示状態変化(開く/閉じる/ドッキング⇄フローティング)の購読を始める。
// ★起動時に1回だけ呼ぶ(KESCMPeekStartup::Startup)。以後、パネルをフローティングに戻したり
//   開き直したりしても、ON なら自動で半透明が貼り直される。
// ★仕組み: kPanelManagerBoss の IID_IPANELMGR subject に飛ぶ kPaletteVisibilityChangedMessage を
//   受ける(2026-07-29 に Debug 版の Spy で実測して特定。ドッキング切り替えのたびに飛ぶ)。
void	KESCMAttachPanelVisibilityObserver();

// カーソルが乗っている扱い(IMouseRollOver の MouseEnter/MouseLeave で上下する内部フラグ)を強制解除する。
// ★MouseLeave は「カーソルを乗せたままパネルを閉じる/ドッキングする/別アプリへ切り替える」経路では
//   飛ばない。取りこぼすと「乗っている」が張り付き、トグルが ON でも一切薄くならなくなる(フックも
//   不透明が正しい状態だと判断するので自己修復しない)。→ パネルの widget が作り直されるタイミング
//   (AutoAttach / 表示状態の変化)から呼ぶこと。実体は KESCMPanelAlpha.cpp。
void	KESCMResetPanelHover();

// 遅延再適用に使う one-shot タイマーの後始末。プラグイン終了時(KESCMPeekStartup::Shutdown)から
// 呼ぶ。★ICallbackTimer のコールバックは参照カウントされない生関数ポインタなので、予約を残した
// まま .pln が降りるとクラッシュする。実体は KESCMPanelAlpha.cpp(Mac では空実装)。
void	KESCMShutdownPanelAlpha();

#endif // __KESCMPanelAlpha_h__
