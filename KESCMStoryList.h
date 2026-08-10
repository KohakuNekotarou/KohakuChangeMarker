//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The list of stories that changed between the two versions, ready to be shown as rows.
//
//  This file turns KESCMStoryStamp's raw findings into something a person can read: the first
//  words of each story, which page it starts on, and what kind of change it saw. It knows nothing
//  about trees or widgets - the Story Edits tree reads this and only this.
//
//  *** THE LIST IS A FILE-STATIC GLOBAL, NOT A BOSS. *** It is built by one comparison and thrown
//  away by the next, so there is nothing to persist and nobody to share it with. KBS keeps its
//  result list exactly this way (KBSResultModel.cpp:27-29). The one obligation that comes with it:
//  ShutdownCleanup() has to empty it during a controlled shutdown, because these rows hold PMStrings
//  and a static PMString still holding storage at DLL unload is the crash KBS logged three times
//  over (KBSResultModel.cpp:363-377).
//
//========================================================================================

#ifndef __KESCMStoryList_h__
#define __KESCMStoryList_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KESCMStoryStamp.h"	// KESCMStoryDiff / KESCMStoryChangeKind

class IDataBase;

/** One row of the Story Edits section. */
struct KESCMStoryRow
{
	UID			fStoryUID;	// the target document's story
	PMString	fText;		// first readable words, already shortened - what the row shows
	uint32		fKinds;		// OR of KESCMStoryChangeKind - named on the right of the row
	UID			fPageUID;	// where the story starts; kInvalidUID when it starts on the pasteboard
	int32		fPageIndex;	// sort key only. kMaxInt32 when there is no page, so those sink to the end

	KESCMStoryRow()
		: fStoryUID(kInvalidUID), fKinds(kKESCMStoryKindNone), fPageUID(kInvalidUID), fPageIndex(kMaxInt32) {}
};

namespace KESCMStoryList
{
	/** Replace the list with one row per entry in diffs, read out of the target document.

		Rows come out in page order. A story that starts on the pasteboard, or on a master page, has
		no page index and sorts to the end rather than being dropped - it is still a real edit.

		Reads only. Nothing here composes, which keeps the property stage 1 measured and wrote into
		KESCMStoryStamp.h:42-43: looking at what changed costs no recomposition.

		@param targetDB the newer document. nil clears the list.
		@param diffs what KESCMStoryEdits::Compare produced for this comparison.
	*/
	void Build(IDataBase* targetDB, const std::vector<KESCMStoryDiff>& diffs);

	/** Empty the list. Called on Stop and when a compared document closes. */
	void Clear();

	int32 GetRowCount();

	/** The nth row, or nil when nth is out of range. Callers get a pointer rather than a reference
		so that an index the tree asks for after the list was rebuilt cannot walk off the end.
	*/
	const KESCMStoryRow* GetRow(int32 nth);

	/** Empty the list during a controlled shutdown. See the file comment for why this exists. */
	void ShutdownCleanup();
}

#endif // __KESCMStoryList_h__

// End, KESCMStoryList.h.
