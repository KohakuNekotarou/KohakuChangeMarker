//========================================================================================
//
//  KCMTableRestore.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <map>
#include <string>
#include <vector>

#include "ICommand.h"
#include "ICommandSequence.h"
#include "IDataBase.h"
#include "IRangeData.h"				// the source range, and (as IID_IRANGEDATA2) the destination range
#include "IScriptLabel.h"			// the cell label KCM puts on and takes off again
#include "ITableAttrAccessor.h"	// QueryCellAttribute - a cell's label IS a cell attribute
#include "ITableCommands.h"		// ClearCellOverrides - the official way to take one off
#include "ITableModel.h"
#include "ITextModel.h"
#include "IUIDData.h"					// the source story of kCopyStoryRangeCmdBoss
#include "CmdUtils.h"
#include "CommandID.h"					// IID_IRANGEDATA2
#include "ErrorUtils.h"
#include "AttributeBossList.h"			// the list that names the override to remove, by its class
#include "TablesID.h"					// kCellAttrScriptLabelBoss / IID_ISCRIPTLABEL
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
#include "KCMStorySnapshot.h"			// the story as the comparison read it
#include "KCMTableShape.h"
#include "KCMTableSnippet.h"

namespace
{

PMString Refused(const char* what)
{
	PMString s("restore: ");
	s.SetTranslatable(kFalse);
	s.Append(what);
	return s;
}

/** One undo step for the whole of a table restore (the same shape as KCMStoryRestore's
    RestoreSequence). ⚠It could be told to make none, for the bulk items dropped on 2026-09-20 -
    which is also what put the scratch document inside somebody else's sequence. One press, one step. */
class TableSequence
{
public:
	explicit TableSequence(const char* name)
		: fSequence(CmdUtils::BeginCommandSequence("KCMTableRestore"))
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
bool16 FindTableStory(const std::vector<UIDRef>& stories, UIDRef& outStory, TextIndex& outStart, TextIndex& outEnd,
					  KCMTableShape& outShape)
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
		outShape = shapes[outer];		// the labels are read and cleared off THIS table
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

/** ★THE KEY OF THE LABEL KCM PUTS ON THE CELLS OF THE SNIPPET IT BUILDS - and takes off again in the
    scratch document, before one byte is copied out of it (the user's design, 2026-09-20: "make the
    script labels when you make the snippet, and once the frame has been made from the snippet, take
    off the labels KCM put on; then paste"). So the reader's document never carries a mark of ours,
    and whether a copy would have carried one is a question nobody has to answer. */
const char* const kCellLabelKey = "kcmCell";

/** What each anchor cell's label says, as "col:row" -> the id it carries. Empty when none of them
    does - a table put back by a version of this that did not label them, or a failure to label.
    ⚠A cell's script label is a CELL ATTRIBUTE: `kCellAttrScriptLabelBoss` is the SDK's one and only
     implementer of IScriptLabel, which is why it is read through ITableAttrAccessor and not off the
     cell itself. */
void ReadCellLabels(IDataBase* db, const KCMTableShape& shape, std::map<std::string, std::string>& out)
{
	out.clear();
	InterfacePtr<ITableModel> model(db, shape.fDictUID, UseDefaultIID());
	InterfacePtr<ITableAttrAccessor> attrs(model, UseDefaultIID());
	if (attrs == nil)
		return;
	PMString key(kCellLabelKey);
	key.SetTranslatable(kFalse);
	for (size_t i = 0; i < shape.fCells.size(); ++i)
	{
		const GridAddress at(shape.fCells[i].fRow, shape.fCells[i].fCol);
		InterfacePtr<IScriptLabel> label(
			(IScriptLabel*)attrs->QueryCellAttribute(at, kCellAttrScriptLabelBoss, IID_ISCRIPTLABEL));
		if (label == nil)
			continue;
		const PMString said = label->GetTag(key);
		if (said.IsEmpty())
			continue;
		char address[32];
		std::snprintf(address, sizeof(address), "%d:%d",
					  static_cast<int>(shape.fCells[i].fCol), static_cast<int>(shape.fCells[i].fRow));
		out[address] = said.GetPlatformString();
	}
}

/** Take our label off every cell of the table. ⚠Done in the SCRATCH document and OUTSIDE the undo
    step, like everything else that happens to it - the reader's Ctrl+Z is "the table went back", not
    "and some labels were tidied up". */
void ClearCellLabels(IDataBase* db, const KCMTableShape& shape)
{
	InterfacePtr<ITableModel> model(db, shape.fDictUID, UseDefaultIID());
	InterfacePtr<ITableCommands> cmds(model, UseDefaultIID());
	InterfacePtr<IScriptLabel> blank((IScriptLabel*)::CreateObject(kCellAttrScriptLabelBoss, IID_ISCRIPTLABEL));
	if (model == nil || cmds == nil || blank == nil || shape.fRows <= 0 || shape.fCols <= 0)
		return;
	// The list names the override to remove BY ITS CLASS - the boss itself carries nothing
	// (ITableCommands.h: "a list of boss objects specifying by their ClassID the override to remove").
	AttributeBossList attrs;
	attrs.ApplyAttribute(blank, kCellAttrScriptLabelBoss);
	// ⚠THE WHOLE TABLE, BY ITS OWN RANGES. The four-number GridArea takes a bottom row and a right
	//   column, and whether either is inclusive is not written down anywhere I could find - and a
	//   row left out here is a label left on a cell that is about to be copied into the reader's
	//   document. RowRange/ColRange are start+count, which cannot be read two ways.
	const GridArea whole(model->GetTotalRows(), model->GetTotalCols());
	cmds->ClearCellOverrides(whole, &attrs);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// a scratch document's tidy-up is never the caller's error
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

bool16 KCMRestoreTable(int32 nth, const KCMStoryChange& changeIn, PMString& outMessage)
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
		// ★WITH THE STYLE ROOTS (2026-09-20): one small export then carries both the live table AND the
		//   style groups, and the groups are as fresh as the table - a cell style MADE since the
		//   comparison started is in them. The kept snippet below stays as the fallback.
		if (!KCMExportStoryInx(db, storyUID, storyInx, kTrue)
			|| !KCMCutTableXml(storyInx.GetData(), storyInx.GetSize(), storyUID, change.fTableOrdinal, liveTableXml))
		{
			outMessage = Refused("the table could not be written out for a later Undo the Restore.");
			return kFalse;
		}
		KCMCutTableStyleGroups(storyInx.GetData(), storyInx.GetSize(), groups);
		// ★THE STYLE GROUPS COME FROM THE SNIPPET THE DIFF KEPT FOR THIS TABLE (2026-09-20, the user:
		//   "stop making the Target's whole IDML when the comparison starts - prepare a snippet for
		//   the tables that changed, and only for those"). Without them the table lands on
		//   "[No table style]" instead of the document's existing style of that name
		//   (KCMTableSnippet.h). A story's own INX carries none, so they are taken from the kept
		//   snippet; the origin is the last resort, and differs only in styles MADE since Task Start.
		if (groups.empty())
		{
			const std::string* const seen = KCMStorySnapshotPeek(storyUID);
			if (seen != nil)
				KCMCutTableStyleGroups(seen->c_str(), seen->size(), groups);
		}
		if (groups.empty())
		{
			const KCMResourceBytes* const origin = KCMOriginBytes();
			if (origin != nil)
				KCMCutTableStyleGroups(origin->Bytes(), origin->Size(), groups);
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
	std::string how;
	{
		const KCMResourceBytes* const origin = KCMOriginBytes();
		std::string olderTableXml, groups, merged;
		if (origin == nil || !KCMCutTableXml(origin->Bytes(), origin->Size(), storyUID, change.fTableOrdinal, olderTableXml))
		{
			outMessage = Refused("the Task Start copy holds no such table.");
			return kFalse;
		}
		const std::map<std::string, std::string>* const wasTaskStart =
			KCMStorySnapshotGetCellIds(storyUID, change.fTableOrdinal);
		// ★AND WHETHER THE LIVE IDS MEAN ANYTHING AT ALL (2026-09-20 evening, found on the running
		//   application): a table an import has written carries ids that import handed out, and when
		//   no translation survives - which is what an Undo the Restore leaves behind - they must not
		//   vote. KCMTableSnippet.h says what went wrong while they did.
		const bool16 staleIds = KCMStorySnapshotTableWasImported(storyUID, change.fTableOrdinal);
		if (!KCMMergeTableCells(olderTableXml, liveTableXml, merged, kept, how, wasTaskStart, staleIds))
		{
			merged = olderTableXml;		// the whole table goes back; the sentence below says 0 kept
			kept = 0;
			how = "the Task Start table could not be walked";
		}
		KCMCutTableStyleGroups(origin->Bytes(), origin->Size(), groups);
		// ★EVERY CELL LABELLED WITH ITS TASK START ID, so that the table can still be recognised cell
		//   by cell after the import has repacked the ids. Read and taken off again in the scratch
		//   document below; the reader's document never sees one (KCMTableSnippet.h).
		KCMLabelTableCells(merged, kCellLabelKey);
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
	std::map<std::string, std::string> labelledAs;	// "col:row" -> the Task Start id that cell had
	bool16 putBack = kFalse;
	{
		KCMScratchDoc scratch;
		std::vector<UIDRef> stories;
		UIDRef tableStory = UIDRef::gNull;
		TextIndex srcStart = 0;
		TextIndex srcEnd = 0;
		KCMTableShape broughtIn;
		PMString why;
		if (!scratch.Open(why) || !scratch.ImportSnippet(snippet, stories, why))
		{
			outMessage = Refused("the Task Start table could not be brought in: ");
			outMessage.Append(why);
		}
		else if (!FindTableStory(stories, tableStory, srcStart, srcEnd, broughtIn))
		{
			outMessage = Refused("the brought-in table could not be read.");
		}
		else
		{
			// ★THE LABELS, READ AND THEN TAKEN OFF - in that order, and both BEFORE the copy. What
			//   they say is "the cell standing at this address was Task Start's cell <id>", which is
			//   the one thing the import throws away (it repacks the ids). Taking them off here is
			//   what keeps them out of the reader's document.
			ReadCellLabels(scratch.DB(), broughtIn, labelledAs);
			ClearCellLabels(scratch.DB(), broughtIn);

			TableSequence undo("Restore Source Text");
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

	// 7a. ★**THIS TABLE HAS NOW BEEN THROUGH AN IMPORT** - said BEFORE anything below can fail, because
	//     it is true the moment the copy landed and it is what stops the next restore from trusting
	//     the ids that import handed out (KCMStorySnapshot.h).
	KCMStorySnapshotMarkTableImported(storyUID, change.fTableOrdinal);

	// 7b. ★**WHICH TASK START CELL EACH CELL STANDING THERE NOW IS** (2026-09-20, the user's design).
	//     The import repacked the ids, so the table that has just gone in shares none with Task
	//     Start's - and the next comparison would have to pair its cells by what they say. What the
	//     labels said in the scratch document is tied here to the ids the Target's own export gives,
	//     and kept for as long as the comparison lasts.
	//     ⚠Best effort: a failure here costs the NEXT restore its strongest evidence and nothing else,
	//      so it is never a reason to refuse a restore that has already been made.
	if (!labelledAs.empty())
	{
		KCMMemXferBytes after;
		std::string afterTableXml;
		std::map<std::string, std::string> idAt;		// "col:row" -> the id it has now
		if (KCMExportStoryInx(db, storyUID, after)
			&& KCMCutTableXml(after.GetData(), after.GetSize(), storyUID, change.fTableOrdinal, afterTableXml))
		{
			KCMReadTableCellIds(afterTableXml, idAt);
			std::map<std::string, std::string> wasTaskStart;
			for (std::map<std::string, std::string>::const_iterator it = idAt.begin(); it != idAt.end(); ++it)
			{
				const std::map<std::string, std::string>::const_iterator said = labelledAs.find(it->first);
				if (said != labelledAs.end())
					wasTaskStart[it->second] = said->second;
			}
			if (!wasTaskStart.empty())
				KCMStorySnapshotPutCellIds(storyUID, change.fTableOrdinal, wasTaskStart);
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
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
	outMessage.Append(" cell(s) keep what you wrote in them (");
	outMessage.Append(how.c_str());		// which evidence paired the two tables' cells
	outMessage.Append(")");
	AppendReChecks(outMessage, guard);

	const int32 left = KCMStoryDiffRun::RunOne(db, nil, nth);
	// ★The counter this record is measured by, asked AFTER the re-diff has recorded it on the row -
	//   the order the words restore keeps (RestoreOne's own tail). ⚠The Table row's StillReplaced
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
		KCMTableShape broughtIn;		// (the redo snippet carries no labels of ours: it is the LIVE table)
		PMString why;
		if (!scratch.Open(why) || !scratch.ImportSnippet(change.fRedoSnippet, stories, why))
		{
			outMessage = Refused("the kept table could not be brought in: ");
			outMessage.Append(why);
		}
		else if (!FindTableStory(stories, tableStory, srcStart, srcEnd, broughtIn))
		{
			outMessage = Refused("the kept table could not be read.");
		}
		else
		{
			TableSequence undo("Undo the Restore");
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

	// ★AND WHAT THE RESTORE LEARNED ABOUT THOSE CELLS GOES WITH IT: the table standing there now is
	//   the LIVE one again, brought in by another import, so its cells have yet another set of ids
	//   and the map would be describing a table that is gone (2026-09-20).
	// ⚠★★★**BUT THE TABLE IS STILL A TABLE AN IMPORT HAS WRITTEN**, and that has to be said out loud
	//   here, because dropping the map alone is exactly what let the NEXT restore pair Task Start's
	//   cells with a row that never was theirs (measured the same evening - KCMStorySnapshot.h).
	KCMStorySnapshotDropCellIds(storyUID, change.fTableOrdinal);
	KCMStorySnapshotMarkTableImported(storyUID, change.fTableOrdinal);

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
