//========================================================================================
//
//  KCMTextRead.cpp
//
//  See KCMTextRead.h for what this is for and why the snippet's own order cannot be trusted for
//  positions.
//
//  The walk follows SnpInspectTextModel's InspectStoryThreads for its shape: QueryStoryThread
//  hands back one thread at a time, and `position + span` steps to the next one. That single loop
//  covers the body, every table cell and every footnote - which is the whole reason for the
//  migration, because the XML route has to know each of those shapes by name and goes silent on
//  the ones it does not.
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
#include "PMString.h"
#include "TableTypes.h"		// GridAddress, RowRange, ColRange
#include "TextChar.h"		// kTextChar_CR / kTextChar_Table / kTextChar_TableContinued
#include "TextIterator.h"
#include "UIDRef.h"

#include <algorithm>
#include <map>
#include <boost/thread/recursive_mutex.hpp>	// the same shape KCMThreadSafety uses
#include <sstream>

// Project includes:
#include "KCMTextRead.h"

namespace
{

/* TableAt
   One table of the story: the dictionary that holds its cells, and where those cells begin.

   **THE ORDER IS THE THREAD BLOCK'S, NOT THE ANCHOR'S**, and that is not a detail: a nested
   table's anchor sits inside its parent's CELLS - past the whole body - so sorting by anchor puts
   it after a second top-level table that is written before it. The SDK states the rule at
   ITextStoryThreadDict::GetThreadBlockTextRange ("the location of the dictionary's thread block is
   determined by the location of the dictionary's anchor relative to other dictionaries"), and it
   is the order the XML is written in, which is the order KCMSnippetText numbers tables in.
   ⚠KCMStoryCellBases learned this the hard way (see EarlierBlock there); the same rule is kept
    here so that the two routes number the same table the same way while both exist.
*/
struct TableAt
{
	UID			fDictUID;
	TextIndex	fBlockStart;
};

bool16 EarlierBlock(const TableAt& a, const TableAt& b)
{
	return a.fBlockStart < b.fBlockStart;
}

/* CellPlace
   Where one cell's text begins, and which cell it is.

   ★THE DOCUMENT IS ASKED, NOT COUNTED - GetTextStart() on the cell's own thread. That is the whole
   difference from the XML route, which counts down the snippet and then has to check the total
   against the model (and refuses the story when they disagree).
*/
struct CellPlace
{
	TextIndex	fStart;
	int32		fTable;
	int32		fRow;
	int32		fCol;
};

bool16 EarlierCell(const CellPlace& a, const CellPlace& b)
{
	return a.fStart < b.fStart;
}

/* BuildCellIndex
   Asks the document where every cell of every table starts, once per story.

   @return kFalse when a dictionary or a table cannot be opened at all. **A story with no tables is
	   not a failure**: it answers kTrue with an empty index.

   @warning MERGED CELLS ARE VISITED ONCE. IsAnchor is what says so - a merged cell is addressed by
	its anchor (ITableModel.h, at GridAddress), and the covered addresses have no thread of their
	own. Walking them anyway would ask QueryThread for a cell that is not there.
*/
bool16 BuildCellIndex(ITextModel* model, std::vector<CellPlace>& out)
{
	out.clear();

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;		// no dictionary hierarchy at all - nothing but a body

	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	// Collect the tables. The walk starts at the story's own dictionary (which is not a table) and
	// follows the hierarchy; NextUID answers kInvalidUID when nothing follows. The shape is
	// SnpIterTableUseDictHier's, the one Adobe calls recommended.
	std::vector<TableAt> tables;
	for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
		if (dict == nil)
			return kFalse;

		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;		// the story's own dictionary - a dictionary IS a table exactly when
							// an ITableModel can be got from it (SnpIterTableUseDictHier)

		TableAt at;
		at.fDictUID = next;
		at.fBlockStart = dict->GetThreadBlockTextRange().Start(nil);
		tables.push_back(at);
	}

	std::sort(tables.begin(), tables.end(), EarlierBlock);

