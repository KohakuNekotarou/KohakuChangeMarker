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
//  - 呼ぶ場所: メニュー押下時(KESCMActionComponent.cpp)と パネルの AutoAttach 時(KESCMPanelObserver.cpp)
void	KESCMApplyPanelTranslucency();

#endif // __KESCMPanelAlpha_h__
