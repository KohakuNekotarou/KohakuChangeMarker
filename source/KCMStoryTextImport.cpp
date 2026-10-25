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
#include "ICmdHistory.h"			// Discard - the import's copy keeps no undo history of its pour (2026-10-24)
#include "ICommandProcessor.h"		// only to reach the boss ICmdHistory lives on
#include "IDataBase.h"
#include "ISession.h"
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
#include "KCMTaskStartSave.h"		// KCMTaskDocumentDB - the document the import takes its copy of, refused when a PDF one
#include "KCMTaskStartClone.h"		// KCMTakeImportClone - the import's copy in memory (2026-10-24)
#include "KCMStoryList.h"			// the rows the Story mode built - what the import-as-Source proposes
#include "KCMProgressBar.h"			// the import's one bar, and the slot its inner loops step (2026-09-17)
#include "KCMDocxPackage.h"			// KCMReadDocxParts - a .docx on disk as its parts (2026-09-19)
#include "KCMStorySync.h"			// Compare - what makes the document's story Word's (2026-09-23)
#include "KCMStorySyncApply.h"		// KCMApplySyncPlan - and that, carried out
#include "KCMStoryTextExport.h"		// KCMStoryFromDocument - the document's story, read the way the export reads it
#include "KCMStoryDocx.h"			// Read - the parts as Word shows them
#include "KCMPageDocx.h"			// ReadParts - a page file's boxes (2026-10-25, Export Pages to Word)
#include "KCMPageImport.h"			// KCMSpliceBoxesIntoSet - the boxes made stories, in the import's copy
#include "KCMZipStore.h"			// Entry - a part, named
#include "KCMModelNotify.h"			// KCMNotify - a cancelled import tells the panel the mode came back
#include "KCMTextWords.h"			// PMStringOfUtf8 - a UTF-8 reason onto the status line without mojibake
#include "KCMPdfDoc.h"				// KCMIsPdfDoc - a PDF document is refused here (2026-10-14)
#include "IKCMCompareFacade.h"		// the refusal's words, which the UI says too

