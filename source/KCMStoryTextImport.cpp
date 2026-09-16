//========================================================================================
//
//  KCMStoryTextImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "ICommand.h"
#include "ICommandSequence.h"		// the whole import is one step
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "SysFileList.h"			// what the open dialog hands back - several files at once
#include "WideString.h"

#include "KCMStoryTextImport.h"
#include "KCMComparisonRun.h"		// KCMToggleStartStop - the start, through the one resolver
#include "KCMStoryAttrPour.h"		// the ruby and the kenten, after the words are in
#include "KCMCore.h"				// KCMActiveDocDB / KCMGetCompareMode / KCMSetCompareMode
#include "KCMOrigin.h"				// the origin slot: taken for this mode, parked for the reader's
#include "KCMRehydrate.h"			// KCMReadOriginUidLabel - the copy's stories carry the original UID
#include "KCMParaText.h"			// ModelOffsetInParagraph / AppendUtf8
#include "KCMTextDiff.h"			// ToCodePoints / Diff
#include "KCMTextRead.h"			// ReadStory - the document, read the way the export read it

namespace
{

/** Which mode was showing before the import took over.

	⚠A file-static, so the model's shutdown empties nothing here on purpose: it is a plain value.
	 (KCMStoryList.h's rule is about statics holding strings and rows.) */
KCMCompareMode	sModeBeforeImport = kKCMModePixel;

/** The words waiting to go into the next copy. ⚠A static holding PMStrings and std::strings, so it
	has a line in the model's shutdown (KCMStoryList.h says what forgetting that costs).

	★★★**sHolding IS ALSO THE ANSWER TO "IS THE IMPORT MODE UP?"** (2026-09-15). A second flag
	stood here, and the two parted company the first time a document was CLOSED with an import
	showing: the words went - KCMReleaseOrigin drops them - and the flag stayed. The UI greys
	Pixel, Story, Resources and Task Start whenever that flag is set (KCMActionComponent.cpp), and
	Stop Comparison - the only caller of KCMEndImportMode - is itself greyed once nothing is armed,
	so there was no way out but restarting InDesign. Measured, then predicted by the user in the
	same minute ("Import モードが解除されない気がしました").
	⇒ **the mode IS the words being held**, asked in one place ([[one-question-one-place]]). */
KCMStoryTextSet	sHeld;
bool16			sHolding = kFalse;

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

/** "269.html" -> 269. kFalse for a name that is not one of ours.

	★**THE FILE NAME IS THE PAIRING**, so the test is exact: every character before ".html" has to
	be a decimal digit. A reader's own notes in that folder ("notes.html", "269 copy.html") are not
	ours and are passed over rather than guessed at. */
bool16 UidOfLeaf(const std::wstring& leaf, uint32& outUid)
{
	const size_t dot = leaf.find_last_of(L'.');
	if (dot == std::wstring::npos || dot == 0)
		return kFalse;

	std::wstring ext = leaf.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); ++i)
	{
		if (ext[i] >= L'A' && ext[i] <= L'Z')
			ext[i] = static_cast<wchar_t>(ext[i] - L'A' + L'a');
	}
	if (ext != L"html")
		return kFalse;

	const std::wstring stem = leaf.substr(0, dot);
	if (stem.empty() || stem.size() > 10)
		return kFalse;

	// ⚠**A PADDED NUMBER IS NOT ONE OF OURS.** The exporter writes "269.html" and never
	//   "0269.html", and Windows keeps both of those in one folder quite happily (measured
	//   2026-09-15) - so a padded name is somebody's own copy, and reading it as story 269 would
	//   let two files claim one story. Refusing it here is what makes that impossible rather than
	//   merely unlikely, now that the reader hands over files by name instead of a whole folder.
	if (stem[0] == L'0')
		return kFalse;

	uint32 value = 0;
	for (size_t i = 0; i < stem.size(); ++i)
	{
		if (stem[i] < L'0' || stem[i] > L'9')
			return kFalse;
		value = value * 10 + static_cast<uint32>(stem[i] - L'0');
	}
	if (value == 0)
		return kFalse;

	outUid = value;
	return kTrue;
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
	const std::vector<KCMStoryHtml::Para>*	fFile;		// what the reader edited, or nil

	Place() : fFile(nil) {}
};

