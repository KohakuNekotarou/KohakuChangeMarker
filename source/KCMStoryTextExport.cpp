//========================================================================================
//
//  KCMStoryTextExport.cpp -- see the header.
//
//  The shape of the work: ask KCMTextRead for a story's paragraphs (it already reports, for each
//  one, whether it is body text, a cell of some table, or a footnote's own words), ask ITableModel
//  for the things a paragraph cannot know - how many rows the table has, which cells are merged,
//  which rows are header rows - hand both to KCMStoryHtml::Write, and put the bytes in a file.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// CreateDirectoryW - Windows only, like the rest of KCM's file work
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>

#include "IDataBase.h"
#include "IDocument.h"
#include "IPMStream.h"
#include "IStoryList.h"
#include "IStoryOptions.h"			// IsVertical - the story's own setting, not a frame's
#include "ITableModel.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"		// a footnote reference IS its note's thread - which note a reference belongs to
#include "ITextStoryThreadDict.h"
#include "ITextStoryThreadDictHier.h"
#include "ITextUtils.h"				// CollectOwnedItems + OwnedItemDataList - the walk KCMTextRead::ScanNotes uses
#include "FileUtils.h"
#include "StreamUtil.h"
#include "TableTypes.h"
#include "TextID.h"					// kFootnoteReferenceBoss
#include "UIDList.h"
#include "UIDRef.h"
#include "WideString.h"

#include "KCMStoryTextExport.h"
#include "KCMStoryHtml.h"
#include "KCMStoryDocx.h"		// the .docx road (2026-09-19)
#include "KCMTextRead.h"
#include "KCMParaText.h"
#include "KCMTextDiff.h"		// ToCodePoints - the one walk over UTF-8 this half is allowed

namespace
{

/** The path of an IDFile as Windows spells it. (KCMReport.cpp has the same three lines; this one
	is here rather than shared because the two files share nothing else, and a header holding one
	helper would be a worse thing to maintain than four lines.) */
std::wstring WidePath(const IDFile& file)
{
	PMString s;
	FileUtils::IDFileToPMString(file, s);
	int32 n = 0;
	const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
	return (b != nil && n > 0)
		   ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n))
		   : std::wstring();
}

/** The document's name, with any extension taken off - the stem of the folder's name. */
std::wstring DocumentStem(IDataBase* db)
{
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc == nil)
		return std::wstring(L"document");

	PMString name;
	doc->GetName(name);
	int32 n = 0;
	const UTF16TextChar* b = name.GrabUTF16Buffer(&n);
	std::wstring stem = (b != nil && n > 0)
						? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n))
						: std::wstring();

	const size_t dot = stem.find_last_of(L'.');
	if (dot != std::wstring::npos && dot > 0)
		stem = stem.substr(0, dot);

	// ⚠A DOCUMENT NAME IS NOT A FILE NAME. An untitled document's name is fine, but anything a
	//   reader has typed can hold a character Windows will not take in a path.
	for (size_t i = 0; i < stem.size(); ++i)
	{
		const wchar_t c = stem[i];
		if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"'
			|| c == L'<' || c == L'>' || c == L'|')
			stem[i] = L'_';
	}
	if (stem.empty())
		stem = L"document";
	return stem;
}

/** "<parent>\<stem> YYYY-MM-DD HHMMSS", created. kFalse when it could not be made. */
bool16 MakeDatedFolder(const std::wstring& parent, const std::wstring& stem, std::wstring& outFolder)
{
	wchar_t stamp[40] = { 0 };
	{
		time_t now = ::time(nil);
		struct tm local;
		::localtime_s(&local, &now);
		::wcsftime(stamp, 40, L" %Y-%m-%d %H%M%S", &local);
	}

	std::wstring folder = parent;
	if (!folder.empty() && folder[folder.size() - 1] != L'\\' && folder[folder.size() - 1] != L'/')
		folder += L"\\";
	folder += stem;
	folder += stamp;

	// ⚠ERROR_ALREADY_EXISTS is not success: the stamp runs to the second, so a folder of this name
	//   already standing there was made by something else, and writing into it would be the silent
	//   overwrite the stamp exists to prevent.
	if (::CreateDirectoryW(folder.c_str(), nil) == 0)
		return kFalse;

	outFolder = folder;
	return kTrue;
}

/*	TableShape
	What a table is, as against what its text says - none of which a paragraph's attributes carry.
*/
struct CellShape
{
	int32	fRow;
	int32	fCol;
	int32	fRowSpan;
	int32	fColSpan;

	CellShape() : fRow(0), fCol(0), fRowSpan(1), fColSpan(1) {}
};