namespace
{

// (A per-write trace to %TEMP% stood here while the 2026-09-17 crash was run down. It is out of the
//  product - the user's rule - and kept, working, as work/kcm-import-matrix/KCMStoryTextImport-with-trace.cpp.txt.)

/** The import's one progress bar, in thousandths of the whole job (2026-09-17). Each constant is where
	a stage STARTS: reading the files runs up to the state, taking the copy up to the pour, the pour
	(into the copy) up to the comparison, and the comparison to the end. ⚠A guess at where the time
	goes, not a measurement: taking the copy is one call into InDesign and is given a slice of its own
	because on a large document it is the heavy part, and the bar can only stand still through it. */
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

/** The two above when `on` - a pour that notes nothing (an Apply, 2026-10-24: its "!" rows were noted when the import
	read the files) still counts what it could not put in. */
void NoteRefusalIf(bool16 on, UID story, const char* kind, const PMString& whereAndWhy, bool16 wholeStory = kFalse,
				   const PMString& fileName = PMString())
{
	if (on)
		NoteRefusal(story, kind, whereAndWhy, wholeStory, fileName);
}

void NoteHeldBackIf(bool16 on, UID story, const char* kind, const PMString& whereAndWhy)
{
	if (on)
		NoteHeldBack(story, kind, whereAndWhy);
}

/** A PMString as wide characters. (KCMReport.cpp has the same four lines inside its WidePath;
	KCMStoryTextExport.cpp had them too until 2026-09-27, when its paths became IDFiles joined with
	FileUtils::AppendPath. What this one is used for is M18's to judge.) */
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

/** "<leaf>: <why>" into `firstReason` when it is still empty.
	⚠`why` IS UTF-8 (the reader's words - "a kenten style this reader cannot read: ..." names a style whose name
	 begins with the Japanese for kenten), so it goes in through SetUTF8String: Append(c_str()) read the bytes in the machine's codepage and
	 put mojibake on the status line (2026-09-25, the Word round trip re-check, item 7 - KCMStoryAttrPour had measured
	 the same on 2026-09-22). */
void NoteFirstReason(PMString& firstReason, const std::wstring& leaf, const std::string& why)
{
	if (!firstReason.IsEmpty())
		return;
	firstReason.AppendW(reinterpret_cast<const UTF16TextChar*>(leaf.c_str()));
	firstReason.Append(": ");
	firstReason.Append(KCMTextWords::PMStringOfUtf8(why));
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
	int32 pageFiles = 0;		// page files read (2026-10-25) - their boxes are in out.fBoxes
	int32 outsideBoxes = 0;		// page files with words typed outside every box
	PMString outsideFile;		// the first of those, by name
	outsideFile.SetTranslatable(kFalse);
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
		// ★A PAGE FILE (2026-10-25, "Export Pages to Word..."): asked first, by its own tag - the story reader would
		//   take its text boxes for drawings and refuse the file. Its boxes become stories once the import holds its
		//   copy of the document (KCMSpliceBoxesIntoSet).
		{
			KCMPageDocx::ReadResult page;
			std::string pageWhy;
			if (!KCMPageDocx::ReadParts(parts, page, pageWhy))
			{
				++refused;
				NoteFirstReason(firstReason, leaf, pageWhy);
				continue;
			}
			if (page.fIsPage)
			{
				++pageFiles;
				for (size_t b = 0; b < page.fBoxes.size(); ++b)
				{
					const KCMPageDocx::ReadBox& rb = page.fBoxes[b];
					KCMPageBoxEdit edit;
					edit.fStory = UID(static_cast<uint32>(rb.fBox.fStory));
					edit.fFrame = UID(static_cast<uint32>(rb.fBox.fFrame));
					edit.fStart = rb.fBox.fStart;
					edit.fEnd = rb.fBox.fEnd;
					edit.fPrintBytes = rb.fBox.fPrintBytes;
					edit.fPrintCrc = rb.fBox.fPrintCrc;
					edit.fBoxN = rb.fBox.fN;
					edit.fContent = rb.fBox.fContent;
					edit.fFileName = PMStringOfLeaf(leaf);
					edit.fWhyNot = KCMTextWords::PMStringOfUtf8(rb.fWhyNot);
					edit.fWhyNot.SetTranslatable(kFalse);
					out.fBoxes.push_back(edit);
				}
				if (page.fTextOutsideBoxes)
				{
					++outsideBoxes;
					if (outsideFile.IsEmpty())
						outsideFile = PMStringOfLeaf(leaf);
				}
				continue;
			}
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
		kept.fBoxes = out.fBoxes;		// ⚠the page files' boxes are not stories yet, and go along whole (2026-10-25)
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
		}
		out = kept;
	}

	const int32 read = static_cast<int32>(out.fUids.size());
	AppendCount(whyNot, "", read, " story file(s) read");
	if (fromWord > 0)
	{
		AppendCount(whyNot, ", ", fromWord, " from Word");
	}
	if (pageFiles > 0)
	{
		AppendCount(whyNot, ", ", pageFiles, " page file(s)");
		AppendCount(whyNot, " (", static_cast<int32>(out.fBoxes.size()), " box(es))");
	}
	if (outsideBoxes > 0)
	{
		whyNot.Append(", words outside the boxes are not read (");
		whyNot.Append(outsideFile);
		if (outsideBoxes > 1)
			AppendCount(whyNot, " and ", outsideBoxes - 1, " more");
		whyNot.Append(")");
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

	return (read > 0 || !out.fBoxes.empty()) ? kTrue : kFalse;
}

// ★★THE POUR RESHAPES TABLES (2026-09-24 evening, the user's decision, which replaces the morning's design 11-1
//   item 5): a table whose rows, columns or merged cells changed in Word is made Word's, one stage per round
//   (S1/S2), the words then written into the cells it has. ★What the user accepted with it: the shape is not a
//   change of its own to take back one by one - Ctrl+Z of the step that put it in takes it back, shape and words
//   together (since 2026-10-25 that step is an Apply - the "Table" row's, or a story's or all of them; the import
//   itself reshapes only its copy). (kFalse held such a table whole, "!", from the morning of 2026-09-24 until the
//   evening; Apply's paragraph road still passes kFalse - it writes one paragraph and reshapes no table.)
static const bool16 kReshapeTablesOnImport = kTrue;
bool16 KCMPourStoryTextWith(IDataBase* db, const KCMStoryTextSet& set, const KCMPourOptions& options,
							PMString& outMessage, bool16& outCancelled)
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

