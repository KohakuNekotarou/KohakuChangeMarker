//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Book comparison: the two judgements that are NOT the pixels.
//
//  The pixel judgement stays in KCMBookCompare.cpp, where it has always been. These two are here
//  because they are a different kind of thing: each is a handful of lines handing two databases to
//  machinery built for the panel. Putting all three in one file would bury the pixel walk -- the
//  part of the book comparison anybody actually comes to read.
//
//  ★★★NEITHER OF THESE TOUCHES THE PANEL'S LISTS, AND THAT IS THE WHOLE DESIGN.
//  KCMStoryList and KCMResourceStore each hold ONE result, and that result IS what the panel is
//  showing. A book comparison built on top of them would rebuild the reader's list -- for two
//  chapters they never asked about -- every time a book was compared. So what is called here is
//  the STATELESS layer underneath both:
//      Story     -> KCMStoryEdits::CollectStamps + ::Compare  (KCMStoryStamp.h: "KEEPS NO STATE")
//      Resources -> KCMReadResourceList + KCMDiffResources     (plain functions; the caller owns
//                                                               everything they fill)
//  ⇒ BK-03 holds for all three modes: a book comparison arms nothing, drops nothing and marks
//    nothing, and can still be run while a document comparison is up.
//
//  ⚠**A CHAPTER'S ANSWER IS ONE BIT PER MODE**, so both functions stop at the first difference and
//  throw the list away. Naming WHAT differs is the panel's job, reached from a chapter row's right
//  click (spec map BK-57).
//
//========================================================================================
#ifndef __KCMBookChapterModes_h__
#define __KCMBookChapterModes_h__

#include "PMString.h"

class IDataBase;

/** What ONE mode concluded about ONE chapter.

    ★THREE VALUES, NOT A bool16. "It could not be judged" is a third answer and has to stay apart
    from "it was judged and nothing differs" -- the same distinction KCMBookResult.h keeps between
    NotCompared and NoChange, and the one whose absence cost KBS a day. */
enum KCMBookModeVerdict
{
	kKCMBookVerdictUnchanged = 0,
	kKCMBookVerdictChanged,
	kKCMBookVerdictUnjudged		// outWhy says which step could not be taken
};

/** Did the STORIES change between these two chapters?

    Asked of the change counters alone (KCMStoryStamp.h), which compose nothing and cost nothing --
    a document with three edited stories is answered as fast as an empty one, however long it is.

    ⚠**IT ANSWERS MORE WIDELY THAN THE PANEL'S Story Edits LIST**, deliberately. The list runs the
    text diff and then drops the rows whose words agree (KCMStoryRowFilter.h), so a story whose only
    edit was a font does not appear there. Here it does. The two are answering different questions:
    the list asks "which rows are worth a reader's attention", this asks "did anything about this
    chapter's text move at all". Narrowing this one to match would mean running the text diff on
    every chapter of the book to produce one bit.

    ⚠**Two documents that are not versions of one another come out CHANGED**, because stories are
    matched by UID and none of them line up. KCMStoryStamp.h says that is deliberate and needs no
    special case, and it is the same answer the panel gives for such a pair.

    @param targetDB the newer chapter. nil is unjudged.
    @param sourceDB the older chapter. nil is unjudged.
    @param outWhy   on kKCMBookVerdictUnjudged, a short English reason beginning "Story: ".
                    Cleared on every call, so a caller may reuse one string for all three modes. */
KCMBookModeVerdict KCMJudgeChapterStory(IDataBase* targetDB, IDataBase* sourceDB, PMString& outWhy);

/** Did the DEFINITIONS change between these two chapters -- styles, swatches, layers, preferences?

    Two whole-document XML exports and a pairing, and this is the expensive one: 200-2400ms per
    chapter (KCMResourceStore.h). Nothing is cached, because two chapters are compared once and
    then closed.

    ⚠IT IS THE FIRST CALLER TO TAKE THE WINDOWLESS ROUTE through KCMTakeResourceSnapshot's session
    guard. That guard refuses a database no session document owns -- KIDMCP's task-start clone,
    which kills InDesign inside ExportINX -- and its own comment says a document opened WITHOUT A
    WINDOW is in the list and passes, "that route is untested but not excluded". Book chapters are
    opened by IDocumentCommands::Open with showInWindow = kFalse and ARE session documents, so they
    pass; this is the run that tests it.

    @param targetDB the newer chapter. nil is unjudged.
    @param sourceDB the older chapter. nil is unjudged.
    @param outWhy   on kKCMBookVerdictUnjudged, a short English reason beginning "Resources: ".
                    Cleared on every call. */
KCMBookModeVerdict KCMJudgeChapterResources(IDataBase* targetDB, IDataBase* sourceDB, PMString& outWhy);

#endif // __KCMBookChapterModes_h__

// End, KCMBookChapterModes.h.
