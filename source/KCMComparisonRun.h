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

class IDataBase;
class IDocument;
class IDocumentList;
class PMString;

// The Start/Stop toggle. When armed, clears (marks removed, peek disarmed); when not armed,
// starts (the chosen Target and Source, and for whichever of the two has not been chosen the
// old automatic rule: active document = Target, another open document = Source).
void	KCMToggleStartStop();

// The two halves of the toggle, so callers that already know which documents they want do
// not have to repeat the procedure. KCMStartComparisonFor contains no resolution logic at
// all -- the caller decides which document is the Target. nil does nothing.
// WARNING: it overwrites an existing armed state without asking, so a caller that wants
// "stop, then start" must call KCMStopComparison first.
void	KCMStopComparison();
void	KCMStartComparisonFor(IDocument* target, IDocument* source);

// The procedure itself, on two DATABASES. ⛔It was made public on 2026-09-12 for ONE more caller -
// KCMOriginCompare (Task Start), which handed it a rehydrated copy as the Source - and that caller
// went on 2026-09-21 with the origin. It resolves nothing and arms on success; see the body for
// the three decisions it holds.
// @return kTrue when the comparison ran and the pair is armed; kFalse on a cancel or a failure.
bool16	KCMStartComparisonOn(IDataBase* targetDB, IDataBase* sourceDB);

// Compare THE SAME TWO DOCUMENTS AGAIN - the flyout's "Refresh Comparison", directly under Start.
//
// ★WHAT IT IS FOR: the reader edits one of the two documents and wants the marks to say what is
// true NOW. Until this existed the only way was Stop then Start, which is two presses and takes
// the marks off the screen in between.
//
// ★**IT IS THE START PROCEDURE, NOT A SECOND ONE.** It runs KCMStartComparisonOn with the ARMED
// pair, so a refresh and a start cannot come to disagree about what starting means
// ([[one-question-one-place]]). Nothing here has to stop first: arming is idempotent
// (KCMDoArmMousePeek only assigns, and skips its cache drop when the pair is unchanged), and
// re-comparing an armed pair is what the register toggle already does (KCMPageMap.cpp).
//
// ⚠**A FULL COMPARISON, NEVER THE INCREMENTAL ONE.** KCMDoMarkChangesDoc's allowIncremental reuses
// the previous pairing and is documented as a speed-up for the register toggle ALONE, "where the
// document's content does not change and only the pairing moves". This exists precisely because
// the content may have changed, so it takes the default.
//
// ⚠★★★**A CANCELLED REFRESH STOPS THE COMPARISON.** The progress bar carries a Cancel on longer
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

// Start with the front document (or whichever the caller names) as the Target and a database
// that is NOT an open document as the Source -- a task-start copy Kohaku InDesign MCP lends
// (KCMExternalSource.h). `sourceLabel` is what the panel's Source: line shows for it.
// **Stops first** if a comparison is armed, unlike KCMStartComparisonFor, and **chooses both**
// (Set as Target + Set as Source + Start in one): a Stop keeps the pair on the panel, and the
// flyout's own Start compares against the same copy again until the lender releases it. nil
// does nothing; a Source that IS the Target's own database is refused with a word on the status
// line.
void	KCMStartComparisonWithSourceDB(IDocument* target, IDataBase* sourceDB, const PMString& sourceLabel);

// The lender is about to delete its database: stop the comparison if that database is the armed
// Source, and say why on the status line. A database that is not the registered lent Source is
// ignored, so the lender may call this for every database it frees.
void	KCMReleaseExternalSource(IDataBase* sourceDB);

// Whether a comparison can be started at all: there is an active (front) document and at
// least one other open document. Used to decide whether the flyout's Start may be enabled.
// Goes through the same resolver as the start branch of KCMToggleStartStop, so what the
// menu shows and what pressing it does cannot disagree.
bool16	KCMCanStartComparison();

//----------------------------------------------------------------------------------------
// ★**THE CHOSEN PAIR MOVED TO KCMPairChoice.h** (2026-09-21), and the resolver that reads it
// went with it. A chosen end may now be a FILE as well as a document - a Task Start saves a copy
// and names it - so "which two has the reader chosen" grew a second kind of answer. It is still
// ONE question, so it lives in one file ([[one-question-one-place]]).
//   Moved: KCMSetChosenTargetToActive / KCMSetChosenSourceToActive / KCMChosenTargetDB /
//   KCMChosenSourceDB / KCMForgetChosenDocsThatClosed / KCMClearChosenDocs, and the origin's
//   KCMChosenSourceIsOrigin / KCMChooseOriginPair.
//----------------------------------------------------------------------------------------

// The print-marks toggle: flips the current print flag and keeps the current opacity choice.
void	KCMTogglePrintMarks();

// Set the mark frame opacity to 25% (op25=kTrue) or 75% (kFalse), keeping the print flag.
void	KCMSetMarkOpacity25(bool16 op25);
// Set the mark colour to red (kFalse) or cyan (kTrue), from the flyout's two items.
void	KCMSetMarkColor(bool16 cyan);

#endif // __KCMComparisonRun_h__
