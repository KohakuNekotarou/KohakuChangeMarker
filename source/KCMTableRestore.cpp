//========================================================================================
//
//  KCMTableRestore.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <string>
#include <vector>

#include "ICommand.h"
#include "ICommandSequence.h"
#include "IDataBase.h"
#include "IRangeData.h"				// the source range, and (as IID_IRANGEDATA2) the destination range
#include "ITextModel.h"
#include "IUIDData.h"					// the source story of kCopyStoryRangeCmdBoss
#include "CmdUtils.h"
#include "CommandID.h"					// IID_IRANGEDATA2
#include "ErrorUtils.h"
#include "TextID.h"						// kCopyStoryRangeCmdBoss
#include "UIDList.h"

#include "KCMTableRestore.h"
#include "KCMCore.h"					// KCMArmedTargetDB / KCMIsDocDBOpen
#include "KCMMemXferBytes.h"
#include "KCMModelNotify.h"			// KCMNotify - the panel redraws its list
#include "KCMOrigin.h"					// KCMOriginBytes - Task Start's internal IDML
#include "KCMResourceBytes.h"
#include "KCMScratchDoc.h"
#include "KCMStoryDiffRun.h"			// RunOne / CountForKind
#include "KCMStoryList.h"
#include "KCMTableShape.h"
#include "KCMTableSnippet.h"
#include "KCMTargetSnapshot.h"

namespace
{

PMString Refused(const char* what)
{
	PMString s("restore: ");
	s.SetTranslatable(kFalse);
	s.Append(what);
	return s;
}

/** One undo step for the whole of a table restore (the same shape as KCMStoryRestore's RestoreSequence;
    a bulk run brings its own, and then this makes none). */
class TableSequence
{
public:
	explicit TableSequence(bool16 own, const char* name)
		: fSequence(own ? CmdUtils::BeginCommandSequence("KCMTableRestore") : nil)
	{
		if (fSequence != nil)
		{
			PMString n(name);
			n.SetTranslatable(kFalse);
			fSequence->SetName(n);
		}
	}
	~TableSequence()
	{
		if (fSequence != nil)
			CmdUtils::EndCommandSequence(fSequence);
	}
private:
	ICommandSequence* fSequence;
	TableSequence(const TableSequence&);
	TableSequence& operator=(const TableSequence&);
};

/** The story among `stories` that holds a table, and the anchor range of its OUTERMOST table (the
    one whose anchor stands earliest - a nested table's anchor is inside a cell, which lives past
    the body). ⚠Asked rather than assumed: a snippet whose cells hold anchored objects brings in a
    frame for each of them, and the import names none of them (KCMScratchDoc.h). */
bool16 FindTableStory(const std::vector<UIDRef>& stories, UIDRef& outStory, TextIndex& outStart, TextIndex& outEnd)
{
	for (size_t i = 0; i < stories.size(); ++i)
	{
		InterfacePtr<ITextModel> model(stories[i], UseDefaultIID());
		std::vector<KCMTableShape> shapes;
		if (model == nil || !KCMReadTableShapes(model, shapes) || shapes.empty())
			continue;
		size_t outer = 0;
		for (size_t s = 1; s < shapes.size(); ++s)
			if (shapes[s].fAnchorStart < shapes[outer].fAnchorStart)
				outer = s;
		outStory = stories[i];
		outStart = shapes[outer].fAnchorStart;
		outEnd = shapes[outer].fAnchorEnd;
		return kTrue;
	}
	return kFalse;
}

/** [srcStart, srcEnd) of `srcStory` copied over [dstStart, dstEnd) of the Target story - the
    cross-document kCopyStoryRangeCmdBoss the spike measured on 2026-09-19. Every InterfacePtr on
    the source document is released before this returns. */
bool16 CopyTableOver(const UIDRef& srcStory, TextIndex srcStart, TextIndex srcEnd,
					 IDataBase* targetDB, UID targetStoryUID,
					 TextIndex dstStart, TextIndex dstEnd, PMString& whyNot)
{
	InterfacePtr<ICommand> copy(CmdUtils::CreateCommand(kCopyStoryRangeCmdBoss));
	InterfacePtr<IUIDData> src(copy, UseDefaultIID());
	InterfacePtr<IRangeData> srcRange(copy, UseDefaultIID());
	InterfacePtr<IRangeData> dstRange(copy, IID_IRANGEDATA2);
	if (copy == nil || src == nil || srcRange == nil || dstRange == nil)
	{
		whyNot = Refused("the copy command could not be made.");
		return kFalse;
	}
	src->Set(srcStory);
	srcRange->Set(srcStart, srcEnd);
	copy->SetItemList(UIDList(UIDRef(targetDB, targetStoryUID)));
	dstRange->Set(dstStart, dstEnd);
	const ErrorCode err = CmdUtils::ProcessCommand(copy);
	if (err != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = Refused("the table could not be put back (the copy command failed).");
		return kFalse;
	}
	return kTrue;
}

void AppendReChecks(PMString& outMessage, const KCMTargetItemCountGuard& guard)
{
	// ★THE TWO RE-CHECKS the user asked for (2026-09-20): the scratch document is gone, and the
	//   Target gained no page item. Either failing is said out loud rather than trusted.
	if (!KCMScratchDoc::LastOneWasClosed())
		outMessage.Append(" WARNING: a scratch document is still open.");
	if (!guard.Unchanged())
	{
		outMessage.Append(" WARNING: the Target gained ");
		outMessage.AppendNumber(guard.Delta());
		outMessage.Append(" item(s).");
	}
}

}	// anonymous namespace