struct TableShape
{
	// ⚠**TWO PLACES, AND THEY ARE NOWHERE NEAR EACH OTHER.** fStart is where the table's CELLS
	//  begin and fAnchor is where the table STANDS in the text. ITableTextContent.h:41-44 is the
	//  reason they have to be kept apart: a table's threads are "ALWAYS at greater TextIndex than
	//  the Text Story Thread that the Table Model is anchored in", so fStart is past the whole
	//  body for EVERY table of a story - it orders the tables and says nothing about where any of
	//  them stands.
	TextIndex				fStart;			// where the cells' text begins - the ordinals' order
	TextIndex				fAnchor;		// where the table stands in the text
	int32					fRowCount;
	int32					fHeaderStart;
	int32					fHeaderCount;
	std::vector<CellShape>	fCells;			// the anchors only, in row then column order

	TableShape() : fStart(0), fAnchor(0), fRowCount(0), fHeaderStart(0), fHeaderCount(0) {}
};

bool16 EarlierTable(const TableShape& a, const TableShape& b)
{
	return a.fStart < b.fStart;
}

/*	ReadTableShapes
	Every table of a story, in the order they stand in it.

	★**THE WALK IS KCMTextRead's**, which is Adobe's own (SnpIterTableUseDictHier): a dictionary IS
	a table exactly when an ITableModel can be got from it. Keeping the same shape matters because
	the ORDER this produces has to be the order KCMTextRead numbered the cells in - fTableOrdinal
	is an index into this list.

	⚠**A MERGED CELL IS VISITED ONCE, AT ITS ANCHOR.** The covered addresses have no thread of their
	 own, and GetCellArea at the anchor is what says how far it reaches.
*/
bool16 ReadTableShapes(ITextModel* model, std::vector<TableShape>& out)
{
	out.clear();

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;				// no hierarchy at all: a story with nothing but a body

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
			continue;				// the story's own dictionary

		TableShape shape;
		shape.fStart = dict->GetThreadBlockTextRange().Start(nil);

		// ★★★**WHERE THE TABLE STANDS IS A QUESTION OF ITS OWN, AND THE DICTIONARY ANSWERS IT.**
		//   GetThreadBlockTextRange is "the StoryRange of text spanned by the threads of the
		//   dictionary" - the CELLS - and those are always past the whole body, so it answers
		//   "after the last paragraph" for every table there has ever been. GetAnchorTextRange is
		//   the anchor itself, and its contract covers the odd case too: a dictionary that is not
		//   anchored "should return the TextIndex of the last carriage return in the primary story
		//   thread" (ITextStoryThreadDict.h), which is the same end-of-body answer the old code
		//   gave by accident - so no guard is needed here, only the right question.
		//   ⚠**MEASURED 2026-09-16**: a story with three tables wrote all three at the end of the
		//    file, whatever paragraph each one really stood after.
		//   ★A NESTED table's anchor is inside a CELL, so it still lands after the last body
		//    paragraph - which is where the writer puts it anyway (a nested table is a table of
		//    its own in this format, KCMStoryHtml's WriteTable says why).
		shape.fAnchor = dict->GetAnchorTextRange().Start(nil);

		const RowRange rows = table->GetTotalRows();
		const ColRange cols = table->GetTotalCols();
		const RowRange header = table->GetHeaderRows();
		shape.fRowCount = rows.count;
		shape.fHeaderStart = header.start;
		shape.fHeaderCount = header.count;

		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		{
			for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			{
				const GridAddress addr(r, c);
				if (!table->IsValid(addr) || !table->IsAnchor(addr))
					continue;

				CellShape cell;
				cell.fRow = r;
				cell.fCol = c;

				const GridArea area = table->GetCellArea(addr);
				const RowRange areaRows = area.GetRows();
				const ColRange areaCols = area.GetCols();
				cell.fRowSpan = (areaRows.count > 0) ? areaRows.count : 1;
				cell.fColSpan = (areaCols.count > 0) ? areaCols.count : 1;

				shape.fCells.push_back(cell);
			}
		}
		out.push_back(shape);
	}

	std::sort(out.begin(), out.end(), EarlierTable);
	return kTrue;
}

/** One paragraph of KCMTextRead's, in the shape the writer wants.

	⚠**A NOTE'S REFERENCE DOES NOT TRAVEL** (2026-09-16, the user's decision). The file carries
	 a note's WORDS - a paragraph of its own, after the body - and not the place in the body
	 where its marker stood: what the reader edits is the words. KCMStoryHtml::Para says what
	 that removed. attrs.fFootnote is therefore read by nobody here. */