	for (size_t t = 0; t < tables.size(); ++t)
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, tables[t].fDictUID, UseDefaultIID());
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (dict == nil || table == nil)
			return kFalse;

		// ⚠GridAddress is (row, column) - TableTypes.h. RowRange/ColRange carry start and count;
		//   `start + count` is used rather than End() so that this depends on the two fields the
		//   header shows outright.
		const RowRange rows = table->GetTotalRows();
		const ColRange cols = table->GetTotalCols();

		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		{
			for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			{
				const GridAddress addr(r, c);
				if (!table->IsValid(addr) || !table->IsAnchor(addr))
					continue;

				InterfacePtr<ITextStoryThread> thread(dict->QueryThread(table->GetGridID(addr)));
				if (thread == nil)
					continue;

				CellPlace place;
				place.fStart = thread->GetTextStart();
				place.fTable = static_cast<int32>(t);
				place.fRow = r;
				place.fCol = c;
				out.push_back(place);
			}
		}
	}

	// Sorted so that the walk below can find a thread's place with one pass rather than a search
	// per thread. (Both lists come out in TextIndex order anyway; the sort makes that a fact
	// rather than an assumption about how the dictionaries were laid out.)
	std::sort(out.begin(), out.end(), EarlierCell);
	return kTrue;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
bool16 KCMTextRead::ReadStory(const UIDRef& storyRef,
							  std::vector<std::string>& outParas,
							  std::vector<KCMParaAttrs>& outAttrs,
							  std::vector<int32>& outStarts)
{
	// **EMPTIED FIRST, ALL THREE.** The caller may hand in vectors that already hold the other
	// route's answer (that is exactly what KCMCompareReadRoutes does), and a reader that appended
	// would silently report agreement on a doubled list.
	outParas.clear();
	outAttrs.clear();
	outStarts.clear();

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	std::vector<CellPlace> cells;
	if (!BuildCellIndex(model, cells))
		return kFalse;

	const TextIndex total = model->TotalLength();
	size_t nextCell = 0;

	// ★★★ONE LOOP FOR THE BODY, THE CELLS AND THE FOOTNOTES. QueryStoryThread hands back the
	//   thread containing a position together with where it starts and how long it is; stepping by
	//   `position + span` walks them all in TextIndex order. Nothing here has to know what shapes a
	//   story can contain - which is precisely what the XML route could not manage, since it has to
	//   recognise every element by name and goes silent on the ones it does not (<Footnote>).
	TextIndex position = 0;
	while (position < total)
	{
		int32 span = 0;
		InterfacePtr<const ITextStoryThread> thread(model->QueryStoryThread(position, &position, &span));
		if (thread == nil || span <= 0)
			break;

		const TextIndex threadEnd = position + span;

		// Which place is this? The body unless a cell begins exactly here. The cells are in
		// TextIndex order and so are the threads, so one cursor walks both.
		KCMParaAttrs place;			// the defaults are "body text"
		while (nextCell < cells.size() && cells[nextCell].fStart < position)
			++nextCell;
		if (nextCell < cells.size() && cells[nextCell].fStart == position)
		{
			place.fTableOrdinal = cells[nextCell].fTable;
			place.fCellRow = cells[nextCell].fRow;
			place.fCellCol = cells[nextCell].fCol;
			++nextCell;
		}

		std::string text;
		TextIndex paraStart = position;
		bool16 paraHasCharacters = kFalse;

		TextIterator iter(model, position);
		for (TextIndex i = position; i < threadEnd; ++i, ++iter)
		{
			// ⚠TextIterator's value_type is UTF32TextChar, a CLASS - it does not convert to an
			//   integer on its own. GetValue() is the way out of it.
			const int32 cp = static_cast<int32>((*iter).GetValue());

			// ★A TABLE'S OWN CHARACTERS ARE NOT TEXT, BUT THEY ARE POSITIONS. The model holds
			//   kTextChar_Table for the anchor plus one kTextChar_TableContinued per row after the
			//   first; the snippet holds none of them, so leaving them in would make every
			//   paragraph text differ from the old route's. They still move the index, and a
			//   paragraph standing behind one starts AFTER it - which is where the old route puts
			//   it too (KCMStoryCellBases adds fLeadingChars before recording the start).
			//   @warning they sit exactly at a paragraph boundary (KCMStoryDiffRun says so at
			//     RunSide), never inside a paragraph's own text.
			if (cp == kTextChar_Table || cp == kTextChar_TableContinued)
			{
				if (!paraHasCharacters)
					paraStart = i + 1;
				continue;
			}

			if (cp == kTextChar_CR)
			{
				outParas.push_back(text);
				outAttrs.push_back(place);
				outStarts.push_back(static_cast<int32>(paraStart));
				text.clear();
				paraStart = i + 1;
				paraHasCharacters = kFalse;
				continue;
			}

			// ⚠ONE CODE POINT PER CHARACTER. Every position handed out here is counted the way
			//   InDesign counts text positions - a surrogate pair is ONE TextIndex - and the diff
			//   downstream counts the same way. Encoding a character as two would put the two
			//   counts out of step, and the comparison would quote the right words at the wrong
			//   place.
			KCMSnippetText::AppendUtf8(text, cp);
			paraHasCharacters = kTrue;
		}

		// A thread always ends with a carriage return (ITextStoryThread's own contract, and
		// SnpInspectTextModel checks for it as a corruption test), so the loop normally closes the
		// last paragraph itself. This catches the one that does not - and an empty tail is NOT
		// pushed, or every thread would end with a paragraph nobody wrote.
		if (!text.empty())
		{
			outParas.push_back(text);
			outAttrs.push_back(place);
			outStarts.push_back(static_cast<int32>(paraStart));
		}

		position = threadEnd;
	}

	return kTrue;
}

