//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryList.h for what this list is and why it is a file static.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IComposeScanner.h"
#include "IFrameList.h"
#include "IPageList.h"
#include "ITextModel.h"

// General includes:
#include "TextChar.h"			// kTextChar_Space - the boundary the readability test draws its line at
#include "UnicodeClass.h"		// IsWhiteSpace
#include "WideString.h"

#include <algorithm>			// std::sort
#include <vector>

// Project includes:
#include "KESCMCore.h"			// KESCMFramePageUID - shared with the overset scan since 2026-08-09
#include "KESCMStoryList.h"

namespace
{

// The list. See the header for why this is a file static rather than a boss.
std::vector<KESCMStoryRow> gRows;

// How many characters of the story to keep. SnpCreateCrossReference.cpp:179 uses 30 for the same
// job - naming a story in a list - and the panel is 224px wide, so nothing longer could be read.
//
// No ellipsis is appended when the text is cut here. The row's cell ellipsizes in the middle by
// itself, and a string that already ends in "..." would be shortened again around it.
const int32 kMaxRowTextChars = 30;

/* IsReadable
	Is this character worth putting in a row? Only characters a reader would recognise as the story's
	opening words qualify.
*/
bool16 IsReadable(const UTF32TextChar& ch)
{
	const uint32 v = ch.GetValue();

	// Everything below the space character, in one test. That single boundary covers the paragraph
	// and line breaks, the tab, the table anchors (0x0016/0x0017) and the page-number and
	// section-name placeholders (0x0018/0x0019): InDesign packs all of its own special characters
	// into the control range, which is exactly why TextChar.h:558-559 draws its line at
	// kTextChar_Space too.
	if (v < kTextChar_Space)
		return kFalse;

	if (v == kTextChar_ObjectReplacementCharacter)
		return kFalse;			// an inline graphic is not text

	// Spaces of every width, the ideographic one included - a paragraph of them says nothing.
	if (UnicodeClass::IsWhiteSpace(ch))
		return kFalse;

	return kTrue;
}

/* FirstReadableText
	The opening words of a story, or an empty string when it has none to show.

	The scan runs paragraph by paragraph and stops at the first paragraph holding anything readable.
	It runs to TotalLength() rather than to the end of the primary thread on purpose: table cells and
	footnotes are further threads inside the SAME model (ITextModel.h:140,145), so a frame holding
	nothing but a table only says something once the scan is allowed past the main thread.

	★Where this parts company with the official example. SnpCreateCrossReference.cpp:206-222 does
	the same job and copies span-1 characters so that the paragraph's CR is left behind. Here the
	whole span is copied and unreadable characters are filtered out instead, because this list has a
	requirement the snippet does not: a paragraph made of nothing but control characters has to be
	SKIPPED so the scan moves on to the next one, which cannot be decided without looking at the
	characters anyway. Filtering also steps around the snippet's edge case, where span-1 eats a real
	character in the last paragraph of a story that ends without a CR.
*/
PMString FirstReadableText(ITextModel* model)
{
	PMString out;
	out.SetTranslatable(kFalse);

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return out;

	const TextIndex total = model->TotalLength();
	TextIndex pos = 0;

	while (pos < total)
	{
		int32 span = 0;

		// excludeEOS = kFalse so that a story ending without a CR still reports its last paragraph,
		// which is the only paragraph a short story has.
		const TextIndex paraStart = scanner->FindSurroundingParagraph(pos, &span, kFalse);
		if (paraStart < 0 || span <= 0)
			break;

		WideString para;
		scanner->CopyText(paraStart, span, &para);

		WideString kept;
		bool16 pendingGap = kFalse;
		const int32 charCount = para.CharCount();

		for (int32 i = 0; i < charCount && kept.CharCount() < kMaxRowTextChars; ++i)
		{
			const UTF32TextChar ch = para.GetChar(i);

			if (!IsReadable(ch))
			{
				// An unreadable character becomes at most one space, and only between readable ones:
				// leading runs are dropped because nothing has been kept yet, and trailing runs
				// because the gap is never spent. A tab between two columns of text reads as a gap,
				// which is what it looks like on the page.
				pendingGap = !kept.empty();
				continue;
			}

			if (pendingGap)
			{
				kept.Append(UTF32TextChar(kTextChar_Space));
				pendingGap = kFalse;
			}
			kept.Append(ch);
		}

		if (!kept.empty())
		{
			out = PMString(kept);
			out.SetTranslatable(kFalse);
			return out;
		}

		// Advance past this paragraph. The guard is not ceremony: FindSurroundingParagraph answers
		// with the start of the paragraph CONTAINING pos, which can lie before it, so a span that
		// does not reach past pos would ask the same question forever.
		const TextIndex next = paraStart + span;
		if (next <= pos)
			break;
		pos = next;
	}

	return out;
}

/* RowIsBefore
	Page order, with the pageless rows last (their index is kMaxInt32).
*/
bool RowIsBefore(const KESCMStoryRow& a, const KESCMStoryRow& b)
{
	if (a.fPageIndex != b.fPageIndex)
		return a.fPageIndex < b.fPageIndex;

	// Same page, or both without one. Broken by UID so that comparing the same two documents twice
	// lists the rows in the same order: std::sort is not stable, so ties left to it could swap
	// places between two runs that found exactly the same edits.
	return a.fStoryUID < b.fStoryUID;
}

}	// anonymous namespace

