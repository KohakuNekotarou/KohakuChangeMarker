//========================================================================================
//
//  IKCMResourcesFacade.h
//
//  Reading which DEFINITIONS differ between the two documents being compared - the Resources
//  mode. The panel's only way in.
//
//  WHAT THIS MODE ANSWERS THAT THE OTHER TWO CANNOT. Pixel says which pages LOOK different;
//  Story says which words changed. Neither can see a definition whose contents were edited: a
//  paragraph style nobody has applied can have its size changed and every page still renders to
//  the same pixel. Measured 2026-09-09 - the pixels came back IDENTICAL and this mode reported
//  `ParagraphStyle/… PointSize 17.00787 -> 34.01574`.
//
//  ★THE RESULT IS HELD, NOT RECOMPUTED. Comparing costs two whole-document exports (200-2400ms),
//  so Compare() runs once and the rows are read from what it kept. Reading a row is free, which
//  is what makes it safe to call from a panel's draw.
//  ⚠It goes STALE: nothing here notices an edit. Compare() again to make it current. That is the
//    same contract IKCMStoryEditsFacade has.
//
//  ★NO Clear() HERE, and that is deliberate. The list is emptied by the model when a comparison
//  stops (KCMCore), which is where the other lists are emptied too. **A method on a boundary that
//  nobody calls is a promise nobody keeps** - the same sentence IKCMStoryEditsFacade uses to
//  explain why it has no Build().
//
//  ⚠**THE IID IS DECLARED IN TWO FILES** - source/KCMBoundaryID.h and ui/KCMBoundaryID.h - and
//  they must hold the same value. Editing one builds cleanly, loads cleanly, and the UI simply
//  gets nil at run time.
//
//========================================================================================

#ifndef __IKCMResourcesFacade_h__
#define __IKCMResourcesFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMBoundaryID.h"		// IID_IKCMRESOURCESFACADE
#include "KCMResourceKinds.h"	// KCMResourceChangeKind. ★A TYPES-ONLY header: a header the UI
								// includes must declare no model-side free function, or the UI can
								// see what it cannot link to.

class IKCMResourcesFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMRESOURCESFACADE };

	/** Compares the definitions of the two documents the comparison is armed on, and keeps the
	    answer for the calls below.

	    ⚠It REFUSES when the Source is a lent clone (KIDMCP's task-start copy): exporting one
	    kills InDesign, measured twice on 2026-09-09, so it is refused before it is attempted.
	    The reason comes back in whyNot.

	    @param whyNot  on kFalse, a short English reason fit to show in the status line.
	    @return kTrue when a result is held afterwards. ★kTrue with GetChangeCount() == 0 is a
	            real answer: the two documents define the same things. */
	virtual bool16	Compare(PMString& whyNot) = 0;

	/** kTrue when a result is being held. ⚠Says nothing about whether it is still true of the
	    documents - see the note on staleness above. */
	virtual bool16	HasResult() = 0;

	/** How many definitions differ. */
	virtual int32	GetChangeCount() = 0;

	/** One row of the list.

	    @param n         0 .. GetChangeCount()-1.
	    @param outKind   the element name a reader sees: "ParagraphStyle", "Color", "Layer"...
	    @param outKey    what identifies it: "Color/Black", "Layer#Background", "DocumentPreference".
	    @param outWhat   Added (Target only) / Removed (Source only) / Changed (both, differing).
	    @return kFalse when n is out of range, leaving the outputs alone. */
	virtual bool16	GetNthChange(int32 n, PMString& outKind, PMString& outKey,
								 KCMResourceChangeKind& outWhat) = 0;

	/** The two sides' text for one row, for the panel's upper pane.

	    ★Asked for ONE ROW rather than carried in GetNthChange, because the bodies are the large
	    part of a result and only the selected row's are ever shown.
	    ★The SOURCE side is the one worth showing: the Target's value can be read off the document
	    in front of the user, and the Source's cannot be read anywhere else at all. */
	virtual bool16	GetNthValues(int32 n, PMString& outSourceBody, PMString& outTargetBody) = 0;

	/** How many ATTRIBUTES of row n differ.

	    ★This is what the panel's upper pane shows: not the whole body, which is a wall of XML,
	    but the one line that moved. 0 is a real answer - the element changed somewhere this
	    differ does not look, inside a child element. */
	virtual int32	GetNthAttrCount(int32 n) = 0;

	/** One differing attribute of row n: its name and each side's value.

	    ⚠A side that does not HAVE the attribute comes back EMPTY, which is how an Added or a
	    Removed definition is told from one whose value merely changed - **so do not draw an
	    arrow between two values without looking at whether both are there.**
	    ⚠Values are VERBATIM. `PointSize` comes back as `8.503937007874015`, with no unit and no
	    rounding: the export writes bare numbers and nothing knows which attributes are lengths.

	    @return kFalse when n or i is out of range, leaving the outputs alone. */
	virtual bool16	GetNthAttr(int32 n, int32 i, PMString& outName,
							   PMString& outSource, PMString& outTarget) = 0;

	/** One line for the status line. ⚠"not compared yet", "no changes" and a failure all read
	    differently - an empty list and a failed comparison must never look alike. */
	virtual void	GetSummary(PMString& out) = 0;
};

#endif // __IKCMResourcesFacade_h__

// End, IKCMResourcesFacade.h.
