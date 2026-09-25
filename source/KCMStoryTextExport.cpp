//========================================================================================
//
//  KCMStoryTextExport.cpp -- see the header.
//
//  The shape of the work: ask KCMTextRead for a story's paragraphs (it already reports, for each
//  one, whether it is body text, a cell of some table, or a footnote's own words), ask
//  KCMReadTableShapes for the things a paragraph cannot know - how many rows the table has, which
//  cells are merged, which rows are header rows - hand both to KCMStoryDocx::WriteParts, and put
//  the bytes in a file. (Until 2026-09-24 this file walked the tables a second time, into a struct
//  of its own that said the same things; the Table row's reading says them once now.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstdio>					// swprintf_s - the file's name
#include <ctime>					// the folder's time stamp
#include <cwchar>
#include <algorithm>
#include <string>
#include <vector>

#include "IDataBase.h"
#include "IDocument.h"
#include "IPMStream.h"
#include "IStoryList.h"
#include "IStoryOptions.h"			// IsVertical - the story's own setting, not a frame's
#include "ITextModel.h"
#include "ITextStoryThread.h"		// a footnote reference IS its note's thread - which note a reference belongs to
#include "ITextUtils.h"				// CollectOwnedItems + OwnedItemDataList - the walk KCMTextRead::ScanNotes uses
#include "FileUtils.h"				// the folder made and the file written the SDK's way (DoesFileExist / CreateFolderIfNeeded)
#include "StreamUtil.h"
#include "TextID.h"					// kFootnoteReferenceBoss
#include "UIDList.h"
#include "UIDRef.h"
#include "WideString.h"

#include "KCMStoryTextExport.h"
#include "KCMStoryShape.h"
#include "KCMStoryDocx.h"		// the .docx road (2026-09-19)
#include "KCMTableShape.h"		// KCMReadTableShapes - the story's tables, the one reading (2026-09-24)
#include "KCMTextRead.h"
#include "KCMSkippedText.h"		// what the page does not set - deleted text and what stands in it (2026-09-24)
#include "KCMParaText.h"
#include "KCMTextDiff.h"		// ToCodePoints - the one walk over UTF-8 this half is allowed
#include "KCMTextWords.h"		// PMStringOfUtf8 - a UTF-8 reason onto the status line without mojibake

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

/** "<parent>\<stem> YYYY-MM-DD HHMMSS", created. kFalse when it could not be made.

	★THE SDK'S OWN FILE UTILITIES (2026-09-24 - until then CreateDirectoryW, the one Win32 call in
	  this half). ⚠A FOLDER ALREADY STANDING THERE IS NOT SUCCESS: the stamp runs to the second, so
	  one of this name was made by something else, and writing into it would be the silent overwrite
	  the stamp exists to prevent. CreateFolderIfNeeded answers kTrue for one that exists, which is
	  why DoesFileExist is asked first. */
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

	PMString path;
	path.SetTranslatable(kFalse);
	path.AppendW(reinterpret_cast<const UTF16TextChar*>(folder.c_str()));
	const IDFile file = FileUtils::PMStringToSysFile(path);
	if (FileUtils::DoesFileExist(file) || !FileUtils::CreateFolderIfNeeded(file))
		return kFalse;

	outFolder = folder;
	return kTrue;
}

/** One paragraph of KCMTextRead's, in the shape the writer wants - its words and the four kinds of
	mark over them. Where its footnote references and endnote markers stand is put in afterwards, by
	BuildStory, from the document's owned items (attrs.fFootnote is read by nobody here). */