void FillPara(const std::string& text, const KCMParaAttrs& attrs, KCMStoryHtml::Para& out)
{
	out.fText = text;
	out.fRuby = attrs.fRuby;
	out.fKenten = attrs.fKenten;
	// ★TATE-CHU-YOKO TRAVELS TOO (2026-09-17, the user's request). Its value is already the characters
	//   it covers - KCMTextRead settles that when it closes the paragraph - which is exactly what
	//   KCMStoryHtml's reader produces, so the self-check compares like with like.
	out.fTcy = attrs.fTcy;
	// ★AND WARICHU (2026-09-17, the same request for it), the same way: its ON/OFF and the characters
	//   it covers, none of its settings.
	out.fWarichu = attrs.fWarichu;

	// ★★★**A READING OVER ONE CHARACTER IS ALWAYS MONO** (the user's rule, 2026-09-16). One
	//   character with one reading is the same typesetting whichever way the document has it set,
	//   and the markup cannot tell the two apart - <ruby>立<rt>た</rt></ruby> either way - so both
	//   sides settle it here rather than letting the trip decide.
	//   ⚠**THE DOCUMENT IS NOT TOUCHED BY THIS.** It changes what the FILE says. ⚠**IT DOES TRAVEL
	//    BACK, THOUGH**: since 2026-09-16 the import pours a paragraph's readings into the copy with
	//    the file's own mono/group setting (KCMStoryAttrPour) - which is exactly why the rule has to
	//    be the same on both sides. (This note said "the import uses fText and nothing else" until
	//    2026-09-17; that stopped being true the day the pour went in.)
	for (size_t k = 0; k < out.fRuby.size(); ++k)
	{
		if (out.fRuby[k].fLen == 1)
			out.fRuby[k].fGroup = kFalse;
	}
}

