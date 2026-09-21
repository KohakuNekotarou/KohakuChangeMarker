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
#include "ITextModelCmds.h"		// DeleteCmd - how a Table + is taken out (its anchor goes, and the table with it)
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

/** One undo step for the whole of a table restore (the same shape as KCMStoryRestore's RestoreSequence;
    a bulk run brings its own, and then this makes none).
    ⚠★**THE SCRATCH DOCUMENT MUST NOT BE OPENED INSIDE SOMEBODY ELSE'S SEQUENCE** - that is what the
     bulk run did wrong before 2026-09-20, and it is why the bulk caller opens its Source ahead of
     its own sequence rather than letting this one nest. */
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

/** The table of `shapes` whose own id is `id`, or nil. ★**HOW EVERY TABLE IS FOUND HERE SINCE
	2026-09-20** (the user: "a table has an id too - can that not say which is which?"): a position
	answers about the wrong table as soon as another one is inserted before it. */
const KCMTableShape* TableById(const std::vector<KCMTableShape>& shapes, UID id)
{
	if (id == kInvalidUID)
		return nil;
	for (size_t i = 0; i < shapes.size(); ++i)
		if (shapes[i].fDictUID == id)
			return &shapes[i];
	return nil;
}

/** The one table `after` holds that `before` did not - what a write has just brought in. An import
	always hands out a new id, so the table that arrived names itself by not having been there.
	@param fallbackAt when every id is accounted for (an id CAN be recycled), the table whose anchor
		starts here instead. nil when neither answers. */
const KCMTableShape* TableThatArrived(const std::vector<KCMTableShape>& before,
									  const std::vector<KCMTableShape>& after, TextIndex fallbackAt)
{
	for (size_t a = 0; a < after.size(); ++a)
	{
		bool16 seen = kFalse;
		for (size_t b = 0; b < before.size() && !seen; ++b)
			if (before[b].fDictUID == after[a].fDictUID)
				seen = kTrue;
		if (!seen)
			return &after[a];
	}
	for (size_t a = 0; a < after.size(); ++a)
		if (after[a].fAnchorStart == fallbackAt)
			return &after[a];
	return nil;
}

/** The live table's own XML and the snippet that would put it back - EXPORTED NOW, not cut out of
	anything kept earlier (the reason is in KCMRestoreTable's step 2). kFalse when the story or the
	table could not be written out. */
bool16 KeepLiveTable(IDataBase* db, UID storyUID, UID tableId,
					 std::string& outTableXml, std::string& outSnippet)
{
	outTableXml.clear();
	outSnippet.clear();
	KCMMemXferBytes storyInx;
	std::string groups;
	// ★WITH THE STYLE ROOTS: one small export carries the live table AND the style groups, as fresh
	//   as each other - a cell style MADE since the comparison started is in them.
	if (!KCMExportStoryInx(db, storyUID, storyInx, kTrue)
		|| !KCMCutTableXmlById(storyInx.GetData(), storyInx.GetSize(), storyUID, tableId, outTableXml))
		return kFalse;
	KCMCutTableStyleGroups(storyInx.GetData(), storyInx.GetSize(), groups);
	// A story's own INX carries no style groups of its own, so the comparison's snapshot answers
	// next, and the origin last - which differs only in styles MADE since Task Start.
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
	KCMBuildTableSnippet(outTableXml, groups, outSnippet);
	return kTrue;
}

/** ★★★THE ONE WRITE: `snippet` is brought into a scratch document and its table copied over
	[dstStart, dstEnd) of the Target story. **A destination of no width INSERTS the table** - which is
	how a Table − comes back (2026-09-20).

	★THE SCRATCH DOCUMENT LIVES OUTSIDE THE UNDO STEP, the step holds the copy alone: making and
	closing a document inside BeginCommandSequence would put those into what Ctrl+Z takes back, and
	the step the reader asked for is "the table went back".

	@param before the story's tables as they stood before this call - how the table that arrives is
		recognised (TableThatArrived).
	@param outLabelled "col:row" -> the Task Start cell id the label on that cell carried, read in the
		scratch document and cleared there, so the reader's document never sees a label of ours.
	@param outNow the table as it stands after the write. */