	// ★★★ONE ABORTABLE STEP (2026-09-19). Poured into the document (an Apply), this IS the undo step Ctrl+Z takes
	//   back; poured into the import's copy, its history is discarded afterwards (KCMImportStoryTextAsSource). Either
	//   way a Cancel between two stories aborts it and leaves the database as it was.
	// ★UNSIGNED AND UNTRACKED (since 2026-10-24; the switches went on 2026-10-25): what a pour writes keeps no change
	//   history, and the reader's own user name is never touched for it. (Until then the import signed its changes
	//   "KohakuChangeMarker" under Track Changes, for "Reject This Import Change" to find - retired with that item.)
	IAbortableCmdSeq* sequence = CmdUtils::BeginAbortableCmdSeq("KCMPourStoryTextWith");
	if (sequence != nil)
		sequence->SetName(options.fStepName);

	// ★The import's bar when there is one (a slice of it), a bar of its own otherwise - the same
	//   shape as the reading loop. Stepped per story; asked for a cancel BETWEEN two stories only,
	//   because WasCancelled pumps events and a story half written is not a place to stop.
	// ★★THE STORIES ARE TAKEN DOWN FIRST, AND EACH IS ASKED FOR AGAIN BEFORE IT IS USED (2026-09-25, the Word round trip
	//   re-check, item 4). A shape round takes tables, rows and columns away "with whatever they hold" (design 10-1) - an
	//   anchored TEXT FRAME in a cell among it, whose story then leaves the story list mid-loop. Walked by index, as this
	//   was, every story after it moved up one: the next was passed over (its file reported as "no story with this ID")
	//   and the last index asked for was past the list's end, which IStoryList makes no promise about.
	const int32 count = stories->GetUserAccessibleStoryCount();
	std::vector<UIDRef> storyRefs;
	storyRefs.reserve(static_cast<size_t>(count > 0 ? count : 0));
	for (int32 s = 0; s < count; ++s)
		storyRefs.push_back(stories->GetNthUserAccessibleStoryUID(s));

	KCMProgressStepper progress(options.fBarTitle, count);

