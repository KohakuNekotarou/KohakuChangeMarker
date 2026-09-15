//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  THE ORIGIN: one document's INX, taken when the user pressed Task Start, held in memory.
//
//  ONE SLOT (the user's rule, 2026-09-12): only ONE origin is held at a time.
//  ★★**Since 2026-09-14 that no longer greys the menu item.** Pressing Task Start while an origin
//  is held - or while a comparison is running - stops, clears, and takes a fresh one (the user's
//  instruction: "if it is started, Stop, then Clear Target and Source, then Task Start"). The slot
//  is still one; what changed is who empties it.
//  What else ends an origin: "Clear Target and Source", or the document it was taken from closing.
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
class IDFile;
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

/** Whether Task Start may be pressed: **an active document, and nothing else** (2026-09-14).
    ★THE ONE PLACE. The menu's grey state and the command both ask this.
    ⚠It used to add "no origin held, no comparison armed"; those two are now cleared by
    KCMTakeTaskStart itself rather than refused here. */
bool16 KCMCanTakeTaskStart();

/** Take the origin from the ACTIVE document and choose the pair (Target = that document,
    Source = the origin). Runs no comparison. kFalse, with a reason, when KCMCanTakeTaskStart
    says no or the export failed - nothing is held then. */
bool16 KCMTakeTaskStart(PMString& whyNot);

bool16 KCMHasOrigin();

/** Move the held origin aside so that the import mode can use the slot, and put it back after.

    ★★★**WHY A PARK RATHER THAN A SECOND ORIGIN** (2026-09-15, the user's requirement: the task
      they made is to keep existing). An import needs the document as it stood a moment ago, which
      is what this slot holds - and giving the plug-in two origins would mean **82 places in 18
      files** choosing between them, measured. The import mode is modal (no other comparison runs
      while it is up, which is the user's own rule), so one slot plus a park is enough, and the
      reader's Task Start comes back exactly as they left it.
    ⚠**ONE PARK SLOT.** Parking twice answers kFalse rather than dropping the first.
    ⚠Parking drops the PEEK, which stood on those bytes. The origin itself is kept.

    @return kFalse when something was already parked (park), or nothing was (unpark). */
bool16 KCMParkOrigin();
bool16 KCMUnparkOrigin();
bool16 KCMHasParkedOrigin();

/** Throw the parked origin away, for a document closing: putting it back would restore an origin
    whose document is gone. */
void KCMDropParkedOrigin();

/** THE DEBUGGING DOOR (2026-09-12; the only one of the three left after 2026-09-14): the held
    origin's XML written to `file` AS IT IS - the bytes Task Start took, before any injection.

    ★THE WAY IN IS THE SCRIPT METHOD app.kcmSaveOriginXml (KCMScriptProvider.cpp, 2026-09-14).
    The flyout items that opened and saved the origin were removed that day and this one was kept
    on the user's decision - as a method rather than a menu item, with the CALLER naming the file.
    So nothing here asks the shell for the Desktop or invents a name any more: both were the old
    menu item's business, and a script that wants "<document>.TaskStart-HHMMSS.xml" can build that
    name itself from app.kcmOriginStatus, which already reports the document and the time.

    @param file where to write. The script engine has already resolved it from the path the caller
           passed, and ScriptData::GetFile validates the PARENT FOLDER on the way (kTrue by
           default), so a path into a folder that does not exist is refused before it reaches here.
    @param whyNot the same answer in words, for a status line or a log.
    @return 0 written / 1 no origin is held / 2 the file could not be created / 3 the write failed.
    ★THE NUMBERS ARE DECIDED HERE, ONCE (one question, one place): the script method returns what
      this returns without re-deciding anything, and KCM.fr spells the same list out for whoever
      reads the DOM. Changing a number means changing that resource string in the same edit. */
int32 KCMOriginSaveRaw(const IDFile& file, PMString& whyNot);

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
