//========================================================================================
//
//  KCMTableXmlCheck.cpp -- see the header.
//
//========================================================================================

// FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including this line.
#include "VCPlugInHeaders.h"

#include "KCMTableXmlCheck.h"

#include <cstdlib>
#include <map>
#include <set>

#include "KCMXmlDeepCompare.h"		// KCMLooksLikeUid - what counts as a uid is decided in one place
#include "KCMXmlTree.h"

namespace
{

/** One cut of the <Table> element: the tree, and its children by kind and by their Name. */
struct TableXml
{
	KCMXmlTree						fTree;
	int32							fTable;
	std::map<std::string, int32>	fRows;		// Row Name -> node
	std::map<std::string, int32>	fCols;		// Column Name -> node
	std::map<std::string, int32>	fCells;		// Cell Name ("column:row") -> node
	std::vector<int32>				fOthers;	// any other element child of the table, in order

	TableXml() : fTable(-1) {}
};

bool16 Load(const std::string& xml, const char* which, TableXml& out, std::string& why)
{
	std::string whyNot;
	if (!out.fTree.Parse(xml.data(), xml.size(), whyNot))
	{
		why = std::string("the ") + which + " table's XML does not read (" + whyNot + ")";
		return kFalse;
	}
	out.fTable = out.fTree.Root();
	if (out.fTable < 0 || out.fTree.At(out.fTable).fName != "Table")
	{
		why = std::string("the ") + which + " XML is not one table";
		return kFalse;
	}
	const KCMXmlNode& table = out.fTree.At(out.fTable);
	for (size_t i = 0; i < table.fChildren.size(); ++i)
	{
		const int32 child = table.fChildren[i];
		const KCMXmlNode& node = out.fTree.At(child);
		if (node.IsText())
			continue;		// the space between the table's children, if the export writes any
		const std::string* const name = out.fTree.Attr(child, "Name");
		if (node.fName == "Row" && name != nil)
			out.fRows[*name] = child;
		else if (node.fName == "Column" && name != nil)
			out.fCols[*name] = child;
		else if (node.fName == "Cell" && name != nil)
			out.fCells[*name] = child;
		else
			out.fOthers.push_back(child);
	}
	return kTrue;
}

/** A value as it goes into a sentence: long ones cut. */
std::string Shown(const std::string& v)
{
	return (v.size() > 60) ? v.substr(0, 57) + "..." : v;
}

/** Whether every space-separated part of both values is uid-shaped, and there are as many on each side - a list of
	references to objects by their ids. */
bool16 BothUidShaped(const std::string& a, const std::string& b)
{
	std::vector<std::string> pa, pb;
	size_t from = 0;
	for (size_t i = 0; i <= a.size(); ++i)
		if (i == a.size() || a[i] == ' ') { if (i > from) pa.push_back(a.substr(from, i - from)); from = i + 1; }
	from = 0;
	for (size_t i = 0; i <= b.size(); ++i)
		if (i == b.size() || b[i] == ' ') { if (i > from) pb.push_back(b.substr(from, i - from)); from = i + 1; }
	if (pa.empty() || pa.size() != pb.size())
		return kFalse;
	for (size_t i = 0; i < pa.size(); ++i)
		if (!KCMLooksLikeUid(pa[i]) || !KCMLooksLikeUid(pb[i]))
			return kFalse;
	return kTrue;
}

/** The attributes that only say WHICH object this is - see the header for each measurement. */
bool16 IgnoredAttr(const std::string& element, const std::string& attr)
{
	return (attr == "Self" || (element == "PageReference" && attr == "Id")) ? kTrue : kFalse;
}

/** The table's attributes that ARE its shape - always the Source's after a match, whether or not a cell was pasted:
	a match that only took rows away pastes nothing, and still leaves the Source's row count (live-rows A E I). */
bool16 IsShapeAttr(const std::string& attr)
{
	return (attr == "BodyRowCount" || attr == "HeaderRowCount" || attr == "FooterRowCount" || attr == "ColumnCount")
		? kTrue : kFalse;
}

enum AttrScope { kAllAttrs = 0, kShapeOnly = 1, kAllButShape = 2 };

bool16 SameAttrs(const KCMXmlTree& a, int32 an, const KCMXmlTree& b, int32 bn, const std::string& where, std::string& why,
				 AttrScope scope = kAllAttrs)
{
	const KCMXmlNode& na = a.At(an);
	const KCMXmlNode& nb = b.At(bn);
	std::map<std::string, std::string> ma, mb;
	for (size_t i = 0; i < na.fAttrs.size(); ++i)
		ma[na.fAttrs[i].fName] = na.fAttrs[i].fValue;
	for (size_t i = 0; i < nb.fAttrs.size(); ++i)
		mb[nb.fAttrs[i].fName] = nb.fAttrs[i].fValue;
	std::set<std::string> names;
	for (std::map<std::string, std::string>::const_iterator it = ma.begin(); it != ma.end(); ++it)
		names.insert(it->first);
	for (std::map<std::string, std::string>::const_iterator it = mb.begin(); it != mb.end(); ++it)
		names.insert(it->first);
	for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
	{
		if (IgnoredAttr(na.fName, *it))
			continue;
		if ((scope == kShapeOnly && !IsShapeAttr(*it)) || (scope == kAllButShape && IsShapeAttr(*it)))
			continue;
		const bool16 inA = (ma.find(*it) != ma.end()) ? kTrue : kFalse;
		const bool16 inB = (mb.find(*it) != mb.end()) ? kTrue : kFalse;
		const std::string va = inA ? ma[*it] : std::string("(absent)");
		const std::string vb = inB ? mb[*it] : std::string("(absent)");
		if (va == vb)
			continue;
		if (inA && inB && BothUidShaped(va, vb))
			continue;		// a reference to an object by its id, on both sides
		why = where + ": " + *it + " is " + Shown(va) + " here and " + Shown(vb) + " there";
		return kFalse;
	}
	return kTrue;
}

/** The whole subtree: the element's name, its attributes, its children in order, their text. */
bool16 SameTree(const KCMXmlTree& a, int32 an, const KCMXmlTree& b, int32 bn, const std::string& where, std::string& why)
{
	const KCMXmlNode& na = a.At(an);
	const KCMXmlNode& nb = b.At(bn);
	if (na.IsText() || nb.IsText())
	{
		if (na.IsText() != nb.IsText())
		{
			why = where + ": text here and an element there, or the other way round";
			return kFalse;
		}
		if (na.fText != nb.fText)
		{
			why = where + ": the text is \"" + Shown(na.fText) + "\" here and \"" + Shown(nb.fText) + "\" there";
			return kFalse;
		}
		return kTrue;
	}
	if (na.fName != nb.fName)
	{
		why = where + ": <" + na.fName + "> here and <" + nb.fName + "> there";
		return kFalse;
	}
	if (!SameAttrs(a, an, b, bn, where, why))
		return kFalse;
	if (na.fChildren.size() != nb.fChildren.size())
	{
		why = where + ": " + std::to_string(na.fChildren.size()) + " item(s) inside here and "
			  + std::to_string(nb.fChildren.size()) + " there";
		return kFalse;
	}
	std::map<std::string, int32> seen;		// the n-th child of each name, for the path
	for (size_t i = 0; i < na.fChildren.size(); ++i)
	{
		const KCMXmlNode& child = a.At(na.fChildren[i]);
		const std::string name = child.IsText() ? std::string("(text)") : child.fName;
		const int32 index = seen[name]++;
		if (!SameTree(a, na.fChildren[i], b, nb.fChildren[i], where + " > " + name + "[" + std::to_string(index) + "]", why))
			return kFalse;
	}
	return kTrue;
}

/** "column:row" -> the two numbers. kFalse when the Name is not of that shape. */
bool16 CellAddress(const std::string& name, int32& outCol, int32& outRow)
{
	const size_t colon = name.find(':');
	if (colon == std::string::npos || colon == 0 || colon + 1 >= name.size())
		return kFalse;
	outCol = static_cast<int32>(std::atoi(name.substr(0, colon).c_str()));
	outRow = static_cast<int32>(std::atoi(name.substr(colon + 1).c_str()));
	return kTrue;
}

int32 SpanOf(const KCMXmlTree& tree, int32 cell, const char* attr)
{
	const std::string* const v = tree.Attr(cell, attr);
	const int32 n = (v != nil) ? static_cast<int32>(std::atoi(v->c_str())) : 1;
	return (n > 0) ? n : 1;
}

std::string Num(int32 n)
{
	return std::to_string(n);
}

/** The same keys on both sides; the first one missing on either side is named. */
bool16 SameNames(const std::map<std::string, int32>& a, const std::map<std::string, int32>& b, const char* what,
				 std::string& why)
{
	for (std::map<std::string, int32>::const_iterator it = b.begin(); it != b.end(); ++it)
		if (a.find(it->first) == a.end())
		{
			why = std::string("the Source's ") + what + " " + it->first + " is not in the table";
			return kFalse;
		}
	for (std::map<std::string, int32>::const_iterator it = a.begin(); it != a.end(); ++it)
		if (b.find(it->first) == b.end())
		{
			why = std::string("the table has a ") + what + " " + it->first + " the Source has not";
			return kFalse;
		}
	return kTrue;
}

}	// anonymous namespace

