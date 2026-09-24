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

#include "ICommandSequence.h"		// the whole import is one step
#include "IDataBase.h"
#include "IStoryList.h"
#include "CmdUtils.h"
#include "FileUtils.h"
#include "SysFileList.h"			// what the open dialog hands back - several files at once

#include "KCMStoryTextImport.h"
#include "KCMComparisonRun.h"		// KCMToggleStartStop - the start, through the one resolver
#include "KCMCore.h"				// KCMSetCompareMode - the import shows its result in the Story mode
#include "KCMPairChoice.h"			// KCMChosenTargetDB - the document the Task Start chose, which is where the words go
// ⛔KCMOrigin.h went on 2026-09-21 - "until it goes" was written for the origin slot, and the slot
//   went that day. Nothing in this file read anything the header declared.
#include "KCMTaskStartSave.h"		// KCMTakeTaskStartCopy - the import's own Task Start, saved to a file
#include "KCMProgressBar.h"			// the import's one bar, and the slot its inner loops step (2026-09-17)
#include "KCMDocxPackage.h"			// KCMReadDocxParts - a .docx on disk as its parts (2026-09-19)
#include "KCMStorySync.h"			// Compare - what makes the document's story Word's (2026-09-23)
#include "KCMStorySyncApply.h"		// KCMApplySyncPlan - and that, carried out
#include "KCMStoryTextExport.h"		// KCMStoryFromDocument - the document's story, read the way the export reads it
#include "KCMStoryDocx.h"			// Read - the parts as Word shows them
#include "KCMImportTracking.h"		// the import writes under Track Changes, as KohakuChangeMarker (2026-09-24)
#include "KCMWordKeep.h"			// the Word content kept per story for "Redo from Word" (2026-09-24, stage 2 C)
#include "K2SmartPtr.h"				// K2::scoped_ptr - the author switch is let go of at a chosen moment (reset)
#include "KCMZipStore.h"			// Entry - a part, named
#include "KCMModelNotify.h"			// KCMNotify - a cancelled import tells the panel the mode came back

