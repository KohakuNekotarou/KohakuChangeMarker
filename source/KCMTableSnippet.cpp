//========================================================================================
//
//  KCMTableSnippet.cpp -- see the header.
//
//  THE CUTS ARE TEXT CUTS, NOT EDITS: a subtree is found by its tags and copied out untouched,
//  the way KCMPdfSpike's MakeObjectPart cuts a <Story> for an IDML part ("a cut cannot corrupt
//  what it does not touch"). Nothing inside a <Table> is read.
//
//========================================================================================

// ⚠NOT INSIDE A GUARD, and it cannot be: this is the precompiled header, and the compiler throws
//   away everything up to and including this line - so an #ifndef opened above it loses its
//   #endif ("error C1020: unexpected #endif", 2026-09-20). The standalone build supplies its own
//   empty VCPlugInHeaders.h instead (work/kcm-tablemerge-test).
#include "VCPlugInHeaders.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>		// atoi - the addresses in Name="col:row"
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifndef KCM_TABLESNIPPET_STANDALONE
#include "IDocument.h"			// GetDocWorkSpace - where the style group managers live
#include "IDocumentList.h"		// FindDocByDataBase - "is this database a document the session has"
#include "IDOMElement.h"
#include "IINXManager.h"
#include "IPMStream.h"
#include "ISession.h"
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"	// GetRootHierarchy - as SnpManipulateTableStyle.cpp does it
#include "IWorkspace.h"
#include "AppFrameworkID.h"		// kActionExportPolicyBoss - the policy S17.8 measured to work
#include "TablesID.h"			// IID_ICELLSTYLEGROUPMANAGER / IID_ITABLESTYLEGROUPMANAGER
#include "ErrorUtils.h"			// GlobalErrorStatePreserver
#include "INXCoreID.h"			// IID_IINXEXPORTPOLICY
#include "PersistUtils.h"
#include "StreamUtil.h"
#endif // KCM_TABLESNIPPET_STANDALONE

#include "KCMTableSnippet.h"
#ifndef KCM_TABLESNIPPET_STANDALONE
#include "KCMMemXferBytes.h"
#endif

