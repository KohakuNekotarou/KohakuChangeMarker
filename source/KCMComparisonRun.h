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
// The chosen Target and Source ("Set as Target" / "Set as Source" on the flyout)
//
// A choice the reader makes BEFORE starting, so that which two documents are compared is
// stated rather than inferred. Whichever of the two has not been chosen still falls to the
// automatic rule inside the resolver, so a reader who chooses nothing keeps the behaviour
// this plug-in has always had.
//
// **A Stop does not clear them.** Stopping ends the comparison, not the choice: the usual
// shape of the work is start, stop, edit, start again on the same pair.
// What does clear one is that document closing -- KCMForgetChosenDocsThatClosed below.
//----------------------------------------------------------------------------------------

// Make the active (front) document the Target / the Source. kFalse, and nothing set, when there
// is no active document -- the flyout greys both items in that case, so this is the guard for a
// document closing while the menu stands open, and it is what lets the caller not say "Target
// set." when nothing was.
// **They only set.** Refreshing the panel and putting a word on the status line are the caller's,
// the same division as SetCompareMode ([[one-question-one-place]]: the UI decides what the UI
// shows).
bool16	KCMSetChosenTargetToActive();
bool16	KCMSetChosenSourceToActive();

// The chosen documents, for the panel's Target:/Source: labels and for the resolver.
// **nil unless the document is still open**: a closed IDataBase* is only ever compared
// against IDocumentList, never dereferenced, and never handed to a caller
// ([[uidref-reuse-after-close]]).
IDataBase*	KCMChosenTargetDB();
IDataBase*	KCMChosenSourceDB();

// Drop whichever choice names a document that is no longer in `docList`, and leave the other
// one standing. Called from the close sweep (KCMHandleDocsClosed), which has the list in hand
// and has already established that it is on the main thread -- the one place that may conclude
// "not in the list" means "closed" (a background thread sees clones, guide vol1-07).
void	KCMForgetChosenDocsThatClosed(IDocumentList* docList);

// Drop both choices. Called from the model's Shutdown (KCMPeekStartup::Shutdown), which closes
// every model-side static on the principle that nothing live may reach static destruction -- the
// same slot as the peek's armed state, and defensive for the same reason (a close responder
// firing after shutdown). Assignment only, so it is safe anywhere in the shutdown sequence.
void	KCMClearChosenDocs();

// The print-marks toggle: flips the current print flag and keeps the current opacity choice.
void	KCMTogglePrintMarks();

// Set the mark frame opacity to 25% (op25=kTrue) or 75% (kFalse), keeping the print flag.
void	KCMSetMarkOpacity25(bool16 op25);
// Set the mark colour to red (kFalse) or cyan (kTrue), from the flyout's two items.
void	KCMSetMarkColor(bool16 cyan);

#endif // __KCMComparisonRun_h__