bool16 BringInAndCopy(const std::string& snippet, IDataBase* db, UID storyUID, ITextModel* target,
					  TextIndex dstStart, TextIndex dstEnd, const char* stepName, bool16 ownSequence,
					  const std::vector<KCMTableShape>& before,
					  std::map<std::string, std::string>& outLabelled,
					  KCMTableShape& outNow, PMString& outMessage)
{
	outLabelled.clear();
	bool16 done = kFalse;
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
			outMessage = Refused("the table could not be brought in: ");
			outMessage.Append(why);
		}
		else if (!FindTableStory(stories, tableStory, srcStart, srcEnd, broughtIn))
		{
			outMessage = Refused("the brought-in table could not be read.");
		}
		else
		{
			// The labels, read and then taken off - in that order, and both BEFORE the copy.
			ReadCellLabels(scratch.DB(), broughtIn, outLabelled);
			ClearCellLabels(scratch.DB(), broughtIn);

			TableSequence undo(ownSequence, stepName);
			std::vector<KCMTableShape> after;
			if (!CopyTableOver(tableStory, srcStart, srcEnd, db, storyUID, dstStart, dstEnd, outMessage))
			{
				// outMessage says why; the sequence closes over nothing.
			}
			else if (!KCMReadTableShapes(target, after))
			{
				outMessage = Refused("the table was put back but the story could not be read again.");
			}
			else
			{
				const KCMTableShape* const now = TableThatArrived(before, after, dstStart);
				if (now == nil)
					outMessage = Refused("the table was put back but could not be found again.");
				else
				{
					outNow = *now;
					done = kTrue;
				}
			}
		}
	}	// the sequence ended above; the scratch document closes HERE, every InterfacePtr on it gone
	return done;
}

/** Remove the table standing at [start, end) of the story - its anchor character and the row
	continuations after it. ★**INDESIGN TAKES THE TABLE WITH THE CHARACTER** (measured 2026-09-20 on
	the running application: removing the anchor left tables.length 0 and the story one character
	shorter; an Undo brought the table back with the same id). */
