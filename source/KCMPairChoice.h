//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - which two to compare
//
//  WHAT THE READER HAS CHOSEN, AND WHAT IT RESOLVES TO. The Target and the Source live here,
//  and each of them may be A DOCUMENT or A FILE.
//
//  ★★**A FILE END IS NEW (2026-09-21).** A Task Start saves a copy of the document and chooses
//  THAT FILE as the Source without opening it, so there is nothing to point at until a Start
//  opens it. (The shape this replaces held the older version as an INX in memory - the origin.)
//
//  ★★★**TWO STAGES, AND THE GREY STATE IS THE WHOLE REASON.** KCMCanStartComparison is asked
//  every single time the flyout is opened. If resolving meant opening, opening the flyout would
//  open a document. So:
//     KCMResolveComparisonPair  answers WHICH TWO and opens nothing. A file end stays a file,
//                               and counts as resolved when THE FILE EXISTS - nothing else about
//                               it is read.
//     KCMRealisePairEnd         turns a file end into a database, by finding the document already
//                               open on it or opening it. ★**ONLY Start CALLS THIS.**
//
//  ★**ALL THE SLOTS IN ONE FILE** (moved out of KCMComparisonRun on 2026-09-21): they answer one
//  question - "which two has the reader chosen" - and split across two files there would be two
//  places deciding what "chosen" means ([[one-question-one-place]]).
//
//  **Databases, not documents, and never dereferenced.** A chosen database pointer is only ever
//  handed to IDocumentList::FindDocByDataBase, which is how this plug-in asks whether a database
//  is still open. A closed document's IDataBase may already be freed and its address reused, so
//  holding a raw IDocument* across a close would be worse, not better
//  ([[uidref-reuse-after-close]]).
//
//  Main thread only.
//
//========================================================================================
#ifndef __KCMPairChoice_h__
#define __KCMPairChoice_h__

#include "BaseType.h"
#include "IDFile.h"
#include "PMString.h"

class IDataBase;
class IDocumentList;

/** One end of the pair: a database (an open document, or the lent one), or a file not yet opened. */
struct KCMPairEnd
{
	IDataBase*	fDB;		// nil unless this end is a database
	IDFile		fFile;		// meaningful only while fIsFile
	bool16		fIsFile;

	KCMPairEnd() : fDB(nil), fIsFile(kFalse) {}
	bool16 IsEmpty() const	{ return (fDB == nil && !fIsFile) ? kTrue : kFalse; }
};

/** THE ONE PLACE that answers "which two". **It opens nothing.**

    The menu's grey state (KCMCanStartComparison) and the command itself (KCMToggleStartStop)
    both ask this, so what the menu shows and what pressing it does cannot drift apart
    ([[one-question-one-place]]).

    **A chosen end wins; an unchosen one falls to the automatic rule** - Target = the active
    document, Source = the first open document that is not the Target.
    ⚠**The automatic Source is still "the first document that is not the Target"**, never "the
    one that is not in front": otherwise bringing a third document forward would hand a chosen
    Target to itself as its own Source.

    **Whether the two come out the same is not decided here.** This answers "which two"; "are
    they one document" is a different question with a different answer (a message, not a grey
    item) and is asked once, at the Start.

    @return kTrue when both ends resolved. A file end resolves when the file exists. */
bool16 KCMResolveComparisonPair(KCMPairEnd& outTarget, KCMPairEnd& outSource);

/** Turn a file end into a database: the document already open on that file when there is one,
    otherwise the file opened in a window. A database end is handed straight back.

    ★**ONLY Start CALLS THIS** - see the two stages at the top of this file.
    ⚠**Identity is asked of IDataBase::GetSysFile, never of the path string**: one file can be
     spelled two ways, and a closed document's address gets re-used ([[uidref-reuse-after-close]]).
    @param why the reason, for the status line, when this answers kFalse.
    @return kFalse when the end is empty, the file is gone, or it could not be opened. */
bool16 KCMRealisePairEnd(const KCMPairEnd& end, IDataBase*& outDB, PMString& why);