namespace
{

#ifndef KCM_TABLESNIPPET_STANDALONE
/** The start of the `nth` occurrence of `needle` in [from, end) of text, or std::string::npos. */
size_t FindNth(const std::string& text, const char* needle, size_t from, size_t end, int32 nth)
{
	size_t at = from;
	const size_t len = std::strlen(needle);
	for (int32 seen = 0; ; ++seen)
	{
		at = text.find(needle, at);
		if (at == std::string::npos || at >= end)
			return std::string::npos;
		if (seen == nth)
			return at;
		at += len;
	}
}
#endif // KCM_TABLESNIPPET_STANDALONE

/** [start, end) of the element whose start tag begins at `open` (open must point at "<Name"),
    matching nested elements of the SAME name. end = one past "</Name>". npos when unbalanced. */
size_t ElementEnd(const std::string& text, size_t open, const char* name)
{
	const std::string openTag = std::string("<") + name;			// "<Table" - followed by a space or '>'
	const std::string closeTag = std::string("</") + name + ">";
	int32 depth = 0;
	size_t at = open;
	for (;;)
	{
		const size_t nextOpen = text.find(openTag, at);
		const size_t nextClose = text.find(closeTag, at);
		if (nextClose == std::string::npos)
			return std::string::npos;
		if (nextOpen != std::string::npos && nextOpen < nextClose)
		{
			// "<Table" must not be "<TableStyle": the character after the name decides.
			const char after = (nextOpen + openTag.size() < text.size()) ? text[nextOpen + openTag.size()] : '\0';
			if (after == ' ' || after == '>' || after == '/')
				++depth;
			at = nextOpen + openTag.size();
			continue;
		}
		--depth;
		at = nextClose + closeTag.size();
		if (depth == 0)
			return at;
	}
}

/** Cut one element by name out of text - the first occurrence whose tag is exactly `name`. */
bool16 CutFirstElement(const std::string& text, const char* name, std::string& out)
{
	const std::string openTag = std::string("<") + name;
	size_t open = 0;
	for (;;)
	{
		open = text.find(openTag, open);
		if (open == std::string::npos)
			return kFalse;
		const char after = (open + openTag.size() < text.size()) ? text[open + openTag.size()] : '\0';
		if (after == ' ' || after == '>')
			break;
		open += openTag.size();
	}
	const size_t end = ElementEnd(text, open, name);
	if (end == std::string::npos)
		return kFalse;
	out = text.substr(open, end - open);
	return kTrue;
}

/** The value of attribute `name` inside the start tag that ends at `tagEnd`. Empty when absent. */
std::string AttributeIn(const std::string& text, size_t open, size_t tagEnd, const char* name)
{
	const std::string needle = std::string(" ") + name + "=\"";
	const size_t at = text.find(needle, open);
	if (at == std::string::npos || at >= tagEnd)
		return std::string();
	const size_t from = at + needle.size();
	const size_t to = text.find('"', from);
	if (to == std::string::npos || to > tagEnd)
		return std::string();
	return text.substr(from, to - from);
}

/** One outermost <Cell> of a table's XML, at or after `from`.
	@param outBodyStart, outBodyEnd the contents BETWEEN the tags - what a merge replaces.
	@param outEnd one past </Cell>, where the next search starts (so a nested table's cells, which
		stand inside this one, are never visited).
	@param outName the Name="col:row" the start tag carries, and outSelf its Self - ⚠both read HERE,
		while the start tag's bounds are in hand: looking for "<Cell" again from outside would find
		"<CellStyle" just as readily.
	@param outEmptyTag kTrue for <Cell … />, which has no contents and is left alone.
	@return kFalse when there is no next cell. */
bool16 NextCell(const std::string& table, size_t from, size_t& outBodyStart, size_t& outBodyEnd,
				size_t& outEnd, std::string& outName, std::string& outSelf, bool16& outEmptyTag)
{
	static const char kOpen[] = "<Cell";
	static const char kClose[] = "</Cell>";
	size_t open = from;
	for (;;)
	{
		open = table.find(kOpen, open);
		if (open == std::string::npos)
			return kFalse;
		const char after = (open + 5 < table.size()) ? table[open + 5] : '\0';
		if (after == ' ' || after == '>' || after == '/')
			break;
		open += 5;					// <CellStyle…, <CellStyleGroup… - a different element
	}
	const size_t tagEnd = table.find('>', open);
	if (tagEnd == std::string::npos)
		return kFalse;
	outName = AttributeIn(table, open, tagEnd, "Name");
	outSelf = AttributeIn(table, open, tagEnd, "Self");
	if (tagEnd > open && table[tagEnd - 1] == '/')
	{
		outEmptyTag = kTrue;
		outBodyStart = tagEnd;
		outBodyEnd = tagEnd;
		outEnd = tagEnd + 1;
		return kTrue;
	}
	const size_t end = ElementEnd(table, open, "Cell");
	if (end == std::string::npos)
		return kFalse;
	outEmptyTag = kFalse;
	outBodyStart = tagEnd + 1;
	outBodyEnd = end - (sizeof(kClose) - 1);
	outEnd = end;
	return kTrue;
}

/** "col:row" as two numbers. kFalse when the name is not of that shape. */
bool16 SplitCellName(const std::string& name, int32& outCol, int32& outRow)
{
	const size_t colon = name.find(':');
	if (colon == std::string::npos || colon == 0 || colon + 1 >= name.size())
		return kFalse;
	outCol = std::atoi(name.substr(0, colon).c_str());
	outRow = std::atoi(name.substr(colon + 1).c_str());
	return kTrue;
}

/** The Self of the table itself - the prefix every one of its cells' Self begins with. */
std::string TableSelf(const std::string& table)
{
	const size_t open = table.find("<Table ");
	if (open == std::string::npos)
		return std::string();
	const size_t tagEnd = table.find('>', open);
	if (tagEnd == std::string::npos)
		return std::string();
	return AttributeIn(table, open, tagEnd, "Self");
}

/** One outermost cell, read out for the pairing. */
struct MergeCell
{
	std::string	fId;			///< Self with the table's own Self taken off: "i4". Empty when absent.
	std::string	fName;			///< "col:row"
	int32		fCol;
	int32		fRow;
	size_t		fBodyStart;		///< the contents between the tags - what a merge replaces
	size_t		fBodyEnd;

	MergeCell() : fCol(0), fRow(0), fBodyStart(0), fBodyEnd(0) {}
};

/** Every outermost cell of a table's XML that carries an address, in document order. */
void ReadMergeCells(const std::string& table, std::vector<MergeCell>& out)
{
	out.clear();
	const std::string selfPrefix = TableSelf(table);
	size_t at = 0, bodyStart = 0, bodyEnd = 0, end = 0;
	std::string name, self;
	bool16 emptyTag = kFalse;
	while (NextCell(table, at, bodyStart, bodyEnd, end, name, self, emptyTag))
	{
		at = end;
		if (name.empty() || emptyTag)
			continue;
		MergeCell cell;
		if (!SplitCellName(name, cell.fCol, cell.fRow))
			continue;
		cell.fName = name;
		cell.fBodyStart = bodyStart;
		cell.fBodyEnd = bodyEnd;
		// The cell's id inside its table: its Self with the table's Self taken off. ⚠Taken off on
		// EACH side separately, so that two texts whose tables are numbered differently still pair.
		if (!selfPrefix.empty() && self.size() > selfPrefix.size()
			&& self.compare(0, selfPrefix.size(), selfPrefix) == 0)
			cell.fId = self.substr(selfPrefix.size());
		else
			cell.fId = self;
		out.push_back(cell);
	}
}

/** The text a cell says - every <Content> of its body, run together. Empty for an empty cell, which
    is why an empty cell never votes: every empty cell says the same thing. */
std::string CellSays(const std::string& table, const MergeCell& cell)
{
	static const char kOpen[] = "<Content>";
	static const char kClose[] = "</Content>";
	std::string says;
	size_t at = cell.fBodyStart;
	for (;;)
	{
		const size_t open = table.find(kOpen, at);
		if (open == std::string::npos || open >= cell.fBodyEnd)
			break;
		const size_t from = open + (sizeof(kOpen) - 1);
		const size_t close = table.find(kClose, from);
		if (close == std::string::npos || close > cell.fBodyEnd)
			break;
		says.append(table, from, close - from);
		at = close + (sizeof(kClose) - 1);
	}
	return says;
}

/** One vote that index `fFrom` on Task Start's side is index `fTo` on the live side. */
struct IndexVote
{
	int32 fFrom;
	int32 fTo;
	int32 fWeight;

