//========================================================================================
//
//  KCMComparisonRun.h
//
//  Starting and stopping a comparison, and the mark-display settings that go with it.
//
//  MODEL side: these run the comparison itself. The flyout items that call them stay on the
//  UI side, and what these do to the screen is emit a notification -- they never reach into
//  the UI.
//
//========================================================================================

#ifndef __KCMComparisonRun_h__
#define __KCMComparisonRun_h__

#include "BaseType.h"

class IDataBase;
class IDocument;
class IDocumentList;
class PMString;

// The Start/Stop toggle. When armed, clears (marks removed, peek disarmed); when not armed,
// starts (the chosen Target and Source, and for whichever of the two has not been chosen the
// automatic rule: active document = Target, another open document = Source).
void	KCMToggleStartStop();

// The two halves of the toggle, so callers that already know which documents they want do
// not have to repeat the procedure. KCMStartComparisonFor contains no resolution logic at
// all -- the caller decides which document is the Target. nil does nothing.
// WARNING: it overwrites an existing armed state without asking, so a caller that wants
// "stop, then start" must call KCMStopComparison first.
void	KCMStopComparison();
void	KCMStartComparisonFor(IDocument* target, IDocument* source);

// The procedure itself, on two DATABASES - public for the Track listing's start (KCMTrackStart.cpp,
// which hands it no Source). It resolves nothing and arms on success; see the body for its callers
// and the decisions it holds.
// @return kTrue when the comparison ran and the pair is armed; kFalse on a cancel or a failure.
bool16	KCMStartComparisonOn(IDataBase* targetDB, IDataBase* sourceDB);

// WHICH TWO, OPENED - the steps Start takes before it compares: the resolver (the chosen pair, the automatic rule for
// an end nobody chose), a FILE end opened (a Task Start copy, in a window, as Start always has), and the refusal of
// one document compared with itself. Two callers, so the two cannot drift ([[one-question-one-place]]): the start
// branch of KCMToggleStartStop, and the Before/After report, which compares for itself.
// @param outWhy the status line's words when there is no pair - which end is missing, why a file would not open, or
//   that the two are one document.
// @return kTrue with both databases set; kFalse with both nil.
bool16	KCMOpenComparisonPair(IDataBase*& outTarget, IDataBase*& outSource, PMString& outWhy);

// THE BEFORE/AFTER REPORT'S OWN COMPARISON (the author's decision: the report is made while nothing is
// started, and it holds every page's pictures whatever the compare mode). The pixels of every page go into the mark
// state a Start fills - nothing is armed, no story list or Resources result is built (the report builds its two
// tables itself), and the screen is neither redrawn nor told (KCMDoMarkChangesDoc's pixelsOnly).
// @return kFalse when it was cancelled at its progress bar (the marks are discarded then, as for a cancelled Start).
bool16	KCMRunReportComparison(IDataBase* targetDB, IDataBase* sourceDB);

// ...and taken down again once the report is written - Stop's own clearing (KCMDoClearMarks), with nothing to
// disarm - so the reader is left with nothing started and no mark, where they pressed the item. Either database may
// have closed meanwhile; the transparency-list insurance asks first.
void	KCMClearReportComparison(IDataBase* targetDB, IDataBase* sourceDB);

// Compare THE SAME TWO DOCUMENTS AGAIN - the flyout's "Refresh Comparison", directly under Start.
//
// WHAT IT IS FOR: the reader edits one of the two documents and wants the marks to say what is
// true NOW - without Stop then Start, which is two presses and takes the marks off the screen in
// between.
//
// **IT IS THE START PROCEDURE, NOT A SECOND ONE.** It runs KCMStartComparisonOn with the ARMED
// pair, so a refresh and a start cannot come to disagree about what starting means
// ([[one-question-one-place]]). Nothing here has to stop first: arming is idempotent
// (KCMDoArmMousePeek only assigns), the comparison itself drops the peek's pictures, and
// re-comparing an armed pair is what the register toggle already does (KCMPageMap.cpp).
//
// ⚠**A FULL COMPARISON, NEVER THE INCREMENTAL ONE.** KCMDoMarkChangesDoc's allowIncremental reuses
// the previous pairing and is documented as a speed-up for the register toggle ALONE, "where the
// document's content does not change and only the pairing moves". This exists precisely because
// the content may have changed, so it takes the default.
//
// ⚠**A CANCELLED REFRESH STOPS THE COMPARISON.** The progress bar carries a Cancel on longer
// runs, and by the time it is pressed the marks are already gone (KCMDoMarkChangesDoc discards them
// before it starts). A Start that is cancelled simply does not arm, which puts the reader back where
// they were; a REFRESH was already armed, so leaving it alone would produce the one state that
// return value exists to prevent - "armed after a cancel, with the menu stuck on Stop" and no marks
// anywhere. So it stops, and says so on the status line.
//
// ⚠Does nothing when no comparison is armed, or when either document has since gone. The menu item
// is greyed in exactly those cases (KCMActionComponent's UpdateActionStates), so this is the second
// line of defence rather than the first.
void	KCMRefreshComparison();

// A TRACK LISTING'S RE-READ CANCELLED AT ITS BAR STOPS IT (the author's answer: "Cancel -> Stop"). The list is
// read again after a row's Refresh, an accept or a reject, and an Undo (KCMFacades.cpp, KCMStoryFollowObserver.cpp); a
// cancelled read leaves no list it could trust (KCMTrackRead::Build sets the list only when it finishes, so the old one
// would stand and be clicked into), so the listing ends the way a cancelled Refresh Comparison ends it, in words of its
// own on the status line. One place for its callers ([[one-question-one-place]]).
void	KCMStopForCancelledReread();

// Whether a comparison can be started at all: whether the resolver finds a pair (the chosen ends,
// and the automatic rule for an end nobody chose). Used to decide whether the flyout's Start may be
// enabled. Goes through the same resolver as the start branch of KCMToggleStartStop, so what the
// menu shows and what pressing it does cannot disagree.
bool16	KCMCanStartComparison();

// (The chosen pair and the resolver that reads it are KCMPairChoice.h's: a chosen end may be a
//  FILE as well as a document - a Task Start saves a copy and names it.)

// The print-marks toggle: flips the current print flag and keeps the current opacity choice.
void	KCMTogglePrintMarks();

// Set the mark frame opacity to 25% (op25=kTrue) or 75% (kFalse), keeping the print flag.
void	KCMSetMarkOpacity25(bool16 op25);
// Set the mark colour to red (kFalse) or cyan (kTrue), from the flyout's two items.
void	KCMSetMarkColor(bool16 cyan);

#endif // __KCMComparisonRun_h__