namespace
{

// (A per-write trace to %TEMP% stood here while the 2026-09-17 crash was run down. It is out of the
//  product - the user's rule - and kept, working, as work/kcm-import-matrix/KCMStoryTextImport-with-trace.cpp.txt.)

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
	PMStrings, so the model's shutdown empties it (KCMPeek.cpp, beside the story list). Dropped at the
	start of the next import and by a cancelled one. */
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

/** The same, for something the document was protected FROM rather than something that failed: it
	gets its "!" row, and the count of what "could not go in" passes it by. */
void NoteHeldBack(UID story, const char* kind, const PMString& whereAndWhy)
{
	NoteRefusal(story, kind, whereAndWhy);
	sRefusals.back().fHeldBack = kTrue;
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
	PMString firstReason;
	firstReason.SetTranslatable(kFalse);

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

		// ---- a .docx: the tag is the pairing, or the name when there is no tag -----------------------
		//
		// ★What goes into fStories is the story AS WORD SHOWS IT - every revision mark accepted - and
		//   that is all the import needs: since 2026-09-23 it makes the document's story Word's
		//   (KCMStorySync), whatever the marks say or whether there are any.
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
		int32 uid = result.fTag.fPresent ? result.fTag.fUid : 0;
		if (!result.fTag.fPresent)
		{
			// ★★A .docx NOBODY EXPORTED - made in Word from nothing (2026-09-23, the user's request: "a Word
			//   file a person made from nothing and named with the UID can be imported too"). The name is
			//   the pairing then, the way the exporter spells it ("269.docx", "269 - chapter one.docx").
			//   ⚠Without a number in the name there is nothing to say which story it is for, and it is named.
			if (leading == 0)
			{
				++refused;
				NoteFirstReason(firstReason, leaf,
								"the file carries no story tag and its name does not begin with a story's number");
				continue;
			}
			uid = static_cast<int32>(leading);
		}
		else if (leading != 0 && leading != static_cast<uint32>(result.fTag.fUid))
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
		out.fUids.push_back(UID(static_cast<uint32>(uid)));
		out.fStories.push_back(result.fAfter);
		out.fFileNames.push_back(PMStringOfLeaf(leaf));
		out.fIsDocx.push_back(kTrue);
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
	bool16 anyIn = kFalse;
	// (the changes are signed "KohakuChangeMarker" INSIDE the pour's own undo step - KCMPourStoryText says why)
	anyIn = KCMPourStoryText(db, set, poured, cancelledPour);
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
	outMessage.Append(poured);			// what went in, and what was left alone and why (KCMPourStoryText)
	// ⚠**WHAT WAS HELD BACK IS NOT COUNTED HERE** (2026-09-22): it has a "!" row and a count of its
	//  own in `poured`, and saying "1 could not go in" of a tate-chu-yoko the import deliberately
	//  kept told the reader the opposite of what happened (seen in the live matrix the same day).
	int32 couldNotGoIn = 0;
	for (size_t i = 0; i < KCMImportRefusals().size(); ++i)
	{
		if (!KCMImportRefusals()[i].fHeldBack)
			++couldNotGoIn;
	}
	if (couldNotGoIn > 0)
		AppendCount(outMessage, " - ", couldNotGoIn, " could not go in (the rows marked !)");
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

// ★★THE IMPORT RESHAPES TABLES AGAIN (2026-09-24 evening, the user's decision, which replaces the morning's
//   design 11-1 item 5): a table whose rows, columns or merged cells changed in Word is made Word's, one
//   stage per round (S1/S2), the words then written into the cells it has. ★What the user accepted with it:
//   InDesign's change history records neither rows, columns nor merges (measured, track.jsx), so THOSE are
//   not taken back one by one - a reject of the import's changes leaves the new shape (a row put in stays,
//   empty; a row taken away does not come back), and the "Table" row's menu offers no reject, its range
//   holding no tracked change. Ctrl+Z takes the whole import back, shape included. The words in the cells
//   ARE recorded and can be rejected, as before. (kFalse held such a table whole, "!", from the morning of
//   2026-09-24 until the evening; the redo of one paragraph, KCMRedoFromWord, still passes kFalse.)
static const bool16 kReshapeTablesOnImport = kTrue;
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
	int32 attrEdits = 0;			// ruby, kenten, tate-chu-yoko, warichu
	int32 noteEdits = 0;			// footnotes made or taken away
	int32 tableEdits = 0;			// the shape rounds' changes: tables added or taken away, rows, columns, merges
	int32 storiesHeld = 0;			// stories left exactly as they were, each named
	int32 heldBack = 0;				// things Word cannot carry, kept as the document has them
	int32 unmatched = 0;
	PMString firstRefusal;
	firstRefusal.SetTranslatable(kFalse);
	// ★**PAIRED IS PAIRED, WHATEVER HAPPENS NEXT** (2026-09-16). The count at the end used to be
	//   "files minus stories WRITTEN", which said "had no story" about a file the reader had simply
	//   not edited - and about every story left alone whole (then by TablesAgree, which went on
	//   2026-09-23), naming that one twice, once under each heading. Kept per file now, so that the ones with no story can be NAMED (a "!" row each).
	std::vector<bool16> matched(set.fUids.size(), kFalse);

	// ★★★ONE ABORTABLE STEP (2026-09-19). The words go into the reader's own document now, so this
	//   IS the undo step Ctrl+Z takes back - and a Cancel between two stories aborts it, leaving the
	//   document as it was. (Until 2026-09-19 a plain sequence poured into the windowless copy.)
	IAbortableCmdSeq* sequence = CmdUtils::BeginAbortableCmdSeq("KCMPourStoryText");
	if (sequence != nil)
		sequence->SetName(PMString("Import Story Text"));

	// ★★THE CHANGES ARE SIGNED "KohakuChangeMarker" (2026-09-24, the user's decision - design 11-1 item 2), AND THE
	//   NAME IS SWITCHED INSIDE THIS STEP (fixed the same day, found on re-check): switched around the whole pour
	//   from KCMImportStoryText, the two name commands were undo steps of their own - "Set User Name" stood on top
	//   of the undo stack after an import, so one Ctrl+Z put the name back to KohakuChangeMarker and left the import
	//   in. Inside the sequence the switch, the words and the switch back are ONE step: undo and redo take all of
	//   it, and a cancel's abort takes back both switches. ⚠Put back BEFORE the sequence ends or aborts (below),
	//   so that the switch back is inside it too.
	K2::scoped_ptr<KCMImportAuthor> author(new KCMImportAuthor());

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

		// ★★WHAT GOES INTO THIS STORY IS RECORDED IN THE CHANGE HISTORY (2026-09-24, the user's decision -
		//   design 11-1): each change can then be rejected one by one in the Track Changes panel, and a
		//   rejected deletion brings back what it took, footnotes and tables included (measured the same
		//   day). Tracking is on for this story from here to the end of this loop's body - every
		//   `continue` included - and back to the reader's own setting after; being inside the sequence,
		//   Ctrl+Z takes the switch back along with the words.
		KCMStoryTrackingOn tracking(storyRef);

		// ★★★**THE DOCUMENT AGAINST WORD, AND THE DOCUMENT MADE WORD'S** (2026-09-23, the user's rule: "Word
		//   is the one that counts" - and "the processing that is simplest and least likely to be wrong").
		//   KCMStorySync says what makes the story as the document holds it into the story as Word left it,
		//   and KCMStorySyncApply carries that out. What was changed in InDesign after the export is written
		//   over. Design: docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md.
		const int32 tableEditsBefore = tableEdits;
		KCMStoryShape::Story now;
		bool16 placed = kTrue;
		if (!KCMStoryFromDocument(storyRef, now, placed) || !placed)
		{
			++storiesHeld;
			PMString why("the story could not be read the way the export reads it");
			why.SetTranslatable(kFalse);
			if (firstRefusal.IsEmpty())
				firstRefusal = why;
			NoteRefusal(original, "Story", why, kTrue);
			continue;
		}
		KCMStorySync::Plan plan;
		KCMStorySync::Compare(now, set.fStories[which], plan, kReshapeTablesOnImport);
		// ★★SHAPE ROUNDS FIRST (S1/S2, design sections 8-2 and 9-4): a table's merges taken apart, its rows
		//   and columns made as many as Word's at the end, Word's merges made - one stage per round. After
		//   each, the story is read again and checked against the same change made on paper, and the
		//   comparison runs once more on what the document now holds - so every number in the plan that
		//   writes the words is the document's own. What does not read back as planned is named, never
		//   guessed at; and five rounds is more than any shape needs (three stages).
		bool16 shapeFailed = kFalse;
		for (int32 round = 0; !plan.fStoryHeld && plan.IsShapeRound(); ++round)
		{
			PMString why;
			if (round >= 5)
				why = "a table's shape is still not Word's after five rounds of changes";
			else
			{
				KCMSyncResult shape;
				KCMApplyTableShape(storyRef, plan, shape);
				tableEdits += shape.fTableEdits;
				for (size_t k = 0; k < shape.fNotes.size(); ++k)
				{
					NoteRefusal(original, shape.fNotes[k].fKind, shape.fNotes[k].fWhy);
					if (firstRefusal.IsEmpty())
						firstRefusal = shape.fNotes[k].fWhy;
				}
				const KCMStoryShape::Story paper = KCMStorySync::ReshapeOnPaper(now, plan);
				KCMStoryShape::Story again;
				bool16 placedAgain = kTrue;
				std::string layoutWhy;
				if (KCMStoryFromDocument(storyRef, again, placedAgain) && placedAgain
					&& KCMStorySync::SameTableLayout(paper, again, layoutWhy))
				{
					now = again;
					KCMStorySync::Compare(now, set.fStories[which], plan, kReshapeTablesOnImport);
					continue;
				}
				why = "a table's shape was changed, but the story does not read back as planned";
				if (!layoutWhy.empty())
				{
					why.Append(" (");
					why.Append(layoutWhy.c_str());
					why.Append(")");
				}
			}
			++storiesHeld;
			why.SetTranslatable(kFalse);
			if (firstRefusal.IsEmpty())
				firstRefusal = why;
			NoteRefusal(original, "Story", why, kTrue);
			shapeFailed = kTrue;
			break;
		}
		if (shapeFailed)
			continue;
		if (plan.fStoryHeld)
		{
			++storiesHeld;
			PMString why;
			why.SetUTF8String(plan.fWhy);
			why.SetTranslatable(kFalse);
			if (firstRefusal.IsEmpty())
				firstRefusal = why;
			NoteRefusal(original, "Story", why, kTrue);
			continue;
		}
		KCMSyncResult result;
		KCMApplySyncPlan(storyRef, now, plan, result);
		for (size_t k = 0; k < result.fNotes.size(); ++k)
		{
			const KCMSyncNote& note = result.fNotes[k];
			if (note.fHeldBack)
			{
				// ⚠A RESCUE MUST NOT TAKE firstRefusal: the status line's one reason is for what failed
				++heldBack;
				NoteHeldBack(original, note.fKind, note.fWhy);
				continue;
			}
			NoteRefusal(original, note.fKind, note.fWhy, note.fWholeStory);
			if (firstRefusal.IsEmpty())
				firstRefusal = note.fWhy;
		}
		// ★★READ AGAIN AND CHECK WHERE THE TABLES STAND (2026-09-24, S3b design 12-3-4): the plan carried out on
		//   paper against the document as it now is. Only the live write can leave a table behind - the
		//   matrix's X03 put one in the middle of a word and said nothing - so this is the one check that sees
		//   it. What it finds cannot be taken back here (the words are in); it is NAMED, never passed over.
		//   ⚠Not when a write was already refused in this story: the document then differs from the plan by
		//    that refusal, which has its own "!" row - a second row would only say the same thing again.
		if (result.fRefused == 0 && result.fWrites + result.fNoteEdits > 0)
		{
			KCMStoryShape::Story n2;
			KCMStoryShape::Story w2;
			std::string normWhy;
			KCMStoryShape::Story after;
			bool16 placedAfter = kTrue;
			std::string placeWhy;
			if (KCMStorySync::Normalize(now, set.fStories[which], n2, w2, normWhy)
				&& KCMStoryFromDocument(storyRef, after, placedAfter) && placedAfter
				&& !KCMStorySync::SameTablePlaces(KCMStorySync::ApplyToShape(n2, plan), after, placeWhy))
			{
				PMString why("a table does not stand where the import put it (");
				why.Append(placeWhy.c_str());
				why.Append(") - check this story");
				why.SetTranslatable(kFalse);
				if (firstRefusal.IsEmpty())
					firstRefusal = why;
				NoteRefusal(original, "Table", why, kTrue);
			}
		}

		edits += result.fWrites;
		attrEdits += result.fAttrWrites;
		noteEdits += result.fNoteEdits;
		const bool16 touched = (result.fWrites + result.fAttrWrites + result.fNoteEdits > 0 || tableEdits > tableEditsBefore) ? kTrue : kFalse;

		if (touched)
			++storiesTouched;
	}