	IndexVote() : fFrom(0), fTo(0), fWeight(0) {}
	IndexVote(int32 from, int32 to) : fFrom(from), fTo(to), fWeight(1) {}
};

bool16 VoteIsBefore(const IndexVote& a, const IndexVote& b)
{
	if (a.fFrom != b.fFrom)
		return (a.fFrom < b.fFrom) ? kTrue : kFalse;
	return (a.fTo < b.fTo) ? kTrue : kFalse;
}

/** The largest set of votes that is STRICTLY INCREASING in BOTH coordinates, by weight.

    ★THIS IS WHAT THROWS A RECYCLED ID OUT (measured 2026-09-20): a cell created after a delete can
    be handed the dead cell's id, and it then votes for a pairing that would put a later row above an
    earlier one. Such a vote cannot be part of any increasing chain, so the heaviest chain drops it.
    The tables are small, so the plain O(n^2) search is the right one. */
void MonotoneMap(std::vector<IndexVote>& votes, std::map<int32, int32>& outMap)
{
	outMap.clear();
	if (votes.empty())
		return;
	// Same pairing voted for twice is one pairing with twice the weight.
	std::sort(votes.begin(), votes.end(), VoteIsBefore);
	std::vector<IndexVote> unique;
	for (size_t i = 0; i < votes.size(); ++i)
	{
		if (!unique.empty() && unique.back().fFrom == votes[i].fFrom && unique.back().fTo == votes[i].fTo)
			unique.back().fWeight += votes[i].fWeight;
		else
			unique.push_back(votes[i]);
	}
	const size_t n = unique.size();
	std::vector<int32> best(n, 0);
	std::vector<int32> cameFrom(n, -1);
	size_t endOfBest = 0;
	for (size_t i = 0; i < n; ++i)
	{
		best[i] = unique[i].fWeight;
		for (size_t j = 0; j < i; ++j)
			if (unique[j].fFrom < unique[i].fFrom && unique[j].fTo < unique[i].fTo
				&& best[j] + unique[i].fWeight > best[i])
			{
				best[i] = best[j] + unique[i].fWeight;
				cameFrom[i] = static_cast<int32>(j);
			}
		if (best[i] > best[endOfBest])
			endOfBest = i;
	}
	for (int32 at = static_cast<int32>(endOfBest); at >= 0; at = cameFrom[static_cast<size_t>(at)])
		outMap[unique[static_cast<size_t>(at)].fFrom] = unique[static_cast<size_t>(at)].fTo;
}

/** One cell of Task Start's table paired with one of the live table's - what a matched id, or a
    matched text, proposes. */
struct CellPair
{
	int32 fOldCol;
	int32 fOldRow;
	int32 fNewCol;
	int32 fNewRow;

