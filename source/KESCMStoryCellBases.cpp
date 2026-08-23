//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryCellBases.h for what this is for and why the snippet's own order cannot be used.
//
//  The walk over the story's thread dictionaries is written the way SnpIterTableUseDictHier writes
//  it - the snippet Adobe calls the recommended one, against SnpIterTableStories which its own
//  header calls deprecated. Two things are taken from it: that the hierarchy is walked by starting
//  at the story's own UID and following NextUID, and that a dictionary IS a table exactly when an
//  ITableModel can be got from it.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITableModel.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"
#include "ITextStoryThreadDictHier.h"

// General includes:
#include "TableTypes.h"
#include "UIDRef.h"

#include <algorithm>
#include <string>
#include <vector>

// Project includes:
#include "KESCMSnippetText.h"
#include "KESCMStoryCellBases.h"

namespace
{

/* TableAt
   One table of the story: the dictionary that holds its cells, and where its own character stands.

   ⚠A UID is kept rather than the interface. InterfacePtr has no copy semantics worth putting in a
   vector, and the interface is cheap to ask for again at the one place it is used.
*/
struct TableAt
{
	UID			fDictUID;
	TextIndex	fAnchor;
};

bool16 EarlierAnchor(const TableAt& a, const TableAt& b)
{
	return a.fAnchor < b.fAnchor;
}

}	// anonymous namespace

bool16 KESCMResolveParagraphPositions(const UIDRef& storyRef,
									  const std::vector<std::string>& paragraphs,
									  const std::vector<KESCMParaAttrs>& attrs,
									  std::vector<int32>& outStarts)
{
	// The body first - the one part the snippet and the document agree about. Cells come back as
	// -1, and the tables' own characters come back in `anchors` to be checked below.
	std::vector<int32> anchors;
	KESCMSnippetText::BodyParagraphStarts(paragraphs, attrs, outStarts, anchors);

	bool16 anyCell = kFalse;
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		// ⚠A TABLE INSIDE A TABLE IS REFUSED OUTRIGHT (2026-08-23). Its own character is not
		//   counted by the reader, so such a story never added up and was already being refused by
		//   LengthAgrees - but by accident. Now that cells are placed by asking rather than by
		//   counting, a story could add up and still be placed wrongly, so it is said here instead.
		//   ★The user does use nested tables; this is work outstanding, not a decision.
		if (attrs[i].IsNestedCell())
			return kFalse;
		if (attrs[i].IsCell())
			anyCell = kTrue;
	}

	// No cells: nothing to ask the document. ⚠But a story with a table character and no cell
	// paragraph means the reader missed something, and its positions cannot be trusted either.
	if (!anyCell)
		return anchors.empty();

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kFalse;

	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	// Collect the story's tables. The walk starts at the story's own dictionary (which is not a
	// table) and follows the hierarchy; NextUID answers kInvalidUID when nothing follows.
	std::vector<TableAt> tables;
	for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
		if (dict == nil)
			return kFalse;

		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;			// the story's own dictionary - see SnpIterTableUseDictHier

		bool16 wasAnchored = kFalse;
		const Text::StoryRange anchorRange = dict->GetAnchorTextRange(&wasAnchored);
		if (!wasAnchored)
			return kFalse;

		TableAt at;
		at.fDictUID = next;
		at.fAnchor = anchorRange.Start(nil);
		tables.push_back(at);
	}

	// ★In the order their own characters stand in the story, which is the order the snippet lists
	//   them: the XML walks the body from the top, and a top-level table's character is in the body.
	std::sort(tables.begin(), tables.end(), EarlierAnchor);

	// ★★REFUSE (1): the two do not even agree about how many tables there are, or about where they
	//   stand. Nothing below could be trusted, and a wrong position is indistinguishable from a
	//   right one afterwards. ⇒ This is also what catches the story shapes the body walk does not
	//   understand - two tables in a row, where the second one's character is charged to a CELL of
	//   the first and so never reaches the body count.
	if (tables.size() != anchors.size())
		return kFalse;
	for (size_t t = 0; t < tables.size(); ++t)
	{
		if (tables[t].fAnchor != anchors[t])
			return kFalse;
	}

	// Now ask the document where each cell's text sits.
	const size_t count = (paragraphs.size() < attrs.size()) ? paragraphs.size() : attrs.size();
	for (size_t i = 0; i < count; )
	{
		if (!attrs[i].IsCell())
		{
			++i;
			continue;
		}

		// ★★ONE CELL AT A TIME, AND A CELL CAN BE SEVERAL PARAGRAPHS (2026-08-23). A cell holds
		//   one for every Return pressed in it, and a merged cell holds the paragraphs of
		//   everything merged into it. One THREAD holds them all, so they are checked and placed
		//   together rather than one by one.
		const size_t runEnd = KESCMSnippetText::CellRunEnd(attrs, i);

		const size_t which = static_cast<size_t>(attrs[i].fTableOrdinal);
		if (which >= tables.size())
			return kFalse;

		InterfacePtr<ITextStoryThreadDict> dict(db, tables[which].fDictUID, UseDefaultIID());
		if (dict == nil)
			return kFalse;
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			return kFalse;

		// ⚠GridAddress is (row, column); the snippet's Name attribute is "column:row". The two
		//   halves were read apart in ExtractParagraphs precisely so they could be put back in this
		//   order here. ★A merged cell is named by its anchor (top-left) in both, so it needs
		//   nothing special (TableTypes.h:146-153).
		const GridID gridID = table->GetGridID(GridAddress(attrs[i].fCellRow, attrs[i].fCellCol));
		InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));
		if (thread == nil)
			return kFalse;

		int32 threadSpan = -1;
		const TextIndex threadStart = thread->GetTextStart(&threadSpan);

		// ★★REFUSE (2): THE LENGTH IS CHECKED, NOT TAKEN ON TRUST. A thread's boundary is always a
		//   carriage return, so a cell's thread is its paragraphs plus one character each (and
		//   ITextStoryThread.h says a span is always greater than 0, which is that one character
		//   for an empty cell). If this disagrees, the cell found is not the cell the snippet meant
		//   - a different table, a different address - and every position taken from here would be
		//   wrong silently.
		//   ★THE WHOLE RUN IS MEASURED, not its first paragraph: that is what the thread holds.
		if (threadSpan != KESCMSnippetText::CellRunLength(paragraphs, i, runEnd))
			return kFalse;

		// The thread's own start belongs to the first paragraph; each one after it begins past the
		// one before and its break.
		int32 at = threadStart;
		for (size_t k = i; k < runEnd; ++k)
		{
			outStarts[k] = at;
			at += KESCMSnippetText::CountCodePoints(paragraphs[k]) + 1;
		}

		i = runEnd;
	}

	return kTrue;
}
