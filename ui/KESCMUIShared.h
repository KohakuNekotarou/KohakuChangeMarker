//========================================================================================
//
//  KESCMUIShared.h
//
//  Shared UI-side API: the panel, its status line, its navigation readout, and the tool
//  button. Everything here touches widgets.
//
//  Created 2026-08-13 by splitting KESCMCore.h. Behaviour unchanged.
//
//  ★INVARIANT: no model-side file may include this header. When a model-side file needs to
//  tell the UI something, it emits a notification instead (KESCMModelNotify.h). A model
//  plug-in cannot instantiate UI bosses -- the query returns nil with no error and no
//  warning -- so this rule is what keeps the split working after Stage 2.
//
//  ⚠ The invariant does not hold yet. Task 5 created this header precisely so that the
//  violations become countable: every model-side file that includes it is one item of
//  reverse flow. The full list is docs/ai-notes/kescm-reverse-flow-ledger-2026-08-13.md,
//  and Tasks 6-10 empty it.
//
//========================================================================================

#ifndef __KESCMUIShared_h__
#define __KESCMUIShared_h__

#include "BaseType.h"
#include "PMString.h"

class IControlView;

// The plug-in's own panel if it is showing, nil if hidden. The
// "session -> app -> panelMgr -> GetVisiblePanel" idiom lives only here. Absorbs the
// shutdown path where session goes nil.
IControlView*	KESCMGetVisibleOwnPanel();

// Bring the showing panel's ON/OFF display (Target/Source names, icon, toggle label) in line
// with the current armed state. Does nothing when the panel is hidden -- AutoAttach reflects
// the real state on re-show.
void			KESCMRefreshPanel();

// Write the panel's status line. forceRedrawNow=kTrue paints immediately instead of waiting
// for the next event loop (used before a blocking comparison loop).
//
// ★UI 内部専用。model 側からは呼ばない -- model は KESCMNotifyStatus() を使う
//   (KESCMModelNotify.h)。この関数の呼び手は、通知を受けた KESCMModelChangeObserver と、
//   UI 側の直接の操作(メニュー・ボタン・行クリック)だけ。
// ★どちらの経路でも文字列は model 側に覚えられる(この関数が KESCMStoreSessionStatus を呼ぶ)。
//   app.kcmStatus はそこから答えるので、**パネルを閉じていても正しい値が返る**。
void			KESCMSetStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// 同じメッセージ欄に、**色の変わり目つき**で出す(2026-08-20)。
// ★呼び手は変更行のジャンプ(KESCMStoryJump.cpp)ただ1つ＝「その編集のもう一方の側」を出す経路。
//   label は見出し(必ず1行を占め、溢れても削られない)、mid が**変更された文字**＝テーマの文字色、
//   pre/post はその前後の文脈＝背景へ寄せた薄い色。
// ★★上の KESCMSetStatus はこれの特別な場合ではなく**同じ器の別の詰め方**＝(空, 空, s, 空)。
//   ⇒ 普通のメッセージは1片＝1色で、stock の静的テキストと同じ絵になる。
// ⚠溢れたら**文脈が外側から削られる**(変更された文字は必ず残る)。規則は変更行のセルと同じ。
void			KESCMSetStatusSegments(const PMString& label, const PMString& pre,
									   const PMString& mid, const PMString& post);

// model からの通知を受ける UI 側 Observer の購読を開始/停止する(KESCMModelChangeObserver.cpp)。
// Startup で attach し、Shutdown では **パネル周りを畳むより前**に detach する。
void			KESCMAttachModelChangeObserver();
void			KESCMDetachModelChangeObserver();

// The Prev/Next position readout ("3/12") and the enabled state of both buttons. Empty
// posText clears. Does nothing when the panel is hidden.
void			KESCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled);

// Show the tool switch button as pressed / not pressed. Called only from KESCMTool::Select
// and ::Deselect, so the toolbox, the panel button and the shortcut all pass through one
// place. Does nothing when the panel is hidden.
void			KESCMSetToolButtonSelected(bool16 selected);

// Make this plug-in's tool the active tool. Returns kTrue when it actually became active.
// Does nothing in a run configuration without a toolbox.
bool16			KESCMActivateOwnTool();

// Is this plug-in's tool the active tool right now? Used to restore the button's pressed
// look when the panel is rebuilt, instead of writing a fixed default.
bool16			KESCMIsOwnToolActive();

// Open the distribution URL from "About this plug-in" in the default browser. Called when
// the panel illustration is clicked.
void			KESCMOpenAboutURL();

#endif // __KESCMUIShared_h__