	CellPair() : fOldCol(0), fOldRow(0), fNewCol(0), fNewRow(0) {}
};

/** A column map and a row map built from `pairs`, and how many of the pairs BOTH maps accept.

    ★★★WHY ONE DIMENSION IS DECIDED FIRST AND THE OTHER IS THEN FILTERED BY IT (2026-09-20, found by
    the offline test's case 6 - the one taken from the running application). Deciding the two
    independently loses: a recycled id proposes a pairing that is nonsense as a CELL while each of its
    two halves looks respectable on its own, and when the honest votes tie with it in one dimension,
    the arbitrary order of the search picks the liar. Filtering the second dimension by the first
    throws that vote out whole, because its column was already refused.
    ⚠Which dimension goes first is not obvious, so THE CALLER TRIES BOTH and keeps the one that
     accepts more cells - see KCMMergeTableCells. */
int32 MapsFromPairs(const std::vector<CellPair>& pairs, bool16 columnsFirst,
					std::map<int32, int32>& outCol, std::map<int32, int32>& outRow)
{
	outCol.clear();
	outRow.clear();
	if (pairs.empty())
		return 0;
	std::vector<IndexVote> first, second;
	for (size_t i = 0; i < pairs.size(); ++i)
		first.push_back(columnsFirst ? IndexVote(pairs[i].fOldCol, pairs[i].fNewCol)
									 : IndexVote(pairs[i].fOldRow, pairs[i].fNewRow));
	std::map<int32, int32> firstMap;
	MonotoneMap(first, firstMap);
	if (firstMap.empty())
		return 0;
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		const int32 from = columnsFirst ? pairs[i].fOldCol : pairs[i].fOldRow;
		const int32 to = columnsFirst ? pairs[i].fNewCol : pairs[i].fNewRow;
		const std::map<int32, int32>::const_iterator it = firstMap.find(from);
		if (it == firstMap.end() || it->second != to)
			continue;								// this pair's first half was already refused
		second.push_back(columnsFirst ? IndexVote(pairs[i].fOldRow, pairs[i].fNewRow)
									  : IndexVote(pairs[i].fOldCol, pairs[i].fNewCol));
	}
	std::map<int32, int32> secondMap;
	MonotoneMap(second, secondMap);
	if (columnsFirst)
	{
		outCol = firstMap;
		outRow = secondMap;
	}
	else
	{
		outRow = firstMap;
		outCol = secondMap;
	}
	int32 accepted = 0;
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		const std::map<int32, int32>::const_iterator c = outCol.find(pairs[i].fOldCol);
		const std::map<int32, int32>::const_iterator r = outRow.find(pairs[i].fOldRow);
		if (c != outCol.end() && c->second == pairs[i].fNewCol
			&& r != outRow.end() && r->second == pairs[i].fNewRow)
			++accepted;
	}
	return accepted;
}

/** The maps that accept the most of `pairs`, trying each dimension first. Empty when none do. */
void BestMaps(const std::vector<CellPair>& pairs,
			  std::map<int32, int32>& outCol, std::map<int32, int32>& outRow)
{
	std::map<int32, int32> colA, rowA, colB, rowB;
	const int32 byColumns = MapsFromPairs(pairs, kTrue, colA, rowA);
	const int32 byRows = MapsFromPairs(pairs, kFalse, colB, rowB);
	if (byRows > byColumns)
	{
		outCol = colB;
		outRow = rowB;
	}
	else
	{
		outCol = colA;
		outRow = rowA;
	}
}

/** One past the highest column and row an address mentions. */
void Extents(const std::vector<MergeCell>& cells, int32& outCols, int32& outRows)
{
	outCols = 0;
	outRows = 0;
	for (size_t i = 0; i < cells.size(); ++i)
	{
		if (cells[i].fCol + 1 > outCols) outCols = cells[i].fCol + 1;
		if (cells[i].fRow + 1 > outRows) outRows = cells[i].fRow + 1;
	}
}

}	// anonymous namespace