/*	BuildStory
	One story, as the writer wants it: a body, its tables, and its footnotes.
*/
bool16 BuildStory(const UIDRef& storyRef, KCMStoryHtml::Story& out, bool16& outNoteRefsPlaced)
{
	out = KCMStoryHtml::Story();
	outNoteRefsPlaced = kTrue;

	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
		return kFalse;

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	std::vector<TableShape> shapes;
	if (model != nil && !ReadTableShapes(model, shapes))
		return kFalse;

	// ★**WHICH WAY THE STORY IS SET**, asked of the STORY rather than of a frame: it is a story
	//   setting (IID_ISTORYOPTIONS is on kTextStoryBoss, and the official form is exactly this -
	//   basicme/BscMEInvertFacade.cpp:198, codesnippetsME/SnpCreateFrameME.cpp:207), so a story
	//   with no frame on any page still answers.
	InterfacePtr<IStoryOptions> options(model, UseDefaultIID());
	out.fVertical = (options != nil && options->IsVertical()) ? kTrue : kFalse;

	// ---- the tables, empty of text for the moment ------------------------------------------
	for (size_t t = 0; t < shapes.size(); ++t)
	{
		KCMStoryHtml::Table table;
		table.fOrdinal = static_cast<int32>(t);
		table.fParaIndex = 0;
		table.fOffset = 0;
		// ⚠**THE EXPORTER NEVER SPLITS A PARAGRAPH.** KCMTextRead reports a paragraph holding a
		//   table as ONE paragraph (the table's own character is simply not counted), so the table
		//   is written after it whole. fSplitsPara is the reader's side of a shape this half does
		//   not produce.
		table.fSplitsPara = kFalse;

		for (int32 r = 0; r < shapes[t].fRowCount; ++r)
		{
			KCMStoryHtml::Row row;
			row.fHeader = (r >= shapes[t].fHeaderStart
						   && r < shapes[t].fHeaderStart + shapes[t].fHeaderCount) ? kTrue : kFalse;
			for (size_t c = 0; c < shapes[t].fCells.size(); ++c)
			{
				if (shapes[t].fCells[c].fRow != r)
					continue;
				KCMStoryHtml::Cell cell;
				cell.fColSpan = shapes[t].fCells[c].fColSpan;
				cell.fRowSpan = shapes[t].fCells[c].fRowSpan;
				row.fCells.push_back(cell);
			}
			table.fRows.push_back(row);
		}
		out.fTables.push_back(table);
	}

	// ---- the paragraphs, each into the place it belongs ---------------------------------------
	//
	// ★**WHERE A PARAGRAPH LANDS IS ALSO WHERE A TABLE ANCHORED IN IT LANDS**, so the walk keeps
	//   these for EVERY paragraph - the cells' and the notes' included, because a table can stand
	//   in a cell and that is what a nested one is.
	std::vector<int32> placeIndex(paras.size(), -1);	// its number within its own place
	std::vector<int32> cellWhich(paras.size(), -1);		// a cell's place in its row, as the <td>s run
	// ⚠**AND WHERE EACH ONE ENDS**, which is not the next one's start: a table anchor standing at
	//  the head of a paragraph is stepped OVER by the reader (KCMTextRead: "if (!paraHasCharacters)
	//  paraStart = i + 1"), so that paragraph is reported as beginning AFTER its own anchor.
	std::vector<TextIndex> paraEnds(paras.size(), 0);

	for (size_t i = 0; i < paras.size(); ++i)
	{
		// The model length of this paragraph: its characters, plus the positions the model counts
		// and the text does not (fUncountedAt - a table standing INSIDE the paragraph).
		std::vector<int32> cps;
		KCMTextDiff::ToCodePoints(paras[i], &cps, nil);
		paraEnds[i] = static_cast<TextIndex>(starts[i])
					  + static_cast<TextIndex>(cps.size())
					  + static_cast<TextIndex>(attrs[i].fUncountedAt.size());

		KCMStoryHtml::Para p;
		FillPara(paras[i], attrs[i], p);

		if (attrs[i].IsCell())
		{
			const size_t t = static_cast<size_t>(attrs[i].fTableOrdinal);
			if (t >= out.fTables.size())
				continue;						// a cell of a table the walk did not find

			// The cell at that grid address, among the anchors of its row.
			KCMStoryHtml::Table& table = out.fTables[t];
			if (attrs[i].fCellRow < 0 || static_cast<size_t>(attrs[i].fCellRow) >= table.fRows.size())
				continue;

			KCMStoryHtml::Row& row = table.fRows[static_cast<size_t>(attrs[i].fCellRow)];
			size_t which = 0;
			bool16 found = kFalse;
			for (size_t c = 0; c < shapes[t].fCells.size(); ++c)
			{
				if (shapes[t].fCells[c].fRow != attrs[i].fCellRow)
					continue;
				if (shapes[t].fCells[c].fCol == attrs[i].fCellCol)
				{
					found = kTrue;
					break;
				}
				++which;
			}
			if (found && which < row.fCells.size())
			{
				row.fCells[which].fParas.push_back(p);
				cellWhich[i] = static_cast<int32>(which);
				placeIndex[i] = static_cast<int32>(row.fCells[which].fParas.size()) - 1;
			}
			continue;
		}

		if (attrs[i].IsFootnote())
		{
			const size_t n = static_cast<size_t>(attrs[i].fFootnoteOrdinal);
			while (out.fNotes.size() <= n)
				out.fNotes.push_back(std::vector<KCMStoryHtml::Para>());
			out.fNotes[n].push_back(p);
			placeIndex[i] = static_cast<int32>(out.fNotes[n].size()) - 1;
			continue;
		}

		out.fBody.push_back(p);
		placeIndex[i] = static_cast<int32>(out.fBody.size()) - 1;
	}

	// ---- where each table stands --------------------------------------------------------------
	//
	// ★**THE ANCHOR, NOT THE CELLS** (2026-09-16). fStart is past every paragraph of the body for
	//   EVERY table of the story (ReadTableShapes says why), so asking it this question handed
	//   each table the LAST paragraph and the file came out with all of its tables at the
	//   bottom, whatever paragraph each one really stood after.
	// ★★**AND THE PARAGRAPH IT STANDS IN MAY BE A CELL'S** (2026-09-16, the user's request), which
	//   is what a nested table is: the walk below is over EVERY paragraph rather than the body's,
	//   and where the anchor lands is where the table is written.
	// ★**WHERE IN THE PARAGRAPH EACH ONE STANDS**, counted per paragraph: a paragraph can hold more
	//   than one table, and the tables are walked in document order, so taking the positions in turn
	//   pairs them up.
	std::vector<int32> spotsUsed(paras.size(), 0);

	for (size_t t = 0; t < out.fTables.size() && t < shapes.size(); ++t)
	{
		int32 host = -1;
		for (size_t i = 0; i < paras.size(); ++i)
		{
			if (static_cast<TextIndex>(starts[i]) <= shapes[t].fAnchor)
				host = static_cast<int32>(i);
		}

		// ★★★**AN ANCHOR CAN STAND IN THE GAP BETWEEN TWO REPORTED PARAGRAPHS**, and that is the
		//   ordinary case rather than the odd one: a table of its own makes a paragraph holding
		//   nothing else, whose reported start is PAST the anchor, so the loop above lands on the
		//   paragraph BEFORE it. Asking whether the anchor is past the end of that paragraph's own
		//   text is what tells the two apart - a table standing inside a paragraph is not.
		//   ⚠MEASURED 2026-09-16 on allin.indd: without this, two tables of one story came out one
		//    paragraph early each, with the empty paragraphs they live in left standing behind them.
		if (host >= 0 && host + 1 < static_cast<int32>(paras.size())
			&& shapes[t].fAnchor >= paraEnds[static_cast<size_t>(host)])
		{
			++host;
		}

		if (host < 0 || placeIndex[static_cast<size_t>(host)] < 0)
		{
			// Nowhere to put it - a cell the walk could not place, or a story with no paragraphs
			// at all. The body's top is where it can still be seen.
			out.fTables[t].fInTable = -1;
			out.fTables[t].fParaIndex = 0;
			continue;
		}

		const KCMParaAttrs& hostAttrs = attrs[static_cast<size_t>(host)];
		if (hostAttrs.IsCell())
		{
			// ★**THE CELL IS NAMED THE WAY THE TWO SIDES PAIR CELLS**: its place among the anchors
			//   of its row, which is the order the <td>s run - not its grid column. ColumnsOfRow
			//   produces the same number on the import's side, so a merged cell is one cell in both.
			out.fTables[t].fInTable = hostAttrs.fTableOrdinal;
			out.fTables[t].fInRow = hostAttrs.fCellRow;
			out.fTables[t].fInCell = cellWhich[static_cast<size_t>(host)];
		}
		else
		{
			// ⚠**A TABLE ANCHORED IN A FOOTNOTE IS WRITTEN AS THE BODY'S.** InDesign does not let
			//  one be put there, so this is a case nobody can produce; inventing a third place for
			//  it would be a shape with no reader rather than a safeguard.
			out.fTables[t].fInTable = -1;
		}
		// ★★**WHERE IN THAT PARAGRAPH IT STANDS**, in the text's own count - which is what lets the
		//   writer put it back among the characters rather than after all of them.
		//   ⚠**AN ANCHOR AT THE HEAD OF A PARAGRAPH LEAVES NO TRACE IN fUncountedAt**: the reader
		//    steps over it and moves the paragraph's start instead (KCMTextRead, "if
		//    (!paraHasCharacters) paraStart = i + 1"), so that case is told by the anchor standing
		//    BEFORE the reported start, and its place is 0.
		if (shapes[t].fAnchor < static_cast<TextIndex>(starts[static_cast<size_t>(host)]))
		{
			out.fTables[t].fOffset = 0;
		}
		else
		{
			// fUncountedAt holds ONE ENTRY PER CHARACTER the table costs the model (the anchor,
			// plus one per row after the first), and they all sit at the same place in the text -
			// so the distinct values, in order, are where this paragraph's tables stand.
			const std::vector<int32>& un = attrs[static_cast<size_t>(host)].fUncountedAt;
			std::vector<int32> spots;
			for (size_t k = 0; k < un.size(); ++k)
			{
				if (spots.empty() || spots.back() != un[k])
					spots.push_back(un[k]);
			}

			const size_t which = static_cast<size_t>(spotsUsed[static_cast<size_t>(host)]);
			++spotsUsed[static_cast<size_t>(host)];
			out.fTables[t].fOffset = (which < spots.size()) ? spots[which] : 0;
		}

		out.fTables[t].fParaIndex = placeIndex[static_cast<size_t>(host)];
	}

	// ---- where each footnote's reference stands (2026-09-19, for the .docx format) ---------------
	//
	// ★★**THE HTML FORMAT DOES NOT CARRY THIS AND THE DOCX ONE HAS TO** (KCMStoryHtml::NoteRef): Word
	//   cannot hold a footnote without its reference in the text. It is filled for both - HTML
	//   never looks - so that there is one BuildStory and not two.
	// ★THE WALK IS KCMTextRead::ScanNotes' OWN (CollectOwnedItems, kFootnoteReferenceBoss). What
	//   that one cannot give is asked of the document here: ScanNotes clips away a reference that
	//   is its paragraph's first character and reports the PRINTED number, while this needs every
	//   reference and the note's ORDINAL.
	//     - which note: the reference boss IS the note's own story thread (ITextStoryThread), so
	//       the thread's range says which footnote paragraphs are its words;
	//     - which paragraph: the last one beginning at or before the reference, where "beginning"
	//       is before whatever the reader stepped over (fLeadingUncounted);
	//     - where in it: the model's distance from the paragraph's start, less the uncounted
	//       positions standing before it - the inverse of KCMParaText::ModelOffsetInParagraph.
	//       The reference is itself one of those positions (KCMTextRead: "A NOTE'S MARKER IS A
	//       POSITION, NOT TEXT"), which is why the comparison below is a strict one.
	// ⚠ANY REFERENCE THAT CANNOT BE PLACED SAYS SO, and the caller then refuses the .docx for this
	//   story rather than writing a note Word would lose. The HTML road is not affected.
	outNoteRefsPlaced = kTrue;
	{
		Utils<ITextUtils> textUtils;
		OwnedItemDataList owned;
		if (model != nil && textUtils != nil && model->TotalLength() > 0)
			textUtils->CollectOwnedItems(model, 0, model->TotalLength() - 1, &owned);

		IDataBase* const db = storyRef.GetDataBase();
		for (int32 k = 0; k < static_cast<int32>(owned.size()); ++k)
		{
			if (owned[k].fClassID != kFootnoteReferenceBoss)
				continue;

			const TextIndex refAt = owned[k].fAt;

			// ---- which note ---------------------------------------------------------------------
			int32 note = -1;
			InterfacePtr<ITextStoryThread> noteThread(db, owned[k].fUID, UseDefaultIID());
			if (noteThread != nil)
			{
				int32 span = 0;
				const TextIndex noteStart = noteThread->GetTextStart(&span);
				for (size_t j = 0; j < paras.size() && note < 0; ++j)
				{
					if (!attrs[j].IsFootnote())
						continue;
					const TextIndex lineStart = static_cast<TextIndex>(starts[j])
												- static_cast<TextIndex>(attrs[j].fLeadingUncounted);
					if (lineStart >= noteStart && lineStart < noteStart + span)
						note = attrs[j].fFootnoteOrdinal;
				}
			}

			// ---- which paragraph ------------------------------------------------------------------
			int32 host = -1;
			for (size_t i = 0; i < paras.size(); ++i)
			{
				const TextIndex lineStart = static_cast<TextIndex>(starts[i])
											- static_cast<TextIndex>(attrs[i].fLeadingUncounted);
				if (lineStart <= refAt && refAt <= paraEnds[i])
					host = static_cast<int32>(i);
			}

			if (note < 0 || host < 0 || attrs[static_cast<size_t>(host)].IsFootnote()
				|| placeIndex[static_cast<size_t>(host)] < 0)
			{
				outNoteRefsPlaced = kFalse;
				continue;
			}

			// ---- where in it ------------------------------------------------------------------------
			const size_t h = static_cast<size_t>(host);
			int32 textOffset = 0;
			if (refAt >= static_cast<TextIndex>(starts[h]))
			{
				const std::vector<int32>& un = attrs[h].fUncountedAt;
				int32 before = 0;
				while (static_cast<size_t>(before) < un.size()
					   && static_cast<TextIndex>(starts[h]) + un[static_cast<size_t>(before)] + before < refAt)
					++before;
				textOffset = static_cast<int32>(refAt - static_cast<TextIndex>(starts[h])) - before;
			}

			// ---- and into the paragraph it belongs to ---------------------------------------------
			KCMStoryHtml::Para* para = nil;
			if (attrs[h].IsCell())
			{
				const size_t t = static_cast<size_t>(attrs[h].fTableOrdinal);
				if (t < out.fTables.size() && attrs[h].fCellRow >= 0
					&& static_cast<size_t>(attrs[h].fCellRow) < out.fTables[t].fRows.size()
					&& cellWhich[h] >= 0)
				{
					KCMStoryHtml::Row& row = out.fTables[t].fRows[static_cast<size_t>(attrs[h].fCellRow)];
					if (static_cast<size_t>(cellWhich[h]) < row.fCells.size()
						&& static_cast<size_t>(placeIndex[h]) < row.fCells[static_cast<size_t>(cellWhich[h])].fParas.size())
						para = &row.fCells[static_cast<size_t>(cellWhich[h])].fParas[static_cast<size_t>(placeIndex[h])];
				}
			}
			else if (static_cast<size_t>(placeIndex[h]) < out.fBody.size())
			{
				para = &out.fBody[static_cast<size_t>(placeIndex[h])];
			}

			if (para == nil)
			{
				outNoteRefsPlaced = kFalse;
				continue;
			}

			KCMStoryHtml::NoteRef ref;
			ref.fAt = (textOffset > 0) ? textOffset : 0;
			ref.fNote = note;
			// ★KEPT IN ORDER OF fAt HERE, NOT TRUSTED TO ARRIVE SO: KCMStoryDocx walks a paragraph's
			//   references once, front to back, and one out of order would be written late. The owned
			//   items do come in TextIndex order on everything measured - this is what makes that an
			//   observation rather than something the writer's correctness hangs on. Equal places keep
			//   the order they arrived in.
			std::vector<KCMStoryHtml::NoteRef>& refs = para->fNoteRefs;
			size_t where = refs.size();
			while (where > 0 && refs[where - 1].fAt > ref.fAt)
				--where;
			refs.insert(refs.begin() + static_cast<std::ptrdiff_t>(where), ref);
		}
	}

	// ⚠A STORY WITH NO PARAGRAPHS AT ALL still gets one, so that "the file is empty" and "there is
	//   no file" stay different things.
	if (out.fBody.empty())
		out.fBody.push_back(KCMStoryHtml::Para());

	return kTrue;
}

