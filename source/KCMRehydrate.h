//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Task Start
//
//  INX -> a windowless document. ★IINXManager::ImportINX IS CALLED HERE AND NOWHERE ELSE.
//
//  Everything below was measured on 2026-09-12 (docs/ai-notes/kcm-inx-rehydration-2026-09-12.md),
//  because the SDK contains not one caller of ImportINX:
//    - the parent is a NEW windowless document's IDOMElement, and the policy is
//      kDocElementImportBoss. kActionImportPolicyBoss - the export's counterpart by name - puts
//      nothing in; kSaveBackImportPolicyBoss (IDML's) takes InDesign down; ISnippetImport refuses
//      the stream before any policy is consulted (kSnippetWrongDocType: the PI says type="action").
//    - the XML is injected first (KCMXmlInject.h): without the sacrificial range the first
//      ParagraphStyleRange of every story is lost.
//    - the result is CHECKED against the origin's shape. A short rehydration is a failure, never
//      a smaller document - the comparison must not be handed one.
//
//  Closing: IDocFileHandler::Close with kProcess, as KCMBookCompare closes its windowless chapters.
//  ⚠★★★A CLOSE UNDER AN OUTSTANDING REFERENCE IS A PROTECTIVE SHUTDOWN, NOT A CRASH: InDesign ends
//  the process itself, without an exception, and writes "CloseDocCmd - document is still
//  referenced / Document has N extra references" to InDesign Recovery/ProtectiveShutdownLog -
//  the only record there is (no crash watch sees it). Measured twice on 2026-09-12: the failure
//  paths of the rehydration closed the document while `parent`, `importedHolder` and the policy
//  were still in scope. That is what the throwaway probe had hit too ("crashed from inside a
//  script property call" was the wrong reading - the context was innocent). The rule that came
//  out of it: KCMCloseRehydrated is called only after every InterfacePtr on the document has gone
//  (KCMRehydrate.cpp, ImportAndCheck). The callers here are menu actions and the tool's press.
//  ★ONE CALLER IS NEITHER: the close sweep
//  (kAfterCloseDoc), where the origin's own document has just closed and the peek document goes
//  with it. Closing a document from inside another document's close responder is not a place
//  anything in the SDK does, so that caller asks for `deferred` = IDocFileHandler::kSchedule,
//  the handler's default mode, and the close runs after the responder has returned.
//
//  THE DOCUMENT IS LEFT CLEAN. A rehydration is a New + an import (+ deleted spreads, for the
//  peek), which dirties the document. It is marked unmodified at the end on purpose: it is ours,
//  there is nothing in it to save, and an untitled dirty document with no window is exactly what
//  a Quit's close-all would stop at with "Save changes?" - about a document the reader never
//  made. Unmodified, the close-all takes it silently (⚠unmeasured on a real Quit; reasoned from
//  how a fresh untitled document closes).
//
//========================================================================================
#ifndef __KCMRehydrate_h__
#define __KCMRehydrate_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

class IDataBase;
class KCMResourceBytes;
struct KCMOriginShape;

/** How a rehydration is made, and what becomes of a copy that does not match the origin.

    ★★★**THE THREE ARE NOT PREFERENCES - EACH ONE ANSWERS A QUESTION THE OTHERS CANNOT.** */
enum KCMRehydrateMode
{
	/** ★**THE COPY A COMPARISON IS GIVEN.** The injection is done (KCMXmlInject.h: the sacrificial
	    ranges, the decoy backing story, the origin-uid labels), the surviving dummies are deleted,
	    the pages are labelled and the copy takes the origin's name - and **a copy whose shape does
	    not match is CLOSED and refused**, because no comparison may be handed a smaller document. */
	kKCMRehydrateForComparison = 0,

	/** ★★**THE SAME COPY, KEPT EVEN WHEN IT DOES NOT MATCH** (2026-09-20). Everything above is
	    done; only the refusal is withheld, so that the copy can be MEASURED. ⚠**This is the one
	    thing kKCMRehydrateForComparison cannot allow**, and it is why this mode exists: the copies
	    worth measuring are exactly the ones that FAIL, and that mode closes those first.
	    ⚠The return is still kFalse when it does not match - "it was made" and "it matches" stay
	    two separate statements, and outDoc carries the copy either way. */
	kKCMRehydrateKeepForCheck,

	/** ★★**THE HELD BYTES, UNTOUCHED** (2026-09-20, the user's ask: no sacrificial text this time -
	    "make a document out of it just as it is, doing nothing to it"). It asks the question the
	    injection was built to answer, the other way round: what does ImportINX make of the held
	    IDML when nothing is done to help it?
	    ★**Three things are skipped, and the skipping IS the measurement**:
	    ①**no injection whatever** - not the sacrificial first ranges, not the KcmOriginUid labels,
	      not the decoy backing story. ②**nothing is written into the copy afterwards** - no dummy
	      deletion, no page labels, no name off the origin. ③**a copy that does not match is kept**,
	      as above.
	    ✅**MEASURED 2026-09-20, and it is severe**: the story that stands second in FILE ORDER
	    (the real <XmlStory> is first, so it is the first <Story>) loses a whole text insertion -
	    and when that story holds a table, **the table's skeleton goes with it**: Table, Row, Column
	    and Cell all fell to zero, Content 11 -> 1. Everything else was untouched (292 element names
	    identical). ⚠**The same bytes opened as a .idml file lose NOTHING, with or without a decoy**
	    - so what drops the insertion is ImportINX's own route, not the IDML.
	    ⚠**The one thing still read off the XML is the page setup of the NEW DOCUMENT**
	    (NewDocumentLike): ImportINX does not apply <DocumentPreference> to a document that already
	    exists, so an untouched import into an application-default document lays every frame half a
	    page out and nothing about the CONTENT could be read off it. Nothing is written into the XML
	    for it - the bytes handed to the import are the origin's, byte for byte. */
	kKCMRehydrateUntouched
};