bool16 KCMMergeTableCells(const std::string& olderTableXml, const std::string& liveTableXml,
						  std::string& outMerged, int32& outKept, std::string& outHow,
						  const std::map<std::string, std::string>* liveWasTaskStart)
{
	outMerged.clear();
	outKept = 0;
	outHow.clear();
	if (olderTableXml.empty())
		return kFalse;

	std::vector<MergeCell> older, live;
	ReadMergeCells(olderTableXml, older);
	ReadMergeCells(liveTableXml, live);

	// The live cells by address, so that a pairing can be looked up once it is known.
	std::map<std::string, size_t> liveByName;
	for (size_t i = 0; i < live.size(); ++i)
		liveByName[live[i].fName] = i;

	// ---- 1. the cells' own ids ------------------------------------------------------------------
	std::vector<CellPair> pairs;
	{
		std::map<std::string, size_t> liveById;
		for (size_t i = 0; i < live.size(); ++i)
		{
			if (live[i].fId.empty())
				continue;
			// ★A cell that a restore has already put back carries a NEW id, and what it WAS is the
			//   only id Task Start knows it by - so the translation goes on first, when there is one.
			std::string id = live[i].fId;
			if (liveWasTaskStart != nil)
			{
				const std::map<std::string, std::string>::const_iterator was = liveWasTaskStart->find(id);
				if (was == liveWasTaskStart->end())
					continue;
				// ⚠★★★**A CELL THE MAP DOES NOT NAME CANNOT VOTE AT ALL** (2026-09-20, caught on the
				//   running application). A restore replaces the WHOLE table, so every cell that came
				//   out of one is in the map; a cell that is not in it was made AFTERWARDS and has no
				//   Task Start counterpart by definition. Letting its raw id through was not merely
				//   useless - it COLLIDED: the table restored to 2x2 had ids 0,1,2,3 and the map read
				//   {2 -> i4, 3 -> i5}, so the row added next was handed the untranslated i4 and i5 -
				//   Task Start's own C and D - and, walked later, overwrote them. Task Start's C and D
				//   were then paired with the new EMPTY row and came back empty, while the message
				//   said "2 cell(s) keep what you wrote in them" - wrong, and confident.
				id = was->second;
			}
			liveById[id] = i;
		}
		for (size_t i = 0; i < older.size(); ++i)
		{
			if (older[i].fId.empty())
				continue;
			const std::map<std::string, size_t>::const_iterator it = liveById.find(older[i].fId);
			if (it == liveById.end())
				continue;
			CellPair pair;
			pair.fOldCol = older[i].fCol;
			pair.fOldRow = older[i].fRow;
			pair.fNewCol = live[it->second].fCol;
			pair.fNewRow = live[it->second].fRow;
			pairs.push_back(pair);
		}
	}
	std::map<int32, int32> colMap, rowMap;
	BestMaps(pairs, colMap, rowMap);
	// ★The sentence says WHICH id road answered: a table that has been put back once is paired
	//   through what that restore left behind, and a live run can see at a glance which happened.
	outHow = (liveWasTaskStart != nil && !liveWasTaskStart->empty())
			   ? "by cell id, through an earlier restore" : "by cell id";

	// ---- 2. what the cells say, when the ids answered nothing ------------------------------------
	// ⚠This is the state a table that has ALREADY been put back once is in: a snippet import repacks
	//   the ids, so Task Start's and the live table's no longer name the same cells.
	if (colMap.empty() || rowMap.empty())
	{
		pairs.clear();
		// Each live cell read ONCE, not once per cell of Task Start: the inner loop below runs
		// older.size() x live.size() times, and a cell holding a nested table is not a cheap read.
		std::vector<std::string> liveSays(live.size());
		for (size_t j = 0; j < live.size(); ++j)
			liveSays[j] = CellSays(liveTableXml, live[j]);
		for (size_t i = 0; i < older.size(); ++i)
		{
			const std::string says = CellSays(olderTableXml, older[i]);
			if (says.empty())
				continue;								// every empty cell says the same thing
			for (size_t j = 0; j < live.size(); ++j)
				if (liveSays[j] == says)
				{
					CellPair pair;
					pair.fOldCol = older[i].fCol;
					pair.fOldRow = older[i].fRow;
					pair.fNewCol = live[j].fCol;
					pair.fNewRow = live[j].fRow;
					pairs.push_back(pair);
				}
		}
		BestMaps(pairs, colMap, rowMap);
		outHow = "by what the cells say";
	}

	// ---- 3. the address, when the two shapes have the same extents -------------------------------
	if (colMap.empty() || rowMap.empty())
	{
		int32 oCols = 0, oRows = 0, lCols = 0, lRows = 0;
		Extents(older, oCols, oRows);
		Extents(live, lCols, lRows);
		if (oCols == lCols && oRows == lRows && oCols > 0 && oRows > 0)
		{
			for (int32 c = 0; c < oCols; ++c) colMap[c] = c;
			for (int32 r = 0; r < oRows; ++r) rowMap[r] = r;
			outHow = "by address";
		}
		else
		{
			// ---- 4. nothing to go on: the shape goes back whole and no cell keeps anything.
			outMerged = olderTableXml;
			outHow = "nothing to pair the cells by";
			return kTrue;
		}
	}

	// Task Start's table, copied through - with the live contents put in wherever the maps pair the
	// two and they differ. Everything outside the cells (the table's own tag, the column and row
	// elements) is Task Start's, untouched.
	size_t copiedTo = 0;
	for (size_t i = 0; i < older.size(); ++i)
	{
		const std::map<int32, int32>::const_iterator c = colMap.find(older[i].fCol);
		const std::map<int32, int32>::const_iterator r = rowMap.find(older[i].fRow);
		if (c == colMap.end() || r == rowMap.end())
			continue;									// a row or column Task Start alone has
		char address[32];
		std::snprintf(address, sizeof(address), "%d:%d", static_cast<int>(c->second), static_cast<int>(r->second));
		const std::map<std::string, size_t>::const_iterator at = liveByName.find(address);
		if (at == liveByName.end())
			continue;									// covered by a merge on the live side
		// ⚠THE WRITE ONLY EVER MOVES FORWARD. The cells are walked in document order and the outermost
		//   ones cannot overlap, so this cannot fire - but `bodyStart - copiedTo` on a size_t would
		//   wrap rather than fail, and append() would then clamp and write the wrong bytes silently.
		if (older[i].fBodyStart < copiedTo)
			continue;
		const MergeCell& from = live[at->second];
		const std::string words = liveTableXml.substr(from.fBodyStart, from.fBodyEnd - from.fBodyStart);
		if (words == olderTableXml.substr(older[i].fBodyStart, older[i].fBodyEnd - older[i].fBodyStart))
			continue;									// the same on both sides: nothing to keep
		outMerged.append(olderTableXml, copiedTo, older[i].fBodyStart - copiedTo);
		outMerged += words;
		copiedTo = older[i].fBodyEnd;
		++outKept;
	}
	outMerged.append(olderTableXml, copiedTo, olderTableXml.size() - copiedTo);
	return kTrue;
}

