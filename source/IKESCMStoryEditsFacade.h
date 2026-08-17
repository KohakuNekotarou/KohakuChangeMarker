//========================================================================================
//
//  IKESCMStoryEditsFacade.h
//
//  Reading the Story Edits list: which stories changed between the two versions, and where a
//  given story begins in a given document.
//
//  Created 2026-08-13 for the model/UI split (Stage 1), Task 14.
//
//  Comparing pixels can only say "this page looks different". Story Edits answers the next
//  question -- whether the words changed, only the formatting changed, or something attached to
//  the story such as a table changed -- by matching ITextModel's change counters story by story
//  (KESCMStoryStamp.h). This interface is how the panel reads what that produced.
//
//  ★READ ONLY, for the same reason IKESCMMarkData is. The list is built by one comparison and
//  thrown away by the next, and both of those happen in the model (KESCMCore.cpp, and the
//  shutdown path in KESCMPeek.cpp). No UI file builds it, clears it or empties it at shutdown,
//  so there is no Build() here: a method on a boundary that nobody calls is a promise nobody
//  keeps. (The plan's draft had a Rebuild() on the strength of a second caller in "Refresh Page
//  Comparison"; grepping for it before writing this file found that caller is model-side too.)
//
//  ★THE LAST TWO METHODS ARE NOT ABOUT THE LIST. They answer "where does this story start in
//  this document", which the navigation asks of the SOURCE document for a story the list only
//  ever read out of the target -- the two versions can hold the same story in different places
//  (user's observation, 2026-08-10). They take a database and a UID and nothing else, so they
//  are model questions: no window has to exist for them to have an answer.
//
//========================================================================================

#ifndef __IKESCMStoryEditsFacade_h__
#define __IKESCMStoryEditsFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMPoint.h"			// PBPMPoint -- a pasteboard point, what a jump centres on
#include "PMString.h"
#include "UIDRef.h"				// UID

// Project includes:
#include "KESCMBoundaryID.h"	// IID_IKESCMSTORYEDITSFACADE。★2026-08-17 に KESCMID.h から絞った
								// (理由は IKESCMCompareFacade.h の同じ位置)
#include "KESCMStoryStamp.h"	// KESCMStoryChangeKind を借りるため。⚠2026-08-17 訂正＝旧「a type only」は
								// 不正確で、このヘッダーは free function の宣言も 3 本連れてくる(実測)

class IDataBase;

class IKESCMStoryEditsFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMSTORYEDITSFACADE };

	/** One row of the Story Edits list, in the five fields the panel actually reads.

		★A COPY, NOT A POINTER INTO THE LIST. The model hands out const KESCMStoryRow* today,
		which is safe only because a row is read and used inside one call while nothing can
		rebuild the list underneath it. Across the boundary that reasoning stops being local, so
		the row is copied out and the caller owns it.

		★fPageIndex is not here. It is the list's sort key, used while the rows are being built
		and never read afterwards -- putting it on the boundary would publish an internal
		ordering decision as if the UI were entitled to it.
	*/
	struct Row
	{
		UID			fStoryUID;	// the target document's story -- the row's identity, shown as a number
		PMString	fText;		// the first readable words. NOT shortened here: the row's text cell is
								// kEllipsizeMiddle and decides for itself at whatever width it has
		uint32		fKinds;		// OR of KESCMStoryChangeKind -- named on the right of the row
		UID			fFrameUID;	// the story's FIRST frame, what a click scrolls to. kInvalidUID for a
								// story in no frame at all, which cannot be jumped to
		UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard

		Row()
			: fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone), fFrameUID(kInvalidUID),
			  fPageUID(kInvalidUID) {}
	};

	// ---- the list ------------------------------------------------------------------------

	/** How many rows the list holds. 0 both when nothing has been compared and when a comparison
		found no edited story -- the two are told apart by asking IKESCMCompareFacade::IsArmed(),
		which is what the section heading and the placeholder row already do. */
	virtual int32	GetRowCount() = 0;

	/** Fill out with row nth (0-based). kFalse when nth is out of range, leaving out untouched.

		★Asked one row at a time on purpose. Both callers want exactly one: the tree writes the
		widget for a single node, and a click reports the row it landed on. (Contrast the page
		pairing on IKESCMMarkData, which is handed over whole because its callers each build a
		map of the lot.) */
	virtual bool16	GetRow(int32 nth, Row& out) = 0;

	// ---- where a story begins, in either document ------------------------------------------

	/** The first frame this story is placed in. kInvalidUID when the document has no such story
		or it sits in no frame.

		Matching by story UID works because saving under a new name carries the UIDs across
		(KESCMStoryStamp.h:46-51). For two documents that are not versions of each other nothing
		lines up, and the rows simply come out as "Added". */
	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID) = 0;

	/** Where the story BEGINS on the page, as a pasteboard point -- what a jump should centre.

		Not the same as the first frame's centre: in a tall frame the centre is the middle of the
		text and what a reader wants is the beginning of it (user's call, 2026-08-10).

		@param outFrame [out] the frame that beginning sits in. Untouched when this answers kFalse.
		@param outPb [out] the point, in pasteboard coordinates. Untouched when this answers kFalse.
		@return kFalse when there is no such story, or none of its parcels are placed -- callers
			fall back to centring GetFirstFrameUID's frame. */
	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb) = 0;
};

#endif // __IKESCMStoryEditsFacade_h__

// End, IKESCMStoryEditsFacade.h.