void FillPara(const std::string& text, const KCMParaAttrs& attrs, KCMStoryShape::Para& out)
{
	out.fText = text;
	out.fRuby = attrs.fRuby;
	out.fKenten = attrs.fKenten;
	// ★TATE-CHU-YOKO TRAVELS TOO (2026-09-17, the user's request). Its value is already the characters
	//   it covers - KCMTextRead settles that when it closes the paragraph - which is exactly what
	//   KCMStoryDocx's reader produces, so the self-check compares like with like.
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
bool16 BuildStory(const UIDRef& storyRef, KCMStoryShape::Story& out, bool16& outNoteRefsPlaced)
{
	out = KCMStoryShape::Story();
	outNoteRefsPlaced = kTrue;

	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
		return kFalse;

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	// ★ONE ANSWER TO "WHAT DOES THE PAGE NOT SET" for the tables and the note references below - the
	//   same one KCMTextRead::ReadStory gave for the paragraphs (KCMSkippedText.h).
	KCMSkippedText skipped;
	if (model != nil)
		skipped.Build(model);
	// ★THE TABLE ROW'S OWN READING (KCMTableShape.h): the same walk as KCMTextRead's, the same tables left
	//   out, so index k here is the table ordinal the paragraphs name.
	std::vector<KCMTableShape> shapes;
	if (model != nil && !KCMReadTableShapes(model, skipped, shapes))
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
		KCMStoryShape::Table table;
		table.fParaIndex = 0;
		table.fOffset = 0;
		// ⚠**THE EXPORTER NEVER SPLITS A PARAGRAPH.** KCMTextRead reports a paragraph holding a
		//   table as ONE paragraph (the table's own character is simply not counted), so the table
		//   is written after it whole, at the offset found below.
		for (int32 r = 0; r < shapes[t].fRows; ++r)
		{
			KCMStoryShape::Row row;
			row.fHeader = (r >= shapes[t].fHeaderStart
						   && r < shapes[t].fHeaderStart + shapes[t].fHeaderCount) ? kTrue : kFalse;
			for (size_t c = 0; c < shapes[t].fCells.size(); ++c)
			{
				if (shapes[t].fCells[c].fRow != r)
					continue;
				KCMStoryShape::Cell cell;
				KCMTableCellSpan(shapes[t], r, shapes[t].fCells[c].fCol, cell.fRowSpan, cell.fColSpan);
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

		KCMStoryShape::Para p;
		FillPara(paras[i], attrs[i], p);

		if (attrs[i].IsCell())
		{
			const size_t t = static_cast<size_t>(attrs[i].fTableOrdinal);
			if (t >= out.fTables.size())
				continue;						// a cell of a table the walk did not find

			// The cell at that grid address, among the anchors of its row.
			KCMStoryShape::Table& table = out.fTables[t];
			if (attrs[i].fCellRow < 0 || static_cast<size_t>(attrs[i].fCellRow) >= table.fRows.size())
				continue;

			KCMStoryShape::Row& row = table.fRows[static_cast<size_t>(attrs[i].fCellRow)];
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
				out.fNotes.push_back(std::vector<KCMStoryShape::Para>());
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
		// ★★★**THE ANCHOR, NOT THE CELLS** (KCMTableShape.h says why the two are nowhere near each other):
		//   a table's cells stand past the whole body, its anchor where the table stands.
		const TextIndex anchor = shapes[t].fAnchorStart;
		int32 host = -1;
		for (size_t i = 0; i < paras.size(); ++i)
		{
			if (static_cast<TextIndex>(starts[i]) <= anchor)
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
			&& anchor >= paraEnds[static_cast<size_t>(host)])
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
		if (anchor < static_cast<TextIndex>(starts[static_cast<size_t>(host)]))
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
	// ★★**THE HTML FORMAT DOES NOT CARRY THIS AND THE DOCX ONE HAS TO** (KCMStoryShape::NoteRef): Word
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
			// ★★**AN ENDNOTE'S ANCHOR RIDES THE SAME WALK** (2026-09-23, the user's call: the reader
			//   in Word has to be able to see that a note hangs here). Everything below - which
			//   paragraph, where in it - is the same question for both; only what is written down at
			//   the end differs, because an endnote's WORDS are not this story's at all.
			const bool16 isFootnote = (owned[k].fClassID == kFootnoteReferenceBoss) ? kTrue : kFalse;
			const bool16 isEndnote = (owned[k].fClassID == kEndnoteAnchorBoss) ? kTrue : kFalse;
			if (!isFootnote && !isEndnote)
				continue;
			// ★A REFERENCE IN TEXT THE PAGE DOES NOT SET is not one to place (2026-09-24): a footnote
			//   deleted under Track Changes has no paragraph to stand in, and refusing the story for it
			//   was exactly what went wrong (KCMSkippedText.h).
			if (skipped.Contains(owned[k].fAt))
				continue;

			const TextIndex refAt = owned[k].fAt;

			// ---- which note ---------------------------------------------------------------------
			// ⚠**ONLY A FOOTNOTE HAS ONE TO FIND.** An endnote's text is a story of its own and
			//  arrives as its own file, so there is nothing in THIS story to pair it with - and the
			//  thread is not even ASKED FOR in that case. ★An InterfacePtr built on kInvalidUID
			//  would answer nil, but asking the database about a UID that is not one is not a thing
			//  this code should do to find that out.
			int32 note = -1;
			if (isFootnote)
			{
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

			// ⚠**AN ENDNOTE THAT CANNOT BE PLACED DOES NOT REFUSE THE FILE.** A footnote that cannot
			//  be placed would be LOST by Word, so the story is turned away; an endnote's words are
			//  safe in their own file either way, and all that is missing is the mark in the body -
			//  which is exactly what this story was in before the mark existed at all.
			// ⚠★★**AN ENDNOTE INSIDE A FOOTNOTE LOSES ITS MARK, AND SAYS SO NOWHERE.** The test
			//  `IsFootnote()` above turns away any anchor standing in a note's own words, which is
			//  right for a footnote reference (Word cannot hold one there) but for an endnote it
			//  means the mark is simply not written. **That is the state this road was in for every
			//  endnote until today**, so it is no worse than before - written down because the next
			//  person to read this will wonder, and because "no worse than before" is not "right".
			if (host < 0 || attrs[static_cast<size_t>(host)].IsFootnote()
				|| placeIndex[static_cast<size_t>(host)] < 0 || (isFootnote && note < 0))
			{
				if (isFootnote)
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
			KCMStoryShape::Para* para = nil;
			if (attrs[h].IsCell())
			{
				const size_t t = static_cast<size_t>(attrs[h].fTableOrdinal);
				if (t < out.fTables.size() && attrs[h].fCellRow >= 0
					&& static_cast<size_t>(attrs[h].fCellRow) < out.fTables[t].fRows.size()
					&& cellWhich[h] >= 0)
				{
					KCMStoryShape::Row& row = out.fTables[t].fRows[static_cast<size_t>(attrs[h].fCellRow)];
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
				if (isFootnote)
					outNoteRefsPlaced = kFalse;
				continue;
			}

			if (isEndnote)
			{
				// ★**THE PLACE ALONE** - see KCMStoryShape::Para::fEndnoteAt. Kept in order for the
				//   same reason the references below are: the writer walks a paragraph once.
				const int32 at = (textOffset > 0) ? textOffset : 0;
				std::vector<int32>& marks = para->fEndnoteAt;
				size_t slot = marks.size();
				while (slot > 0 && marks[slot - 1] > at)
					--slot;
				marks.insert(marks.begin() + static_cast<std::ptrdiff_t>(slot), at);
				continue;
			}

			KCMStoryShape::NoteRef ref;
			ref.fAt = (textOffset > 0) ? textOffset : 0;
			ref.fNote = note;
			// ★KEPT IN ORDER OF fAt HERE, NOT TRUSTED TO ARRIVE SO: KCMStoryDocx walks a paragraph's
			//   references once, front to back, and one out of order would be written late. The owned
			//   items do come in TextIndex order on everything measured - this is what makes that an
			//   observation rather than something the writer's correctness hangs on. Equal places keep
			//   the order they arrived in.
			std::vector<KCMStoryShape::NoteRef>& refs = para->fNoteRefs;
			size_t where = refs.size();
			while (where > 0 && refs[where - 1].fAt > ref.fAt)
				--where;
			refs.insert(refs.begin() + static_cast<std::ptrdiff_t>(where), ref);
		}
	}

	// ⚠A STORY WITH NO PARAGRAPHS AT ALL still gets one, so that "the file is empty" and "there is
	//   no file" stay different things.
	if (out.fBody.empty())
		out.fBody.push_back(KCMStoryShape::Para());

	return kTrue;
}

/** One file's bytes, exactly as given.

	⚠**NO BOM, EVER.** A .docx is a zip, and three bytes in front of a zip's first signature are three
	 bytes in front of everything its directory points at. (The retired .html spelling asked for one,
	 and this took a `withBom` until 2026-09-24 - always kFalse since the spelling went.) */
bool16 WriteFileBytes(const std::wstring& path, const std::string& bytes)
{
	PMString pathString;
	pathString.SetTranslatable(kFalse);
	pathString.AppendW(reinterpret_cast<const UTF16TextChar*>(path.c_str()));

	const IDFile file = FileUtils::PMStringToSysFile(pathString);

	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWriteLazy(file, kOpenOut | kOpenTrunc));
	if (stream == nil)
		return kFalse;
	if (!bytes.empty())
	{
		stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(bytes.c_str())),
						 static_cast<int32>(bytes.size()));
	}
	// ★★THE STATE, READ AFTER Flush (2026-09-25, the Word round trip re-check, item 8). A LAZY stream opens the file at
	//   its first write, so it cannot answer nil for a folder it may not write into - the state is the only thing that
	//   can say so, and XferByte may only buffer, so a full disk surfaces at Flush. This counted every file as
	//   written until today. KBS (KBSReportSave) and KESCL read it the same way.
	stream->Flush();
	const bool16 failed = (stream->GetStreamState() == kStreamStateFailure) ? kTrue : kFalse;
	stream->Close();
	return failed ? kFalse : kTrue;
}

/** The bytes of one story, into "<folder>\<uid>.docx". */
bool16 WriteStoryFile(const std::wstring& folder, int32 uid, const std::string& bytes)
{
	wchar_t leaf[64] = { 0 };
	::swprintf_s(leaf, 64, L"\\%d.docx", static_cast<int>(uid));
	return WriteFileBytes(folder + leaf, bytes);
}

// (⛔DocumentNameUtf8 stood here until 2026-09-22. It read the document's name for the .docx's story
//  tag, and the tag stopped carrying it that day - the user's call: a name changes under a Save As,
//  so it is not something to write down or to check a file against. KCMStoryDocx.h says what pairs a
//  file with a story instead.)

}	// anonymous namespace

bool16 KCMStoryFromDocument(const UIDRef& storyRef, KCMStoryShape::Story& out, bool16& outNoteRefsPlaced)
{
	// The one reader, made public for the import's comparison (the header says why); BuildStory stays
	// where the export's other helpers are.
	return BuildStory(storyRef, out, outNoteRefsPlaced);
}

bool16 KCMTableRefsOfStory(const UIDRef& storyRef, std::vector<UIDRef>& out)
{
	out.clear();
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;
	// ★THE SAME READING AS BuildStory's, so index k here IS table ordinal k there - the same order and
	//   the same tables left out (a table deleted under Track Changes is still a dictionary, and counting
	//   it here would hand the import's next step the WRONG table). Each shape names its table boss.
	std::vector<KCMTableShape> shapes;
	if (!KCMReadTableShapes(model, shapes))
		return kFalse;
	IDataBase* const db = storyRef.GetDataBase();
	for (size_t k = 0; k < shapes.size(); ++k)
		out.push_back(UIDRef(db, shapes[k].fDictUID));
	return kTrue;
}

bool16 KCMExportStoryText(IDataBase* db, const IDFile& parent, const UIDList& onlyThese,
						  PMString& outMessage)
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

	int32 written = 0;
	int32 refused = 0;
	// What stopped the first story that could not be written, so the message can say more than a
	// number - the reader needs to know WHICH story and WHY before they can do anything about it.
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);

	for (size_t t = 0; t < targets.size(); ++t)
	{
		const UIDRef storyRef = targets[t];

		KCMStoryShape::Story story;
		bool16 noteRefsPlaced = kTrue;
		if (!BuildStory(storyRef, story, noteRefsPlaced))
		{
			++refused;
			continue;
		}

		// ★★★**THE FILE CHECKS ITSELF BEFORE IT IS WRITTEN** (stage 2 of the docx plan, 2026-09-19,
		//   keeping the rule the retired .html road brought in on 2026-09-16): the parts are read
		//   straight back (as Word would show them - until 2026-09-23 the side as written was read and
		//   compared too) and compared with the story they came from, and the file is
		//   only written when they agree. A story this format cannot carry (a ruby a table cuts in
		//   two; the reader's Slice says why) is refused HERE, not after somebody has spent an
		//   afternoon editing it and the import turns them away with a count.
		//   ⚠**COMPARED AS THIS FORMAT SETTLES IT** (KCMStoryDocx::SettleForThisFormat): a mono reading
		//    over several characters is one <w:ruby> and reads back as group, and that is not a
		//    difference the comparison minds (the user's rule of 2026-09-12).
		//   ⚠**IT TOUCHES NOTHING**: WriteParts, Read and Same are pure functions on plain structs
		//    (KCMStoryShape and KCMStoryDocx hold no SDK type at all), so the document is not read a
		//    second time and cannot be dirtied by this.
		//   What it also refuses: a story whose footnote references could not be placed, which Word
		//   would lose.
		{
			std::vector<KCMZipStore::Entry> parts;
			std::string why;
			if (!noteRefsPlaced)
				why = "a footnote's reference could not be placed";

			bool16 sound = why.empty()
						   && KCMStoryDocx::WriteParts(story, static_cast<int32>(storyRef.GetUID().Get()),
													   parts, why);
			if (sound)
			{
				KCMStoryShape::Story settled = story;
				KCMStoryDocx::SettleForThisFormat(settled);
				KCMStoryDocx::ReadResult back;
				sound = KCMStoryDocx::Read(parts, back, why)
						&& KCMStoryShape::Same(settled, back.fAfter, why, kTrue);
			}
			if (!sound)
			{
				++refused;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal.AppendNumber(static_cast<int32>(storyRef.GetUID().Get()));
					firstRefusal.Append(": ");
					// ⚠UTF-8 (a kenten's name can be Japanese): through SetUTF8String, not Append(c_str()), which read
					//  the bytes in the machine's codepage (2026-09-25, the Word round trip re-check, item 7)
					firstRefusal.Append(KCMTextWords::PMStringOfUtf8(why));
				}
				continue;
			}

			std::string docx;
			KCMZipStore::Write(parts, docx);
			if (WriteStoryFile(folder, storyRef.GetUID().Get(), docx))
				++written;
			else
			{
				++refused;
				if (firstRefusal.IsEmpty())
				{
					firstRefusal.AppendNumber(static_cast<int32>(storyRef.GetUID().Get()));
					firstRefusal.Append(": the file could not be written (is the folder writable, is the disk full?)");
				}
			}
		}
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