#ifndef KCM_TABLESNIPPET_STANDALONE
bool16 KCMCutTableXml(const char* xml, size_t size, UID storyUID, int32 ordinal, std::string& outTable)
{
	outTable.clear();
	if (xml == nil || size == 0 || ordinal < 0)
		return kFalse;
	const std::string text(xml, size);

	// The story: the <Story …> whose start tag carries Self="u<hex>" (the origin's INX and a story's
	// own INX both name it by the document's uid - measured 2026-09-19, u15a = 346).
	char self[32];
	std::snprintf(self, sizeof(self), "Self=\"u%x\"", static_cast<unsigned>(storyUID.Get()));
	size_t storyOpen = 0;
	size_t storyEnd = std::string::npos;
	for (;;)
	{
		storyOpen = text.find("<Story ", storyOpen);
		if (storyOpen == std::string::npos)
			return kFalse;
		const size_t tagEnd = text.find('>', storyOpen);
		if (tagEnd == std::string::npos)
			return kFalse;
		if (text.substr(storyOpen, tagEnd - storyOpen).find(self) != std::string::npos)
		{
			storyEnd = ElementEnd(text, storyOpen, "Story");
			break;
		}
		storyOpen = tagEnd;
	}
	if (storyEnd == std::string::npos)
		return kFalse;

	// The ordinal-th "<Table " inside it, in document order - nested tables counted, which is the order
	// KCMTextRead numbers them in (a nested table's cells begin after the cell that holds it).
	const size_t tableOpen = FindNth(text, "<Table ", storyOpen, storyEnd, ordinal);
	if (tableOpen == std::string::npos)
		return kFalse;
	const size_t tableEnd = ElementEnd(text, tableOpen, "Table");
	if (tableEnd == std::string::npos || tableEnd > storyEnd)
		return kFalse;
	outTable = text.substr(tableOpen, tableEnd - tableOpen);
	return kTrue;
}
#endif // KCM_TABLESNIPPET_STANDALONE

void KCMCutTableStyleGroups(const char* xml, size_t size, std::string& outGroups)
{
	outGroups.clear();
	if (xml == nil || size == 0)
		return;
	const std::string text(xml, size);
	std::string one;
	if (CutFirstElement(text, "RootCellStyleGroup", one))
	{
		outGroups += one;
		outGroups += "\n";
	}
	if (CutFirstElement(text, "RootTableStyleGroup", one))
	{
		outGroups += one;
		outGroups += "\n";
	}
}

void KCMReadTableCellIds(const std::string& tableXml, std::map<std::string, std::string>& out)
{
	out.clear();
	std::vector<MergeCell> cells;
	ReadMergeCells(tableXml, cells);
	for (size_t i = 0; i < cells.size(); ++i)
		if (!cells[i].fId.empty())
			out[cells[i].fName] = cells[i].fId;
}

int32 KCMLabelTableCells(std::string& tableXml, const char* key)
{
	if (key == nil || key[0] == '\0')
		return 0;
	std::vector<MergeCell> cells;
	ReadMergeCells(tableXml, cells);

	// Written back to front, so that every offset ReadMergeCells gave is still good when it is used.
	int32 labelled = 0;
	for (size_t n = cells.size(); n > 0; --n)
	{
		const MergeCell& cell = cells[n - 1];
		if (cell.fId.empty())
			continue;
		const std::string pair = std::string("<ListItem type=\"list\"><ListItem type=\"string\">") + key
							   + "</ListItem><ListItem type=\"string\">" + cell.fId + "</ListItem></ListItem>";
		const std::string body = tableXml.substr(cell.fBodyStart, cell.fBodyEnd - cell.fBodyStart);

		// Where it goes depends on what the cell already has - a cell whose live contents were merged
		// in can carry the reader's OWN label, and that one is not ours to take away.
		size_t at = std::string::npos;
		std::string text = pair;
		const size_t label = body.find("<Label ");
		const size_t properties = body.find("<Properties>");
		if (label != std::string::npos)
		{
			const size_t labelTagEnd = body.find('>', label);
			if (labelTagEnd != std::string::npos)
				at = cell.fBodyStart + labelTagEnd + 1;			// join the list that is there
		}
		else if (properties != std::string::npos)
		{
			at = cell.fBodyStart + properties + 12;				// a <Properties> with no <Label> yet
			text = "<Label type=\"list\">" + pair + "</Label>";
		}
		else
		{
			at = cell.fBodyStart;								// neither: the cell gets both
			text = "<Properties><Label type=\"list\">" + pair + "</Label></Properties>";
		}
		if (at == std::string::npos)
			continue;
		tableXml.insert(at, text);
		++labelled;
	}
	return labelled;
}