/** Rehydrate inx into a fresh windowless document.
    @param expect  the shape the origin had; whether a mismatch closes the copy is the mode's to say.
    @param outDoc  the document. Windowless, untitled, in app.documents. ⚠Under the two modes that
                   KEEP a mismatched copy this is set even when the answer is kFalse.
    @param whyNot  when kFalse, the step that failed - or what did not line up.
    @param mode    which of the three copies above (the default is the comparison's). */
bool16 KCMRehydrate(const KCMResourceBytes& inx, const KCMOriginShape& expect, UIDRef& outDoc,
                    PMString& whyNot, KCMRehydrateMode mode = kKCMRehydrateForComparison);

/** ★★**THE HELD IDML, MADE INTO A DOCUMENT AND NOTHING ELSE** (2026-09-20, the user's request:
    "it has the sacrificial text in it now - without that, just make a document out of it doing
    nothing to it").

    ★**THE BYTES ARE THE ORIGIN'S, BYTE FOR BYTE** (kKCMRehydrateUntouched, which says what is
    skipped and why). That is the value of it: everything the comparison's copy has done to it -
    the sacrificial ranges, the decoy story, the labels, the deletions afterwards - was added to
    work around what the import does, and this shows what the import does when none of it is there.
    ⚠**It is therefore NOT the copy the comparison makes any more** (until 2026-09-20 evening it
     was, with the sacrificial paragraphs left in).

    ⚠**A COPY THAT FAILS THE CHECK IS THE INTERESTING ONE**, so it is opened anyway and the mismatch
     goes on the status line rather than into a refusal.
    ⚠**A WINDOWLESS DOCUMENT CANNOT ALWAYS BE GIVEN A WINDOW.** This asks kOpenLayoutCmdBoss for one
     (KBSBookScope.cpp asks the same for a windowless chapter, and checks the presentation rather
     than the return code) and says so when none appeared - the document is in app.documents either
     way, so nothing is lost when it does not.
    @return ★**kTrue when EVERYTHING IS WELL** - a document was made, it matched the origin's shape,
     AND the round-trip check found nothing missing (2026-09-20; it used to answer "a document was
     made", which the one caller ignored). The caller colours the status line with it: red is for
     bad news, and what counts as bad news is decided here rather than by reading the words.
     ⚠**kFalse does not mean "no document"** - outMessage says which of the three went wrong, and a
      copy that failed is still open for the reader to look at. */
// ⛔KCMOpenOriginForInspection went on 2026-09-21 with the origin (see the .cpp).

/** ★★★**THE ROUND-TRIP CHECK** (2026-09-20, the user's design: "turn the INX into a hidden
    document, take THAT document's INX, compare it with the Task Start's - and if what differs is a
    nested table, put it back with the snippet road").

    This is step 2 of that: photograph the copy the same way the origin was photographed, and count
    every element name on both sides (KCMCompareElementCounts). ★**It is deliberately blind to what
    it is looking at** - it does not know about tables - so a fault nobody has met yet still shows
    up, named. That is the whole reason it is worth having: the repairs can only ever be written
    for faults that are already known, and this is what finds the next one.

    ⚠**WHAT A UID DOES NOT DO HERE.** ImportINX renumbers every UID it brings in, so the two sides
     can never be compared byte for byte - but a UID is not an element NAME, so counting names is
     untouched by it. Measured 2026-09-20: a whole round trip of a two-story document came back
     with 265 element names equal and the differences confined to the fault being hunted.

    ⚠**THREE NAMES GROW ON PURPOSE**: Label, KeyValuePair and Properties, because the injection
     writes a KcmOriginUid label into every story and spread (KCMXmlInject.h, 2.). Those are
     reported as EXPECTED. Everything else - and any of those three going DOWN - is a real
     difference.

    @param copyDB the rehydrated copy's database.
    @param out    one line: what matches, and what does not, largest difference first.
    @return kTrue when nothing but the expected differences were found. */
// ⛔KCMVerifyRehydration went on 2026-09-21 with the origin (see the .cpp).

/** Close a document this file made. Nothing else may be handed to it. A document that is already
    gone, or UIDRef::gNull, is ignored.
    @param deferred kTrue schedules the close (IDocFileHandler::kSchedule) instead of running it
           now - for the one caller that is inside a close responder (see the head of this file). */
void KCMCloseRehydrated(const UIDRef& doc, bool16 deferred = kFalse);

/** Mark a rehydrated document unmodified (see the head of this file). The peek calls it again
    after cutting the copy down; nil-safe. */
void KCMMarkRehydratedClean(IDataBase* db);

/** The KcmOriginUid label of uid in db (a story or a spread of a rehydrated document), as the
    ORIGINAL uid. kFalse when there is no such label or it does not parse. */
bool16 KCMReadOriginUidLabel(IDataBase* db, UID uid, UID& outOriginal);

#endif // __KCMRehydrate_h__

// End, KCMRehydrate.h.