	for (int32 s = 0; s < count; ++s)
	{
		if (s > 0 && progress.WasCancelled())
		{
			outCancelled = kTrue;
			break;
		}
		{
			// The bar's title with its "..." taken off: "Putting the edited text into the document (3 / 9)".
			PMString step(options.fBarTitle);
			const int32 ellipsis = step.IndexOfString(PMString("..."));
			if (ellipsis > 0)
				step.Truncate(step.CharCount() - ellipsis);
			step.Append(" (");
			step.AppendNumber(s + 1);
			step.Append(" / ");
			step.AppendNumber(count);
			step.Append(")");
			step.SetTranslatable(kFalse);
			progress.Step(s, step);
		}

		const UIDRef storyRef = storyRefs[static_cast<size_t>(s)];

		// ★THE STORY'S OWN UID IS THE PAIRING (2026-09-19): the file is named after it, and the import's copy
		//   holds the document's own uids (a database clone), so the copy and the document pair alike.
		const UID original = storyRef.GetUID();

		// ⚠GONE SINCE THE LIST WAS TAKEN (see above): a story an earlier story's shape round took away with its table.
		//   Its file, if there was one, stays unmatched and is named below - which is now the truth.
		if (stories->GetUserAccessibleStoryIndex(original) < 0)
			continue;

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
		// ★ONLY THESE (2026-10-24): "Apply All Changes in This Story" pours one story of what the import read.
		if (!options.fOnly.empty() && std::find(options.fOnly.begin(), options.fOnly.end(), original) == options.fOnly.end())
			continue;

		matched[which] = kTrue;

		// (NO CHANGE TRACKING IS SWITCHED ON HERE - since 2026-10-24: a tracked deletion would stay in the text model
		//  for the comparison to read, and the copy has to hold what the file says. A story the reader tracks
		//  themselves still records what an Apply writes into it.)

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
			NoteRefusalIf(options.fNoteRefusals, original, "Story", why, kTrue);
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
					NoteRefusalIf(options.fNoteRefusals, original, shape.fNotes[k].fKind, shape.fNotes[k].fWhy);
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
					why.Append(KCMTextWords::PMStringOfUtf8(layoutWhy));
					why.Append(")");
				}
			}
			++storiesHeld;
			why.SetTranslatable(kFalse);
			if (firstRefusal.IsEmpty())
				firstRefusal = why;
			NoteRefusalIf(options.fNoteRefusals, original, "Story", why, kTrue);
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
			NoteRefusalIf(options.fNoteRefusals, original, "Story", why, kTrue);
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
				NoteHeldBackIf(options.fNoteRefusals, original, note.fKind, note.fWhy);
				continue;
			}
			NoteRefusalIf(options.fNoteRefusals, original, note.fKind, note.fWhy, note.fWholeStory);
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
				why.Append(KCMTextWords::PMStringOfUtf8(placeWhy));
				why.Append(") - check this story");
				why.SetTranslatable(kFalse);
				if (firstRefusal.IsEmpty())
					firstRefusal = why;
				NoteRefusalIf(options.fNoteRefusals, original, "Table", why, kTrue);
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
		if (sequence != nil)
			CmdUtils::AbortCommandSequence(sequence);
		// ⚠Only a pour that notes the "!" rows drops them: an Apply's cancel leaves the import's own rows standing.
		if (options.fNoteRefusals)
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
		// A file outside `fOnly` was not asked about (2026-10-24).
		if (!options.fOnly.empty() && std::find(options.fOnly.begin(), options.fOnly.end(), set.fUids[k]) == options.fOnly.end())
			continue;
		++unmatched;
		NoteRefusalIf(options.fNoteRefusals, set.fUids[k], "File", PMString("no story with this ID in the document"), kTrue,
					  (k < set.fFileNames.size()) ? set.fFileNames[k] : PMString());
	}

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	// (⛔WORD'S CONTENT KEPT PER STORY for Apply stood here - 2026-09-24 for the retired "Redo from Word", Apply's from
	//  2026-10-24 - and went on 2026-10-26, the import tidy S5: what Apply puts in is read from the import's copy, where
	//  this pour has just put it (KCMImportSourceStory). The two had been two copies of the files that could disagree -
	//  a part the pour held back was in the keep and not in the copy.)

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

//----------------------------------------------------------------------------------------
// ★★IMPORT STORY TEXT INTO THE SOURCE (2026-10-24, adopted 2026-10-25 - docs/superpowers/specs/2026-10-24-kcm-import-as-
//   source-design.md). The user: "right now the state before the change is the Source and the Target is what gets
//   changed - I want that the other way round". The words go into a Task Start in memory, and the Story mode then
//   shows - against the document, which nothing here touches - what they would change.
//----------------------------------------------------------------------------------------

namespace
{

/** Nothing of the copy's own making left in any undo history (the pour wrote into it): KCMPdfDoc.cpp's DiscardHistory,
	for the same reason - ICmdHistory::Discard, aimed at the copy's root, never at UIDRef::gNull (the application). */
void DiscardHistoryOf(IDataBase* db)
{
	if (db == nil)
		return;
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<ICommandProcessor> processor(session->QueryCommandProcessor());
	if (processor == nil)
		return;
	InterfacePtr<ICmdHistory> history(processor, UseDefaultIID());
	if (history != nil)
		history->Discard(UIDRef(db, db->GetRootUID()));
}

/** "Imported: 269.docx", "Imported: 269.docx (+2 more)" - the Source line before its time. ★A page file counts once,
	however many boxes it holds (2026-10-25). */
void ImportLabel(const KCMStoryTextSet& set, PMString& out)
{
	std::vector<PMString> names(set.fFileNames.begin(), set.fFileNames.end());
	for (size_t b = 0; b < set.fBoxes.size(); ++b)
	{
		if (std::find(names.begin(), names.end(), set.fBoxes[b].fFileName) == names.end())
			names.push_back(set.fBoxes[b].fFileName);
	}
	out = "Imported: ";
	if (!names.empty())
		out.Append(names[0]);
	if (names.size() > 1)
	{
		out.Append(" (+");
		out.AppendNumber(static_cast<int32>(names.size()) - 1);
		out.Append(" more)");
	}
	out.SetTranslatable(kFalse);
}

}	// anonymous namespace

