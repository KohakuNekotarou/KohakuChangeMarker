//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryCellBases.h for what this is for and why the snippet's own order cannot be used.
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
#include "KCMSnippetText.h"
#include "KCMStoryCellBases.h"

namespace
{

/* TableAt
   One table of the story: the dictionary that holds its cells, and where its own character
   stands.

   @warning a UID is kept rather than the interface. InterfacePtr has no copy semantics worth
    putting in a vector, and the interface is cheap to ask for again at the one place it is used.
*/
struct TableAt
{
	UID			fDictUID;
	TextIndex	fAnchor;
	TextIndex	fBlockStart;
};

/* EarlierBlock
   The order the document itself keeps its tables in.

   **WHY THE THREAD BLOCK AND NOT THE ANCHOR.** Sorting by the anchor is sorting by raw
   TextIndex, and that is the wrong order the moment a table stands inside another one: a
   nested table's anchor is inside its parent's CELLS, which are laid out after the whole of the
   body -- so a second top-level table, anchored a few characters into the body, sorts BEFORE it,
   while the reader numbered them the other way round (a table is numbered where it is written,
   and a nested one is written inside its parent).
   The thread blocks are the order wanted, and the SDK says so: "The location of the
     dictionary's thread block is determined by the location of the dictionary's anchor relative
     to other dictionaries in the same and other thread blocks. Determining this relative
     location is the job of the ITextStoryThreadDictHier." (ITextStoryThreadDict.h, at
     GetThreadBlockTextRange). That relative order is depth-first, which is the order the XML is
     written in.
   @warning it is CHECKED and not trusted: every table's anchor is compared with the position the
    reader worked out for it below, so a wrong order cannot pass silently.
*/
bool16 EarlierBlock(const TableAt& a, const TableAt& b)
{
	return a.fBlockStart < b.fBlockStart;
}

/* CheckNestedAnchor
   One table's first character, as the reader worked it out, against the document's own answer.

   @warning it also MARKS the table as accounted for. The two go together: a table whose position
    nobody ever worked out is exactly as dangerous as one whose position disagrees -- its cells
    would be placed from an ordering that nothing had checked -- and keeping the two facts in one
    place is what stops the second from being forgotten.
*/
bool16 CheckNestedAnchor(const std::vector<TableAt>& tables, std::vector<bool16>& checked,
						 int32 ordinal, TextIndex expected)
{
	const size_t which = static_cast<size_t>(ordinal);
	if (ordinal < 0 || which >= tables.size())
		return kFalse;
	if (tables[which].fAnchor != expected)
		return kFalse;
	checked[which] = kTrue;
	return kTrue;
}

}	// anonymous namespace

