//========================================================================================
//
//  KCMStoryTextImport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>				// FindFirstFileW - Windows only, like the rest of KCM's file work
#include <cstdio>
#include <string>
#include <vector>

#include "ICommand.h"
#include "ICommandSequence.h"		// the whole pouring is one step
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "WideString.h"

#include "KCMStoryTextImport.h"
#include "KCMComparisonRun.h"		// KCMToggleStartStop - the start, through the one resolver
#include "KCMCore.h"				// KCMSetCompareMode
#include "KCMOrigin.h"				// KCMTakeTaskStart - the state the edited words are compared against
#include "KCMParaText.h"			// ModelOffsetInParagraph / AppendUtf8
#include "KCMRehydrate.h"			// KCMReadOriginUidLabel - the copy's stories carry the original UID
#include "KCMTextDiff.h"			// ToCodePoints / Diff
#include "KCMTextRead.h"			// ReadStory - the copy, read the same way the export read the original

namespace
{

/** The set held beside the origin. ⚠A file-static holding PMStrings and std::strings, so it has a
	line in the model's shutdown (KCMStoryList.h says what forgetting that costs). */
KCMStoryTextSet		sHeld;
bool16				sHolding = kFalse;

/** The path of an IDFile as Windows spells it. (KCMStoryTextExport.cpp has the same four lines,
	and for the same reason: the two files share nothing else.) */
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

/** "<folder>\<leaf>" as an IDFile. */
IDFile FileInFolder(const std::wstring& folder, const std::wstring& leaf)
{
	std::wstring path = folder;
	if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
		path += L"\\";
	path += leaf;

	PMString s;
	s.SetTranslatable(kFalse);
	s.AppendW(reinterpret_cast<const UTF16TextChar*>(path.c_str()));
	return FileUtils::PMStringToSysFile(s);
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
//  Pouring the edited text into the copy
//========================================================================================

/** One place in a story - the body, one cell, or one footnote - as the two sides see it.

	★**THE PLACES ARE BUILT THE SAME WAY ON BOTH SIDES**, from KCMParaAttrs on the copy and from
	the Story's own shape in the file, which is how a cell's paragraphs find their cell without
	anything having to be written down in the file about where they came from. */
struct Place
{
	std::vector<size_t>						fCopy;		// indices into the copy's flat arrays
	const std::vector<KCMStoryHtml::Para>*	fFile;		// what the reader edited, or nil

	Place() : fFile(nil) {}
};

/** kTrue when any code point of `text` in [from, to) is one the reader may not move or delete. */
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

	The copy's paragraphs name their (row, column); the file's cells are a plain list. Sorting the
	columns that actually occur is what turns one into the other - and it agrees with the exporter
	by construction, because that walked the anchors in column order too. */
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
			body.fCopy.push_back(i);
	}
	out.push_back(body);

	// ---- the cells ----------------------------------------------------------------------------
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsCell())
			continue;

		// One place per cell: the first paragraph of it opens the place, the rest join.
		bool16 already = kFalse;
		for (size_t k = 0; k < out.size() && !already; ++k)
		{
			if (out[k].fCopy.empty())
				continue;
			const KCMParaAttrs& first = attrs[out[k].fCopy[0]];
			already = (first.IsCell()
					   && first.fTableOrdinal == attrs[i].fTableOrdinal
					   && first.fCellRow == attrs[i].fCellRow
					   && first.fCellCol == attrs[i].fCellCol) ? kTrue : kFalse;
			if (already)
				out[k].fCopy.push_back(i);
		}
		if (already)
			continue;

		Place cell;
		cell.fCopy.push_back(i);

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
			const KCMStoryHtml::Row& row = file.fTables[t].fRows[static_cast<size_t>(attrs[i].fCellRow)];
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
			if (out[k].fCopy.empty())
				continue;
			const KCMParaAttrs& first = attrs[out[k].fCopy[0]];
			already = (first.IsFootnote()
					   && first.fFootnoteOrdinal == attrs[i].fFootnoteOrdinal) ? kTrue : kFalse;
			if (already)
				out[k].fCopy.push_back(i);
		}
		if (already)
			continue;

		Place note;
		note.fCopy.push_back(i);
		const size_t n = static_cast<size_t>(attrs[i].fFootnoteOrdinal);
		if (n < file.fNotes.size())
			note.fFile = &file.fNotes[n];
		out.push_back(note);
	}
}