bool16 RemoveTableAt(ITextModel* target, TextIndex start, TextIndex end, const char* stepName,
					 bool16 ownSequence, PMString& outMessage)
{
	const int32 count = end - start;
	if (count <= 0)
	{
		outMessage = Refused("the table stands nowhere that can be removed.");
		return kFalse;
	}
	TableSequence undo(ownSequence, stepName);
	InterfacePtr<ITextModelCmds> cmds(target, UseDefaultIID());
	if (cmds == nil)
	{
		outMessage = Refused("the story would not take a delete command.");
		return kFalse;
	}
	// ★The official shape of a deletion: DeleteCmd, never a ReplaceCmd with nothing to put in
	//   (KCMStoryRestore's KCMCreateWordsWriteCmd states the rule and where it came from).
	InterfacePtr<ICommand> del(cmds->DeleteCmd(start, count));
	if (del == nil || CmdUtils::ProcessCommand(del) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		outMessage = Refused("the table could not be removed.");
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
	if (change.fWhat != KCMStoryChange::kTable)
	{
		outMessage = Refused("this row is not a table.");
		return kFalse;
	}
	const UID storyUID = row->fStoryUID;
	InterfacePtr<ITextModel> target(UIDRef(db, storyUID), UseDefaultIID());
	if (target == nil)
	{
		outMessage = Refused("the story is no longer in the Target document.");
		return kFalse;
	}

	// ★★★THE THREE ROADS (2026-09-20, the user: "I want to be able to put a table that was added or
	//   removed back too"). They differ only in what is written:
	//     Table ≠ (kReplace) - Task Start's table, with the live cells' contents merged in, copied
	//                          over the live one;
	//     Table + (kInsert)  - the table this version added is REMOVED (its anchor deleted);
	//     Table − (kDelete)  - Task Start's table is INSERTED where it stood (a destination of no
	//                          width), the position being Task Start's anchor clamped to the story.
	const bool16 removing = (change.fKind == KCMStoryChange::kInsert) ? kTrue : kFalse;
	const bool16 bringingBack = (change.fKind == KCMStoryChange::kDelete) ? kTrue : kFalse;

	// 1. The story's tables as they stand NOW, and the checks that this row still describes them.
	//    ⚠**FOUND BY ID, NOT BY POSITION** - see TableById.
	std::vector<KCMTableShape> live;
	if (!KCMReadTableShapes(target, live))
	{
		outMessage = Refused("the story's tables could not be read.");
		return kFalse;
	}
	const KCMTableShape* const standing = TableById(live, change.fTableId);
	if (!bringingBack)
	{
		if (standing == nil)
		{
			outMessage = Refused("the table is no longer in this story - run Refresh Story Comparison on its row.");
			return kFalse;
		}
		if (KCMTableShapeSignature(*standing) != change.fShapeSigAfter)
		{
			outMessage = Refused("the table has been edited since the comparison - run Refresh Story Comparison on its row.");
			return kFalse;
		}
	}
	else
	{
		if (change.fSourceTableId == kInvalidUID)
		{
			outMessage = Refused("this comparison cannot name Task Start's table by its id, so it cannot bring it back.");
			return kFalse;
		}
		// ⚠Already there: the story has been put back some other way since the comparison read it.
		if (TableById(live, change.fSourceTableId) != nil)
		{
			outMessage = Refused("that table is in the story already - run Refresh Story Comparison on its row.");
			return kFalse;
		}
	}
	const KCMTableShape liveShape = (standing != nil) ? *standing : KCMTableShape();

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
	// ⚠**TABLE − HAS NOTHING TO KEEP**: no table stands here, and its Undo the Restore removes the one
	//   this restore is about to insert.
	std::string redo;
	std::string liveTableXml;
	if (!bringingBack && !KeepLiveTable(db, storyUID, change.fTableId, liveTableXml, redo))
	{
		outMessage = Refused("the table could not be written out for a later Undo the Restore.");
		return kFalse;
	}

	// 3. ★★★TASK START'S TABLE, WITH THE LIVE CELLS' CONTENTS MERGED IN, AS TEXT (2026-09-20, the
	//    user's design: "the snippet is text, and both sides' snippets are in hand - the cells that
	//    changed can be told from the difference"). The shape comes back from Task Start; a cell both
	//    tables have keeps what the reader wrote in it. One snippet, one replacement, and nothing is
	//    written into cells afterwards - which is where every position bug of this feature lived.
	// ⚠**TABLE + PUTS NOTHING IN**: the table this version added is simply removed.
	std::string snippet;
	int32 kept = 0;
	std::string how;
	if (!removing)
	{
		const KCMResourceBytes* const origin = KCMOriginBytes();
		std::string olderTableXml, groups, merged;
		if (origin == nil || change.fSourceTableId == kInvalidUID
			|| !KCMCutTableXmlById(origin->Bytes(), origin->Size(), storyUID, change.fSourceTableId, olderTableXml))
		{
			outMessage = Refused("the Task Start copy holds no such table.");
			return kFalse;
		}
		if (bringingBack)
		{
			// Nothing stands here, so there are no cells of the reader's to keep - Task Start's table
			// goes in exactly as it was.
			merged = olderTableXml;
			how = "the table was not in this version";
		}
		else
		{
			const std::map<std::string, std::string>* const wasTaskStart =
				KCMStorySnapshotGetCellIds(storyUID, change.fTableId);
			// ★AND WHETHER THE LIVE IDS MEAN ANYTHING AT ALL (2026-09-20 evening, found on the running
			//   application): a table an import has written carries ids that import handed out, and when
			//   no translation survives - which is what an Undo the Restore leaves behind - they must not
			//   vote. KCMTableSnippet.h says what went wrong while they did.
			const bool16 staleIds = KCMStorySnapshotTableWasImported(storyUID, change.fTableId);
			if (!KCMMergeTableCells(olderTableXml, liveTableXml, merged, kept, how, wasTaskStart, staleIds))
			{
				merged = olderTableXml;		// the whole table goes back; the sentence below says 0 kept
				kept = 0;
				how = "the Task Start table could not be walked";
			}
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
	// ★WHERE THE WRITE GOES. For a table that stands here, its own anchor range. For a table that is
	//   coming BACK, Task Start's anchor position - clamped to the story, because the body may have
	//   grown or shrunk since (the user's "plan A": the surrounding text's own differences are rows
	//   of their own, so putting those back first makes this land exactly).
	TextIndex writeAt = liveShape.fAnchorStart;
	TextIndex writeTo = liveShape.fAnchorEnd;
	const char* placedBy = "Task Start's own position";
	if (bringingBack)
	{
		// ★★★**MEASURED FROM THE TABLE THAT STOOD BEFORE IT** (2026-09-20, found on the running
		//   application - KCMStoryList.h, fPrevTableId, states the measurement). Task Start's anchor
		//   alone is right only while no OTHER table has changed; a row added to an earlier table, or
		//   a table inserted before this one, moves every later anchor, and those are folded into
		//   Table rows that say nothing about the body's length. Measured: anchor 12 landed inside
		//   the word "two".
		writeAt = change.fTargetStart;
		if (change.fPrevTableId != kInvalidUID && change.fGapFromPrev >= 0)
		{
			for (size_t i = 0; i < live.size(); ++i)
				if (KCMStorySnapshotTranslateTableId(storyUID, live[i].fDictUID) == change.fPrevTableId)
				{
					writeAt = live[i].fAnchorEnd + change.fGapFromPrev;
					placedBy = "after the table before it";
					break;
				}
		}
		else if (change.fGapFromPrev >= 0)
		{
			writeAt = change.fGapFromPrev;		// no table stood before it: from the story's start
			placedBy = "from the start of the story";
		}
		const TextIndex total = target->TotalLength();
		if (writeAt > total)
			writeAt = total;
		if (writeAt < 0)
			writeAt = 0;
		writeTo = writeAt;			// no width: the copy INSERTS
	}

	const int32 slot = KCMStoryList::ReplacedSlotFor(nth, writeAt);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KCMTargetItemCountGuard guard(db);
	KCMTableShape now;
	std::map<std::string, std::string> labelledAs;	// "col:row" -> the Task Start id that cell had
	const bool16 putBack = removing
		? RemoveTableAt(target, writeAt, writeTo, "Restore Source Text", standalone, outMessage)
		: BringInAndCopy(snippet, db, storyUID, target, writeAt, writeTo, "Restore Source Text",
						 standalone, live, labelledAs, now, outMessage);
	if (!putBack)
	{
		AppendReChecks(outMessage, guard);	// said on the way out too: a scratch document must not linger
		return kFalse;
	}

	// 7a. ★**THIS TABLE HAS NOW BEEN THROUGH AN IMPORT** - said BEFORE anything below can fail, because
	//     it is true the moment the copy landed and it is what stops the next restore from trusting
	//     the ids that import handed out (KCMStorySnapshot.h).
	// ★★★**AND WHICH TASK START TABLE IT IS** (2026-09-20, the user: "if you bring a table in from a
	//     snippet its id changes - is putting it back still all right?"). It is, because of this line:
	//     the table standing there now carries an id Task Start never saw, and without the translation
	//     the next comparison would call it a table added here and Task Start's one removed.
	//     ⚠Nothing to say for a removal - there is no table to name.
	if (!removing)
	{
		KCMStorySnapshotMarkTableImported(storyUID, now.fDictUID);
		KCMStorySnapshotPutTableId(storyUID, now.fDictUID, change.fSourceTableId);
	}
	else
	{
		// ⚠**AND WHAT WAS KNOWN ABOUT THE TABLE JUST TAKEN OUT GOES WITH IT** (found re-reading this,
		//   2026-09-20). Ids ARE recycled - measured for cells the same day - so an entry left behind
		//   for a table that no longer exists is an answer waiting to be given about somebody else.
		KCMStorySnapshotDropTableId(storyUID, change.fTableId);
		KCMStorySnapshotDropCellIds(storyUID, change.fTableId);
	}

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
			&& KCMCutTableXmlById(after.GetData(), after.GetSize(), storyUID, now.fDictUID, afterTableXml))
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
				KCMStorySnapshotPutCellIds(storyUID, now.fDictUID, wasTaskStart);
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	// 8. **EVERYTHING ALREADY REPLACED FURTHER DOWN THIS STORY SLIDES WITH THE WRITE** - the rule
	//    every restore keeps (KCMStoryRestore.cpp:971-982); nothing else would move those records, and
	//    a record whose position quietly rots is a row whose jump lands in the wrong place.
	//    ⚠A table's cells stand past the body (ITableTextContent.h), so what one shift can carry is the
	//     ANCHOR's delta - which is exactly what the records in the BODY need.
	const int32 wasWide = removing ? (writeTo - writeAt)
					   : bringingBack ? 0
					   : (liveShape.fAnchorEnd - liveShape.fAnchorStart);
	const int32 nowWide = removing ? 0 : (now.fAnchorEnd - now.fAnchorStart);
	KCMStoryList::ShiftReplacedChanges(nth, slot, writeAt, wasWide, nowWide);

	// 9. The record: the row stays, drawn as replaced while what the restore did is still standing.
	change.fRedoSnippet = redo;
	// ★**THE TABLE THE RESTORE LEFT, BY ITS ID** - kInvalidUID for a removal, which is how
	//   StillReplaced knows to ask whether the table is still absent rather than still there.
	change.fReplacedTableId = removing ? kInvalidUID : now.fDictUID;
	change.fReplacedShapeSig = removing ? std::string() : KCMTableShapeSignature(now);
	change.fReplacedStart = removing ? writeAt : now.fAnchorStart;
	change.fReplacedEnd = removing ? writeAt : now.fAnchorEnd;
	change.fBeforeStart = writeAt;
	change.fBeforeEnd = writeTo;
	change.fBeforeTextPre = change.fTextPre;
	change.fBeforeText = change.fText;
	change.fBeforeTextPost = change.fTextPost;
	change.fReplacedTextPre = PMString();
	change.fReplacedText = change.fOtherText;		// Task Start's shape and first words - what stands there now
	change.fReplacedTextPost = PMString();
	// ★THE SPANS THE REPLACED ROW CARRIES: the table standing there now, whole - or, for a removal, the
	//   caret where it stood. ⚠The spans the diff made name CELLS OF THE TABLE THAT IS GONE, and the
	//   JUMP reads them off the record whether it is live or replaced (KCMStoryJump's tableCorner,
	//   through the facade's GetChangeMarkSpan) - left alone they would aim at a grid of another shape,
	//   or at cells that no longer exist at all.
	//   ⚠**THE STANDING MARKS DO NOT READ THEM WHILE THE ROW IS REPLACED**: KCMStoryMarkBuild skips a
	//    change the reader has taken in before it ever asks what kind it is. And once they are written
	//    here, KCMStoryList's ShiftReplacedChanges keeps them abreast of later writes in this story,
	//    the same as the record's own positions.
	change.fMarkSpans.clear();
	change.fMarkSpans.push_back(removing ? KCMTextSpan(writeAt, writeAt)
										 : KCMTextSpan(now.fAnchorStart, now.fAnchorEnd));

	if (removing)
	{
		outMessage = "the table this version added has been taken out";
	}
	else if (bringingBack)
	{
		outMessage = "the table Task Start had is back (placed ";
		outMessage.Append(placedBy);		// which measurement decided where - see KCMStoryList.h
		outMessage.Append(")");
	}
	else
	{
		outMessage = "table put back - ";
		outMessage.AppendNumber(kept);
		outMessage.Append(" cell(s) keep what you wrote in them (");
		outMessage.Append(how.c_str());		// which evidence paired the two tables' cells
		outMessage.Append(")");
	}
	AppendReChecks(outMessage, guard);

	if (!standalone)
	{
		// A bulk run owns the bookkeeping - including fReplacedCount, which it measures once for all
		// of them after its single re-diff (KCMStoryRestore.cpp's BulkRun).
		if (outDone != nil)
			*outDone = change;
		if (outSlot != nil)
			*outSlot = slot;
		return kTrue;
	}

	const int32 left = KCMStoryDiffRun::RunOne(db, nil, nth);
	// ★The counter this record is measured by, asked AFTER the re-diff has recorded it on the row -
	//   the order the words restore keeps (RestoreOne's own tail). ⚠The Table row's StillReplaced
	//   answers by the SHAPE of the table rather than by this count - but it does read it: a record
	//   whose count is 0 is "never replaced" for every kind alike, before the kinds are told apart.
	//   So recording it truly is what keeps a Table row from being read as one that was never written.
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
	// The three roads again, mirrored: a Table + was REMOVED, so its undo brings that table back from
	// the snippet the restore kept; a Table − was INSERTED, so its undo takes that table out again;
	// a Table ≠ was replaced, so its undo replaces it the other way round.
	const bool16 removing = (change.fKind == KCMStoryChange::kInsert) ? kTrue : kFalse;
	const bool16 bringingBack = (change.fKind == KCMStoryChange::kDelete) ? kTrue : kFalse;
	if (!bringingBack && change.fRedoSnippet.empty())
	{
		outMessage = Refused("the table that stood here before was not kept, so it cannot be put back - Ctrl+Z still can.");
		return kFalse;
	}
	const UID storyUID = row->fStoryUID;
	InterfacePtr<ITextModel> target(UIDRef(db, storyUID), UseDefaultIID());
	std::vector<KCMTableShape> live;
	if (target == nil || !KCMReadTableShapes(target, live))
	{
		outMessage = Refused("the story is no longer in the Target document.");
		return kFalse;
	}

	// ★IS WHAT THE RESTORE DID STILL STANDING? For a removal that means the table is still absent;
	//   for the other two, that the table it left is there with the shape it left.
	KCMTableShape liveShape;
	if (removing)
	{
		if (TableById(live, change.fTableId) != nil)
		{
			outMessage = Refused("that table is back in the story already - run Refresh Story Comparison on its row.");
			return kFalse;
		}
	}
	else
	{
		const KCMTableShape* const standing = TableById(live, change.fReplacedTableId);
		if (standing == nil || KCMTableShapeSignature(*standing) != change.fReplacedShapeSig)
		{
			outMessage = Refused("the table has been edited since it was put back - run Refresh Story Comparison on its row.");
			return kFalse;
		}
		liveShape = *standing;
	}
	const int32 slot = KCMStoryList::ReplacedSlotOfMerged(nth, which);
	if (slot < 0)
	{
		outMessage = Refused("no such change (the list was rebuilt - right-click the row again).");
		return kFalse;
	}

	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KCMTargetItemCountGuard guard(db);
	KCMTableShape now;
	std::map<std::string, std::string> ignored;	// (the redo snippet carries no labels: it is the LIVE table)
	TextIndex writeAt = liveShape.fAnchorStart;
	TextIndex writeTo = liveShape.fAnchorEnd;
	bool16 putBack = kFalse;
	if (removing)
	{
		// Back where it stood - a destination of no width, so the copy inserts.
		writeAt = change.fReplacedStart;
		const TextIndex total = target->TotalLength();
		if (writeAt > total)
			writeAt = total;
		if (writeAt < 0)
			writeAt = 0;
		writeTo = writeAt;
		putBack = BringInAndCopy(change.fRedoSnippet, db, storyUID, target, writeAt, writeTo,
								 "Undo the Restore", kTrue, live, ignored, now, outMessage);
	}
	else if (bringingBack)
	{
		putBack = RemoveTableAt(target, writeAt, writeTo, "Undo the Restore", kTrue, outMessage);
	}
	else
	{
		putBack = BringInAndCopy(change.fRedoSnippet, db, storyUID, target, writeAt, writeTo,
								 "Undo the Restore", kTrue, live, ignored, now, outMessage);
	}
	if (!putBack)
	{
		AppendReChecks(outMessage, guard);
		return kFalse;
	}

	// What went back in, so that the records standing further down can follow it.
	const int32 inserted = bringingBack ? 0 : (now.fAnchorEnd - now.fAnchorStart);
	// ★**ONLY THE RECORDS AFTER THIS ONE** (slot + 1, the rule KCMStoryRestore.cpp:1751 states), and
	//   ⚠BEFORE this change's own record is taken out, so that the walk sees the list as the write
	//   left it.
	KCMStoryList::ShiftReplacedChanges(nth, slot + 1, writeAt, writeTo - writeAt, inserted);

	// ★AND WHAT THE RESTORE LEARNED ABOUT THAT TABLE GOES WITH IT: the table standing there now is
	//   the LIVE one again, brought in by another import, so its cells have yet another set of ids
	//   and both maps would be describing a table that is gone (2026-09-20).
	// ⚠★★★**BUT THE TABLE IS STILL A TABLE AN IMPORT HAS WRITTEN**, and that has to be said out loud
	//   here, because dropping the map alone is exactly what let the NEXT restore pair Task Start's
	//   cells with a row that never was theirs (measured the same evening - KCMStorySnapshot.h).
	if (change.fReplacedTableId != kInvalidUID)
	{
		KCMStorySnapshotDropCellIds(storyUID, change.fReplacedTableId);
		KCMStorySnapshotDropTableId(storyUID, change.fReplacedTableId);
	}
	if (now.fDictUID != kInvalidUID)
	{
		KCMStorySnapshotMarkTableImported(storyUID, now.fDictUID);
		// ★★★**AND THE TABLE THAT HAS JUST COME BACK IS STILL TASK START'S TABLE** (found re-reading
		//   this before the live run, 2026-09-20). The undo brings the LIVE table in through another
		//   import, so it too arrives with an id Task Start never saw - and without this line the next
		//   comparison would pair nothing with Task Start's table and show the story as "a table added
		//   here, and Task Start's one missing" where it had shown one shape change.
		//   ⚠Nothing to record when Task Start had no such table at all: a Table + that was taken out
		//    and put back is, rightly, a table this version alone has.
		if (change.fSourceTableId != kInvalidUID)
			KCMStorySnapshotPutTableId(storyUID, now.fDictUID, change.fSourceTableId);
	}

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