bool16 KCMTableXmlMatches(const std::string& after, const std::string& source, const std::string& before,
						  const std::vector<std::string>& keptCells, std::string& outWhy)
{
	outWhy.clear();
	TableXml a, s, b;
	if (!Load(after, "matched", a, outWhy) || !Load(source, "Source's", s, outWhy) || !Load(before, "earlier", b, outWhy))
		return kFalse;

	// the Source's cells, rows and columns - no more, no fewer
	if (!SameNames(a.fCells, s.fCells, "cell", outWhy) || !SameNames(a.fRows, s.fRows, "row", outWhy)
		|| !SameNames(a.fCols, s.fCols, "column", outWhy))
		return kFalse;

	// what the match touched: every cell not left alone, and the rows and columns it stands in
	const std::set<std::string> kept(keptCells.begin(), keptCells.end());
	std::set<std::string> touchedRows, touchedCols;
	bool16 anyTouched = kFalse;
	for (std::map<std::string, int32>::const_iterator it = s.fCells.begin(); it != s.fCells.end(); ++it)
	{
		if (kept.find(it->first) != kept.end())
			continue;
		anyTouched = kTrue;
		int32 col = 0, row = 0;
		if (!CellAddress(it->first, col, row))
		{
			outWhy = "the Source's cell \"" + it->first + "\" has a name that is not column:row";
			return kFalse;
		}
		const int32 rows = SpanOf(s.fTree, it->second, "RowSpan");
		const int32 cols = SpanOf(s.fTree, it->second, "ColumnSpan");
		for (int32 r = row; r < row + rows; ++r)
			touchedRows.insert(Num(r));
		for (int32 c = col; c < col + cols; ++c)
			touchedCols.insert(Num(c));
	}

	// the table's own attributes: its shape always the Source's; the rest the Source's once anything was pasted (eAll
	// writes them), else as they were
	const TableXml& tableRef = anyTouched ? s : b;
	const char* const tableAgainst = anyTouched ? " (against the Source)" : " (against the table before the match)";
	if (!SameAttrs(a.fTree, a.fTable, s.fTree, s.fTable, "the table's shape (against the Source)", outWhy, kShapeOnly)
		|| !SameAttrs(a.fTree, a.fTable, tableRef.fTree, tableRef.fTable, std::string("the table") + tableAgainst, outWhy,
					  kAllButShape))
		return kFalse;
	if (a.fOthers.size() != tableRef.fOthers.size())
	{
		outWhy = std::string("the table holds ") + Num(static_cast<int32>(a.fOthers.size())) + " other item(s) and "
				 + Num(static_cast<int32>(tableRef.fOthers.size())) + " were expected" + tableAgainst;
		return kFalse;
	}
	for (size_t i = 0; i < a.fOthers.size(); ++i)
		if (!SameTree(a.fTree, a.fOthers[i], tableRef.fTree, tableRef.fOthers[i],
					  "the table > " + a.fTree.At(a.fOthers[i]).fName + tableAgainst, outWhy))
			return kFalse;

	// rows and columns: the Source's where a cell was pasted into them, else as they were
	for (std::map<std::string, int32>::const_iterator it = a.fRows.begin(); it != a.fRows.end(); ++it)
	{
		const bool16 fromSource = (touchedRows.find(it->first) != touchedRows.end() || b.fRows.find(it->first) == b.fRows.end())
			? kTrue : kFalse;
		const TableXml& ref = fromSource ? s : b;
		if (!SameTree(a.fTree, it->second, ref.fTree, ref.fRows.find(it->first)->second,
					  "row " + it->first + (fromSource ? " (against the Source)" : " (against the table before the match)"), outWhy))
			return kFalse;
	}
	for (std::map<std::string, int32>::const_iterator it = a.fCols.begin(); it != a.fCols.end(); ++it)
	{
		const bool16 fromSource = (touchedCols.find(it->first) != touchedCols.end() || b.fCols.find(it->first) == b.fCols.end())
			? kTrue : kFalse;
		const TableXml& ref = fromSource ? s : b;
		if (!SameTree(a.fTree, it->second, ref.fTree, ref.fCols.find(it->first)->second,
					  "column " + it->first + (fromSource ? " (against the Source)" : " (against the table before the match)"), outWhy))
			return kFalse;
	}

	// cells: the Source's, except the ones left alone, which must be exactly as they were
	for (std::map<std::string, int32>::const_iterator it = a.fCells.begin(); it != a.fCells.end(); ++it)
	{
		if (kept.find(it->first) != kept.end())
		{
			std::map<std::string, int32>::const_iterator was = b.fCells.find(it->first);
			if (was == b.fCells.end())
			{
				outWhy = "cell " + it->first + " was to be left alone but was not in the table before the match";
				return kFalse;
			}
			if (!SameTree(a.fTree, it->second, b.fTree, was->second, "cell " + it->first + " (left alone - against the table before the match)", outWhy))
				return kFalse;
			continue;
		}
		if (!SameTree(a.fTree, it->second, s.fTree, s.fCells.find(it->first)->second, "cell " + it->first + " (against the Source)", outWhy))
			return kFalse;
	}
	return kTrue;
}

// End, KCMTableXmlCheck.cpp.