bool16 KCMImportStoryTextAsSource(const SysFileList& files, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// ★NOT A PDF DOCUMENT - the import's own refusal, asked before a file is read.
	if (KCMIsPdfDoc(KCMTaskDocumentDB()))
	{
		outMessage = kKCMPdfTargetRefusal;
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ★ONE BAR FOR ALL OF IT, the import's: the reading, the pour into the copy and the comparison step slices of it.
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

	// 2. THE COPY: a Task Start in memory of the task document (the chosen Target, else the one in front - the one rule),
	//    chosen as the Source. It stops a running comparison first and lets the previous copy go (KCMTaskStartClone.cpp).
	KCMClearImportRefusals();
	PMString stateStep("Keeping the document's state in memory");
	stateStep.SetTranslatable(kFalse);
	progress.Step(kImportUnitsState, stateStep);
	PMString label;
	ImportLabel(set, label);
	PMString whyNot;
	if (!KCMTakeImportClone(label, whyNot))
	{
		outMessage = "import: ";
		outMessage.Append(whyNot);
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	IDataBase* const targetDB = KCMChosenTargetDB();
	IDataBase* const copyDB = KCMChosenSourceDB();
	if (targetDB == nil || copyDB == nil || !KCMIsImportClone(copyDB))
	{
		// The take said it succeeded, so this cannot normally happen; a nil here would otherwise reach the pour.
		KCMLetGoTaskStartClone(kTrue);
		outMessage = "import: the copy of the document could not be chosen as the Source";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	// 2b. ★THE PAGE FILES' BOXES BECOME STORIES (2026-10-25, "Export Pages to Word..." - KCMPageImport.h): each box put back
	//     into its story as the copy holds it, at the range it came from and only while that range is what was exported
	//     (its fingerprint); the whole story then goes on the way a story file's does. What cannot go in is a "!" row.
	int32 boxesIn = 0;
	int32 storiesFromBoxes = 0;
	if (!set.fBoxes.empty())
	{
		std::vector<KCMImportRefusal> boxRefusals;
		KCMSpliceBoxesIntoSet(copyDB, set, boxRefusals, boxesIn, storiesFromBoxes);
		sRefusals.insert(sRefusals.end(), boxRefusals.begin(), boxRefusals.end());
	}
	KCMSetImportCloneStories(set.fUids);

	// 3. ★★THE WORDS GO INTO THE COPY - unsigned and untracked (the copy keeps no change history), the "!" rows noted as
	//    the import notes them. ★What the copy then holds is what Apply puts in (KCMImportSourceStory reads it from here).
	PMString pourStep("Putting the edited text into the Source");
	pourStep.SetTranslatable(kFalse);
	progress.Step(kImportUnitsCopy, pourStep);
	outer.Slice(kImportUnitsCopy, kImportUnitsCompare);
	KCMPourOptions options;
	options.fNoteRefusals = kTrue;
	options.fStepName = "Import Story Text into the Source";
	options.fStepName.SetTranslatable(kFalse);
	options.fBarTitle = "Putting the edited text into the Source...";
	options.fBarTitle.SetTranslatable(kFalse);
	PMString poured;
	bool16 cancelledPour = kFalse;
	KCMPourStoryTextWith(copyDB, set, options, poured, cancelledPour);
	DiscardHistoryOf(copyDB);
	if (cancelledPour)
	{
		// ⚠THE COPY GOES: a copy the files only half went into is no Source to compare against.
		KCMLetGoTaskStartClone(kTrue);
		outMessage = kImportCancelledMessage;
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// 4. THE STORY MODE: the Source is the copy with the files' words, the Target the document as it stands.
	outer.Slice(kImportUnitsCompare, kImportUnitsAll);
	KCMSetCompareMode(kKCMModeStory);
	KCMToggleStartStop();

	// 5. WHAT THE LIST NOW PROPOSES - its live changes (the "!" rows are counted apart).
	int32 proposals = 0;
	int32 storiesWith = 0;
	for (int32 r = 0; r < KCMStoryList::GetRowCount(); ++r)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(r);
		if (row == nil || row->fChanges.empty())
			continue;
		proposals += static_cast<int32>(row->fChanges.size());
		++storiesWith;
	}
	int32 cannotGoIn = 0;
	int32 heldBack = 0;
	for (size_t i = 0; i < KCMImportRefusals().size(); ++i)
	{
		if (KCMImportRefusals()[i].fHeldBack)
			++heldBack;
		else
			++cannotGoIn;
	}

	outMessage = "import: ";
	outMessage.SetTranslatable(kFalse);
	outMessage.Append(readMessage);
	if (boxesIn > 0)
	{
		AppendCount(outMessage, ", ", boxesIn, " box(es) put back into ");
		AppendCount(outMessage, "", storiesFromBoxes, " story(ies)");
	}
	outMessage.Append("; ");
	if (!(KCMIsArmed() && KCMArmedTargetDB() != nil))
	{
		PMString startSaid;
		KCMGetSessionStatus(startSaid);
		outMessage.Append("the edited text is in the Source, but the comparison did not start");
		if (startSaid.CharCount() > 0)
		{
			outMessage.Append(" (");
			outMessage.Append(startSaid);
			outMessage.Append(")");
		}
		outMessage.Append(" - your document is unchanged");
		KCMNotify(kKCMMarksClearedMessage);
		return kTrue;
	}
	AppendCount(outMessage, "", proposals, " change(s) proposed");
	AppendCount(outMessage, " in ", storiesWith, " story(ies)");
	outMessage.Append(" - your document is unchanged; right-click a change: Apply This Change");
	if (cannotGoIn > 0)
		AppendCount(outMessage, " - ", cannotGoIn, " cannot go in (the rows marked !)");
	if (heldBack > 0)
		AppendCount(outMessage, ", ", heldBack, " thing(s) Word cannot carry kept as they are (the rows marked !)");

	// ★THE TARGET IS WHAT THE READER IS LEFT LOOKING AT - the import's rule; the copy has no window to come to the front.
	KCMNotify(kKCMTargetToFrontMessage);
	return kTrue;
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

//----------------------------------------------------------------------------------------
// KCMImportSourceStory (2026-10-26, the import tidy S5)
//----------------------------------------------------------------------------------------
bool16 KCMImportSourceStory(UID story, KCMStoryShape::Story& out, PMString& outWhyNot)
{
	outWhyNot.Clear();
	outWhyNot.SetTranslatable(kFalse);
	// ★THE COPY HAS TO BE THE SOURCE NOW: the rows Apply acts on are this comparison's, and a Source that is no longer the
	//   import's copy (stopped, another Source set, the copy let go) has no words of the files to give.
	IDataBase* const copyDB = KCMArmedSourceDB();
	if (copyDB == nil || !KCMIsImportClone(copyDB))
	{
		outWhyNot = "the imported text is no longer the Source - import again";
		return kFalse;
	}
	// The copy's story, read the way the export reads a story; a footnote whose reference cannot be placed would be lost
	// in Word's format, so such a story is not offered (the export refuses it the same way).
	KCMStoryShape::Story read;
	bool16 placed = kTrue;
	bool16 readable = kFalse;
	if (story != kInvalidUID)
	{
		IDataBase::SaveRestoreModifiedState guard(copyDB);	// reading must not dirty it (the copy has no window, but the rule is one)
		readable = (KCMStoryFromDocument(UIDRef(copyDB, story), read, placed) && placed) ? kTrue : kFalse;
	}
	if (!readable)
	{
		outWhyNot = "the imported text's story could not be read - import again";
		return kFalse;
	}
	// ★THROUGH WORD'S FORMAT AND BACK: Word's side is what Read makes of the parts (fAfter - a table splits a paragraph
	//   into pieces there, and KCMStorySync::Compare rejoins them), so the copy's story is put into exactly that shape -
	//   the reading a file exported from the copy would give. Pure functions on plain structs: nothing is written anywhere.
	std::vector<KCMZipStore::Entry> parts;
	std::string why;
	KCMStoryDocx::ReadResult back;
	if (!KCMStoryDocx::WriteParts(read, static_cast<int32>(story.Get()), parts, why) || !KCMStoryDocx::Read(parts, back, why))
	{
		outWhyNot = "the imported text's story does not go through Word's format: ";
		outWhyNot.Append(KCMTextWords::PMStringOfUtf8(why));
		outWhyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	out = back.fAfter;
	return kTrue;
}

// End, KCMStoryTextImport.cpp.
