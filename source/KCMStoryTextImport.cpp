//========================================================================================
//
//  KCMStoryTextImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ICommand.h"
#include "ICommandSequence.h"		// the whole import is one step
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"		// the thread a paragraph stands in - a write may not leave it
#include "CmdUtils.h"
#include "TextIterator.h"			// the characters a write is about to take out, read before it does
#include "TextChar.h"				// kTextChar_Table / kTextChar_TableContinued - which side of a table
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "SysFileList.h"			// what the open dialog hands back - several files at once
#include "WideString.h"

#include "KCMStoryTextImport.h"
#include "KCMComparisonRun.h"		// KCMToggleStartStop - the start, through the one resolver
#include "KCMStoryAttrPour.h"		// the ruby and the kenten, after the words are in
#include "KCMStoryRestore.h"		// KCMCreateWordsWriteCmd - one answer to "replace, insert or delete"
#include "KCMCore.h"				// KCMSetCompareMode - the import shows its result in the Story mode
#include "KCMPairChoice.h"			// KCMChosenTargetDB - the document the Task Start chose, which is where the words go
// ⛔KCMOrigin.h went on 2026-09-21 - "until it goes" was written for the origin slot, and the slot
//   went that day. Nothing in this file read anything the header declared.
#include "KCMTaskStartSave.h"		// KCMTakeTaskStartCopy - the import's own Task Start, saved to a file
#include "KCMRehydrate.h"			// KCMReadOriginUidLabel - the copy's stories carry the original UID
#include "KCMParaText.h"			// ModelOffsetInParagraph / AppendUtf8
#include "KCMParaPairing.h"			// which paragraph goes with which when <p>s were added or removed
#include "KCMParagraphStyle.h"		// the next style for a paragraph put in after another
#include "KCMTextDiff.h"			// ToCodePoints / Diff
#include "KCMTextRead.h"			// ReadStory - the document, read the way the export read it
#include "KCMProgressBar.h"			// the import's one bar, and the slot its inner loops step (2026-09-17)
#include "KCMDocxPackage.h"			// KCMReadDocxParts - a .docx on disk as its parts (2026-09-19)
#include "KCMStoryMerge.h"			// Merge - Word's changes onto the copy as it stands (stage 3, 2026-09-19)
#include "KCMStoryTextExport.h"		// KCMStoryFromDocument - the copy's story in the shape the merge takes
#include "KCMStoryDocx.h"			// Read / OriginMatchesTag - the parts as two stories, and whether the marks are whole
#include "KCMZipStore.h"			// Entry - a part, named
#include "KCMModelNotify.h"			// KCMNotify - a cancelled import tells the panel the mode came back

namespace
{

// (A per-write trace to %TEMP% stood here while the 2026-09-17 crash was run down. It is out of the
//  product - the user's rule - and kept, working, as work/kcm-import-matrix/KCMStoryTextImport-with-trace.cpp.txt.)

/** The one character at `at`, or -1 outside the story. */
int32 CharAt(ITextModel* model, TextIndex at)
{
	if (model == nil || at < 0 || at >= model->TotalLength())
		return -1;
	TextIterator iter(model, at);
	return static_cast<int32>((*iter).GetValue());
}

/** For every paragraph of the file that has tables standing in it, those tables' text offsets,
	ascending (2026-09-17, G1). A table stands in the body (fInTable -1) or in one cell of another
	table, and fParaIndex counts inside whichever holds it - so the paragraph is found the same way. */
void FileTablesByParagraph(const KCMStoryShape::Story& story,
						   std::map<const KCMStoryShape::Para*, std::vector<int32> >& out)
{
	out.clear();
	for (size_t t = 0; t < story.fTables.size(); ++t)
	{
		const KCMStoryShape::Table& table = story.fTables[t];
		const std::vector<KCMStoryShape::Para>* holder = nil;
		if (table.fInTable < 0)
		{
			holder = &story.fBody;
		}
		else if (static_cast<size_t>(table.fInTable) < story.fTables.size())
		{
			const KCMStoryShape::Table& parent = story.fTables[static_cast<size_t>(table.fInTable)];
			if (table.fInRow >= 0 && static_cast<size_t>(table.fInRow) < parent.fRows.size()
				&& table.fInCell >= 0
				&& static_cast<size_t>(table.fInCell) < parent.fRows[static_cast<size_t>(table.fInRow)].fCells.size())
			{
				holder = &parent.fRows[static_cast<size_t>(table.fInRow)].fCells[static_cast<size_t>(table.fInCell)].fParas;
			}
		}
		if (holder != nil && table.fParaIndex >= 0 && static_cast<size_t>(table.fParaIndex) < holder->size())
			out[&(*holder)[static_cast<size_t>(table.fParaIndex)]].push_back(table.fOffset);
	}
	for (std::map<const KCMStoryShape::Para*, std::vector<int32> >::iterator it = out.begin(); it != out.end(); ++it)
		std::sort(it->second.begin(), it->second.end());
}

/** The import's one progress bar, in thousandths of the whole job (2026-09-17). Each constant is where
	a stage STARTS: reading the files runs up to the state, taking the state up to the pour, the pour
	(into the document, since 2026-09-19; the names below are older) up to the comparison, and the
	comparison to the end. ⚠A guess at where the time goes, not a measurement - see KCMImportStoryText. */
const int32		kImportUnitsState	= 100;
const int32		kImportUnitsCopy	= 350;
const int32		kImportUnitsCompare	= 650;
const int32		kImportUnitsAll		= 1000;

/** What the status line says whenever the reader pressed Cancel, wherever in the import it landed. */
const char* const	kImportCancelledMessage = "import: cancelled - your document is unchanged";

/** What the last import could not put in, one entry each - the material of the "!" rows
	(KCMStoryList::Build reads it through KCMImportRefusals; 2026-09-19). ⚠A static holding
	PMStrings, so the model's shutdown empties it (KCMPeek.cpp, beside the story list). Dropped with
	the origin (KCMReleaseOrigin) and at the start of the next import. */
std::vector<KCMImportRefusal>	sRefusals;

/** One more thing the pour could not put in. `kind` is the ID column's word. */
void NoteRefusal(UID story, const char* kind, const PMString& whereAndWhy, bool16 wholeStory = kFalse,
				 const PMString& fileName = PMString())
{
	KCMImportRefusal r;
	r.fStory = story;
	r.fKind = kind;
	r.fKind.SetTranslatable(kFalse);
	r.fWhereAndWhy = whereAndWhy;
	r.fWhereAndWhy.SetTranslatable(kFalse);
	r.fFileName = fileName;
	r.fFileName.SetTranslatable(kFalse);
	r.fWholeStory = wholeStory;
	sRefusals.push_back(r);
}

/** The same, for a reason the merge gives as std::strings. */
void NoteRefusal(UID story, const char* kind, const std::string& where, const std::string& why)
{
	PMString text(where.c_str());
	text.SetTranslatable(kFalse);
	if (!why.empty())
	{
		text.Append(" - ");
		text.Append(why.c_str());
	}
	NoteRefusal(story, kind, text);
}

/** A PMString as wide characters. (KCMStoryTextExport.cpp has these four lines inside its own
	WidePath, which takes an IDFile instead - the two files share nothing else, and a header holding
	one helper would be a worse thing to maintain.) */
std::wstring WideOf(const PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
	return (b != nil && n > 0)
		   ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n))
		   : std::wstring();
}

/** A file's own name, with the folders in front of it taken off. */
std::wstring LeafOf(const IDFile& file)
{
	PMString leaf;
	FileUtils::GetFileName(file, leaf);
	return WideOf(leaf);
}

/** The leaf back as a PMString, untranslatable - what a "!" row shows for a file with no story. */
PMString PMStringOfLeaf(const std::wstring& leaf)
{
	PMString s;
	s.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
	s.SetTranslatable(kFalse);
	return s;
}

/** The whole file, as bytes. kFalse when it could not be opened.

	⚠stdio rather than IPMStream, which is the road KCM already took for its settings file and
	 states the reason for there (KCMPageCheck.cpp): IPMStream::Close and Flush both return void. */
bool16 ReadWholeFile(const IDFile& file, std::string& out)
{
	out.clear();

	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return kFalse;

	char buf[4096];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0)
		out.append(buf, n);

	std::fclose(fp);
	return kTrue;
}

/*	⛔**"269.html" -> 269 STOOD HERE UNTIL 2026-09-21.** For the .html spelling THE NAME WAS THE
	PAIRING, so it had to be exact - every character before the dot a decimal digit, and a padded
	"0269.html" refused outright, because two files must never be able to claim one story. The HTML
	road was retired that day on the user's word ("Word format only") and the rule went with it:
	★**a .docx is paired by the TAG INSIDE IT**, and its name is a cross-check and a courtesy
	(IsDocxLeaf, below). The retired measurements are in docs/ai-notes/kcm-html-retired-2026-09-21.md.
*/

/** ".docx"? and, when the name begins with a decimal number the way the exporter writes it
	("269.docx", "269 - chapter one.docx"), that number - for the check against the tag (the design,
	section 4-3: a file copied to another story's name and its inside replaced). 0 when the name
	carries none ("chapter one.docx", "2026年度原稿.docx"): then the tag alone says which story.

	★THE TAG IS THE PAIRING FOR A .docx, NOT THE NAME - the name is a cross-check and a courtesy. */
bool16 IsDocxLeaf(const std::wstring& leaf, uint32& outLeadingNumber)
{
	outLeadingNumber = 0;

	const size_t dot = leaf.find_last_of(L'.');
	if (dot == std::wstring::npos || dot == 0)
		return kFalse;
	std::wstring ext = leaf.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); ++i)
	{
		if (ext[i] >= L'A' && ext[i] <= L'Z')
			ext[i] = static_cast<wchar_t>(ext[i] - L'A' + L'a');
	}
	if (ext != L"docx")
		return kFalse;

	// A leading number counts only when it is the exporter's spelling: no padding, and either the
	// extension's dot or a space right after it.
	if (leaf[0] < L'1' || leaf[0] > L'9')
		return kTrue;
	uint32 value = 0;
	size_t i = 0;
	for (; i < dot && leaf[i] >= L'0' && leaf[i] <= L'9'; ++i)
	{
		if (i >= 10)
			return kTrue;
		value = value * 10 + static_cast<uint32>(leaf[i] - L'0');
	}
	if (i == dot || leaf[i] == L' ')
		outLeadingNumber = value;
	return kTrue;
}

