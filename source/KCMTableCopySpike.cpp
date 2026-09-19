//========================================================================================
//
//  KCMTableCopySpike.cpp -- see the header.
//
//  THE SHAPE OF THE FILE is KCMPdfSpike's: one entry point that runs the steps in order and
//  writes a line about each into the answer, so that a reading says WHICH step failed.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>				// std::sort - the tables in the order their cells begin
#include <sstream>
#include <string>
#include <vector>

#include "ICommand.h"
#include "IDataBase.h"
#include "IRangeData.h"				// the source range, and (as IID_IRANGEDATA2) the destination range
#include "ITableModel.h"
#include "ITextModel.h"
#include "ITextStoryThreadDict.h"		// GetAnchorTextRange - where the table's anchor stands in the body
#include "ITextStoryThreadDictHier.h"	// the walk over a story's tables (SnpIterTableUseDictHier)
#include "IUIDData.h"					// the source story
#include "CmdUtils.h"
#include "CommandID.h"					// IID_IRANGEDATA2
#include "PersistUtils.h"				// ::GetDataBase / ::GetUIDRef
#include "RangeData.h"					// Text::StoryRange
#include "TextChar.h"					// kTextChar_Table / kTextChar_TableContinued
#include "TextID.h"						// kCopyStoryRangeCmdBoss
#include "TextIterator.h"
#include "UIDList.h"

#include "KCMTableCopySpike.h"
#include "KCMCore.h"				// KCMArmedTargetDB - the live document of the armed comparison
#include "KCMOriginCompare.h"		// KCMOriginScopedCopy / KCMOriginToSourceUID - the Task Start copy
#include "KCMStoryList.h"			// KCMStoryList::GetRow - the story a row names

namespace
{

/** One table of a story: its dictionary, where its cells begin (the order KCMTextRead numbers
    tables in), and the anchor range the dictionary reports. */
struct TableAt
{
	UID			fDict;
	TextIndex	fBlockStart;
	TextIndex	fAnchorStart;
	TextIndex	fAnchorEnd;
	bool16		fAnchored;
	int32		fRows;
	int32		fCols;
};

bool EarlierBlock(const TableAt& a, const TableAt& b)
{
	return a.fBlockStart < b.fBlockStart;
}

/** Every table of the story, in KCMTextRead's order (by the start of its cells' block). */
bool16 ListTables(ITextModel* model, std::vector<TableAt>& out)
{
	out.clear();
	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;
	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
		if (dict == nil)
			return kFalse;
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;			// the story's own dictionary

		TableAt at;
		at.fDict = next;
		at.fBlockStart = dict->GetThreadBlockTextRange().Start(nil);
		bool16 anchored = kFalse;
		const Text::StoryRange anchor = dict->GetAnchorTextRange(&anchored);
		at.fAnchored = anchored;
		at.fAnchorStart = anchor.Start(nil);
		at.fAnchorEnd = anchor.End();
		at.fRows = table->GetTotalRows().count;
		at.fCols = table->GetTotalCols().count;
		out.push_back(at);
	}
	std::sort(out.begin(), out.end(), EarlierBlock);
	return kTrue;
}

/** The first index at or after `from` (and before `limit`) that is NOT a kTextChar_TableContinued:
    a table with N rows holds its anchor plus N-1 of those right behind it (KCMTextRead.cpp,
    measured 2026-09-01), and a range that stops at the anchor leaves them behind. */
TextIndex PastContinuations(ITextModel* model, TextIndex from, TextIndex limit)
{
	TextIndex i = from;
	if (i >= limit)
		return i;
	TextIterator iter(model, i);
	while (i < limit)
	{
		const int32 cp = static_cast<int32>((*iter).GetValue());
		if (cp != kTextChar_TableContinued)
			break;
		++i;
		++iter;
	}
	return i;
}

void Describe(std::ostringstream& o, const char* side, const std::vector<TableAt>& tables, int32 ordinal)
{
	const TableAt& t = tables[static_cast<size_t>(ordinal)];
	o << side << " table " << ordinal << ": dict u" << t.fDict.Get()
	  << " anchored=" << (t.fAnchored ? 1 : 0)
	  << " anchor [" << t.fAnchorStart << "," << t.fAnchorEnd << ")"
	  << " cells from " << t.fBlockStart
	  << " " << t.fRows << "x" << t.fCols << "\n";
}

}	// anonymous namespace