//----------------------------------------------------------------------------------------
//  The parallel run. See the header for why it exists and why it is off by default.
//
//  **File-scope statics, not function-local ones** - guide vol1-07 names function-local statics
//  as the thing to remove, and KCMThreadSafety follows the same rule for its mutex.
//----------------------------------------------------------------------------------------

static boost::recursive_mutex	sKCMReadCompareMutex;
static bool16					sKCMReadCompareOn = kFalse;
static std::string				sKCMReadCompareReport;

/** How many disagreements are spelt out in full before the report stops describing them.

	★THE COUNT IS NOT CAPPED, ONLY THE DESCRIPTION. "How many differ" and "which ones" are two
	questions, and capping the first is how an instrument stops being able to raise an alarm.
*/
static const size_t kKCMMaxDetails = 12;

//----------------------------------------------------------------------------------------
bool16 KCMStoryReadCompareIsOn()
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	return sKCMReadCompareOn;
}

//----------------------------------------------------------------------------------------
void KCMSetStoryReadCompare(bool16 on)
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	sKCMReadCompareOn = on;

	// **CLEARED EITHER WAY.** A report left over from the last run, read after a re-arm, would be
	// answering about a comparison the reader is no longer looking at.
	sKCMReadCompareReport.clear();
}

//----------------------------------------------------------------------------------------
void KCMGetStoryReadCompareReport(PMString& out)
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);

	const char* const text = sKCMReadCompareReport.empty()
							 ? (sKCMReadCompareOn ? "armed, nothing compared yet" : "off")
							 : sKCMReadCompareReport.c_str();

	// ⚠NOT TRANSLATABLE. It is a diagnostic, and a PMString built from a c-string is treated as a
	//   translation key unless it is told otherwise.
	out.SetCString(text, PMString::kEncodingASCII);
	out.SetTranslatable(kFalse);
}

