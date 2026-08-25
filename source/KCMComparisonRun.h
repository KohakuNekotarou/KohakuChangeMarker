//========================================================================================
//
//  KCMComparisonRun.h
//
//  Starting and stopping a comparison, and the two mark-display settings that go with it.
//
//  MODEL side: these run the comparison itself. The flyout items that call them stay on the
//  UI side, and what these do to the screen is emit a notification -- they never reach into
//  the UI.
//
//========================================================================================

#ifndef __KCMComparisonRun_h__
#define __KCMComparisonRun_h__

#include "BaseType.h"

class IDocument;

// The Start/Stop toggle. When armed, clears (marks removed, peek disarmed); when not armed,
// starts (active document = Target, another open document = Source).
void	KCMToggleStartStop();

// The two halves of the toggle, so callers that already know which documents they want do
// not have to repeat the procedure. KCMStartComparisonFor contains no resolution logic at
// all -- the caller decides which document is the Target. nil does nothing.
// WARNING: it overwrites an existing armed state without asking, so a caller that wants
// "stop, then start" must call KCMStopComparison first.
void	KCMStopComparison();
void	KCMStartComparisonFor(IDocument* target, IDocument* source);

// Whether a comparison can be started at all: there is an active (front) document and at
// least one other open document. Used to decide whether the flyout's Start may be enabled.
// Goes through the same resolver as the start branch of KCMToggleStartStop, so what the
// menu shows and what pressing it does cannot disagree.
bool16	KCMCanStartComparison();

// The print-marks toggle: flips the current print flag and keeps the current opacity choice.
void	KCMTogglePrintMarks();

// Set the mark frame opacity to 25% (op25=kTrue) or 75% (kFalse), keeping the print flag.
void	KCMSetMarkOpacity25(bool16 op25);
// Set the mark colour to red (kFalse) or cyan (kTrue), from the flyout's two items.
void	KCMSetMarkColor(bool16 cyan);

#endif // __KCMComparisonRun_h__
