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

	@return how many writes went in; -1 when the paragraph was refused (whyNot filled).
*/
int32 ApplyParagraph(ITextModel* model, TextIndex paraStart, const KCMParaAttrs& attrs,
					 const std::string& docText, const std::string& fileText,
					 PMString& whyNot)
{
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
		return -1;
	}

	// ⚠**NOTHING IS WRITTEN UNTIL EVERY CHANGE HAS BEEN JUDGED.** A paragraph half applied and then
	//   refused would be worse than one refused whole, and the reader would have no way to tell.
	for (size_t c = 0; c < changes.size(); ++c)
	{
		const KCMTextDiff::Change& ch = changes[c];
		if (RangeTouchesInvisible(a, ch.aStart, ch.aStart + ch.aCount)
			|| RangeTouchesInvisible(b, ch.bStart, ch.bStart + ch.bCount))
		{
			whyNot = "a change would move or delete a character that is not a letter "
					 "(an anchored object, a page number, an index marker)";
			whyNot.SetTranslatable(kFalse);
			return -1;
		}
	}

	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
	{
		whyNot = "the story cannot be edited";
		whyNot.SetTranslatable(kFalse);
		return -1;
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
			whyNot = "the write failed (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			return (written > 0) ? written : -1;
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
	int32 refusedParas = 0;
	int32 refusedPlaces = 0;
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

		std::vector<std::string> paras;
		std::vector<KCMParaAttrs> attrs;
		std::vector<int32> starts;
		if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
			continue;

		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

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
				const int32 n = ApplyParagraph(model, static_cast<TextIndex>(starts[i]), attrs[i],
											   paras[i], (*place.fFile)[q - 1].fText, whyNot);
				if (n < 0)
				{
					++refusedParas;
					if (firstRefusal.IsEmpty())
						firstRefusal = whyNot;
					continue;
				}
				if (n > 0)
				{
					edits += n;
					touched = kTrue;
				}
			}
		}
		if (touched)
			++storiesTouched;
	}

	// Files whose story the copy does not carry - counted by elimination, since the pairing above
	// is the only place that can tell.
	unmatched = static_cast<int32>(set->fUids.size()) - storiesTouched;
	if (unmatched < 0)
		unmatched = 0;

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	AppendCount(outMessage, "", edits, " change(s) poured into the copy");
	AppendCount(outMessage, " in ", storiesTouched, " story(ies)");
	if (refusedPlaces > 0)
		AppendCount(outMessage, ", ", refusedPlaces, " place(s) refused");
	if (refusedParas > 0)
		AppendCount(outMessage, ", ", refusedParas, " paragraph(s) refused");
	if (unmatched > 0)
		AppendCount(outMessage, ", ", unmatched, " file(s) had no story");
	if (!firstRefusal.IsEmpty())
	{
		outMessage.Append(" (");
		outMessage.Append(firstRefusal);
		outMessage.Append(")");
	}

	return (edits > 0) ? kTrue : kFalse;
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