//----------------------------------------------------------------------------------------
void KCMCompareReadRoutes(const UIDRef& storyRef,
						  const std::vector<std::string>& oldParas,
						  const std::vector<KCMParaAttrs>& oldAttrs,
						  const std::vector<int32>& oldStarts,
						  const char* which)
{
	std::vector<std::string> newParas;
	std::vector<KCMParaAttrs> newAttrs;
	std::vector<int32> newStarts;

	const bool16 read = KCMTextRead::ReadStory(storyRef, newParas, newAttrs, newStarts);

	std::ostringstream line;
	line << which << ": ";

	if (!read)
	{
		line << "NEW ROUTE COULD NOT READ THE STORY (old had " << oldParas.size() << " paragraphs)";
	}
	else
	{
		// ★★★THE TWO ROUTES DO NOT AGREE ON ORDER, AND THAT IS BY DESIGN.
		//   The snippet puts a table's cells where the table STANDS, so the old route reads
		//   body, body, cell, cell, cell, cell, body. The text model keeps cells PAST the body
		//   (ITableTextContent.h), so the new route reads body, body, body, cell, cell, cell, cell.
		//   Both lists are complete and both carry the right TextIndex for every paragraph - they
		//   are the same set in a different sequence.
		//   ⇒ **MATCHED BY POSITION, NOT BY INDEX.** Comparing entry i against entry i reported
		//     every paragraph after the first table as different while nothing was wrong at all
		//     (measured 2026-08-31: tablepost came back "DIFFER text=5 start=5 place=5 of 7", and
		//     reading the detail showed the two lists were the same paragraphs, shifted by one).
		//   ⚠THE ORDER IS STILL REPORTED, because the diff downstream walks these lists IN ORDER.
		//     "The same set" is what makes the positions trustworthy; "the same sequence" is a
		//     different question, and the migration has to answer both.
		std::map<int32, size_t> oldByStart;
		std::map<int32, size_t> newByStart;
		for (size_t i = 0; i < oldStarts.size() && i < oldParas.size(); ++i)
			oldByStart[oldStarts[i]] = i;
		for (size_t i = 0; i < newStarts.size() && i < newParas.size(); ++i)
			newByStart[newStarts[i]] = i;

		size_t missing = 0, extra = 0, textDiffs = 0, placeDiffs = 0;
		std::ostringstream detail;
		size_t shown = 0;

		for (std::map<int32, size_t>::const_iterator it = newByStart.begin();
			 it != newByStart.end(); ++it)
		{
			const std::map<int32, size_t>::const_iterator o = oldByStart.find(it->first);
			if (o == oldByStart.end())
			{
				++extra;
				if (shown < kKCMMaxDetails)
				{
					++shown;
					detail << " [+" << it->first << "]";
				}
				continue;
			}

			const size_t ni = it->second;
			const size_t oi = o->second;

			const bool16 sameText = (oldParas[oi] == newParas[ni]) ? kTrue : kFalse;

			// ⚠THE CELL IDENTITY IS PART OF "THE SAME ANSWER". SplitRunAtPlaces cuts rows by it,
			//   so two readers that agree on every character and every position can still put a
			//   row in the wrong place if they disagree here.
			const bool16 samePlace = (oi < oldAttrs.size() && ni < newAttrs.size()
									  && oldAttrs[oi].fTableOrdinal == newAttrs[ni].fTableOrdinal
									  && oldAttrs[oi].fCellRow == newAttrs[ni].fCellRow
									  && oldAttrs[oi].fCellCol == newAttrs[ni].fCellCol) ? kTrue : kFalse;

			if (!sameText) ++textDiffs;
			if (!samePlace) ++placeDiffs;

			if ((!sameText || !samePlace) && shown < kKCMMaxDetails)
			{
				++shown;
				detail << " [@" << it->first << ":";
				if (!sameText)
					detail << "text(len " << oldParas[oi].size() << "/" << newParas[ni].size() << ")";
				if (!samePlace)
					detail << "place(" << (oi < oldAttrs.size() ? oldAttrs[oi].fTableOrdinal : -9)
						   << "," << (oi < oldAttrs.size() ? oldAttrs[oi].fCellRow : -9)
						   << "," << (oi < oldAttrs.size() ? oldAttrs[oi].fCellCol : -9)
						   << "/" << (ni < newAttrs.size() ? newAttrs[ni].fTableOrdinal : -9)
						   << "," << (ni < newAttrs.size() ? newAttrs[ni].fCellRow : -9)
						   << "," << (ni < newAttrs.size() ? newAttrs[ni].fCellCol : -9) << ")";
				detail << "]";
			}
		}

		for (std::map<int32, size_t>::const_iterator it = oldByStart.begin();
			 it != oldByStart.end(); ++it)
		{
			if (newByStart.find(it->first) == newByStart.end())
			{
				++missing;
				if (shown < kKCMMaxDetails)
				{
					++shown;
					detail << " [-" << it->first << "]";
				}
			}
		}

		// The sequence, asked separately from the set.
		size_t orderDiffs = 0;
		const size_t common = (oldStarts.size() < newStarts.size()) ? oldStarts.size() : newStarts.size();
		for (size_t i = 0; i < common; ++i)
		{
			if (oldStarts[i] != newStarts[i])
				++orderDiffs;
		}

		if (missing == 0 && extra == 0 && textDiffs == 0 && placeDiffs == 0)
		{
			line << "agree (" << newParas.size() << " paragraphs";
			if (orderDiffs != 0)
				line << ", " << orderDiffs << " in a different ORDER - expected where a table stands";
			line << ")";
		}
		else
		{
			line << "DIFFER missing=" << missing << " extra=" << extra
				 << " text=" << textDiffs << " place=" << placeDiffs
				 << " of " << newParas.size() << " (order " << orderDiffs << ")" << detail.str();
			if (shown == kKCMMaxDetails)
				line << " ...";
		}
	}

	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	if (!sKCMReadCompareReport.empty())
		sKCMReadCompareReport += "\n";
	sKCMReadCompareReport += line.str();
}

// End, KCMTextRead.cpp.