void KCMBuildTableSnippet(const std::string& tableXml, const std::string& styleGroups, std::string& outSnippet)
{
	// The dress, copied from a snippet InDesign itself wrote (table-only-frame.idms, 2026-09-19) and cut
	// to what the import needs: the PI header (⚠type="snippet" - the importer refuses type="action"),
	// the style groups, one spread with one frame, one story with the table. The uids are the ones
	// that file used; the import renumbers everything.
	static const char kHead[] =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<?aid style=\"50\" type=\"snippet\" readerVersion=\"6.0\" featureSet=\"257\" product=\"21.0(2)\" ?>\n"
		"<?aid SnippetType=\"PageItem\"?>\n"
		"<Document DOMVersion=\"21.0\" Self=\"d\">\n";
	static const char kSpread[] =
		"<Spread Self=\"uf0\">\n"
		"<TextFrame Self=\"u1ab\" ParentStory=\"u197\" PreviousTextFrame=\"n\" NextTextFrame=\"n\" ContentType=\"TextType\""
		" ParentInterfaceChangeCount=\"\" TargetInterfaceChangeCount=\"\" LastUpdatedInterfaceChangeCount=\"\" OverriddenPageItemProps=\"\""
		" HorizontalLayoutConstraints=\"FlexibleDimension FixedDimension FlexibleDimension\" VerticalLayoutConstraints=\"FlexibleDimension FixedDimension FlexibleDimension\""
		" FlexItemWidthMode=\"FlexFixed\" FlexItemHeightMode=\"FlexFixed\" GradientFillStart=\"0 0\" GradientFillLength=\"0\" GradientFillAngle=\"0\""
		" GradientStrokeStart=\"0 0\" GradientStrokeLength=\"0\" GradientStrokeAngle=\"0\" Locked=\"false\" LocalDisplaySetting=\"Default\""
		" GradientFillHiliteLength=\"0\" GradientFillHiliteAngle=\"0\" GradientStrokeHiliteLength=\"0\" GradientStrokeHiliteAngle=\"0\""
		" AppliedObjectStyle=\"ObjectStyle/$ID/[Normal Text Frame]\" Visible=\"true\" Name=\"$ID/\" ItemTransform=\"1 0 0 1 0 0\">\n"
		"<Properties>\n<PathGeometry>\n<GeometryPathType PathOpen=\"false\">\n<PathPointArray>\n"
		"<PathPointType Anchor=\"0 0\" LeftDirection=\"0 0\" RightDirection=\"0 0\" />\n"
		"<PathPointType Anchor=\"0 400\" LeftDirection=\"0 400\" RightDirection=\"0 400\" />\n"
		"<PathPointType Anchor=\"400 400\" LeftDirection=\"400 400\" RightDirection=\"400 400\" />\n"
		"<PathPointType Anchor=\"400 0\" LeftDirection=\"400 0\" RightDirection=\"400 0\" />\n"
		"</PathPointArray>\n</GeometryPathType>\n</PathGeometry>\n</Properties>\n"
		"<TextFramePreference TextColumnCount=\"1\" TextColumnFixedWidth=\"400\" TextColumnMaxWidth=\"0\">\n"
		"<Properties>\n<InsetSpacing type=\"list\">\n<ListItem type=\"unit\">0</ListItem>\n<ListItem type=\"unit\">0</ListItem>\n"
		"<ListItem type=\"unit\">0</ListItem>\n<ListItem type=\"unit\">0</ListItem>\n</InsetSpacing>\n</Properties>\n"
		"</TextFramePreference>\n"
		"<TextWrapPreference Inverse=\"false\" ApplyToMasterPageOnly=\"false\" TextWrapSide=\"BothSides\" TextWrapMode=\"None\">\n"
		"<Properties>\n<TextWrapOffset Top=\"0\" Left=\"0\" Bottom=\"0\" Right=\"0\" />\n</Properties>\n</TextWrapPreference>\n"
		"</TextFrame>\n"
		"</Spread>\n";
	static const char kStoryOpen[] =
		"<Story Self=\"u197\" AppliedTOCStyle=\"n\" UserText=\"true\" IsEndnoteStory=\"false\" TrackChanges=\"false\" StoryTitle=\"$ID/\" AppliedNamedGrid=\"n\">\n"
		"<StoryPreference OpticalMarginAlignment=\"false\" OpticalMarginSize=\"9.2125984251969\" FrameType=\"TextFrameType\" StoryOrientation=\"Horizontal\" StoryDirection=\"LeftToRightDirection\" />\n"
		"<InCopyExportOption IncludeGraphicProxies=\"true\" IncludeAllResources=\"false\" />\n"
		"<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/$ID/NormalParagraphStyle\">\n"
		"<CharacterStyleRange AppliedCharacterStyle=\"CharacterStyle/$ID/[No character style]\">\n";
	static const char kStoryClose[] =
		"\n</CharacterStyleRange>\n</ParagraphStyleRange>\n</Story>\n"
		"</Document>\n";

	outSnippet.clear();
	outSnippet.reserve(sizeof(kHead) + styleGroups.size() + sizeof(kSpread) + sizeof(kStoryOpen) + tableXml.size() + sizeof(kStoryClose));
	outSnippet += kHead;
	outSnippet += styleGroups;
	outSnippet += kSpread;
	outSnippet += kStoryOpen;
	outSnippet += tableXml;
	outSnippet += kStoryClose;
}