/** One file's bytes, with the BOM the design asks a FILE to carry (KCMStoryHtml deliberately does
	not put one in the strings it builds).

	★The stylesheet gets one too. A custom kenten mark is a character out of the document, so the
	 sheet is not ASCII either, and a browser that guessed at its encoding would draw the wrong
	 mark - or a pair of mojibake - with nothing at all to say why. */
bool16 WriteFileBytes(const std::wstring& path, const std::string& bytes, bool16 withBom)
{
	PMString pathString;
	pathString.SetTranslatable(kFalse);
	pathString.AppendW(reinterpret_cast<const UTF16TextChar*>(path.c_str()));

	const IDFile file = FileUtils::PMStringToSysFile(pathString);

	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWriteLazy(file, kOpenOut | kOpenTrunc));
	if (stream == nil)
		return kFalse;

	// ⚠NEVER ON A .docx: that file is a zip, and three bytes in front of a zip's first signature
	//  are three bytes in front of everything its directory points at.
	if (withBom)
	{
		const char bom[3] = { '\xEF', '\xBB', '\xBF' };
		stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(bom)), 3);
	}
	if (!bytes.empty())
	{
		stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(bytes.c_str())),
						 static_cast<int32>(bytes.size()));
	}
	stream->Flush();
	stream->Close();
	return kTrue;
}