	// ★A CANCEL TAKES THE WHOLE POUR BACK - and says nothing else: what was noted so far is dropped
	//   too, because the document those refusals were about is the document as it was.
	if (outCancelled)
	{
		author.reset();		// the name back, inside the sequence - the abort then takes back both switches
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

	author.reset();			// the name back, inside the sequence (see where it was switched)
	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	// ★WORD'S CONTENT IS KEPT PER STORY for "Redo from Word" (2026-09-24, stage 2 C - design 15-1-6): every story
	//   this pour paired, whether or not anything went in - a story held whole has nothing to redo, and the redo's
	//   own plan says so. Replaces what the last import kept for the same story.
	for (size_t k = 0; k < matched.size(); ++k)
		if (matched[k] && k < set.fStories.size())
			KCMWordKeepPut(db, set.fUids[k], set.fStories[k]);

	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	AppendCount(outMessage, "", edits, " change(s) put into the document");
	AppendCount(outMessage, " in ", storiesTouched, " story(ies)");
	if (attrEdits > 0)
		AppendCount(outMessage, ", ", attrEdits, " ruby/kenten write(s)");
	if (noteEdits > 0)
		AppendCount(outMessage, ", ", noteEdits, " footnote(s) added or removed");
	if (tableEdits > 0)
		AppendCount(outMessage, ", ", tableEdits, " table shape change(s) (tables added or taken away, rows, columns, merges - not in the change history)");
	if (storiesHeld > 0)
		AppendCount(outMessage, ", ", storiesHeld, " story(ies) left alone (the rows marked !)");
	if (heldBack > 0)
		AppendCount(outMessage, ", ", heldBack, " thing(s) Word cannot carry kept as they were (the rows marked !)");
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
	return (edits > 0 || attrEdits > 0 || noteEdits > 0 || tableEdits > 0) ? kTrue : kFalse;
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