bool16 KCMRestoreTable(int32 nth, int32 which, const KCMStoryChange& changeIn, bool16 standalone,
					   PMString& outMessage, KCMStoryChange* outDone, int32* outSlot)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	KCMStoryChange change = changeIn;
	const KCMStoryRow* const row = KCMStoryList::GetRow(nth);
	IDataBase* const db = KCMArmedTargetDB();
	if (row == nil || db == nil || !KCMIsDocDBOpen(db))
	{
		outMessage = Refused("no such row, or the Target document is not open.");
		return kFalse;
	}
	if (change.fWhat != KCMStoryChange::kTable || change.fKind != KCMStoryChange::kReplace)
	{
		outMessage = Refused("only a table whose shape changed can be put back this way.");
		return kFalse;
	}
	const UID storyUID = row->fStoryUID;
	InterfacePtr<ITextModel> target(UIDRef(db, storyUID), UseDefaultIID());
	if (target == nil)
	{
		outMessage = Refused("the story is no longer in the Target document.");
		return kFalse;
	}

	// 1. The live table as it stands NOW, and the check that it is the one the diff described.
	std::vector<KCMTableShape> live;
	if (!KCMReadTableShapes(target, live) || change.fTableOrdinal < 0
		|| static_cast<size_t>(change.fTableOrdinal) >= live.size())
	{
		outMessage = Refused("the table is no longer where it was - run Refresh Story Comparison on its row.");
		return kFalse;
	}
	const KCMTableShape liveShape = live[static_cast<size_t>(change.fTableOrdinal)];
	if (KCMTableShapeSignature(liveShape) != change.fShapeSigAfter)
	{
		outMessage = Refused("the table has been edited since the comparison - run Refresh Story Comparison on its row.");
		return kFalse;
	}

	// 2. THE LIVE TABLE, EXPORTED NOW: the snippet an Undo the Restore will put back, and the source
	//    of the cell contents that are about to be kept.
	// ⚠★★★**NOT CUT OUT OF THE SNAPSHOT ON A FRESHNESS TEST** (re-checked 2026-09-20, before the
	//   first live run). The snapshot is taken when the comparison starts, while the ROW's counter is
	//   brought up to date by every re-diff - including the one "Restore Source Text" itself runs when
	//   the story has been edited (RefindAfterEdit). So "the counter still equals the row's" stopped
	//   meaning "the snapshot is still this story", and the Undo the Restore would have put back a
	//   table from before the reader's own edit. A column width or a table style leaves the text
	//   counter exactly where it was, so the test could not see those at all.
	//   ★One story's export is cheap, and it is the only answer that cannot be stale.
	std::string redo;
	std::string liveTableXml;
	{
		KCMMemXferBytes storyInx;
		std::string groups;
		if (!KCMExportStoryInx(db, storyUID, storyInx)
			|| !KCMCutTableXml(storyInx.GetData(), storyInx.GetSize(), storyUID, change.fTableOrdinal, liveTableXml))
		{
			outMessage = Refused("the table could not be written out for a later Undo the Restore.");
			return kFalse;
		}
		KCMCutTableStyleGroups(storyInx.GetData(), storyInx.GetSize(), groups);
		// A story's own INX carries no style groups of its own; the document's snapshot has them, and
		// with them the table lands on the document's EXISTING styles of those names (KCMTableSnippet.h).
		if (groups.empty())
		{
			const KCMResourceBytes* const snap = KCMTargetSnapshotBytes();
			if (snap != nil)
				KCMCutTableStyleGroups(snap->Bytes(), snap->Size(), groups);
		}
		KCMBuildTableSnippet(liveTableXml, groups, redo);
	}

	// 3. ★★★TASK START'S TABLE, WITH THE LIVE CELLS' CONTENTS MERGED IN, AS TEXT (2026-09-20, the
	//    user's design: "the snippet is text, and both sides' snippets are in hand - the cells that
	//    changed can be told from the difference"). The shape comes back from Task Start; a cell both
	//    tables have keeps what the reader wrote in it. One snippet, one replacement, and nothing is
	//    written into cells afterwards - which is where every position bug of this feature lived.
	std::string snippet;
	int32 kept = 0;
	{
		const KCMResourceBytes* const origin = KCMOriginBytes();
		std::string olderTableXml, groups, merged;
		if (origin == nil || !KCMCutTableXml(origin->Bytes(), origin->Size(), storyUID, change.fTableOrdinal, olderTableXml))
		{
			outMessage = Refused("the Task Start copy holds no such table.");
			return kFalse;
		}
		if (!KCMMergeTableCells(olderTableXml, liveTableXml, merged, kept))
		{
			merged = olderTableXml;		// the whole table goes back; the sentence below says 0 kept
			kept = 0;
		}
		KCMCutTableStyleGroups(origin->Bytes(), origin->Size(), groups);
		KCMBuildTableSnippet(merged, groups, snippet);
	}

	// 4. THE ONE REPLACEMENT. The scratch document lives OUTSIDE the undo step; the step holds the
	//    replacement alone - nothing is written into cells afterwards any more.
	// ★★(2026-09-20) Making and closing a document INSIDE BeginCommandSequence would put those into
	//   what Ctrl+Z takes back, and the one undo step the user asked for is "the table went back" -
	//   not "and a document appeared". KCMRehydrate makes its copy outside every sequence too.
	// ★**THE SLOT IS ASKED BEFORE THE WRITE**, while the positions are still the ones the write is
	//   about to be made against (the rule KCMStoryList.h:769 states for every replaced record).
	const int32 slot = KCMStoryList::ReplacedSlotFor(nth, liveShape.fAnchorStart);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KCMTargetItemCountGuard guard(db);
	KCMTableShape now;
	bool16 putBack = kFalse;
	{
		KCMScratchDoc scratch;
		std::vector<UIDRef> stories;
		UIDRef tableStory = UIDRef::gNull;
		TextIndex srcStart = 0;
		TextIndex srcEnd = 0;
		PMString why;
		if (!scratch.Open(why) || !scratch.ImportSnippet(snippet, stories, why))
		{
			outMessage = Refused("the Task Start table could not be brought in: ");
			outMessage.Append(why);
		}
		else if (!FindTableStory(stories, tableStory, srcStart, srcEnd))
		{
			outMessage = Refused("the brought-in table could not be read.");
		}
		else
		{
			TableSequence undo(standalone, "Restore Source Text");
			std::vector<KCMTableShape> after;
			if (!CopyTableOver(tableStory, srcStart, srcEnd, db, storyUID,
							   liveShape.fAnchorStart, liveShape.fAnchorEnd, outMessage))
			{
				// outMessage says why; the sequence closes over nothing.
			}
			else if (!KCMReadTableShapes(target, after) || static_cast<size_t>(change.fTableOrdinal) >= after.size())
			{
				outMessage = Refused("the table was put back but could not be read again.");
			}
			else
			{
				now = after[static_cast<size_t>(change.fTableOrdinal)];
				putBack = kTrue;
			}
		}
	}	// the sequence ended above; the scratch document closes HERE, every InterfacePtr on it gone
	if (!putBack)
	{
		AppendReChecks(outMessage, guard);	// said on the way out too: a scratch document must not linger
		return kFalse;
	}

	// 8. **EVERYTHING ALREADY REPLACED FURTHER DOWN THIS STORY SLIDES WITH THE WRITE** - the rule
	//    every restore keeps (KCMStoryRestore.cpp:971-982); nothing else would move those records, and
	//    a record whose position quietly rots is a row whose jump lands in the wrong place.
	//    ⚠A table's cells stand past the body (ITableTextContent.h), so what one shift can carry is the
	//     ANCHOR's delta - which is exactly what the records in the BODY need.
	KCMStoryList::ShiftReplacedChanges(nth, slot, liveShape.fAnchorStart,
									   liveShape.fAnchorEnd - liveShape.fAnchorStart,
									   now.fAnchorEnd - now.fAnchorStart);

	// 9. The record: the row stays, drawn as replaced while the table keeps Task Start's shape.
	change.fRedoSnippet = redo;
	change.fReplacedShapeSig = KCMTableShapeSignature(now);
	change.fReplacedStart = now.fAnchorStart;
	change.fReplacedEnd = now.fAnchorEnd;
	change.fBeforeStart = liveShape.fAnchorStart;
	change.fBeforeEnd = liveShape.fAnchorEnd;
	change.fBeforeTextPre = change.fTextPre;
	change.fBeforeText = change.fText;
	change.fBeforeTextPost = change.fTextPost;
	change.fReplacedTextPre = PMString();
	change.fReplacedText = change.fOtherText;		// Task Start's shape and first words - what stands there now
	change.fReplacedTextPost = PMString();
	// ★THE MARKS THE REPLACED ROW DRAWS: the table standing there now, whole. ⚠The spans the diff made
	//   name CELLS OF THE TABLE THAT IS GONE, and KCMStoryMarkBuild.cpp:206 draws a Table row from
	//   fMarkSpans whether it is live or replaced - left alone they would light a grid of another shape.
	change.fMarkSpans.clear();
	change.fMarkSpans.push_back(KCMTextSpan(now.fAnchorStart, now.fAnchorEnd));

	outMessage = "table put back - ";
	outMessage.AppendNumber(kept);
	outMessage.Append(" cell(s) keep what you wrote in them");
	AppendReChecks(outMessage, guard);

	if (!standalone)
	{
		// A bulk run owns the bookkeeping - including fReplacedCount, which it measures once for all
		// of them after its single re-diff (KCMStoryRestore.cpp:1369).
		if (outDone != nil)
			*outDone = change;
		if (outSlot != nil)
			*outSlot = slot;
		return kTrue;
	}

	const int32 left = KCMStoryDiffRun::RunOne(db, nil, nth);
	// ★The counter this record is measured by, asked AFTER the re-diff has recorded it on the row -
	//   the order the words restore keeps (KCMStoryRestore.cpp:1180). ⚠The Table row's StillReplaced
	//   answers by SHAPE and never reads this; it is kept true so that one record cannot mean two
	//   things depending on what made it.
	change.fReplacedCount = KCMStoryDiffRun::CountForKind(UIDRef(db, storyUID), kKCMStoryAttrNone);
	KCMStoryList::AddReplacedChangeAt(nth, slot, change);
	KCMNotify(kKCMStoryEditsRebuiltMessage);
	if (left >= 0)
	{
		outMessage.Append(" - ");
		outMessage.AppendNumber(left);
		outMessage.Append(" change(s) left in this story");
	}
	outMessage.Append(". Ctrl+Z undoes it.");
	return kTrue;
}