/** kTrue when any code point of the text in [from, to) is one the reader may not move or delete. */
bool16 RangeTouchesInvisible(const std::vector<int32>& cps, int32 from, int32 to)
{
	for (int32 k = from; k < to && k < static_cast<int32>(cps.size()); ++k)
	{
		if (k >= 0 && KCMStoryHtml::IsInvisible(cps[k]))
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
	  tags in the file say WHERE the words sit; they are not an instruction to build a table. So if
	  the shapes disagree - a table added or removed, a row or a column added, cells merged or split
	  - the story this file names is left ENTIRELY ALONE. Not the body, not the notes, not the cells
	  that happen to still line up: the whole story.
	⚠**WHY THE WHOLE STORY AND NOT THE CELL.** The cells are paired BY POSITION (ColumnsOfRow's
	  order against the row's <td> order), so a merge does not read as a miss - it reads as a
	  DIFFERENT CELL. Merging B and C of [A][B][C] leaves the document with two cells, and the
	  file's B would be poured into the merged BC without anything looking wrong. Refusing per cell
	  cannot catch that; only comparing the shapes first can.
	@param whyNot filled with the first disagreement found, for the panel's status line. */
bool16 TablesAgree(const std::vector<KCMParaAttrs>& attrs, const KCMStoryHtml::Story& file,
				   PMString& whyNot)
{
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
		const KCMStoryHtml::Table& fileTable = file.fTables[static_cast<size_t>(tbl)];
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
				whyNot = "a table row's number of cells changed (a merge, a split, a row or a column)";
				whyNot.SetTranslatable(kFalse);
				return kFalse;
			}
		}
	}

	return kTrue;
}

/** Every place of one story, with the file's paragraphs for each. */
void BuildPlaces(const std::vector<KCMParaAttrs>& attrs, const KCMStoryHtml::Story& file,
				 std::vector<Place>& out)
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
			const KCMStoryHtml::Row& row =
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
	@return how many writes went in - 0 when none did, refused or not.