//----------------------------------------------------------------------------------------
// The chosen pair ("Set as Target" / "Set as Source" on the flyout, and a Task Start)
//
// A choice the reader makes BEFORE starting, so that which two documents are compared is stated
// rather than inferred. Whichever end has not been chosen falls to the automatic rule inside the
// resolver, so a reader who chooses nothing keeps the behaviour this plug-in has always had.
//
// **A Stop does not clear them.** Stopping ends the comparison, not the choice: the usual shape
// of the work is start, stop, edit, start again on the same pair.
//----------------------------------------------------------------------------------------

/** Make the active (front) document the Target / the Source. kFalse, and nothing set, when there
    is no active document - the flyout greys both items in that case, so this is the guard for a
    document closing while the menu stands open.
    **They only set.** Refreshing the panel and putting a word on the status line are the
    caller's ([[one-question-one-place]]: the UI decides what the UI shows). */
bool16	KCMSetChosenTargetToActive();
bool16	KCMSetChosenSourceToActive();

/** Choose both ends outright, from databases the caller already holds - "Set as Target" plus
    "Set as Source" in one, for a route that knows both (the lent Source: KIDMCP's Compare).
    ★**It does not ask what is active**, which is the whole difference from the two above.
    ⚠A file choice on either end gives way, as does the origin - the same courtesies the two
     above pay. */
void	KCMChooseDBPair(IDataBase* targetDB, IDataBase* sourceDB);

/** Drop the SOURCE choice when it names this database; leave it alone otherwise. The lender of
    an external Source calls in with this when it is about to free one. */
void	KCMForgetChosenSourceIfDB(IDataBase* db);

/** The chosen databases, for the panel's labels and for the resolver.
    **nil unless the document is still open** - a closed IDataBase* is only ever compared against
    IDocumentList, never dereferenced, and never handed to a caller.
    ⚠The Source may be THE LENT DATABASE (KCMExternalSource.h), which is in no document list: it
     counts as live for exactly as long as it is registered. */
IDataBase*	KCMChosenTargetDB();
IDataBase*	KCMChosenSourceDB();

/** Choose a FILE as that end of the pair. ★**The database slot for the same end is cleared**, so
    the two kinds of choice can never both stand on one end. A Task Start is the only caller for
    now; "choose a file as the Target / Source" from the flyout is the planned second one. */
void	KCMSetChosenSourceFile(const IDFile& file);
void	KCMSetChosenTargetFile(const IDFile& file);

/** The file chosen for that end. kFalse when that end is not a file choice. */
bool16	KCMChosenSourceFile(IDFile& out);
bool16	KCMChosenTargetFile(IDFile& out);

/** The path a file choice shows on the panel; empty when that end is not a file choice.
    ⚠**The file is not read** - a path is all this is. */
void	KCMChosenSourceFileLabel(PMString& out);
void	KCMChosenTargetFileLabel(PMString& out);

/** The close sweep's half: drop whichever DOCUMENT choice is no longer in `docList`, and leave
    the other one standing. **Each choice is judged on its own**, which is the whole point of
    stating the pair rather than inferring it.

    ★★**IT DOES NOT TOUCH THE FILE CHOICES**, and that is the difference between the two kinds.
      A Task Start copy whose window the reader closes is still on disk, so the next Start opens
      it again; dropping the choice would make closing a window quietly undo the Task Start.

    Called from the close sweep (KCMHandleDocsClosed), which has the list in hand and has already
    established that it is on the main thread - the one place that may conclude "not in the list"
    means "closed" (a background thread sees clones, guide vol1-07). */
void	KCMForgetChosenDocsThatClosed(IDocumentList* docList);

/** Drop every choice, of both kinds. **Two callers, and only one of them is a shutdown**: the
    flyout's "Clear Target and Source" (★which stops a running comparison BEFORE calling this, so
    this never runs under one), and the model's Shutdown, which closes every model-side static on
    the principle that nothing live may reach static destruction. Assignment only, so it is safe
    at any point of the shutdown sequence. */
void	KCMClearChosenDocs();

//----------------------------------------------------------------------------------------
// ⛔THE ORIGIN'S TWO, kept only until the origin itself goes (Task 8 of the 2026-09-21 rework).
// The Source used to have a third kind - a moment rather than a document - and these are what
// the resolver asked about it.
//----------------------------------------------------------------------------------------
bool16	KCMChosenSourceIsOrigin();
void	KCMChooseOriginPair(IDataBase* originDocDB);

#endif // __KCMPairChoice_h__

// End, KCMPairChoice.h.
