//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  THE ORIGIN: one document's INX, taken when the user pressed Task Start, held in memory.
//
//  ONE SLOT (the user's rule, 2026-09-12): while an origin is held, Task Start cannot be pressed
//  again. What ends it is "Clear Target and Source" or the document it was taken from closing.
//  A Stop keeps it - the usual shape of the work is start, stop, edit, start again.
//
//  WHAT IS KEPT WITH THE BYTES. The document's shape (a rehydration is checked against it), and
//  every story's change counters AS THEY STOOD at Task Start, in the document's own uids. A
//  rehydrated copy is freshly imported and its own counters mean nothing; the Story mode pairs
//  against these instead, exactly as it would against a saved older version. That is the whole
//  of the "extension" the copy needs, and it lives here rather than in the XML or the DOM.
//
//  THE DOCUMENT IS HELD AS A DATABASE POINTER THAT IS NEVER DEREFERENCED WITHOUT A LIVENESS
//  TEST ([[uidref-reuse-after-close]]): KCMOriginDocDB answers nil the moment the document is not
//  in the list, and the close sweep forgets the origin outright.
//
//  Main thread only. Nothing here is read from the drawing path.
//
//========================================================================================
#ifndef __KCMOrigin_h__
#define __KCMOrigin_h__

#include "BaseType.h"
#include "PMString.h"
#include <vector>
#include "KCMStoryStamp.h"		// KCMStoryStamp - the counters kept with the origin

class IDataBase;
class IDocumentList;
class KCMResourceBytes;

/** What a document looks like in numbers. Taken with the origin, and checked against a rehydration. */
struct KCMOriginShape
{
	int32 fSpreads;
	int32 fPages;
	int32 fStories;		// user-accessible stories
	int32 fTextLen;		// ITextModel::TotalLength summed over them
	KCMOriginShape() : fSpreads(0), fPages(0), fStories(0), fTextLen(0) {}
	bool16 operator==(const KCMOriginShape& o) const
	{ return (fSpreads == o.fSpreads && fPages == o.fPages && fStories == o.fStories && fTextLen == o.fTextLen) ? kTrue : kFalse; }
};

/** Counts db's spreads, pages, user stories and their text. nil db yields zeros. */
void KCMMeasureShape(IDataBase* db, KCMOriginShape& out);

/** Whether Task Start may be pressed: no origin held, no comparison armed, an active document.
    ★THE ONE PLACE. The menu's grey state and the command both ask this. */
bool16 KCMCanTakeTaskStart();

/** Take the origin from the ACTIVE document and choose the pair (Target = that document,
    Source = the origin). Runs no comparison. kFalse, with a reason, when KCMCanTakeTaskStart
    says no or the export failed - nothing is held then. */
bool16 KCMTakeTaskStart(PMString& whyNot);

bool16 KCMHasOrigin();

/** The test instrument (2026-09-12): the held origin's XML imported UNTOUCHED into a new
    windowless document (KCMRehydrateRaw). The UI gives it a window; it is the reader's to close
    and is not the run's copy nor the peek document. kFalse, with a reason, when nothing is held
    or the import failed. */
bool16 KCMOriginOpenRaw(UIDRef& outDoc, PMString& whyNot);

/** Its twin: the held origin rehydrated EXACTLY as a comparison rehydrates it (KCMRehydrate -
    the sacrificial range, the deletion of a surviving one, the compose, the shape check), so the
    reader can look at the very copy the comparison reads - paragraph styles included. The UI
    gives it a window; it is the reader's to close and is not the run's copy nor the peek document.
    kFalse, with a reason, when nothing is held or the rehydration failed. */
bool16 KCMOriginOpenCopy(UIDRef& outDoc, PMString& whyNot);

/** The origin's document, or nil when none is held or it has closed. Compared, never dereferenced
    by callers that did not get it from here a moment ago. */
IDataBase* KCMOriginDocDB();

/** The bytes and the shape. nil when nothing is held. Valid until KCMReleaseOrigin. */
const KCMResourceBytes*	KCMOriginBytes();
const KCMOriginShape*	KCMOriginShapeOf();

/** The stories' change counters as they stood at Task Start, in the document's own uids
    (KCMStoryEdits::CollectStamps). nil when nothing is held. */
const std::vector<KCMStoryStamp>* KCMOriginStoryStamps();

/** "Task Start 12:34:56" - the panel's Source: line. Empty when nothing is held. */
void KCMOriginLabel(PMString& out);

/** Drop the origin (bytes, shape, stamps, document) and the peek document that stood on it.
    Idempotent; safe at any point of the shutdown sequence. The slot is emptied BEFORE the peek
    document is closed, so the close sweep that close raises sees no origin and returns.
    @param deferPeekClose kTrue schedules the peek document's close instead of running it now -
           the close sweep's choice, being inside a close responder itself (KCMRehydrate.h). */
void KCMReleaseOrigin(bool16 deferPeekClose = kFalse);

/** The close sweep's half: forget the origin when its document is no longer in docList (the peek
    document's close is scheduled, not run, from here). */
void KCMForgetOriginIfDocClosed(IDocumentList* docList);

/** One line for app.kcmOriginStatus:
    "held=yes doc=<name> (open|closed) taken=<hh:mm:ss> bytes=<n> shape=<spreads>/<pages>/<stories>/<textLen> stamps=<n> peek=<spread uid or ->"
    or "held=no". */
void KCMOriginStatusLine(PMString& out);

#endif // __KCMOrigin_h__

// End, KCMOrigin.h.