/** The bytes of one story, into "<folder>\<uid>.html" - or ".docx", which carries no BOM. */
bool16 WriteStoryFile(const std::wstring& folder, int32 uid, const std::string& bytes,
					  KCMStoryTextFormat format)
{
	wchar_t leaf[64] = { 0 };
	::swprintf_s(leaf, 64, (format == kKCMStoryTextDocx) ? L"\\%d.docx" : L"\\%d.html",
				 static_cast<int>(uid));
	return WriteFileBytes(folder + leaf, bytes, (format == kKCMStoryTextDocx) ? kFalse : kTrue);
}

/** The document's name as UTF-8, for the .docx's tag (the import's "is this the right document?"). */
std::string DocumentNameUtf8(IDataBase* db)
{
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc == nil)
		return std::string();
	PMString name;
	doc->GetName(name);
	return name.GetUTF8String();
}

}	// anonymous namespace

bool16 KCMStoryFromDocument(const UIDRef& storyRef, KCMStoryHtml::Story& out, bool16& outNoteRefsPlaced)
{
	// The one reader, made public for the import's merge (the header says why); BuildStory stays where
	// the export's other helpers are.
	return BuildStory(storyRef, out, outNoteRefsPlaced);
}

bool16 KCMExportStoryText(IDataBase* db, const IDFile& parent, const UIDList& onlyThese,
						  PMString& outMessage, KCMStoryTextFormat format)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	if (db == nil)
	{
		outMessage = "there is no document to export";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	const std::wstring parentPath = WidePath(parent);
	if (parentPath.empty())
	{
		outMessage = "the chosen folder could not be read";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ★★READING COMPOSES, AND COMPOSING DIRTIES. The document is left exactly as clean as it was
	//   found - the same guard KCMStoryDiffRun puts around its own walk.
	IDataBase::SaveRestoreModifiedState guard(db);

	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil)
	{
		outMessage = "the document has no stories to export";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ---- what to write, settled before any folder exists --------------------------------------
	//
	// ⚠**NO FOLDER IS MADE UNTIL THERE IS A STORY TO PUT IN IT.** Making it first leaves an empty
	//  dated folder behind every run that has nothing to write, and the reader has to go and delete
	//  it - a mess made by the failure rather than by the work. (A story that turns out to be
	//  unreadable LATER does still leave the folder standing with the stylesheet in it. By then the
	//  run has begun, and "there was nothing to export" and "what was there could not be read" are
	//  different things to say.)
	// ★**A UID FROM THE CALLER IS CHECKED, NOT TRUSTED.** The selection is read on the UI side, one
	//   plug-in away: a page item that is not a story of this document is counted and passed over,
	//   and a story named twice is written once - "2 of 7" has to be a count of files, and two
	//   passes over one story would write one file and claim two.
	const int32 count = stories->GetUserAccessibleStoryCount();

	std::vector<UIDRef> targets;
	int32 notAStory = 0;

	if (onlyThese.IsEmpty())
	{
		for (int32 i = 0; i < count; ++i)
			targets.push_back(stories->GetNthUserAccessibleStoryUID(i));
	}
	else
	{
		for (int32 k = 0; k < onlyThese.Length(); ++k)
		{
			const UID wanted = onlyThese[k];
			if (stories->GetUserAccessibleStoryIndex(wanted) < 0)
			{
				++notAStory;
				continue;
			}

			bool16 already = kFalse;
			for (size_t t = 0; t < targets.size() && !already; ++t)
				already = (targets[t].GetUID() == wanted) ? kTrue : kFalse;
			if (!already)
				targets.push_back(UIDRef(db, wanted));
		}
	}

	if (targets.empty())
	{
		outMessage = "there is no story to export";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	std::wstring folder;
	if (!MakeDatedFolder(parentPath, DocumentStem(db), folder))
	{
		outMessage = "the export folder could not be created";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	const std::string documentName = DocumentNameUtf8(db);

	int32 written = 0;
	int32 refused = 0;
	// What stopped the first story that could not be written, so the message can say more than a
	// number - the reader needs to know WHICH story and WHY before they can do anything about it.
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);

	for (size_t t = 0; t < targets.size(); ++t)
	{
		const UIDRef storyRef = targets[t];

		KCMStoryHtml::Story story;
		bool16 noteRefsPlaced = kTrue;
		if (!BuildStory(storyRef, story, noteRefsPlaced))
		{
			++refused;
			continue;
		}

		// ---- the .docx road (2026-09-19) ---------------------------------------------------------
		//
		// ★★★**THE FILE CHECKS ITSELF BEFORE IT IS WRITTEN, AS THE .html ROAD BELOW DOES** (stage 2 of
		//   the docx plan, the same day): the parts are read straight back - BOTH sides of the revision
		//   marks, since a file nobody has edited has to read the same either way - and compared with
		//   the story they came from, and the file is only written when they agree. A story this
		//   format cannot carry (a ruby a table cuts in two; the reader's Slice says why) is refused
		//   HERE, not after somebody has spent an afternoon editing it.
		//   ⚠**COMPARED AS THIS FORMAT SETTLES IT** (KCMStoryDocx::SettleForThisFormat): a mono reading
		//    over several characters is one <w:ruby> and reads back as group, and that is not a
		//    difference the comparison minds (the user's rule of 2026-09-12).
		//   ⚠**IT TOUCHES NOTHING**: pure functions on plain structs, exactly as below.
		//   What it also refuses: a story whose footnote references could not be placed, which Word
		//   would lose.
		if (format == kKCMStoryTextDocx)
		{
			std::vector<KCMZipStore::Entry> parts;
			std::string why;
			if (!noteRefsPlaced)
				why = "a footnote's reference could not be placed";

			bool16 sound = why.empty()
						   && KCMStoryDocx::WriteParts(story, static_cast<int32>(storyRef.GetUID().Get()),
													   documentName, parts, why);
			if (sound)
			{
				KCMStoryHtml::Story settled = story;
				KCMStoryDocx::SettleForThisFormat(settled);
				KCMStoryDocx::ReadResult back;
				sound = KCMStoryDocx::Read(parts, back, why)
						&& KCMStoryHtml::Same(settled, back.fAfter, why, kTrue)
						&& KCMStoryHtml::Same(settled, back.fOrigin, why, kTrue);
			}
			if (!sound)
			{
				++refused;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal.AppendNumber(static_cast<int32>(storyRef.GetUID().Get()));
					firstRefusal.Append(": ");
					firstRefusal.Append(why.c_str());
				}
				continue;
			}

			std::string docx;
			KCMZipStore::Write(parts, docx);
			if (WriteStoryFile(folder, storyRef.GetUID().Get(), docx, kKCMStoryTextDocx))
				++written;
			else
				++refused;
			continue;
		}

		std::string html;
		KCMStoryHtml::Write(story, storyRef.GetUID().Get(), html);

		// ★★★**THE FILE CHECKS ITSELF BEFORE IT IS WRITTEN** (2026-09-16, the user's decision). The
		//   bytes are read straight back and compared against the story they came from, and the
		//   file is only written when the two agree. A story this format cannot carry is therefore
		//   refused HERE - not after somebody has spent an afternoon editing it and the import
		//   turns them away with a count.
		//   ⚠**IT TOUCHES NOTHING.** Write, Read and Same are pure functions on plain structs
		//    (KCMStoryHtml holds no SDK type at all), so the document is not read a second time and
		//    cannot be dirtied by this. What it costs is one parse of a string already in memory.
		{
			KCMStoryHtml::Story back;
			std::string why;
			if (!KCMStoryHtml::Read(html.c_str(), html.size(), back, why)
				|| !KCMStoryHtml::Same(story, back, why))
			{
				++refused;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal.AppendNumber(static_cast<int32>(storyRef.GetUID().Get()));
					firstRefusal.Append(": ");
					firstRefusal.Append(why.c_str());
				}
				continue;
			}
		}

		if (WriteStoryFile(folder, storyRef.GetUID().Get(), html, kKCMStoryTextHtml))
			++written;
		else
			++refused;
	}

	PMString path;
	path.SetTranslatable(kFalse);
	path.AppendW(reinterpret_cast<const UTF16TextChar*>(folder.c_str()));

	outMessage = "exported ";
	outMessage.SetTranslatable(kFalse);
	outMessage.AppendNumber(written);
	// ★**"2 of 7" WHEN THE SELECTION DECIDED IT.** Without the second number "exported 2 story
	//   file(s)" reads as a document with two stories in it, and the reader goes looking for the
	//   other five in the folder.
	if (!onlyThese.IsEmpty())
	{
		outMessage.Append(" of ");
		outMessage.AppendNumber(count);
	}
	outMessage.Append(" story file(s)");
	if (refused > 0)
	{
		outMessage.Append(", ");
		outMessage.AppendNumber(refused);
		outMessage.Append(" refused");
		if (!firstRefusal.IsEmpty())
		{
			outMessage.Append(" (");
			outMessage.Append(firstRefusal);
			outMessage.Append(")");
		}
	}
	if (notAStory > 0)
	{
		outMessage.Append(", ");
		outMessage.AppendNumber(notAStory);
		outMessage.Append(" not a story of this document");
	}
	outMessage.Append(" to ");
	outMessage.Append(path);

	return (written > 0) ? kTrue : kFalse;
}

// End, KCMStoryTextExport.cpp.