/** "<leaf>: <why>" into `firstReason` when it is still empty. */
void NoteFirstReason(PMString& firstReason, const std::wstring& leaf, const std::string& why)
{
	if (!firstReason.IsEmpty())
		return;
	firstReason.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
	firstReason.Append(": ");
	firstReason.Append(why.c_str());
}

void AppendCount(PMString& out, const char* before, int32 n, const char* after)
{
	out.Append(before);
	out.AppendNumber(n);
	out.Append(after);
}

//========================================================================================
//  Writing the edited text into the document
//========================================================================================

/** One place in a story - the body, one cell, or one footnote - as the two sides see it.

	★**THE PLACES ARE BUILT THE SAME WAY ON BOTH SIDES**, from KCMParaAttrs on the document and
	from the Story's own shape in the file, which is how a cell's paragraphs find their cell
	without anything having to be written down in the file about where they came from. */
struct Place
{
	std::vector<size_t>						fDoc;		// indices into the document's flat arrays
	const std::vector<KCMStoryShape::Para>*	fFile;		// what the reader edited, or nil

	Place() : fFile(nil) {}
};

/** kTrue when any code point of the text in [from, to) is one the reader may not move or delete.

	★**AN OBJECT'S CHARACTER, NOT EVERY INVISIBLE ONE** (2026-09-17 afternoon, the user's request: a forced
	  line break added or removed is taken in). This asked KCMStoryShape::IsInvisible until then, which
	  turned away a forced line break, a zero width space and an indent-to-here as if they were tables -
	  while writing the document back asks KCMParaText::IsObjectCharacter. Now the pour asks the same
	  question: those characters carry nothing, and text commands write them whole. */
bool16 RangeTouchesObject(const std::vector<int32>& cps, int32 from, int32 to)
{
	for (int32 k = from; k < to && k < static_cast<int32>(cps.size()); ++k)
	{
		if (k >= 0 && KCMParaText::IsObjectCharacter(cps[k]))
			return kTrue;
	}
	return kFalse;
}

/** The cells of one row, in the order the exporter wrote them: by column, anchors only.

	The document's paragraphs name their (row, column); the file's cells are a plain list. Sorting
	the columns that actually occur is what turns one into the other - and it agrees with the
	exporter by construction, because that walked the anchors in column order too. */
void ColumnsOfRow(const std::vector<KCMParaAttrs>& attrs, int32 table, int32 row,
				  std::vector<int32>& outCols)
{
	outCols.clear();
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsCell() || attrs[i].fTableOrdinal != table || attrs[i].fCellRow != row)
			continue;
		bool16 seen = kFalse;
		for (size_t k = 0; k < outCols.size() && !seen; ++k)
			seen = (outCols[k] == attrs[i].fCellCol) ? kTrue : kFalse;
		if (!seen)
			outCols.push_back(attrs[i].fCellCol);
	}
	std::sort(outCols.begin(), outCols.end());
}

/** kTrue when the document's tables and the file's are the SAME SHAPE.

	★★★**THE USER'S RULE (2026-09-16): A TABLE IS A LANDMARK, NOT SOMETHING THIS EDITS.** The table
	  tags in the file say WHERE the words sit; they are not an instruction to build a table. So a
	  table whose shape the file does not agree with is left alone - a row or a column added, cells
	  merged or split - and none of its cells is written.
	★★**WHAT THAT COSTS WAS MADE SMALLER ON 2026-09-22** (the user's call: "or refusing is fine
	  too"). It used to be the WHOLE STORY - not the body, not the notes, not the other tables. Now
	  only the table itself is left alone, and it is named in a "!" row; the reader's edits to the
	  body were never in question, and losing them for a table's sake was the expensive half of this
	  rule. ⚠**The whole story is still refused when the NUMBER of tables differs**: a table added or
	  taken away moves the paragraphs around it, so nothing can be lined up at all.
	⚠**WHY THE TABLE AND NOT THE CELL.** The cells are paired BY POSITION (ColumnsOfRow's
	  order against the row's <td> order), so a merge does not read as a miss - it reads as a
	  DIFFERENT CELL. Merging B and C of [A][B][C] leaves the document with two cells, and the
	  file's B would be poured into the merged BC without anything looking wrong. Refusing per cell
	  cannot catch that; only comparing the shapes first can.
	@param whyNot filled with the first disagreement found, for the panel's status line. */
bool16 TablesAgree(const std::vector<KCMParaAttrs>& attrs, const KCMStoryShape::Story& file,
				   PMString& whyNot, std::vector<int32>& outRefusedTables,
				   std::vector<PMString>& outRefusedWhy)
{
	outRefusedTables.clear();
	outRefusedWhy.clear();
	// The document's shape is read off the paragraph attributes: the ordinals are assigned in
	// document order, depth first, which is the order the file writes its <table>s in - so a
	// nested table is just another ordinal and needs no special case here.
	int32 docTables = 0;
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (attrs[i].IsCell() && attrs[i].fTableOrdinal + 1 > docTables)
			docTables = attrs[i].fTableOrdinal + 1;
	}

	const int32 fileTables = static_cast<int32>(file.fTables.size());
	if (docTables != fileTables)
	{
		whyNot = "the number of tables changed";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	for (int32 tbl = 0; tbl < docTables; ++tbl)
	{
		const KCMStoryShape::Table& fileTable = file.fTables[static_cast<size_t>(tbl)];
		const int32 fileRows = static_cast<int32>(fileTable.fRows.size());

		// ★★**THE TWO SIDES COUNT ROWS DIFFERENTLY, SO NEITHER COUNT IS COMPARED.** The writer walks
		//   the MODEL's row count (KCMStoryTextExport's ReadTableShapes -> fRowCount) and emits a
		//   <tr> for every row, empty or not. Here there are only paragraph attributes, where a row
		//   with no cell of its own leaves no trace - so "highest fCellRow + 1" and fRows.size()
		//   are not the same quantity, and comparing them would be comparing two different things.
		// ★**WHAT IS COMPARED INSTEAD: the cells of each row, over the union of both row ranges.**
		//   An empty row reads as 0 cells on both sides, so it agrees; a row added or removed shows
		//   up as a row with cells on one side and none on the other. Nothing is lost.
		// ⚠**THIS IS A GUARD, NOT A FIX FOR SOMETHING SEEN.** The obvious way to produce an empty
		//   row - merging every column of a row into the one above - does NOT produce one: InDesign
		//   REMOVES THE ROW instead (measured 2026-09-16: BodyRowCount 2 -> 1, the row and its last
		//   cell deleted, RowSpan back to 1). So no such table has been observed, and the earlier
		//   reading of this code that said every table with one would be refused for ever was WRONG
		//   - it was read, not measured. The union form costs nothing and is kept because the two
		//   counts genuinely mean different things; if a row with no cells ever does arrive, by
		//   some route not tried here, it will agree rather than refuse the story.
		int32 docRows = 0;
		for (size_t i = 0; i < attrs.size(); ++i)
		{
			if (attrs[i].IsCell() && attrs[i].fTableOrdinal == tbl && attrs[i].fCellRow + 1 > docRows)
				docRows = attrs[i].fCellRow + 1;
		}

		const int32 rowsToCheck = (docRows > fileRows) ? docRows : fileRows;
		for (int32 r = 0; r < rowsToCheck; ++r)
		{
			std::vector<int32> cols;
			ColumnsOfRow(attrs, tbl, r, cols);		// no such row in the document -> empty

			// ⚠**A MERGED CELL IS ONE CELL ON BOTH SIDES.** The document gives it the column it
			//   starts in and no other; the file writes one <td> with colspan. That is why the
			//   counts can be compared directly rather than having to add the spans up.
			const size_t fileCells = (r < fileRows)
				? fileTable.fRows[static_cast<size_t>(r)].fCells.size()
				: 0;
			if (cols.size() != fileCells)
			{
				// ★**ONE TABLE, NOT THE WHOLE STORY** (2026-09-22, the user's call). This table is
				//   left exactly as the document has it - none of its cells is written - and the
				//   body, the notes and the other tables go in as usual.
				PMString why("table ");
				why.AppendNumber(tbl);
				why.Append(": a row's number of cells changed (a merge, a split, a row or a column)"
						   " - that table was left as it is");
				why.SetTranslatable(kFalse);
				outRefusedTables.push_back(tbl);
				outRefusedWhy.push_back(why);
				break;			// one reason per table is enough; on to the next table
			}
		}
	}

	return kTrue;
}

/** Every place of one story, with the file's paragraphs for each.

	@param refusedTables the ordinals of tables the shapes disagree about (TablesAgree). ★**Their
		   cells are not places at all**: no place, no job, nothing written, and no refusal of their
		   own either - the table itself has already been named once, and one reason per table reads
		   better than one per cell (2026-09-22). */
void BuildPlaces(const std::vector<KCMParaAttrs>& attrs, const KCMStoryShape::Story& file,
				 const std::vector<int32>& refusedTables, std::vector<Place>& out)
{
	out.clear();

	Place body;
	body.fFile = &file.fBody;
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsCell() && !attrs[i].IsFootnote())
			body.fDoc.push_back(i);
	}
	out.push_back(body);

	// ---- the cells ----------------------------------------------------------------------------
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsCell())
			continue;

		bool16 refused = kFalse;
		for (size_t k = 0; k < refusedTables.size() && !refused; ++k)
			refused = (refusedTables[k] == attrs[i].fTableOrdinal) ? kTrue : kFalse;
		if (refused)
			continue;			// this table is left as the document has it - see the header

		bool16 already = kFalse;
		for (size_t k = 0; k < out.size() && !already; ++k)
		{
			if (out[k].fDoc.empty())
				continue;
			const KCMParaAttrs& first = attrs[out[k].fDoc[0]];
			already = (first.IsCell()
					   && first.fTableOrdinal == attrs[i].fTableOrdinal
					   && first.fCellRow == attrs[i].fCellRow
					   && first.fCellCol == attrs[i].fCellCol) ? kTrue : kFalse;
			if (already)
				out[k].fDoc.push_back(i);
		}
		if (already)
			continue;

		Place cell;
		cell.fDoc.push_back(i);

		const size_t t = static_cast<size_t>(attrs[i].fTableOrdinal);
		if (t < file.fTables.size() && attrs[i].fCellRow >= 0
			&& static_cast<size_t>(attrs[i].fCellRow) < file.fTables[t].fRows.size())
		{
			std::vector<int32> cols;
			ColumnsOfRow(attrs, attrs[i].fTableOrdinal, attrs[i].fCellRow, cols);

			size_t which = 0;
			bool16 found = kFalse;
			for (size_t k = 0; k < cols.size(); ++k)
			{
				if (cols[k] == attrs[i].fCellCol)
				{
					which = k;
					found = kTrue;
					break;
				}
			}
			const KCMStoryShape::Row& row =
				file.fTables[t].fRows[static_cast<size_t>(attrs[i].fCellRow)];
			if (found && which < row.fCells.size())
				cell.fFile = &row.fCells[which].fParas;
		}
		out.push_back(cell);
	}

	// ---- the footnotes ------------------------------------------------------------------------
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsFootnote())
			continue;

		bool16 already = kFalse;
		for (size_t k = 0; k < out.size() && !already; ++k)
		{
			if (out[k].fDoc.empty())
				continue;
			const KCMParaAttrs& first = attrs[out[k].fDoc[0]];
			already = (first.IsFootnote()
					   && first.fFootnoteOrdinal == attrs[i].fFootnoteOrdinal) ? kTrue : kFalse;
			if (already)
				out[k].fDoc.push_back(i);
		}
		if (already)
			continue;

		Place note;
		note.fDoc.push_back(i);
		const size_t n = static_cast<size_t>(attrs[i].fFootnoteOrdinal);
		if (n < file.fNotes.size())
			note.fFile = &file.fNotes[n];
		out.push_back(note);
	}
}