*/
int32 ApplyParagraph(ITextModel* model, TextIndex paraStart, const KCMParaAttrs& attrs,
					 const std::string& docText, const std::string& fileText,
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
		if (RangeTouchesInvisible(a, ch.aStart, ch.aStart + ch.aCount)
			|| RangeTouchesInvisible(b, ch.bStart, ch.bStart + ch.bCount))
		{
			whyNot = "a change would move or delete a character that is not a letter "
					 "(an anchored object, a page number, an index marker)";
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

	int32 written = 0;
	for (size_t c = changes.size(); c > 0; --c)
	{
		const KCMTextDiff::Change& ch = changes[c - 1];

		// ★THE CROSSING IS ModelOffsetInParagraph's, never an addition of our own: a table standing
		//   inside this paragraph makes the two counts disagree from there on (KCMParaText.h).
		const int32 from = KCMParaText::ModelOffsetInParagraph(attrs, ch.aStart);
		const int32 to = KCMParaText::ModelOffsetInParagraph(attrs, ch.aStart + ch.aCount);

		boost::shared_ptr<WideString> words(new WideString());
		if (ch.bCount > 0)
		{
			std::string piece;
			for (int32 k = ch.bStart; k < ch.bStart + ch.bCount
								   && k < static_cast<int32>(b.size()); ++k)
				KCMParaText::AppendUtf8(piece, b[k]);

			PMString asString;
			asString.SetUTF8String(piece);		// marks it not translatable, which is what we want
			*words = WideString(asString);
		}

		const int32 count = to - from;
		InterfacePtr<ICommand> write(count > 0
			? cmds->ReplaceCmd(paraStart + from, count, words)
			: cmds->InsertCmd(paraStart + from, words));
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

}	// anonymous namespace

bool16 KCMReadStoryTextFiles(const SysFileList& files, KCMStoryTextSet& out, PMString& whyNot)
{
	out = KCMStoryTextSet();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	const int32 fileCount = files.GetFileCount();
	if (fileCount <= 0)
	{
		whyNot = "no file was chosen";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	int32 skippedName = 0;		// not one of ours: a name that is not a plain decimal number
	int32 refused = 0;			// ours, but the markup could not be read
	PMString firstReason;
	firstReason.SetTranslatable(kFalse);

	for (int32 i = 0; i < fileCount; ++i)
	{
		const IDFile* const file = files.GetNthFile(i);
		if (file == nil)
		{
			++refused;
			continue;
		}

		const std::wstring leaf = LeafOf(*file);
		uint32 uid = 0;
		if (!UidOfLeaf(leaf, uid))
		{
			// ⚠**CHOSEN AND THEN PASSED OVER HAS TO BE SAID OUT LOUD.** Walking a folder could
			//  pass over a file in silence - nobody had asked for that one. A file the reader
			//  picked by hand is a different thing: they meant it, and the count below is the only
			//  place that can tell them it was the NAME that stopped it.
			++skippedName;
			continue;
		}

		std::string bytes;
		if (!ReadWholeFile(*file, bytes))
		{
			++refused;
			continue;
		}

		KCMStoryHtml::Story story;
		std::string why;
		if (!KCMStoryHtml::Read(bytes.c_str(), bytes.size(), story, why))
		{
			// ⚠ONE BAD FILE MUST NOT COST THE OTHERS. It is counted and the first reason is kept,
			//   so the reader is told what to fix rather than left with nothing.
			++refused;
			if (firstReason.IsEmpty())
			{
				firstReason.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
				firstReason.Append(": ");
				firstReason.Append(why.c_str());
			}
			continue;
		}

		out.fUids.push_back(UID(uid));
		out.fStories.push_back(story);
	}

	const int32 read = static_cast<int32>(out.fUids.size());
	AppendCount(whyNot, "", read, " story file(s) read");
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

	// 1. THE FILES FIRST. Nothing is touched if they cannot be read.
	KCMStoryTextSet set;
	PMString readMessage;
	if (!KCMReadStoryTextFiles(files, set, readMessage))
	{
		outMessage = "import: ";
		outMessage.SetTranslatable(kFalse);
		outMessage.Append(readMessage);
		return kFalse;
	}

	IDataBase* const db = KCMActiveDocDB();
	if (db == nil)
	{
		outMessage = "import: there is no active document";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// 2. ★★★THE READER'S OWN TASK START IS MOVED ASIDE, NOT THROWN AWAY (the user's requirement).
	//    The import needs this slot for its own snapshot - the document as it stands a moment
	//    before the words go in - and gives it back when the mode ends.
	const bool16 parked = KCMParkOrigin();

	// 3. The import's own origin: the document as it is NOW, before a single character is written.
	//    That is what makes the mode show exactly what the import did and nothing else.
	PMString whyNot;
	if (!KCMTakeTaskStart(whyNot))
	{
		if (parked)
			KCMUnparkOrigin();		// nothing happened; the reader's own task comes straight back
		outMessage = "import: the document's state could not be taken (";
		outMessage.SetTranslatable(kFalse);
		outMessage.Append(whyNot);
		outMessage.Append(")");
		return kFalse;
	}
	// 4. ★★★THE WORDS ARE HELD, NOT WRITTEN. They go into the COPY when the comparison makes one
	//    (KCMRehydrate, the one place), and into the reader's document only through "Restore
	//    Source Text", one change at a time. An import changes nothing by itself.
	// ★KCMHoldStoryText IS what puts the mode up: holding the words and being in the import mode
	//   are one fact (see sHolding). The mode to come back to is noted a line before it.
	sModeBeforeImport = KCMGetCompareMode();
	KCMHoldStoryText(set);

	// 5. The fourth mode, and the comparison that shows the edited words against the document.
	KCMSetCompareMode(kKCMModeImport);
	KCMToggleStartStop();

	outMessage = "import: ";
	outMessage.SetTranslatable(kFalse);
	outMessage.Append(readMessage);
	outMessage.Append(" - your document is unchanged; Restore Source Text puts a change in, "
					  "Stop Comparison ends the import");
	return kTrue;
}

bool16 KCMApplyStoryTextToCopy(IDataBase* copyDB, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	const KCMStoryTextSet* const set = KCMHeldStoryText();
	if (set == nil || set->fUids.empty() || copyDB == nil)
		return kFalse;

	InterfacePtr<IStoryList> stories(copyDB, copyDB->GetRootUID(), UseDefaultIID());
	if (stories == nil)
		return kFalse;

	int32 storiesTouched = 0;
	int32 edits = 0;
	int32 attrEdits = 0;			// ruby and kenten: a second pass, after the words of a story are in
	int32 refusedAttrs = 0;
	int32 refusedParas = 0;
	int32 refusedPlaces = 0;
	int32 skippedByTables = 0;		// stories left alone entirely: their table shape changed
	int32 matchedFiles = 0;			// files whose story the copy really does carry
	int32 unmatched = 0;
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);

	// ★ONE STEP. The copy has no window and nobody presses Ctrl+Z in it, but a sequence keeps the
	//   writes from arriving as a hundred separate entries in a history the peek shares.
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence("KCMPourStoryText");
	if (sequence != nil)
		sequence->SetName(PMString("Edited story text"));

	const int32 count = stories->GetUserAccessibleStoryCount();
	for (int32 s = 0; s < count; ++s)
	{
		const UIDRef storyRef = stories->GetNthUserAccessibleStoryUID(s);

		// ⚠**THE COPY'S UIDS ARE NEW ONES.** What pairs a file with a story is the label the
		//   rehydration wrote, which carries the ORIGINAL uid - the one the file is named after.
		UID original = kInvalidUID;
		if (!KCMReadOriginUidLabel(copyDB, storyRef.GetUID(), original))
			continue;

		size_t which = 0;
		bool16 found = kFalse;
		for (size_t k = 0; k < set->fUids.size(); ++k)
		{
			if (set->fUids[k] == original)
			{
				which = k;
				found = kTrue;
				break;
			}
		}
		if (!found)
			continue;					// a story nobody exported, or exported and then deleted

		// ★**PAIRED IS PAIRED, WHATEVER HAPPENS NEXT** (2026-09-16). The count at the end used to
		//   be "files minus stories WRITTEN", which said "had no story" about a file the reader
		//   had simply not edited - and about every story TablesAgree left alone, naming that one
		//   twice, once under each heading.
		++matchedFiles;

		std::vector<std::string> paras;
		std::vector<KCMParaAttrs> attrs;
		std::vector<int32> starts;
		if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
			continue;

		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

		// ★★★**THE TABLE SHAPES DECIDE WHETHER THIS STORY IS TOUCHED AT ALL** (the user's rule,
		//   2026-09-16). Asked BEFORE the places are built, because the pairing inside BuildPlaces
		//   is by position and would happily pour the file's cell into a different cell of the
		//   document. Nothing of this story is written when the answer is no.
		PMString tableWhyNot;
		if (!TablesAgree(attrs, set->fStories[which], tableWhyNot))
		{
			++skippedByTables;
			if (firstRefusal.IsEmpty())
				firstRefusal = tableWhyNot;
			continue;
		}

		std::vector<Place> places;
		BuildPlaces(attrs, set->fStories[which], places);

		bool16 touched = kFalse;
		for (size_t p = 0; p < places.size(); ++p)
		{
			const Place& place = places[p];
			if (place.fFile == nil)
			{
				++refusedPlaces;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal = "a cell or note in the document is not in the file";
					firstRefusal.SetTranslatable(kFalse);
				}
				continue;
			}

			// ⚠**THE PARAGRAPH COUNT HAS TO MATCH, so far.** Adding and removing paragraphs needs
			//   the exact end of a thread, which is measured work not yet done - so the place is
			//   refused with a reason rather than half-applied.
			if (place.fDoc.size() != place.fFile->size())
			{
				++refusedPlaces;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal = "the number of paragraphs changed "
								   "(this version writes changes inside a paragraph only)";
					firstRefusal.SetTranslatable(kFalse);
				}
				continue;
			}

			// ★BACK TO FRONT ACROSS THE PARAGRAPHS TOO, for the same reason as inside one: an
			//   earlier write moves every position after it.
			for (size_t q = place.fDoc.size(); q > 0; --q)
			{
				const size_t i = place.fDoc[q - 1];
				PMString whyNot;
				bool16 refused = kFalse;
				const int32 n = ApplyParagraph(model, static_cast<TextIndex>(starts[i]), attrs[i],
											   paras[i], (*place.fFile)[q - 1].fText, whyNot, refused);
				// ⚠**BOTH ANSWERS ARE READ, because both can be true of one paragraph**: a write that
				//  failed half way leaves what went in ahead of it (ApplyParagraph says so).
				if (refused)
				{
					++refusedParas;
					if (firstRefusal.IsEmpty())
						firstRefusal = whyNot;
				}
				if (n > 0)
				{
					edits += n;
					touched = kTrue;
				}
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
				BuildPlaces(attrs2, set->fStories[which], places2);

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
						bool16 attrRefused = kFalse;
						const int32 n = KCMPourParagraphAttributes(
											model, static_cast<TextIndex>(starts2[i]),
											attrs2[i], paras2[i], (*place.fFile)[q],
											attrWhyNot, attrRefused);
						if (attrRefused)
						{
							++refusedAttrs;
							if (firstRefusal.IsEmpty())
								firstRefusal = attrWhyNot;
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

	// Files whose story the copy does not carry, counted by the pairing above - the only place
	// that can tell. ⚠**A PAIRING, NOT A WRITE**: a file that matched and changed nothing has a
	// story, and so has one whose story was left alone because its tables had moved.
	unmatched = static_cast<int32>(set->fUids.size()) - matchedFiles;
	if (unmatched < 0)
		unmatched = 0;

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	AppendCount(outMessage, "", edits, " change(s) poured into the copy");
	AppendCount(outMessage, " in ", storiesTouched, " story(ies)");
	// ★COUNTED APART FROM THE WORDS, because an import that changed nothing else is exactly the
	//   case this pass was written for ("I only changed the ruby") - and a line saying "0 changes"
	//   under it would be the plug-in denying what it had just done.
	if (attrEdits > 0)
		AppendCount(outMessage, ", ", attrEdits, " ruby/kenten write(s)");
	if (skippedByTables > 0)
		AppendCount(outMessage, ", ", skippedByTables, " story(ies) left alone (table structure changed)");
	if (refusedPlaces > 0)
		AppendCount(outMessage, ", ", refusedPlaces, " place(s) refused");
	if (refusedParas > 0)
		AppendCount(outMessage, ", ", refusedParas, " paragraph(s) refused");
	if (refusedAttrs > 0)
		AppendCount(outMessage, ", ", refusedAttrs, " paragraph(s) kept their own ruby/kenten");
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

const KCMStoryTextSet* KCMHeldStoryText()
{
	return sHolding ? &sHeld : nil;
}

void KCMHoldStoryText(const KCMStoryTextSet& set)
{
	sHeld = set;
	sHolding = kTrue;
}

void KCMReleaseStoryText()
{
	const bool16 wasImporting = sHolding;
	sHeld = KCMStoryTextSet();
	sHolding = kFalse;

	// ★★★**THE MODE COMES DOWN WITH THE WORDS, AND THIS IS THE ONLY PLACE THAT CAN DO IT.** A
	//   document closed while an import was showing arrives here through KCMReleaseOrigin and
	//   through nothing else: Stop Comparison, which is the one caller of KCMEndImportMode, is
	//   greyed by then. Leaving kKCMModeImport set stranded the reader with every mode and Task
	//   Start greyed (measured 2026-09-15).
	// ⚠Guarded on BOTH counts on purpose: a release that was not an import must not move the
	//   reader's mode, and neither must the second, empty pass KCMEndImportMode makes through
	//   KCMReleaseOrigin.
	if (wasImporting && KCMGetCompareMode() == kKCMModeImport)
		KCMSetCompareMode(sModeBeforeImport);
}

bool16 KCMInImportMode()
{
	// ★One fact, one place: holding the edited words IS the mode (see sHolding).
	return sHolding;
}

void KCMEndImportMode()
{
	if (!KCMInImportMode())
		return;

	// ★THE ORDER: the words go first, so that nothing asks "are we importing?" while the origin is
	//   being moved back underneath it. Dropping them takes the mode down with them.
	KCMReleaseStoryText();

	// The import's own origin goes; the reader's own comes back exactly as they left it.
	if (KCMHasParkedOrigin())
		KCMUnparkOrigin();
	else
		KCMReleaseOrigin();
}

// End, KCMStoryTextImport.cpp.