bool16 KCMUndoRestoreTable(int32 nth, int32 which, const KCMStoryChange& change, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	const KCMStoryRow* const row = KCMStoryList::GetRow(nth);
	IDataBase* const db = KCMArmedTargetDB();
	if (row == nil || db == nil || !KCMIsDocDBOpen(db))
	{
		outMessage = Refused("no such row, or the Target document is not open.");
		return kFalse;
	}
	if (change.fRedoSnippet.empty())
	{
		outMessage = Refused("the table that stood here before was not kept, so it cannot be put back - Ctrl+Z still can.");
		return kFalse;
	}
	const UID storyUID = row->fStoryUID;
	InterfacePtr<ITextModel> target(UIDRef(db, storyUID), UseDefaultIID());
	std::vector<KCMTableShape> live;
	if (target == nil || !KCMReadTableShapes(target, live) || change.fTableOrdinal < 0
		|| static_cast<size_t>(change.fTableOrdinal) >= live.size()
		|| KCMTableShapeSignature(live[static_cast<size_t>(change.fTableOrdinal)]) != change.fReplacedShapeSig)
	{
		outMessage = Refused("the table has been edited since it was put back - run Refresh Story Comparison on its row.");
		return kFalse;
	}
	const KCMTableShape liveShape = live[static_cast<size_t>(change.fTableOrdinal)];
	const int32 slot = KCMStoryList::ReplacedSlotOfMerged(nth, which);
	if (slot < 0)
	{
		outMessage = Refused("no such change (the list was rebuilt - right-click the row again).");
		return kFalse;
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KCMTargetItemCountGuard guard(db);
	bool16 putBack = kFalse;
	{
		// The scratch document outside the undo step, and the step inside - the shape KCMRestoreTable
		// explains. ⚠Declared together they would be destroyed the other way round (the document
		// first, while the sequence is still open), which is the very thing being avoided.
		KCMScratchDoc scratch;
		std::vector<UIDRef> stories;
		UIDRef tableStory = UIDRef::gNull;
		TextIndex srcStart = 0;
		TextIndex srcEnd = 0;
		PMString why;
		if (!scratch.Open(why) || !scratch.ImportSnippet(change.fRedoSnippet, stories, why))
		{
			outMessage = Refused("the kept table could not be brought in: ");
			outMessage.Append(why);
		}
		else if (!FindTableStory(stories, tableStory, srcStart, srcEnd))
		{
			outMessage = Refused("the kept table could not be read.");
		}
		else
		{
			TableSequence undo(kTrue, "Undo the Restore");
			if (CopyTableOver(tableStory, srcStart, srcEnd, db, storyUID,
							  liveShape.fAnchorStart, liveShape.fAnchorEnd, outMessage))
				putBack = kTrue;
		}
	}	// the sequence ended above; the scratch document closes here
	if (!putBack)
	{
		AppendReChecks(outMessage, guard);
		return kFalse;
	}

	// What went back in, so that the records standing further down can follow it.
	int32 inserted = liveShape.fAnchorEnd - liveShape.fAnchorStart;
	{
		std::vector<KCMTableShape> after;
		if (KCMReadTableShapes(target, after) && static_cast<size_t>(change.fTableOrdinal) < after.size())
		{
			const KCMTableShape& t = after[static_cast<size_t>(change.fTableOrdinal)];
			inserted = t.fAnchorEnd - t.fAnchorStart;
		}
	}
	// ★**ONLY THE RECORDS AFTER THIS ONE** (slot + 1, the rule KCMStoryRestore.cpp:1751 states), and
	//   ⚠BEFORE this change's own record is taken out, so that the walk sees the list as the write
	//   left it.
	KCMStoryList::ShiftReplacedChanges(nth, slot + 1, liveShape.fAnchorStart,
									   liveShape.fAnchorEnd - liveShape.fAnchorStart, inserted);

	// The record goes - and the kept snippet with it (the user: discard it once it is redone).
	KCMStoryList::RemoveReplacedChangeAt(nth, slot);
	KCMStoryDiffRun::RunOne(db, nil, nth);
	KCMNotify(kKCMStoryEditsRebuiltMessage);
	outMessage = "table put back the way it was.";
	AppendReChecks(outMessage, guard);
	outMessage.Append(" Ctrl+Z undoes it.");
	return kTrue;
}

// End, KCMTableRestore.cpp.