bool16 KCMResolveParagraphPositions(const UIDRef& storyRef,
									  const std::vector<std::string>& paragraphs,
									  const std::vector<KCMParaAttrs>& attrs,
									  std::vector<int32>& outStarts)
{
	// The body first - the one part the snippet and the document agree about. Cells come back as
	// -1, and the tables' own characters come back in `anchors` to be checked below.
	std::vector<KCMTableAnchor> anchors;
	KCMSnippetText::BodyParagraphStarts(paragraphs, attrs, outStarts, anchors);

	// **EVERY TABLE, NESTED ONES INCLUDED.** A table inside a table used to be refused here; now
	//   it is read like any other, because its own characters are charged to the CELL it stands
	//   in and its cells are asked of the document the same way.
	const int32 tableCount = KCMSnippetText::TableCount(attrs);

	bool16 anyCell = kFalse;
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (attrs[i].IsCell())
		{
			anyCell = kTrue;
			break;
		}
	}

	// No cells: nothing to ask the document. @warning but a story with a table character and no
	// cell paragraph means the reader missed something, and its positions cannot be trusted either.
	if (!anyCell)
		return (tableCount == 0);

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
		at.fBlockStart = dict->GetThreadBlockTextRange().Start(nil);
		tables.push_back(at);
	}

	// In the order the document lays their cells out, which is the order the reader numbers them
	//   in -- see EarlierBlock for why the anchor will not do.
	std::sort(tables.begin(), tables.end(), EarlierBlock);

	// **REFUSE (1a):** the two do not even agree about how many tables there are. Nothing below
	//   could be trusted, and a wrong position is indistinguishable from a right one afterwards.
	if (tables.size() != static_cast<size_t>(tableCount))
		return kFalse;

	// **REFUSE (1b):** ...or about where the BODY's tables stand. This is also what catches the
	//   story shapes the body walk does not understand -- two tables sharing one boundary, where
	//   nothing says how the characters divide between them, so no anchor is reported at all.
	//   @warning a NESTED table is not checked here -- where its cell's text sits is not known
	//     until the document has been asked. It is checked below, on the same terms, once it is.
	std::vector<bool16> anchorChecked(tables.size(), kFalse);
	for (size_t a = 0; a < anchors.size(); ++a)
	{
		const size_t which = static_cast<size_t>(anchors[a].fOrdinal);
		if (anchors[a].fOrdinal < 0 || which >= tables.size())
			return kFalse;
		if (tables[which].fAnchor != anchors[a].fIndex)
			return kFalse;
		anchorChecked[which] = kTrue;
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

		// **ONE CELL AT A TIME, AND A CELL CAN BE SEVERAL PARAGRAPHS.** A cell holds one for every
		//   Return pressed in it, and a merged cell holds the paragraphs of everything merged into
		//   it. One THREAD holds them all, so they are checked and placed together rather than one
		//   by one.
		//   @warning CLAMPED TO WHAT BOTH LISTS HOLD. CellRunEnd walks the ATTRIBUTES, and `count`
		//     above exists because this function does not assume the two lists are the same length
		//     -- so an unclamped end would write past `outStarts` (which is as long as `paragraphs`).
		size_t runEnd = KCMSnippetText::CellRunEnd(attrs, i);
		if (runEnd > count)
			runEnd = count;

		const size_t which = static_cast<size_t>(attrs[i].fTableOrdinal);
		if (which >= tables.size())
			return kFalse;

		InterfacePtr<ITextStoryThreadDict> dict(db, tables[which].fDictUID, UseDefaultIID());
		if (dict == nil)
			return kFalse;
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			return kFalse;

		// @warning GridAddress is (row, column); the snippet's Name attribute is "column:row". The
		//   two halves were read apart in ExtractParagraphs precisely so they could be put back in
		//   this order here. A merged cell is named by its anchor (top-left) in both, so it needs
		//   nothing special (TableTypes.h, at GridAddress).
		const GridID gridID = table->GetGridID(GridAddress(attrs[i].fCellRow, attrs[i].fCellCol));
		InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));
		if (thread == nil)
			return kFalse;

		int32 threadSpan = -1;
		const TextIndex threadStart = thread->GetTextStart(&threadSpan);

		// **REFUSE (2): THE LENGTH IS CHECKED, NOT TAKEN ON TRUST.** A thread's boundary is always
		//   a carriage return, so a cell's thread is its paragraphs plus one character each (and
		//   ITextStoryThread::GetTextStart says a span is always greater than 0, which is that one
		//   character for an empty cell). If this disagrees, the cell found is not the cell the
		//   snippet meant -- a different table, a different address -- and every position taken from
		//   here would be wrong silently.
		//   THE WHOLE RUN IS MEASURED, not its first paragraph: that is what the thread holds.
		if (threadSpan != KCMSnippetText::CellRunLength(paragraphs, attrs, i, runEnd))
			return kFalse;

		// The thread's own start belongs to the first paragraph; each one after it begins past the
		// one before and its break.
		// **AND PAST ANY TABLE STANDING IN THE CELL**, which is charged to the paragraph it stands
		//   in -- in front of its text (the ordinary shape: a cell whose whole content is a table)
		//   or behind it. This is the same walk BodyParagraphStarts does for the body; the only
		//   difference is where it starts from.
		int32 at = threadStart;
		for (size_t k = i; k < runEnd; ++k)
		{
			// **REFUSE (3): A NESTED TABLE'S POSITION IS CHECKED TOO.** The body's tables were checked
			//   above against the anchors the reader worked out; this is the same check for the ones
			//   the reader could not place until now. So every table in the story has had its position
			//   agreed by both sides before any cell of it is placed, which is what makes the ORDER the
			//   tables were sorted into safe to rely on.
			if (attrs[k].fLeadingTables == 1
				&& !CheckNestedAnchor(tables, anchorChecked, attrs[k].fLeadingTable, at))
				return kFalse;
			at += attrs[k].fLeadingChars;

			outStarts[k] = at;
			at += KCMSnippetText::CountCodePoints(paragraphs[k]) + 1;

			if (attrs[k].fExtraTables == 1
				&& !CheckNestedAnchor(tables, anchorChecked, attrs[k].fExtraTable, at))
				return kFalse;
			at += attrs[k].fExtraChars;
		}

		i = runEnd;
	}

	// **REFUSE (4): A TABLE NOBODY CLAIMED.** Every table the document holds must have been
	//   placed by one side or the other above -- a body anchor, or a cell's. One that was not
	//   means the reader and the document disagree about the SHAPE of the story even though the
	//   counts matched, and the cells of that table would have been aimed from an order nothing
	//   checked.
	for (size_t t = 0; t < anchorChecked.size(); ++t)
	{
		if (!anchorChecked[t])
			return kFalse;
	}

	return kTrue;
}
