//========================================================================================
//
//  KESCMComparisonRun.h
//
//  Starting and stopping a comparison, and the two mark-display settings that go with it.
//
//  Split out of KESCMPanelObserver.cpp on 2026-08-13. Behaviour unchanged.
//
//  MODEL side: these run the comparison itself. The flyout items that call them stay on the
//  UI side. The panel refresh each of them currently performs is reverse flow (model
//  reaching into UI) and is inverted into a notification in Task 9.
//
//========================================================================================

#ifndef __KESCMComparisonRun_h__
#define __KESCMComparisonRun_h__

#include "BaseType.h"

class IDocument;

// The Start/Stop toggle. When armed, clears (marks removed, peek disarmed); when not armed,
// starts (active document = Target, another open document = Source).
void	KESCMToggleStartStop();

// The two halves of the toggle, so callers that already know which documents they want do
// not have to repeat the procedure. KESCMStartComparisonFor contains no resolution logic at
// all -- the caller decides which document is the Target. nil does nothing.
// WARNING: it overwrites an existing armed state without asking, so a caller that wants
// "stop, then start" must call KESCMStopComparison first.
void	KESCMStopComparison();
void	KESCMStartComparisonFor(IDocument* target, IDocument* source);

// Whether a comparison can be started at all: there is an active (front) document and at
// least one other open document. Used to decide whether the flyout's Start may be enabled.
// Goes through the same resolver as the start branch of KESCMToggleStartStop, so what the
// menu shows and what pressing it does cannot disagree.
bool16	KESCMCanStartComparison();

// The print-marks toggle: flips the current print flag and keeps the current opacity choice.
void	KESCMTogglePrintMarks();

// Set the mark frame opacity to 25% (op25=kTrue) or 75% (kFalse), keeping the print flag.
void	KESCMSetMarkOpacity25(bool16 op25);
// マークの色を 赤/シアン に設定(フライアウトの2項目から。★2026-08-24 に背景適応を置き換えたもの)。
void	KESCMSetMarkColor(bool16 cyan);

#endif // __KESCMComparisonRun_h__