/*	ApplyParagraph
	The edits between one paragraph of the copy and the same paragraph of the file.

	★★**MINIMAL EDITS, BACK TO FRONT.** Replacing the whole paragraph would be simpler and would
	throw away every attribute on the parts nobody touched - the ruby and the kenten this format
	works so hard to carry. So the two are diffed by code point and only the runs that differ are
	written, starting from the end so that the earlier positions are still true when they are used.

	@return how many writes went in; -1 when the paragraph was refused (whyNot filled).
*/
int32 ApplyParagraph(ITextModel* model, TextIndex paraStart, const KCMParaAttrs& attrs,
					 const std::string& copyText, const std::string& fileText,
					 PMString& whyNot)
{
	if (copyText == fileText)
		return 0;

	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::ToCodePoints(copyText, &a, nil);
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

bool16 KCMReadStoryTextFolder(const IDFile& folder, KCMStoryTextSet& out, PMString& whyNot)
{
	out = KCMStoryTextSet();
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);

	const std::wstring folderPath = WidePath(folder);
	if (folderPath.empty())
	{
		whyNot = "the chosen folder could not be read";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	// The folder's own name, for the panel's Source: line.
	{
		const size_t slash = folderPath.find_last_of(L"\\/");
		const std::wstring leaf = (slash == std::wstring::npos) ? folderPath
																: folderPath.substr(slash + 1);
		out.fFolderName.SetTranslatable(kFalse);
		out.fFolderName.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
	}

	std::wstring pattern = folderPath;
	if (!pattern.empty() && pattern[pattern.size() - 1] != L'\\' && pattern[pattern.size() - 1] != L'/')
		pattern += L"\\";
	pattern += L"*.html";

	WIN32_FIND_DATAW found;
	HANDLE search = ::FindFirstFileW(pattern.c_str(), &found);
	if (search == INVALID_HANDLE_VALUE)
	{
		whyNot = "there is no .html file in that folder";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	int32 skippedName = 0;		// not one of ours: a name that is not a decimal number
	int32 refused = 0;			// ours, but the markup could not be read
	PMString firstReason;
	firstReason.SetTranslatable(kFalse);

	do
	{
		if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			continue;

		const std::wstring leaf(found.cFileName);
		uint32 uid = 0;
		if (!UidOfLeaf(leaf, uid))
		{
			++skippedName;
			continue;
		}

		std::string bytes;
		if (!ReadWholeFile(FileInFolder(folderPath, leaf), bytes))
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
	while (::FindNextFileW(search, &found) != 0);

	::FindClose(search);

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
	sHeld = KCMStoryTextSet();
	sHolding = kFalse;
}

bool16 KCMImportStoryText(const IDFile& folder, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// 1. THE FOLDER FIRST. Nothing is disturbed if it cannot be read - no origin taken, no
	//    comparison stopped, and whatever was held is still held.
	KCMStoryTextSet set;
	PMString readMessage;
	if (!KCMReadStoryTextFolder(folder, set, readMessage))
	{
		outMessage = "import: ";
		outMessage.SetTranslatable(kFalse);
		outMessage.Append(readMessage);
		return kFalse;
	}

	// 2. THE ORIGIN SECOND, and this is the order that matters: taking one releases the origin that
	//    was held, and releasing an origin releases the held stories with it (KCMReleaseOrigin).
	//    Held any earlier, what was just read would be thrown away by the next line.
	PMString whyNot;
	if (!KCMTakeTaskStart(whyNot))
	{
		outMessage = "import: the document's state could not be taken (";
		outMessage.SetTranslatable(kFalse);
		outMessage.Append(whyNot);
		outMessage.Append(")");
		return kFalse;
	}

	// 3. Now they can be held: the copy has not been made yet, and every copy made from here on
	//    gets them poured in (KCMRehydrate, the one place).
	KCMHoldStoryText(set);

	// 4. The Story mode is what shows words against words. ★Set before the start, so the start
	//    builds what this mode needs rather than rasterising pages first.
	KCMSetCompareMode(kKCMModeStory);
	KCMToggleStartStop();

	outMessage = "import: ";
	outMessage.SetTranslatable(kFalse);
	outMessage.Append(readMessage);
	outMessage.Append(" - comparing the document against them");
	return kTrue;
}

bool16 KCMApplyStoryTextToCopy(IDataBase* copyDB, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	const KCMStoryTextSet* set = KCMHeldStoryText();
	if (set == nil || set->fUids.empty())
	{
		outMessage = "no edited stories are held";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	if (copyDB == nil)
	{
		outMessage = "there is no copy to write into";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	InterfacePtr<IStoryList> stories(copyDB, copyDB->GetRootUID(), UseDefaultIID());
	if (stories == nil)
	{
		outMessage = "the copy has no stories";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	int32 storiesTouched = 0;
	int32 edits = 0;
	int32 refusedParas = 0;
	int32 refusedPlaces = 0;
	int32 unmatched = 0;
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);

	// ★ONE STEP. The copy has no window and nobody will press Ctrl+Z in it, but a sequence is what
	//   keeps the writes from arriving as a hundred separate entries in a history that the peek
	//   document shares.
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence("KCMApplyStoryText");
	if (sequence != nil)
		sequence->SetName(PMString("Apply edited story text"));

	const int32 count = stories->GetUserAccessibleStoryCount();
	for (int32 s = 0; s < count; ++s)
	{
		const UIDRef copyRef = stories->GetNthUserAccessibleStoryUID(s);

		// ⚠**THE COPY'S UIDS ARE NEW ONES.** What pairs a file with a story is the label the
		//   rehydration wrote, which carries the ORIGINAL uid - the one the file is named after.
		UID original = kInvalidUID;
		if (!KCMReadOriginUidLabel(copyDB, copyRef.GetUID(), original))
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
		{
			++unmatched;
			continue;
		}

		std::vector<std::string> paras;
		std::vector<KCMParaAttrs> attrs;
		std::vector<int32> starts;
		if (!KCMTextRead::ReadStory(copyRef, paras, attrs, starts))
			continue;

		InterfacePtr<ITextModel> model(copyRef, UseDefaultIID());
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
					firstRefusal = "a cell or note in the document is not in the file";
				continue;
			}

			// ⚠**THE PARAGRAPH COUNT HAS TO MATCH, so far.** Adding and removing paragraphs needs
			//   the exact end of a thread, which is measured work not yet done - so the place is
			//   refused with a reason rather than half-applied.
			if (place.fCopy.size() != place.fFile->size())
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
			for (size_t q = place.fCopy.size(); q > 0; --q)
			{
				const size_t i = place.fCopy[q - 1];
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

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	AppendCount(outMessage, "", edits, " change(s) applied");
	AppendCount(outMessage, " in ", storiesTouched, " story(ies)");
	if (refusedPlaces > 0)
		AppendCount(outMessage, ", ", refusedPlaces, " place(s) refused");
	if (refusedParas > 0)
		AppendCount(outMessage, ", ", refusedParas, " paragraph(s) refused");
	if (unmatched > 0)
		AppendCount(outMessage, ", ", unmatched, " story(ies) had no file");
	if (!firstRefusal.IsEmpty())
	{
		outMessage.Append(" (");
		outMessage.Append(firstRefusal);
		outMessage.Append(")");
	}

	return (edits > 0) ? kTrue : kFalse;
}

// End, KCMStoryTextImport.cpp.