/* Build
*/
void KESCMStoryList::Build(IDataBase* db, const std::vector<KESCMStoryDiff>& diffs)
{
	gRows.clear();
	if (db == nil)
		return;

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());

	for (std::vector<KESCMStoryDiff>::const_iterator it = diffs.begin(); it != diffs.end(); ++it)
	{
		InterfacePtr<ITextModel> model(db, it->fStoryUID, UseDefaultIID());
		if (model == nil)
			continue;	// a story that cannot be read cannot be shown, or jumped to later

		KESCMStoryRow row;
		row.fStoryUID = it->fStoryUID;
		row.fKinds = it->fKinds;
		row.fText = FirstReadableText(model);

		// ★Document text is not a string key. Left translatable, a PMString that happens to match an
		//   entry in the built-in table comes back as something else entirely - KESCM has already
		//   had "Source:" turn into a Japanese style-source label that way.
		row.fText.SetTranslatable(kFalse);

		// Where the story starts, which is the frame a jump would want. RecomposeThruLastFrame is
		// deliberately NOT called: this asks for the FIRST frame, not for where the text overflows,
		// so there is no reason to compose - and composing here would cost the property stage 1
		// measured, that reading what changed changes nothing (KESCMStoryStamp.h:42-43).
		InterfacePtr<IFrameList> frameList(model->QueryFrameList());
		if (frameList != nil && frameList->GetFrameCount() > 0)
			row.fPageUID = KESCMFramePageUID(db, frameList->GetNthFrameUID(0));

		if (row.fPageUID != kInvalidUID && pageList != nil)
		{
			// Master pages answer with a negative index (IPageList.h:96-104 counts pages within the
			// pub, and a master is not one of them). Those keep kMaxInt32 and sort to the end rather
			// than to the front, where a negative index would have put them.
			const int32 idx = pageList->GetPageIndex(row.fPageUID);
			if (idx >= 0)
				row.fPageIndex = idx;
		}

		gRows.push_back(row);
	}

	std::sort(gRows.begin(), gRows.end(), RowIsBefore);
}

/* Clear
*/
void KESCMStoryList::Clear()
{
	gRows.clear();
}

/* GetRowCount
*/
int32 KESCMStoryList::GetRowCount()
{
	return static_cast<int32>(gRows.size());
}

/* GetRow
*/
const KESCMStoryRow* KESCMStoryList::GetRow(int32 nth)
{
	if (nth < 0 || nth >= static_cast<int32>(gRows.size()))
		return nil;

	return &gRows[nth];
}

/* ShutdownCleanup
*/
void KESCMStoryList::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do. clear() would leave the rows' PMStrings
	// holding their buffers - the very thing this call exists to prevent (KBSResultModel.cpp:363-370).
	gRows = std::vector<KESCMStoryRow>();
}


// End, KESCMStoryList.cpp.