/*	ApplyParagraph
	The edits between one paragraph of the document and the same paragraph of the file.

	★★**MINIMAL EDITS, BACK TO FRONT.** Replacing the whole paragraph would be simpler and would
	throw away every attribute on the parts nobody touched - the ruby and the kenten this format
	works so hard to carry. So the two are diffed by code point and only the runs that differ are
	written, starting from the end so that the earlier positions are still true when they are used.

	@param outRefused kTrue when the paragraph was turned away, or when a write failed part way
	       through it. ⚠**REFUSED AND WRITTEN ARE NOT EXCLUSIVE, WHICH IS WHY THIS IS NOT THE
	       RETURN VALUE.** The judging happens before anything is written, so a refusal there
	       costs nothing - but a command that fails in the MIDDLE of the loop leaves the writes
	       that went in ahead of it, and the paragraph then holds neither the document's words
	       nor the file's. Answering with a count alone hid the failure (the old -1 turned into a
	       success as soon as one write had gone in); answering with -1 alone hid the change.
	@param fileTableOffsets the text offsets of the tables standing in the FILE's version of this
	       paragraph, ascending - what decides which side of a table an insertion goes (2026-09-17).
	@return how many writes went in - 0 when none did, refused or not.
*/
int32 ApplyParagraph(ITextModel* model, TextIndex paraStart, const KCMParaAttrs& attrs,
					 const std::string& docText, const std::string& fileText,
					 const std::vector<int32>& fileTableOffsets,
					 PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;

	if (docText == fileText)
		return 0;

	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::ToCodePoints(docText, &a, nil);
	KCMTextDiff::ToCodePoints(fileText, &b, nil);

	std::vector<KCMTextDiff::Change> changes;
	if (!KCMTextDiff::Diff(a, b, changes))
	{
		whyNot = "the paragraph differs too much to place the changes";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	// ⚠**NOTHING IS WRITTEN UNTIL EVERY CHANGE HAS BEEN JUDGED**, so a paragraph turned away
	//   here is turned away whole. ★What that cannot rule out is a write that FAILS half way
	//   through the loop below; the reader is TOLD about that one (outRefused) rather than it
	//   being counted as a success, which is what used to happen.
	for (size_t c = 0; c < changes.size(); ++c)
	{
		const KCMTextDiff::Change& ch = changes[c];
		if (RangeTouchesObject(a, ch.aStart, ch.aStart + ch.aCount)
			|| RangeTouchesObject(b, ch.bStart, ch.bStart + ch.bCount))
		{
			whyNot = "a change would add, move or delete a character InDesign hangs an object on "
					 "(an anchored object, a note reference, a page number, an index marker)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
	{
		whyNot = "the story cannot be edited";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	// ---- where each change lands in the MODEL, judged before anything is written ------------------
	//
	// ★★★**A RANGE HAS A START AND AN END, AND THEY ARE COUNTED DIFFERENTLY** (2026-09-17). A table
	//   standing inside the paragraph puts characters in the model that the text does not have, and
	//   ModelOffsetInParagraph answers for a START - "the character at text offset t" stands AFTER a
	//   table standing at t (KCMParaText.h says so, and that a range ENDING there comes back one wide).
	//   This used to take the END from the same function, so changing the last characters before a
	//   table took the table's own anchor out with them - in the copy, silently. The end is now "just
	//   past the last character": ModelOffsetInParagraph(last) + 1.
	// ★★**A CHANGE ACROSS A TABLE IS DONE IN PIECES**, one for each side, so the table stays - and since
	//   the same day's G1/G2 the FILE's table position says which of the new words go on which side
	//   (KCMParaText::CutChangeAtObjects). A replacement across a NOTE REFERENCE is still refused: the
	//   file does not carry where a reference stands, so nothing can say which side its words belong on.
	// ★★★**AND EVERY PIECE IS CHECKED AGAINST THE DOCUMENT BEFORE ANYTHING GOES IN** (the same day,
	//   after the crash): the range has to stay inside this paragraph's own story thread, and what it
	//   takes out has to BE the characters the reading gave. A position that has gone stale fails the
	//   second test even when it lands inside a thread of the right length - which is exactly how the
	//   crash began (cell A's deletion landed, three characters long, on cell C).
	struct Piece
	{
		size_t	fChange;	// which change of `changes`
		int32	fFrom;		// model offset from the paragraph's start
		int32	fCount;		// model characters it takes out (0 = an insertion)
		int32	fAStart;	// the same run in the text's count
		int32	fACount;
		int32	fBStart;	// the FILE's words this piece puts in (an insertion cut at a table has two)
		int32	fBCount;
	};
	std::vector<Piece> pieces;

	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(paraStart, &threadStart, &threadSpan));
		if (thread == nil)
		{
			whyNot = "the paragraph's story thread could not be found (nothing was written)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	// ★★**THE TABLES STANDING IN THIS PARAGRAPH, AS THE DOCUMENT HAS THEM** (2026-09-17, G1) - read off
	//   the model, because the paragraph's attributes do not say which of its uncounted positions are
	//   tables (a note's marker is one too), and a table standing at the very START is not among them
	//   at all: the reader moved the paragraph's start past it.
	std::vector<KCMParaText::KCMTableInPara> tables;
	{
		TextIndex lead = paraStart;
		while (lead > threadStart)
		{
			const int32 cp = CharAt(model, lead - 1);
			if (cp != kTextChar_Table && cp != kTextChar_TableContinued)
				break;
			--lead;
		}
		for (TextIndex m = lead; m < paraStart; ++m)
		{
			if (CharAt(model, m) == kTextChar_Table)
			{
				KCMParaText::KCMTableInPara t;
				t.fTextOffset = 0;
				t.fModelOffset = static_cast<int32>(m - paraStart);
				tables.push_back(t);
			}
		}
		for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
		{
			const TextIndex m = paraStart + attrs.fUncountedAt[k] + static_cast<int32>(k);
			if (CharAt(model, m) == kTextChar_Table)
			{
				KCMParaText::KCMTableInPara t;
				t.fTextOffset = attrs.fUncountedAt[k];
				t.fModelOffset = static_cast<int32>(m - paraStart);
				tables.push_back(t);
			}
		}
	}

	std::vector<int32> docTableOffsets;
	for (size_t j = 0; j < tables.size(); ++j)
		docTableOffsets.push_back(tables[j].fTextOffset);

	for (size_t c = 0; c < changes.size(); ++c)
	{
		const KCMTextDiff::Change& ch = changes[c];

		// ★★**FIRST CUT WHERE THE TABLES STAND, THE DOCUMENT'S PAIRED WITH THE FILE'S** (G1/G2): which side
		//   of a table words go on, and which of them are before it and which after, only the file's own
		//   table position says. `表の前の文章` + `後の文` is 章 before the table and 表の gone after it.
		std::vector<KCMParaText::KCMObjectPiece> parts;
		KCMParaText::CutChangeAtObjects(docTableOffsets, fileTableOffsets, ch.aStart, ch.aCount,
										ch.bStart, ch.bCount, parts);
		for (size_t q = 0; q < parts.size(); ++q)
		{
			const KCMParaText::KCMObjectPiece& part = parts[q];

			if (part.fACount <= 0)
			{
				// An insertion: in front of the first table the file puts after these words, or after
				// everything standing there when none does.
				const int32 ob = part.fObjectsBefore;
				Piece p;
				p.fChange = c;
				p.fFrom = (ob >= 0 && static_cast<size_t>(ob) < tables.size()
						   && tables[static_cast<size_t>(ob)].fTextOffset == part.fAStart)
						  ? tables[static_cast<size_t>(ob)].fModelOffset
						  : KCMParaText::ModelOffsetInParagraph(attrs, part.fAStart);
				p.fCount = 0;
				p.fAStart = part.fAStart;
				p.fACount = 0;
				p.fBStart = part.fBStart;
				p.fBCount = part.fBCount;
				pieces.push_back(p);
				continue;
			}

			// Then cut where anything else the text leaves out stands BETWEEN two of its characters - a
			// note's reference, whose place the file does not carry (KCMStoryShape::Para) - or a table
			// the pairing above could not place.
			const int32 partEnd = part.fAStart + part.fACount;
			std::vector<int32> cuts;
			for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
			{
				const int32 u = attrs.fUncountedAt[k];
				if (u > part.fAStart && u < partEnd && (cuts.empty() || cuts.back() != u))
					cuts.push_back(u);
			}
			if (!cuts.empty() && part.fBCount > 0)
			{
				whyNot = "a change replaces words on both sides of a table or a note reference standing inside "
						 "the paragraph (which side the new words belong on cannot be told - edit the two sides apart)";
				whyNot.SetTranslatable(kFalse);
				outRefused = kTrue;
				return 0;
			}

			int32 s = part.fAStart;
			for (size_t k = 0; k <= cuts.size(); ++k)
			{
				const int32 e = (k < cuts.size()) ? cuts[k] : partEnd;
				Piece p;
				p.fChange = c;
				p.fFrom = KCMParaText::ModelOffsetInParagraph(attrs, s);
				p.fCount = KCMParaText::ModelOffsetInParagraph(attrs, e - 1) + 1 - p.fFrom;
				p.fAStart = s;
				p.fACount = e - s;
				p.fBStart = part.fBStart;		// only ever words when there is one piece (a replacement
				p.fBCount = part.fBCount;		// across a note reference was refused just above)
				pieces.push_back(p);
				s = e;
			}
		}
	}

	for (size_t i = 0; i < pieces.size(); ++i)
	{
		const Piece& p = pieces[i];
		const TextIndex at = paraStart + p.fFrom;
		bool16 inPlace = (at >= threadStart && p.fCount >= 0
						  && at + p.fCount <= threadStart + threadSpan - 1		// never the thread's own end
						  && p.fCount == p.fACount) ? kTrue : kFalse;
		if (inPlace && p.fCount > 0)
		{
			WideString standing;
			TextIterator iter(model, at);
			iter.AppendToStringAndIncrement(&standing, p.fCount);
			if (standing.CharCount() != p.fACount)
				inPlace = kFalse;
			for (int32 k = 0; inPlace && k < p.fACount; ++k)
			{
				if (static_cast<int32>(standing.GetChar(k).GetValue()) != a[static_cast<size_t>(p.fAStart + k)])
					inPlace = kFalse;
			}
		}
		if (!inPlace)
		{
			whyNot = "the copy does not hold the words where they were read, so nothing of this paragraph "
					 "was written (please report this - it is a fault of the plug-in, not of the file)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	int32 written = 0;
	for (size_t i = pieces.size(); i > 0; --i)
	{
		const Piece& piece = pieces[i - 1];
		const int32 from = piece.fFrom;

		WideString words;
		if (piece.fBCount > 0)
		{
			std::string text;
			for (int32 k = piece.fBStart; k < piece.fBStart + piece.fBCount
								   && k < static_cast<int32>(b.size()); ++k)
				KCMParaText::AppendUtf8(text, b[k]);

			PMString asString;
			asString.SetUTF8String(text);		// marks it not translatable, which is what we want
			words = WideString(asString);
		}

		// ★★A DELETION IS A DeleteCmd (2026-09-17, the user's call: "match the official way") - the one
		//   place that decides, shared with the restore. (Replace against Delete was NOT what crashed:
		//   both crashed at the same place - the positions were stale; see KCMApplyStoryTextToCopy.)
		const int32 count = piece.fCount;
		InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, paraStart + from, count, words));
		if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = (written > 0)
					 ? "a write failed part way through a paragraph, which now holds neither the "
					   "document's words nor the file's (a locked story or layer?)"
					 : "the write failed (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}
	return written;
}

/** One write of the text pass: a paragraph's words (ApplyParagraph), new paragraphs, or paragraphs
	taken out. */
struct Job
{
	enum { kParagraph = 0, kInsert = 1, kDelete = 2 };

	int32								fKind;
	/** ★**THE ORDER**: writes go from the highest key down. Twice the position the write starts at, so
		that a write standing at the same position can be put before (+1) or after (-1) the paragraph
		whose start that is - new paragraphs after a paragraph's return go in before that paragraph's own
		words are rewritten (their position is read before those words move it), and new paragraphs in
		front of a place's first paragraph go in after (the paragraph's words are written at the positions
		they were read at). */
	int64								fKey;
	size_t								fPara;			// kParagraph: index into paras / attrs / starts
	const KCMStoryShape::Para*			fFile;			// kParagraph: the same paragraph in the file
	TextIndex							fAt;			// kInsert: where; kDelete: from
	TextIndex							fTo;			// kDelete: up to, not including
	bool16								fAfterReturn;	// kInsert: "\rNEW" after a return, not "NEW\r" before a paragraph
	std::vector<const KCMStoryShape::Para*>	fNew;		// kInsert: the file's new paragraphs, in order

	Job() : fKind(kParagraph), fKey(0), fPara(0), fFile(nil), fAt(0), fTo(0), fAfterReturn(kFalse) {}
};

/** kTrue when a code point of the UTF-8 text is one InDesign hangs an object on. */
bool16 TextHoldsObject(const std::string& utf8)
{
	std::vector<int32> cps;
	KCMTextDiff::ToCodePoints(utf8, &cps, nil);
	for (size_t k = 0; k < cps.size(); ++k)
	{
		if (KCMParaText::IsObjectCharacter(cps[k]))
			return kTrue;
	}
	return kFalse;
}

/** Where a paragraph's return stands: after its text and anything standing at its end. */
TextIndex ReturnOfParagraph(int32 paraStart, const KCMParaAttrs& attrs, const std::string& text)
{
	return static_cast<TextIndex>(paraStart)
		   + KCMParaText::ModelOffsetInParagraph(attrs, KCMParaText::CountCodePoints(text));
}

/** How many tables stand in one paragraph of the document - at its start and inside it. */
int32 DocTablesInParagraph(ITextModel* model, int32 paraStart, const KCMParaAttrs& attrs)
{
	int32 n = 0;
	for (int32 k = 1; k <= attrs.fLeadingUncounted; ++k)
	{
		if (CharAt(model, static_cast<TextIndex>(paraStart - k)) == kTextChar_Table)
			++n;
	}
	for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
	{
		const TextIndex m = static_cast<TextIndex>(paraStart) + attrs.fUncountedAt[k] + static_cast<int32>(k);
		if (CharAt(model, m) == kTextChar_Table)
			++n;
	}
	return n;
}

/** The writes that turn one place's paragraphs into the file's when the file has more or fewer of them
	(2026-09-17 afternoon, the user's rule: a <p> added or removed is a paragraph added or removed).

	★**NEW PARAGRAPHS GO IN RIGHT BEFORE THE RETURN OF THE ONE THEY FOLLOW**, as "\rNEW" - the way pressing
	  Return at the end of that paragraph puts them in, so they start out in its style with its overrides
	  (measured 2026-09-17), and InsertParagraphs then applies the next style. In front of a place's first
	  paragraph there is nothing to follow: "NEW\r" goes in at its start and takes its style.
	★**A REMOVED PARAGRAPH GOES WITH THE RETURN BEFORE IT**, [return of the one before, its own return) -
	  measured the same day: joining two paragraphs keeps the UPPER one's style and overrides, so the
	  paragraph before keeps its own. A place's first paragraph has no return before it and goes with its
	  own.
	⚠**REFUSED, WHOLE PLACE, NOTHING WRITTEN**: a place the file leaves without a paragraph; a paragraph
	  holding a table, a note reference or an anchored object taken out (the object would go with it); a
	  new paragraph holding one (text cannot bring it); a paired paragraph whose tables are not the file's
	  (a table would have to change paragraphs).
	@param outWhyNot empty when the place can be written. */
std::vector<Job> PlanParagraphSteps(ITextModel* model, const Place& place,
									const std::vector<std::string>& paras,
									const std::vector<KCMParaAttrs>& attrs,
									const std::vector<int32>& starts,
									const std::map<const KCMStoryShape::Para*, std::vector<int32> >& fileTables,
									PMString& outWhyNot)
{
	std::vector<Job> out;
	outWhyNot.Clear();
	outWhyNot.SetTranslatable(kFalse);

	const std::vector<KCMStoryShape::Para>& fileParas = *place.fFile;
	if (fileParas.empty() || place.fDoc.empty())
	{
		outWhyNot = "a cell or a note has to keep at least one paragraph (<p>)";
		outWhyNot.SetTranslatable(kFalse);
		return std::vector<Job>();
	}

	std::vector<std::string> docTexts;
	for (size_t q = 0; q < place.fDoc.size(); ++q)
		docTexts.push_back(paras[place.fDoc[q]]);
	std::vector<std::string> fileTexts;
	for (size_t q = 0; q < fileParas.size(); ++q)
		fileTexts.push_back(fileParas[q].fText);

	std::vector<KCMParaPairing::Step> steps;
	KCMParaPairing::Pair(docTexts, fileTexts, steps);

	for (size_t s = 0; s < steps.size(); ++s)
	{
		const KCMParaPairing::Step& step = steps[s];
		if (step.fKind == KCMParaPairing::Step::kPair)
		{
			const size_t i = place.fDoc[static_cast<size_t>(step.fDoc)];
			const KCMStoryShape::Para* file = &fileParas[static_cast<size_t>(step.fFile)];
			const std::map<const KCMStoryShape::Para*, std::vector<int32> >::const_iterator ft = fileTables.find(file);
			const int32 fileTableCount = (ft != fileTables.end()) ? static_cast<int32>(ft->second.size()) : 0;
			if (DocTablesInParagraph(model, starts[i], attrs[i]) != fileTableCount)
			{
				outWhyNot = "paragraphs were added or removed next to a table in a way that would move the table "
							"into another paragraph (a table cannot be moved by text)";
				outWhyNot.SetTranslatable(kFalse);
				return std::vector<Job>();
			}
			Job job;
			job.fKind = Job::kParagraph;
			job.fPara = i;
			job.fFile = file;
			job.fKey = 2 * static_cast<int64>(starts[i]);
			out.push_back(job);
		}
		else if (step.fKind == KCMParaPairing::Step::kInsert)
		{
			Job job;
			job.fKind = Job::kInsert;
			for (int32 k = 0; k < step.fCount; ++k)
			{
				const KCMStoryShape::Para* file = &fileParas[static_cast<size_t>(step.fFile + k)];
				if (fileTables.find(file) != fileTables.end() || TextHoldsObject(file->fText))
				{
					outWhyNot = "a new paragraph holds a table, a note reference or an anchored object "
								"(text cannot bring an object into the document)";
					outWhyNot.SetTranslatable(kFalse);
					return std::vector<Job>();
				}
				job.fNew.push_back(file);
			}
			if (step.fDoc >= 0)
			{
				const size_t before = place.fDoc[static_cast<size_t>(step.fDoc)];
				job.fAt = ReturnOfParagraph(starts[before], attrs[before], paras[before]);
				job.fAfterReturn = kTrue;
				job.fKey = 2 * static_cast<int64>(job.fAt) + 1;
			}
			else
			{
				const size_t first = place.fDoc[0];
				job.fAt = static_cast<TextIndex>(starts[first] - attrs[first].fLeadingUncounted);
				job.fAfterReturn = kFalse;
				job.fKey = 2 * static_cast<int64>(job.fAt) - 1;
			}
			out.push_back(job);
		}
		else
		{
			for (int32 k = 0; k < step.fCount; ++k)
			{
				const size_t i = place.fDoc[static_cast<size_t>(step.fDoc + k)];
				if (attrs[i].fLeadingUncounted > 0 || !attrs[i].fUncountedAt.empty() || TextHoldsObject(paras[i]))
				{
					outWhyNot = "a paragraph holding a table, a note reference or an anchored object cannot be "
								"removed (the object would go with it)";
					outWhyNot.SetTranslatable(kFalse);
					return std::vector<Job>();
				}
			}
			const size_t last = place.fDoc[static_cast<size_t>(step.fDoc + step.fCount - 1)];
			Job job;
			job.fKind = Job::kDelete;
			if (step.fDoc > 0)
			{
				const size_t before = place.fDoc[static_cast<size_t>(step.fDoc - 1)];
				job.fAt = ReturnOfParagraph(starts[before], attrs[before], paras[before]);
				job.fTo = ReturnOfParagraph(starts[last], attrs[last], paras[last]);
				job.fKey = 2 * static_cast<int64>(job.fAt) + 1;
			}
			else
			{
				const size_t first = place.fDoc[static_cast<size_t>(step.fDoc)];
				job.fAt = static_cast<TextIndex>(starts[first] - attrs[first].fLeadingUncounted);
				job.fTo = ReturnOfParagraph(starts[last], attrs[last], paras[last]) + 1;
				job.fKey = 2 * static_cast<int64>(job.fAt) - 1;
			}
			out.push_back(job);
		}
	}
	return out;
}

/** Puts new paragraphs in - "\rNEW" right before a return, or "NEW\r" at a paragraph's start - and gives
	the ones after a return the next style (KCMApplyNextStyleAfter).
	@return how many writes went in. */
int32 InsertParagraphs(ITextModel* model, TextIndex at, bool16 afterReturn,
					   const std::vector<const KCMStoryShape::Para*>& news, PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadSpan));
	const bool16 inPlace = (thread != nil && at >= threadStart && at < threadStart + threadSpan
							&& (!afterReturn || CharAt(model, at) == kTextChar_CR)) ? kTrue : kFalse;
	if (!inPlace || news.empty())
	{
		whyNot = "the copy does not hold the paragraphs where they were read, so no paragraph was added "
				 "(please report this - it is a fault of the plug-in, not of the file)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	std::string text;
	for (size_t k = 0; k < news.size(); ++k)
	{
		if (afterReturn)
			text += '\r';
		text += news[k]->fText;
		if (!afterReturn)
			text += '\r';
	}
	PMString asString;
	asString.SetUTF8String(text);
	const WideString words(asString);

	InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, at, 0, words));
	if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = "adding a paragraph failed (a locked story or layer?)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}
	if (afterReturn)
		KCMApplyNextStyleAfter(model, at, at + 1, words.CharCount() - 1);	// a style that cannot be applied leaves the inherited one
	return 1;
}

/** Takes paragraphs out: [from, to), checked first to be whole paragraphs of one thread holding nothing
	InDesign hangs an object on.
	@return how many writes went in. */
int32 DeleteParagraphs(ITextModel* model, TextIndex from, TextIndex to, PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(from, &threadStart, &threadSpan));
	bool16 inPlace = (thread != nil && from >= threadStart && to > from
					  && to <= threadStart + threadSpan - 1			// never the thread's own last return
					  && CharAt(model, to - 1) != -1) ? kTrue : kFalse;
	// Either [a return, the next return) or [a paragraph's start, just past its return).
	if (inPlace && !(CharAt(model, from) == kTextChar_CR && CharAt(model, to) == kTextChar_CR)
		&& !(CharAt(model, to - 1) == kTextChar_CR && (from == threadStart || CharAt(model, from - 1) == kTextChar_CR)))
		inPlace = kFalse;
	if (inPlace)
	{
		WideString standing;
		TextIterator iter(model, from);
		iter.AppendToStringAndIncrement(&standing, to - from);
		for (int32 k = 0; inPlace && k < standing.CharCount(); ++k)
		{
			if (KCMParaText::IsObjectCharacter(static_cast<int32>(standing.GetChar(k).GetValue())))
				inPlace = kFalse;
		}
	}
	if (!inPlace)
	{
		whyNot = "the copy does not hold the paragraphs where they were read, so no paragraph was removed "
				 "(please report this - it is a fault of the plug-in, not of the file)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, from, to - from, WideString()));
	if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = "removing a paragraph failed (a locked story or layer?)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}
	return 1;
}

}	// anonymous namespace

bool16 KCMReadStoryTextFiles(const SysFileList& files, KCMStoryTextSet& out, PMString& whyNot,
							 bool16* outCancelled)
{
	out = KCMStoryTextSet();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (outCancelled != nil)
		*outCancelled = kFalse;

	const int32 fileCount = files.GetFileCount();
	if (fileCount <= 0)
	{
		whyNot = "no file was chosen";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	int32 skippedName = 0;		// not one of ours: not a .docx at all
	int32 refused = 0;			// ours, but the markup could not be read
	int32 fromWord = 0;			// .docx files read
	int32 trackedCount = 0;		// of those, the ones whose revision marks account for everything
	PMString firstReason;
	firstReason.SetTranslatable(kFalse);
	PMString firstUntracked;	// the first .docx whose marks do not - named, not refused
	firstUntracked.SetTranslatable(kFalse);

	// ★The import's bar when there is one (a slice of it), a bar of its own otherwise.
	PMString barTitle("Reading story files...");
	barTitle.SetTranslatable(kFalse);
	KCMProgressStepper progress(barTitle, fileCount);

	for (int32 i = 0; i < fileCount; ++i)
	{
		// ★A CANCEL IS ASKED BETWEEN TWO FILES - a safe point, since WasCancelled pumps events - and never
		//   after the last one, for the rule the comparison loops keep: nothing is left to interrupt.
		if (i > 0 && progress.WasCancelled())
		{
			out = KCMStoryTextSet();
			whyNot = "cancelled";
			whyNot.SetTranslatable(kFalse);
			if (outCancelled != nil)
				*outCancelled = kTrue;
			return kFalse;
		}
		PMString step("Reading story files (");
		step.AppendNumber(i + 1);
		step.Append(" / ");
		step.AppendNumber(fileCount);
		step.Append(")");
		step.SetTranslatable(kFalse);
		progress.Step(i, step);

		const IDFile* const file = files.GetNthFile(i);
		if (file == nil)
		{
			++refused;
			continue;
		}

		const std::wstring leaf = LeafOf(*file);
		uint32 leading = 0;
		if (!IsDocxLeaf(leaf, leading))
		{
			// ⚠**CHOSEN AND THEN PASSED OVER HAS TO BE SAID OUT LOUD.** Walking a folder could
			//  pass over a file in silence - nobody had asked for that one. A file the reader
			//  picked by hand is a different thing: they meant it, and the count below is the only
			//  place that can tell them it was the NAME that stopped it.
			++skippedName;
			continue;
		}

		// ---- a .docx (2026-09-19, stage 2 of the docx plan): the tag is the pairing ----------------
		//
		// ★What goes into fStories is the story AS WORD SHOWS IT - the after side - so the pour and
		//   the Import mode compare the whole text against the document. What
		//   is new is beside it: when the file's revision marks account for every change since it
		//   was written (OriginMatchesTag), the story AS WRITTEN is kept too, for stage 3 to show
		//   only Word's changes. Nothing is refused on that account (the user's rule, 2026-09-19).
		std::vector<KCMZipStore::Entry> parts;
		PMString packageWhy;
		if (!KCMReadDocxParts(*file, parts, packageWhy))
		{
			++refused;
			if (firstReason.IsEmpty())
			{
				firstReason.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
				firstReason.Append(": ");
				firstReason.Append(packageWhy);
			}
			continue;
		}
		KCMStoryDocx::ReadResult result;
		std::string why;
		if (!KCMStoryDocx::Read(parts, result, why))
		{
			++refused;
			NoteFirstReason(firstReason, leaf, why);
			continue;
		}
		if (!result.fTag.fPresent)
		{
			// A .docx nobody exported (written from scratch in Word) is the design's section 7 and a
			// later stage; until then it is named, not guessed at.
			++refused;
			NoteFirstReason(firstReason, leaf,
							"the file carries no story tag (a Word document not written by Kohaku Change Marker is not imported yet)");
			continue;
		}
		if (leading != 0 && leading != static_cast<uint32>(result.fTag.fUid))
		{
			// ★THE FILE COPIED TO ANOTHER STORY'S NAME (the design, 4-3): which of the two is meant is
			//   not this plug-in's to decide.
			++refused;
			std::string mismatch = "the name says story ";
			{
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%u but the file says %d", static_cast<unsigned int>(leading), static_cast<int>(result.fTag.fUid));
				mismatch += buf;
			}
			NoteFirstReason(firstReason, leaf, mismatch);
			continue;
		}

		++fromWord;
		out.fUids.push_back(UID(static_cast<uint32>(result.fTag.fUid)));
		out.fStories.push_back(result.fAfter);
		std::string whole;
		const bool16 tracked = KCMStoryDocx::OriginMatchesTag(result, whole);
		out.fOrigins.push_back(tracked ? result.fOrigin : KCMStoryShape::Story());
		out.fOriginKnown.push_back(tracked);
		out.fFileNames.push_back(PMStringOfLeaf(leaf));
		out.fIsDocx.push_back(kTrue);
		if (tracked)
		{
			++trackedCount;
		}
		else if (firstUntracked.IsEmpty())
		{
			firstUntracked.AppendNumber(result.fTag.fUid);
			firstUntracked.Append(": ");
			firstUntracked.Append(whole.c_str());
		}
	}

	// ★A STORY CHOSEN TWICE - two .docx files whose tags name one story - is refused on both counts (the design,
	//   section 8): which of the two is meant is not this plug-in's to decide, and the pour would
	//   otherwise take the first and pass over the second without a word.
	{
		std::vector<bool16> twice(out.fUids.size(), kFalse);
		for (size_t i = 0; i < out.fUids.size(); ++i)
		{
			for (size_t j = 0; j < i; ++j)
			{
				if (out.fUids[i] == out.fUids[j])
					twice[i] = twice[j] = kTrue;
			}
		}
		KCMStoryTextSet kept;
		for (size_t i = 0; i < out.fUids.size(); ++i)
		{
			if (twice[i])
			{
				++refused;
				if (firstReason.IsEmpty())
				{
					firstReason = "story ";
					firstReason.AppendNumber(static_cast<int32>(out.fUids[i].Get()));
					firstReason.Append(" was chosen twice");
					firstReason.SetTranslatable(kFalse);
				}
				continue;
			}
			kept.fUids.push_back(out.fUids[i]);
			kept.fStories.push_back(out.fStories[i]);
			kept.fOrigins.push_back(out.fOrigins[i]);
			kept.fOriginKnown.push_back(out.fOriginKnown[i]);
			kept.fFileNames.push_back(out.fFileNames[i]);
			kept.fIsDocx.push_back(out.fIsDocx[i]);
		}
		out = kept;
	}

	const int32 read = static_cast<int32>(out.fUids.size());
	AppendCount(whyNot, "", read, " story file(s) read");
	if (fromWord > 0)
	{
		AppendCount(whyNot, ", ", fromWord, " from Word");
		AppendCount(whyNot, ", ", trackedCount, " with complete revision marks");
		if (!firstUntracked.IsEmpty())
		{
			whyNot.Append(" (");
			whyNot.Append(firstUntracked);
			whyNot.Append(")");
		}
	}
	if (refused > 0)
	{
		AppendCount(whyNot, ", ", refused, " refused");
		if (!firstReason.IsEmpty())
		{
			whyNot.Append(" (");
			whyNot.Append(firstReason);
			whyNot.Append(")");
		}
	}
	if (skippedName > 0)
		AppendCount(whyNot, ", ", skippedName, " not named after a story");

	return (read > 0) ? kTrue : kFalse;
}

bool16 KCMImportStoryText(const SysFileList& files, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// ★★ONE BAR FOR ALL OF IT (2026-09-17, the user's choice). The reading, the comparison's raster
	//   loop and its story loop each step a slice of this one instead of raising their own - two bars
	//   alive at once is what KCMProgressBar.h forbids. The units are thousandths of the whole job.
	//   ⚠The slices are a guess at where the time goes, not a measurement: the state and the copy are
	//    single calls into InDesign and are given the middle, because on a large document they are
	//    the heavy part and the bar can only stand still through them.
	PMString barTitle("Importing story text...");
	barTitle.SetTranslatable(kFalse);
	KCMDeferredProgressBar progress(barTitle, kImportUnitsAll);
	KCMOuterProgressScope outer(progress);

	// 1. THE FILES FIRST. Nothing is touched if they cannot be read.
	KCMStoryTextSet set;
	PMString readMessage;
	bool16 cancelledReading = kFalse;
	outer.Slice(0, kImportUnitsState);
	if (!KCMReadStoryTextFiles(files, set, readMessage, &cancelledReading))
	{
		outMessage = cancelledReading ? PMString(kImportCancelledMessage) : PMString("import: ");
		outMessage.SetTranslatable(kFalse);
		if (!cancelledReading)
			outMessage.Append(readMessage);
		return kFalse;
	}

	// (⛔**THE DOCUMENT WAS CHOSEN HERE UNTIL 2026-09-22**, and it was chosen as KCMActiveDocDB().
	//   That made TWO answers to one question: the Task Start below picks the CHOSEN TARGET when
	//   there is one and only falls back to the active document (KCMTaskStartSave.cpp,
	//   DocumentToCopy - the user's rule "if there is a Target, start the task on that Target;
	//   if not, register the active one as the Target and start"), while this line always took the
	//   active one. Measured on the application: the panel named one document as the Target and the
	//   words went into ANOTHER ([[one-question-one-place]]). The question is now asked ONCE, and
	//   after the Task Start rather than before it - see below.)

	// 2. ★★★THE IMPORT TAKES A TASK START (2026-09-19, the user's rule: "an import always takes a
	//    Task"; on 2026-09-21 that Task Start became a COPY SAVED ON DISK).
	//    ⚠★★**THE READER IS ASKED WHERE TO SAVE IT, AND A CANCEL ENDS THE IMPORT** (the user's
	//     rule: "ユーザーが保存を拒否したら、そこで終わり"). Nothing has gone into the document at
	//     this point, so there is nothing to undo - and the call changes nothing of its own when
	//     the dialog is cancelled.
	//    ⚠**THERE IS NO PARKING ANY MORE.** The old Task Start was a single in-memory slot that had
	//     to be moved aside so a failure could put the reader's back; a file choice simply replaces
	//     the one before it, and the copy stays on disk whatever happens next.
	KCMClearImportRefusals();
	PMString stateStep("Saving a copy of the document");
	stateStep.SetTranslatable(kFalse);
	progress.Step(kImportUnitsState, stateStep);	// ★before the call: the bar can only appear at a Step
	PMString whyNot;
	if (!KCMTakeTaskStartCopy(whyNot))
	{
		// ★★An EMPTY reason means the reader cancelled the save dialog: the import ends here and
		//   says nothing at all, the same silence the flyout's own Task Start keeps.
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		if (whyNot.CharCount() > 0)
		{
			outMessage = "import: the document's state could not be saved (";
			outMessage.Append(whyNot);
			outMessage.Append(")");
		}
		return kFalse;
	}
	// ★A CANCEL PRESSED WHILE THE COPY WAS BEING SAVED is answered here, the first safe point after
	//   it. ⚠**The copy stays on disk and stays chosen as the Source**: the reader asked for it and
	//   paid for it with a save dialog, so throwing it away would be worse than keeping it.
	if (progress.WasCancelled())
	{
		outMessage = kImportCancelledMessage;
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ★**AND THE DOCUMENT IS THE ONE THE TASK START JUST CHOSE** (2026-09-22, the user's rule).
	//   The Task Start has named the pair by now - the Target is the document it copied, the Source
	//   is the copy (KCMTaskStartSave.cpp, step 6) - so asking it here makes the three things that
	//   must agree agree by construction: what was copied, what the panel calls the Target, and
	//   where the words go. ⚠**ASKED AFTER, NOT BEFORE**: before the Task Start this would be the
	//   old pair's Target, which is exactly the state the bug was found in.
	IDataBase* const db = KCMChosenTargetDB();
	if (db == nil)
	{
		// The Task Start said it succeeded, so this cannot normally happen; it is written because a
		// nil here would otherwise reach the pour as a dereference.
		outMessage = "import: the Task Start left no Target to put the words into";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// 3. ★★★THE WORDS GO INTO THE DOCUMENT - all of them, as one undo step (KCMPourStoryText). What
	//    could not go in is noted for the "!" rows. A cancel between two stories aborts the whole
	//    pour, and the reader's own Task Start comes back as if nothing had happened.
	PMString pourStep("Putting the edited text into the document");
	pourStep.SetTranslatable(kFalse);
	progress.Step(kImportUnitsCopy, pourStep);
	outer.Slice(kImportUnitsCopy, kImportUnitsCompare);
	PMString poured;
	bool16 cancelledPour = kFalse;
	const bool16 anyIn = KCMPourStoryText(db, set, poured, cancelledPour);
	if (cancelledPour)
	{
		// ⚠The copy stays, for the reason given at the cancel above: it is a Task Start the reader
		//   saved, and the abandoned pour leaves the document as it was.
		outMessage = kImportCancelledMessage;
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// 4. THE STORY MODE, against the Task Start just taken: the Source is the moment before the
	//    import, the Target is the document with the edits in. The comparison's story loop steps
	//    the last slice of the bar.
	outer.Slice(kImportUnitsCompare, kImportUnitsAll);
	KCMSetCompareMode(kKCMModeStory);
	KCMToggleStartStop();

	outMessage = "import: ";
	outMessage.SetTranslatable(kFalse);
	outMessage.Append(readMessage);
	outMessage.Append("; ");
	outMessage.Append(poured);			// carries the merge's own sentence when there was one
	if (!KCMImportRefusals().empty())
	{
		AppendCount(outMessage, " - ", static_cast<int32>(KCMImportRefusals().size()),
					" could not go in (the rows marked !)");
	}
	// ★A START THAT DID NOT ARM (a Cancel pressed on the comparison's own loop, say) is not the
	//   import's failure any more: the words are in, Ctrl+Z takes them out, and the reader is told
	//   which of the two states they are looking at.
	if (!(KCMIsArmed() && KCMArmedTargetDB() != nil))
	{
		KCMNotify(kKCMMarksClearedMessage);
		outMessage.Append(". The edits are in the document but the comparison did not start"
						  " - Start Comparison shows them; Ctrl+Z takes the whole import back");
		return anyIn;
	}
	// ⚠**THIS LINE OFFERED "Restore Source Text" UNTIL 2026-09-21**, months after that item and the
	//   whole restore behind it were taken out. A status line is read by the reader and by nobody
	//   else, so nothing failed and nothing warned: it simply named a menu item that is not there.
	//   ★What takes its place is what the user said when they removed it - "the Source document is
	//   in front of you, so if you want it back, take it from there" - and Start has it open.
	outMessage.Append(". Ctrl+Z takes the whole import back; the older words are in the Source document");

	// ★**AND THE TARGET IS WHAT THE READER IS LEFT LOOKING AT** (2026-09-22, the user's rule from
	//   2026-09-21 carried to the second road). Start opens the Source copy in a window of its own and
	//   InDesign leaves what it has just opened in front, so without this an import ends on the OLDER
	//   version of the reader's own work. Measured on the application through the script door:
	//   activeDocument WAS the Task Start copy, and the next thing typed would have gone into it.
	//   ⚠**SAID HERE, WHERE BOTH ROADS MEET**, rather than by the caller: the flyout item did it for
	//     itself until today (KCMActionComponent) and the script door could not, because doing it
	//     means touching the UI - the reason the message exists is beside it in KCMBoundaryID.h.
	KCMNotify(kKCMTargetToFrontMessage);
	return anyIn || !KCMImportRefusals().empty();
}

bool16 KCMPourStoryText(IDataBase* db, const KCMStoryTextSet& set, PMString& outMessage,
						bool16& outCancelled)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	outCancelled = kFalse;

	if (set.fUids.empty() || db == nil)
		return kFalse;

	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil)
		return kFalse;

	int32 storiesTouched = 0;
	int32 edits = 0;
	int32 attrEdits = 0;			// ruby and kenten: a second pass, after the words of a story are in
	int32 refusedAttrs = 0;
	int32 keptTcyParas = 0;			// held back, not refused: a tate-chu-yoko under a warichu (2026-09-22)
	int32 refusedParas = 0;
	int32 refusedPlaces = 0;
	int32 skippedByTables = 0;		// stories left alone entirely: their table shape changed
	int32 unmatched = 0;
	int32 wordChanges = 0;			// a .docx whose marks are whole: Word's changes taken by the merge (stage 3)
	int32 conflicts = 0;			// ...and the ones the document's own edits kept out
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);
	PMString firstConflict;
	firstConflict.SetTranslatable(kFalse);
	// ★**PAIRED IS PAIRED, WHATEVER HAPPENS NEXT** (2026-09-16). The count at the end used to be
	//   "files minus stories WRITTEN", which said "had no story" about a file the reader had simply
	//   not edited - and about every story TablesAgree left alone, naming that one twice, once under
	//   each heading. Kept per file now, so that the ones with no story can be NAMED (a "!" row each).
	std::vector<bool16> matched(set.fUids.size(), kFalse);

	// ★★★ONE ABORTABLE STEP (2026-09-19). The words go into the reader's own document now, so this
	//   IS the undo step Ctrl+Z takes back - and a Cancel between two stories aborts it, leaving the
	//   document as it was. (Until 2026-09-19 a plain sequence poured into the windowless copy.)
	IAbortableCmdSeq* sequence = CmdUtils::BeginAbortableCmdSeq("KCMPourStoryText");
	if (sequence != nil)
		sequence->SetName(PMString("Import Story Text"));

	// ★The import's bar when there is one (a slice of it), a bar of its own otherwise - the same
	//   shape as the reading loop. Stepped per story; asked for a cancel BETWEEN two stories only,
	//   because WasCancelled pumps events and a story half written is not a place to stop.
	const int32 count = stories->GetUserAccessibleStoryCount();
	PMString barTitle("Putting the edited text into the document...");
	barTitle.SetTranslatable(kFalse);
	KCMProgressStepper progress(barTitle, count);

	for (int32 s = 0; s < count; ++s)
	{
		if (s > 0 && progress.WasCancelled())
		{
			outCancelled = kTrue;
			break;
		}
		{
			PMString step("Putting the edited text into the document (");
			step.AppendNumber(s + 1);
			step.Append(" / ");
			step.AppendNumber(count);
			step.Append(")");
			step.SetTranslatable(kFalse);
			progress.Step(s, step);
		}

		const UIDRef storyRef = stories->GetNthUserAccessibleStoryUID(s);

		// ★THE STORY'S OWN UID IS THE PAIRING (2026-09-19): the file is named after it, and the words
		//   go into this very document. (The copy's uids were new ones and went through a label.)
		const UID original = storyRef.GetUID();

		size_t which = 0;
		bool16 found = kFalse;
		for (size_t k = 0; k < set.fUids.size(); ++k)
		{
			if (set.fUids[k] == original)
			{
				which = k;
				found = kTrue;
				break;
			}
		}
		if (!found)
			continue;					// a story nobody exported, or exported and then deleted

		matched[which] = kTrue;

		std::vector<std::string> paras;
		std::vector<KCMParaAttrs> attrs;
		std::vector<int32> starts;
		if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
		{
			NoteRefusal(original, "Story", PMString("the story could not be read"), kTrue);
			continue;
		}

		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

		// ★★★**A .docx WHOSE MARKS ACCOUNT FOR EVERYTHING IS MERGED, NOT COMPARED WHOLE** (stage 3 of the
		//   docx plan, 2026-09-19; the design's section 6). The story as written, as Word left it and as
		//   the document holds it now go into KCMStoryMerge, and what is poured is the document plus
		//   Word's changes - so the rows that follow are Word's changes and nothing else, however much
		//   the document has been edited in InDesign since the export. A conflict keeps the document's
		//   words and is named. A .docx whose tracking was off is poured as before: the whole text.
		// ★THE DOCUMENT IS READ WITH THE EXPORT'S OWN READER (KCMStoryFromDocument): the merge compares
		//   three stories that have to be in one shape, and the two that came from the file were
		//   written from that reader's shape to begin with.
		const KCMStoryShape::Story* file = &set.fStories[which];
		KCMStoryMerge::Result merged;
		KCMStoryShape::Story rejoined;
		// ★A .docx HOLDS ITS PARAGRAPHS IN THE SPLIT SHAPE AROUND TABLES (KCMStoryDocx.h, SplitAtTables -
		//   2026-09-19 evening, the user's rule: "the document decides"): every table alone in a paragraph
		//   of its own, whatever paragraph it stands in here. So the document's story is read for every
		//   .docx (not only for a merge), the merge runs in that same shape, and the result is put back
		//   into the document's shape (RejoinTables) before the pour pairs its paragraphs.
		const bool16 fromDocx = (which < set.fIsDocx.size() && set.fIsDocx[which]) ? kTrue : kFalse;
		KCMStoryShape::Story now;
		bool16 haveNow = kFalse;
		if (fromDocx)
		{
			bool16 placed = kTrue;
			haveNow = KCMStoryFromDocument(storyRef, now, placed);
		}
		if (which < set.fOriginKnown.size() && set.fOriginKnown[which])
		{
			if (haveNow)
			{
				KCMStoryShape::Story nowSplit = now;
				KCMStoryDocx::SplitAtTables(nowSplit);
				KCMStoryMerge::Merge(set.fOrigins[which], set.fStories[which], nowSplit, merged);
				wordChanges += merged.fApplied;
				conflicts += static_cast<int32>(merged.fConflicts.size());
				for (size_t c = 0; c < merged.fConflicts.size(); ++c)
					NoteRefusal(original, "Word", merged.fConflicts[c].fWhere, merged.fConflicts[c].fWhy);
				// ★**THE TABLES THE MERGE LEFT ALONE** (2026-09-22). Their cells keep the document's
				//   own words, so the pour below finds them already in agreement and writes nothing;
				//   what the reader needs is to be TOLD, once per table.
				for (size_t c = 0; c < merged.fTableRefusals.size(); ++c)
				{
					NoteRefusal(original, "Table", merged.fTableRefusals[c].fWhere, merged.fTableRefusals[c].fWhy);
					if (firstRefusal.IsEmpty())
					{
						firstRefusal = merged.fTableRefusals[c].fWhere.c_str();
						firstRefusal.Append(" - ");
						firstRefusal.Append(merged.fTableRefusals[c].fWhy.c_str());
						firstRefusal.SetTranslatable(kFalse);
					}
				}
				if (!merged.fConflicts.empty() && firstConflict.IsEmpty())
				{
					firstConflict.AppendNumber(static_cast<int32>(original.Get()));
					firstConflict.Append(": ");
					firstConflict.Append(merged.fConflicts[0].fWhere.c_str());
					firstConflict.Append(" - ");
					firstConflict.Append(merged.fConflicts[0].fWhy.c_str());
				}
				if (merged.fStoryRefused)
				{
					++skippedByTables;
					if (firstRefusal.IsEmpty())
					{
						firstRefusal = merged.fWhy.c_str();
						firstRefusal.SetTranslatable(kFalse);
					}
					NoteRefusal(original, "Table", PMString(merged.fWhy.c_str()), kTrue);
					continue;
				}
				file = &merged.fMerged;
			}
		}
		if (fromDocx && haveNow)
		{
			// ★BACK INTO THE DOCUMENT'S SHAPE: which paragraph a table stands in, and whether the words
			//   after it are that paragraph's, is what the document says - a break a person put next to
			//   a table in Word, or took away there, is not carried (KCMStoryDocx.h, RejoinTables).
			rejoined = *file;
			KCMStoryDocx::RejoinTables(rejoined, now);
			file = &rejoined;
		}

		// ★★★**THE TABLE SHAPES DECIDE WHETHER THIS STORY IS TOUCHED AT ALL** (the user's rule,
		//   2026-09-16). Asked BEFORE the places are built, because the pairing inside BuildPlaces
		//   is by position and would happily pour the file's cell into a different cell of the
		//   document. Nothing of this story is written when the answer is no.
		PMString tableWhyNot;
		std::vector<int32> refusedTables;
		std::vector<PMString> refusedTableWhy;
		if (!TablesAgree(attrs, *file, tableWhyNot, refusedTables, refusedTableWhy))
		{
			++skippedByTables;
			if (firstRefusal.IsEmpty())
				firstRefusal = tableWhyNot;
			NoteRefusal(original, "Table", tableWhyNot, kTrue);
			continue;
		}

		// ★**A TABLE LEFT ALONE IS NAMED, AND THE STORY GOES ON** (2026-09-22, the user's call:
		//   refusing is the right answer for a table whose shape changed, but refusing the story
		//   for its sake is not - the reader's edits to the body were never in question).
		for (size_t k = 0; k < refusedTableWhy.size(); ++k)
		{
			if (firstRefusal.IsEmpty())
				firstRefusal = refusedTableWhy[k];
			NoteRefusal(original, "Table", refusedTableWhy[k]);
		}

		std::vector<Place> places;
		BuildPlaces(attrs, *file, refusedTables, places);

		// ★★★**EVERY WRITE OF THE STORY GOES IN BACK TO FRONT - ACROSS PLACES, NOT ONLY INSIDE ONE**
		//   (2026-09-17, measured with a trace). The body, each cell and each note are separate PLACES,
		//   and a cell's thread always stands after the whole body (ITableTextContent.h:41-44). The
		//   places used to be written in the order BuildPlaces made them - the body first - so a body
		//   edit that changed the length moved every cell, and the cell writes still used positions
		//   read before it. Measured on emptytags.indd: eight characters came out of the body, then
		//   "empty cell A" at 48 took three characters out of cell C, and "empty cell B" at 52 ran
		//   across the end of its thread - and InDesign crashed inside the text command. Replace
		//   against Delete had nothing to do with it (both crashed at the same place).
		//   ⇒ Every place is JUDGED first (nothing is written for a place turned away), and then the
		//     paragraphs are written from the highest TextIndex down: a write moves only what stands
		//     after it, and everything after it has been written already.
		std::vector<Job> jobs;

		// ★The file's own table positions, per paragraph - which side of a table an insertion goes (G1),
		//   and whether a paragraph added or removed would move a table into another paragraph.
		std::map<const KCMStoryShape::Para*, std::vector<int32> > fileTables;
		FileTablesByParagraph(*file, fileTables);

		bool16 touched = kFalse;
		for (size_t p = 0; p < places.size(); ++p)
		{
			const Place& place = places[p];
			if (place.fFile == nil)
			{
				++refusedPlaces;
				PMString placeWhy("a cell or note in the document is not in the file");
				placeWhy.SetTranslatable(kFalse);
				if (firstRefusal.IsEmpty())
					firstRefusal = placeWhy;
				NoteRefusal(original, "Place", placeWhy);
				continue;
			}

			if (place.fDoc.size() == place.fFile->size())
			{
				for (size_t q = 0; q < place.fDoc.size(); ++q)
				{
					Job job;
					job.fPara = place.fDoc[q];
					job.fFile = &(*place.fFile)[q];
					job.fKey = 2 * static_cast<int64>(starts[job.fPara]);
					jobs.push_back(job);
				}
				continue;
			}

			// ★★**<p> ADDED OR REMOVED = A PARAGRAPH ADDED OR REMOVED** (2026-09-17 afternoon, the user's
			//   rule). Which paragraph goes with which is KCMParaPairing's answer; the place is JUDGED whole
			//   before anything of it is written, like every other refusal here.
			PMString placeWhyNot;
			const std::vector<Job> placeJobs = PlanParagraphSteps(model, place, paras, attrs, starts, fileTables,
																  placeWhyNot);
			if (!placeWhyNot.IsEmpty())
			{
				++refusedPlaces;
				if (firstRefusal.IsEmpty())
					firstRefusal = placeWhyNot;
				NoteRefusal(original, "Place", placeWhyNot);
				continue;
			}
			jobs.insert(jobs.end(), placeJobs.begin(), placeJobs.end());
		}

		// ★BACK TO FRONT OVER THE WHOLE STORY (the note above) - by Job::fKey, which also says which of two
		//   writes at one position goes first. Stable, so nothing else about the order is decided here.
		std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.fKey > b.fKey; });

		const std::vector<int32> noTables;

		for (size_t j = 0; j < jobs.size(); ++j)
		{
			const Job& job = jobs[j];
			PMString whyNot;
			bool16 refused = kFalse;
			int32 n = 0;
			if (job.fKind == Job::kInsert)
			{
				n = InsertParagraphs(model, job.fAt, job.fAfterReturn, job.fNew, whyNot, refused);
			}
			else if (job.fKind == Job::kDelete)
			{
				n = DeleteParagraphs(model, job.fAt, job.fTo, whyNot, refused);
			}
			else
			{
				const size_t i = job.fPara;
				const std::map<const KCMStoryShape::Para*, std::vector<int32> >::const_iterator ft =
					fileTables.find(job.fFile);
				n = ApplyParagraph(model, static_cast<TextIndex>(starts[i]), attrs[i],
								   paras[i], job.fFile->fText,
								   (ft != fileTables.end()) ? ft->second : noTables,
								   whyNot, refused);
			}
			// ⚠**BOTH ANSWERS ARE READ, because both can be true of one paragraph**: a write that
			//  failed half way leaves what went in ahead of it (ApplyParagraph says so).
			if (refused)
			{
				++refusedParas;
				if (firstRefusal.IsEmpty())
					firstRefusal = whyNot;
				NoteRefusal(original, "Para", whyNot);
			}
			if (n > 0)
			{
				edits += n;
				touched = kTrue;
			}
		}
		// ---- and now the ruby and the kenten, over the words that went in ----------------------
		//
		// ★★★**THE STORY IS READ AGAIN FIRST.** Every write above moved the positions after it, so
		//   an attribute placed from the reading the WORDS were planned from would land on the
		//   wrong characters. This second reading is also what lets each paragraph be asked the
		//   one question that makes an offset mean the same thing on both sides - do the words
		//   match now? - which KCMPourParagraphAttributes asks, and refuses on.
		// ★**FORWARDS, unlike the text pass**: an attribute never changes how many characters
		//   there are, so nothing standing after it moves.
		// ⚠A story whose second read fails keeps the words that went in; only its attributes are
		//   left alone.
		{
			std::vector<std::string> paras2;
			std::vector<KCMParaAttrs> attrs2;
			std::vector<int32> starts2;
			if (KCMTextRead::ReadStory(storyRef, paras2, attrs2, starts2))
			{
				std::vector<Place> places2;
				// ⚠**THE SAME TABLES ARE LEFT ALONE HERE.** The attribute pass pairs cells by
				//   position exactly as the text pass does, so a table whose shape disagrees would
				//   have its ruby and kenten written into a DIFFERENT cell - the one failure this
				//   whole rule exists to prevent.
				BuildPlaces(attrs2, *file, refusedTables, places2);

				for (size_t p = 0; p < places2.size(); ++p)
				{
					const Place& place = places2[p];
					// ⚠The two kinds of place the text pass has already counted and named are
					//   passed over in silence here. Counting them again would tell the reader
					//   about one cell twice, under two headings - the lesson matchedFiles above
					//   was written for.
					if (place.fFile == nil || place.fDoc.size() != place.fFile->size())
						continue;

					for (size_t q = 0; q < place.fDoc.size(); ++q)
					{
						const size_t i = place.fDoc[q];
						PMString attrWhyNot;
						PMString attrKept;
						bool16 attrRefused = kFalse;
						const int32 n = KCMPourParagraphAttributes(
											model, static_cast<TextIndex>(starts2[i]),
											attrs2[i], paras2[i], (*place.fFile)[q],
											attrWhyNot, attrRefused, attrKept);
						if (attrRefused)
						{
							++refusedAttrs;
							if (firstRefusal.IsEmpty())
								firstRefusal = attrWhyNot;
							NoteRefusal(original, "Attr", attrWhyNot);
						}
						// ★**HELD BACK, NOT REFUSED** (2026-09-22): the paragraph went in and one
						//   thing in it was kept as the document has it, because Word cannot carry
						//   it. It gets a count and a row of its own.
						// ⚠**IT MUST NOT TAKE firstRefusal**, which only the FIRST thing to fill it
						//  ever reaches the status line by: a rescue standing there would push a
						//  real refusal, found later in the same import, out of the one line the
						//  reader reads.
						if (!attrKept.IsEmpty())
						{
							++keptTcyParas;
							NoteRefusal(original, "Word", attrKept);
						}
						if (n > 0)
						{
							attrEdits += n;
							touched = kTrue;
						}
					}
				}
			}
		}

		if (touched)
			++storiesTouched;
	}

	// ★A CANCEL TAKES THE WHOLE POUR BACK - and says nothing else: what was noted so far is dropped
	//   too, because the document those refusals were about is the document as it was.
	if (outCancelled)
	{
		if (sequence != nil)
			CmdUtils::AbortCommandSequence(sequence);
		KCMClearImportRefusals();
		outMessage = "cancelled";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// Files whose story the document does not carry, by the pairing above - the only place that
	// can tell. ⚠**A PAIRING, NOT A WRITE**: a file that matched and changed nothing has a story,
	// and so has one whose story was left alone because its tables had moved. Each is a "!" row
	// standing for the file (2026-09-19).
	for (size_t k = 0; k < matched.size(); ++k)
	{
		if (matched[k])
			continue;
		++unmatched;
		NoteRefusal(set.fUids[k], "File", PMString("no story with this ID in the document"), kTrue,
					(k < set.fFileNames.size()) ? set.fFileNames[k] : PMString());
	}

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	AppendCount(outMessage, "", edits, " change(s) put into the document");
	AppendCount(outMessage, " in ", storiesTouched, " story(ies)");
	// ★COUNTED APART FROM THE WORDS, because an import that changed nothing else is exactly the
	//   case this pass was written for ("I only changed the ruby") - and a line saying "0 changes"
	//   under it would be the plug-in denying what it had just done.
	if (attrEdits > 0)
		AppendCount(outMessage, ", ", attrEdits, " ruby/kenten write(s)");
	// ★A .docx MERGED THREE WAYS SAYS SO (stage 3): how many of Word's changes went in, and how many
	//   the document's own edits kept out - the first of those named, so the reader knows where to look.
	//   ⚠It was kept in a file-static until 2026-09-20, for a caller that has not existed since the
	//    pour moved out of the rehydration: the pour and this sentence are now one function.
	PMString sLastMergeNote;
	sLastMergeNote.SetTranslatable(kFalse);
	if (wordChanges > 0 || conflicts > 0)
	{
		AppendCount(sLastMergeNote, "", wordChanges, " change(s) from Word");
		if (conflicts > 0)
		{
			AppendCount(sLastMergeNote, ", ", conflicts, " conflict(s) kept the document's words");
			if (!firstConflict.IsEmpty())
			{
				sLastMergeNote.Append(" (");
				sLastMergeNote.Append(firstConflict);
				sLastMergeNote.Append(")");
			}
		}
		outMessage.Append(", ");
		outMessage.Append(sLastMergeNote);
	}
	if (skippedByTables > 0)
		AppendCount(outMessage, ", ", skippedByTables, " story(ies) left alone (table structure changed)");
	if (refusedPlaces > 0)
		AppendCount(outMessage, ", ", refusedPlaces, " place(s) refused");
	if (refusedParas > 0)
		AppendCount(outMessage, ", ", refusedParas, " paragraph(s) refused");
	if (refusedAttrs > 0)
		AppendCount(outMessage, ", ", refusedAttrs, " paragraph(s) kept their own ruby/kenten");
	if (keptTcyParas > 0)
		AppendCount(outMessage, ", ", keptTcyParas, " paragraph(s) kept a tate-chu-yoko Word cannot carry");
	if (unmatched > 0)
		AppendCount(outMessage, ", ", unmatched, " file(s) had no story");
	if (!firstRefusal.IsEmpty())
	{
		outMessage.Append(" (");
		outMessage.Append(firstRefusal);
		outMessage.Append(")");
	}

	// ★★★**A RUBY-ONLY IMPORT IS AN IMPORT** (2026-09-16). This answered on the word writes alone
	//   until the attributes were poured, so a file whose only edit was a reading came back as
	//   "nothing could be applied" - the very case the user asked for.
	return (edits > 0 || attrEdits > 0) ? kTrue : kFalse;
}

const std::vector<KCMImportRefusal>& KCMImportRefusals()
{
	return sRefusals;
}

void KCMClearImportRefusals()
{
	// A fresh vector releases the storage too (the same reason KCMStoryList::ShutdownCleanup gives).
	sRefusals = std::vector<KCMImportRefusal>();
}

// End, KCMStoryTextImport.cpp.