#ifndef KCM_TABLESNIPPET_STANDALONE
bool16 KCMExportStoryInx(IDataBase* db, UID storyUID, KCMMemXferBytes& out, bool16 includeStyleRoots)
{
	// The call KCMPdfSpike's S17.8 measured (ExportElementAsInxWith), on a story: what comes out is the
	// <Story> the IDML holds, byte for byte, dressed as <?aid type="action"?><Document>…</Document>.
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	// ★★★THE DATABASE HAS TO BE ONE THE SESSION KNOWS ABOUT - the guard KCMResourceSnapshot.cpp:62-93
	//   states at length: handed a cloned database, ExportINX does not fail and does not return nil,
	//   THE PROCESS IS GONE. There is nothing to nil-check afterwards; the only defence is to refuse
	//   before starting. A KIDMCP comparison lends exactly such a database as its Source.
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return kFalse;
	InterfacePtr<IDOMElement> element(db, storyUID, UseDefaultIID());
	InterfacePtr<IINXManager> inx(session->QueryINXManager());
	// ⚠IINXExportPolicy is forward-declared only in the SDK: received as IPMUnknown and C-cast, the
	//   way the product's own InCopyImportProvider.cpp does (KCMPdfSpike says so at length).
	InterfacePtr<IPMUnknown> holder((IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
	if (element == nil || inx == nil || holder == nil)
		return kFalse;
	IINXExportPolicy* const policy = (IINXExportPolicy*)holder.get();
	IDOMElement::ElementList roots;
	roots.push_back(element);
	// ★THE TWO STYLE ROOTS AS ROOTS OF THEIR OWN - what makes one story's export enough to dress a
	//   table snippet in (see the header). The access point is the one SnpManipulateTableStyle.cpp:204
	//   uses: the document's workspace, asked for the style group manager by IID.
	if (includeStyleRoots)
	{
		IDocument* const doc = docList->FindDocByDataBase(db);
		InterfacePtr<IWorkspace> workspace(doc != nil ? doc->GetDocWorkSpace() : UIDRef::gNull, UseDefaultIID());
		const PMIID whichManager[2] = { IID_ICELLSTYLEGROUPMANAGER, IID_ITABLESTYLEGROUPMANAGER };
		for (int32 i = 0; workspace != nil && i < 2; ++i)
		{
			InterfacePtr<IStyleGroupManager> manager(workspace, whichManager[i]);
			const IStyleGroupHierarchy* const root = (manager != nil) ? manager->GetRootHierarchy() : nil;
			const UID rootUID = (root != nil) ? ::GetUID(root) : kInvalidUID;
			if (rootUID == kInvalidUID)
				continue;
			InterfacePtr<IDOMElement> styles(db, rootUID, UseDefaultIID());
			if (styles != nil)
				roots.push_back(styles);
		}
	}
	// ⚠★★★**THE STYLE ROOTS ARE A SECOND CHANCE, NEVER A REQUIREMENT** (2026-09-20). Handing ExportINX
	//   more than one root is a shape of the call NOTHING HAS MEASURED, and this export stands in the
	//   only path a Table restore has: if it failed, the reader would be told the table could not be
	//   put back at all. So a failure with the style roots is followed by the call that IS measured -
	//   the story alone - and the groups are then found where they were found before.
	//   ★The rule this follows: a new road may be tried in a working path only if the old road is still
	//    underneath it.
	ErrorCode err = kFailure;
	for (int32 attempt = 0; attempt < 2; ++attempt)
	{
		if (attempt == 1)
		{
			if (roots.size() < 2)
				break;							// there was no second road to fall back to
			roots.resize(1);					// the story alone - the call S17.8 measured
			out.Seek(0, kSeekFromStart);		// and empty what the failed attempt wrote
			out.SetEndOfStream();
		}
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&out, kFalse, kFalse));
		if (stream == nil)
			return kFalse;
		{
			// ★THE CALLER'S ERROR STATE IS KEPT OUT OF THIS (KCMResourceSnapshot.cpp:134-143): an error
			//   raised by the export would otherwise stand in the global state and pull down the very
			//   next command - and the next command here is the one that puts the table back.
			GlobalErrorStatePreserver errorState;
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			inx->BeginExportSession();
			// Inside the session: IDOMElement.h:58-60 - the interface is for use under INX context.
			// ⚠EVERY root, not just the story: a style root handed in unreset is the same mistake.
			for (size_t i = 0; i < roots.size(); ++i)
				roots[i]->Reset();
			err = inx->ExportINX(roots, policy, stream, kSuppressUI);
			// And again when done (IDOMElement.h:54-56), so no cache is left on any of them.
			for (size_t i = 0; i < roots.size(); ++i)
				roots[i]->Reset();
			inx->EndExportSession();
		}
		stream->Flush();
		if (err == kSuccess && out.GetSize() > 0)
			return kTrue;
	}
	return kFalse;
}
#endif // KCM_TABLESNIPPET_STANDALONE

// End, KCMTableSnippet.cpp.