void KCMProbeTableCopy(int32 storyRow, int32 tableOrdinal, PMString& out)
{
	std::ostringstream o;

	IDataBase* const targetDB = KCMArmedTargetDB();
	const KCMStoryRow* const row = KCMStoryList::GetRow(storyRow);
	if (targetDB == nil || row == nil || row->fStoryUID == kInvalidUID)
	{
		o << "S1 FAILED: no armed comparison, or no story row " << storyRow << "\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}
	const UID targetStory = row->fStoryUID;
	o << "S1 target story u" << targetStory.Get() << "\n";

	// The copy: a document of its own, closed when this object goes out of scope.
	KCMOriginScopedCopy copy;
	PMString why;
	if (!copy.Open(why))
	{
		o << "S2 FAILED: copy: " << why.GetUTF8String() << "\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}
	IDataBase* const copyDB = copy.DB();
	const UID sourceStory = KCMOriginToSourceUID(copyDB, targetStory);
	o << "S2 copy open; the story there is u" << sourceStory.Get() << "\n";

	InterfacePtr<ITextModel> tModel(UIDRef(targetDB, targetStory), UseDefaultIID());
	InterfacePtr<ITextModel> sModel(UIDRef(copyDB, sourceStory), UseDefaultIID());
	if (tModel == nil || sModel == nil)
	{
		o << "S3 FAILED: text model target=" << (tModel != nil) << " copy=" << (sModel != nil) << "\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}

	std::vector<TableAt> tTables, sTables;
	ListTables(tModel, tTables);
	ListTables(sModel, sTables);
	o << "S3 tables: target " << tTables.size() << ", copy " << sTables.size() << "\n";
	if (tableOrdinal < 0 || static_cast<size_t>(tableOrdinal) >= tTables.size()
		|| static_cast<size_t>(tableOrdinal) >= sTables.size())
	{
		o << "S4 FAILED: no table " << tableOrdinal << " on both sides\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}
	Describe(o, "S4 target", tTables, tableOrdinal);
	Describe(o, "S4 copy  ", sTables, tableOrdinal);

	const TableAt& t = tTables[static_cast<size_t>(tableOrdinal)];
	const TableAt& s = sTables[static_cast<size_t>(tableOrdinal)];

	// Question 3: widen each range over the continuation characters behind the anchor.
	const TextIndex tEnd = PastContinuations(tModel, t.fAnchorEnd, tModel->GetPrimaryStoryThreadSpan());
	const TextIndex sEnd = PastContinuations(sModel, s.fAnchorEnd, sModel->GetPrimaryStoryThreadSpan());
	o << "S5 ranges after the continuations: target [" << t.fAnchorStart << "," << tEnd
	  << ") copy [" << s.fAnchorStart << "," << sEnd << ")\n";

	// Questions 1 and 2: the command itself.
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kCopyStoryRangeCmdBoss));
	if (cmd == nil)
	{
		o << "S6 FAILED: kCopyStoryRangeCmdBoss could not be made\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}
	InterfacePtr<IUIDData> srcStory(cmd, UseDefaultIID());
	InterfacePtr<IRangeData> srcRange(cmd, UseDefaultIID());
	InterfacePtr<IRangeData> dstRange(cmd, IID_IRANGEDATA2);
	if (srcStory == nil || srcRange == nil || dstRange == nil)
	{
		o << "S6 FAILED: the command's data interfaces\n";
		out = PMString(o.str().c_str());
		out.SetTranslatable(kFalse);
		return;
	}
	srcStory->Set(UIDRef(copyDB, sourceStory));
	srcRange->Set(s.fAnchorStart, sEnd);
	cmd->SetItemList(UIDList(UIDRef(targetDB, targetStory)));
	dstRange->Set(t.fAnchorStart, tEnd);

	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	o << "S6 kCopyStoryRangeCmdBoss -> " << err << (err == kSuccess ? " (kSuccess)" : "") << "\n";

	std::vector<TableAt> after;
	ListTables(tModel, after);
	o << "S7 target tables afterwards: " << after.size() << "\n";
	if (static_cast<size_t>(tableOrdinal) < after.size())
		Describe(o, "S7 target", after, tableOrdinal);
	o << "S7 target primary span: " << tModel->GetPrimaryStoryThreadSpan() << "\n";

	out = PMString(o.str().c_str());
	out.SetTranslatable(kFalse);
}

// End, KCMTableCopySpike.cpp.
