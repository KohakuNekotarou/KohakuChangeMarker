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
#include "IGeometry.h"			// the frame's inner->pasteboard matrix, for where a story begins
#include "IPageList.h"
#include "IParcelList.h"		// GetFirstParcelKey / GetNextParcelKey / GetParcelToFrameMatrix
#include "ITextModel.h"
#include "ITextParcelList.h"	// QueryTextParcelList - the parcels a story flows through

// General includes:
#include "PMMatrix.h"
#include "PMRect.h"				// GetParcelBounds - the leading corner comes off this
#include "TextChar.h"			// kTextChar_Space - the boundary the readability test draws its line at
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
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

// A safety valve, NOT a display limit. How much of a story's opening text is shown is decided by
// the row's text cell alone: it is kEllipsizeMiddle and bound kBindLeft|kBindRight (KESCM.fr), so
// it is the one cell that grows when the panel is widened, and it shortens its own text to fit.
//
// ★This panel already answered this exact question once. KESCMDocNameFromDB used to shorten the
//   Target / Source document names by character count in C++; the block 8 A-2 audit (2026-08-06)
//   deleted that and left the shortening to kEllipsizeMiddle, so the question is asked in one
//   place only (memory one-question-one-place). A character cap here was the same duplicate, and
//   it went unnoticed until the panel became resizable on 2026-08-10: from then on 30 characters
//   silently capped every width, so widening the panel bought empty space rather than more text.
//   The old cap took its number from SnpCreateCrossReference.cpp:179, which names stories in a
//   fixed-width dialog - a list that cannot be resized has no such question to answer.
//
// The number below only has to sit past anything a cell could ever show. A palette-font character
// is never narrower than about 3px, so even a text cell spanning a 4K screen (~3700px) runs out
// before ~1300 characters. 2000 leaves room and still bounds a pathological single-paragraph
// story, which is the only thing this limit exists to stop.
//
// No ellipsis is appended when the text is cut here: the cell adds its own, and a string already
// ending in "..." would be shortened again around it.
const int32 kRowTextSafetyLimit = 2000;

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

		for (int32 i = 0; i < charCount && kept.CharCount() < kRowTextSafetyLimit; ++i)
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

/* KESCMStoryFirstFrameUID (declared in KESCMStoryList.h)

	★RecomposeThruLastFrame is deliberately NOT called. This asks where the story STARTS, not where
	its text overflows, so there is nothing to compose - and composing here would cost the property
	stage 1 measured and wrote down: reading what changed changes nothing (KESCMStoryStamp.h:42-43).
*/
UID KESCMStoryFirstFrameUID(IDataBase* db, UID storyUID)
{
	if (db == nil || storyUID == kInvalidUID)
		return kInvalidUID;

	// Quietly nil for a UID this document does not hold a story at - which is the ordinary answer
	// when the SOURCE is asked about a story that only the target has (an "Added" row).
	InterfacePtr<ITextModel> model(db, storyUID, UseDefaultIID());
	if (model == nil)
		return kInvalidUID;

	InterfacePtr<IFrameList> frameList(model->QueryFrameList());
	if (frameList == nil || frameList->GetFrameCount() == 0)
		return kInvalidUID;	// a real story, placed in no frame: there is nowhere to scroll to

	return frameList->GetNthFrameUID(0);
}

/* KESCMStoryStartPoint (declared in KESCMStoryList.h)

	The mirror image of the overset scan's KESCMLastPlacedOutport: that one walks BACK from the last
	parcel to find where the text stopped fitting; this walks FORWARD from the first to find where it
	started. Same three coordinate spaces, same reason vertical text needs no branch.
*/
bool16 KESCMStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
{
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;

	InterfacePtr<ITextModel> textModel(db, storyUID, UseDefaultIID());
	if (textModel == nil)
		return kFalse;

	// Index 0 is the start of the story, so this is the thread the beginning is in.
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(0));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetFirstParcelKey(); k.IsValid(); k = pl->GetNextParcelKey(k))
	{
		const UID frameUID = pl->GetParcelFrameUID(k);
		if (frameUID == kInvalidUID)
			continue;	// this piece is not placed; keep going forward for one that is

		InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
		if (frameGeo == nil)
			continue;

		const PMRect   parcelBounds = pl->GetParcelBounds(k);				// parcel-local
		const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(k);			// parcel -> frame inner
		const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner -> pasteboard

		PMPoint corner(parcelBounds.Left(), parcelBounds.Top());	// the inport corner, parcel-local
		toFrame.Transform(&corner);
		toPasteboard.Transform(&corner);

		outFrame = frameUID;
		outPb    = PBPMPoint(corner.X(), corner.Y());
		return kTrue;
	}
	return kFalse;
}

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

		// ★The frame is kept as well as the page it sits on, because the two answer different
		//   questions: the frame is WHERE TO SCROLL (a click centres it), and the page is WHERE IT
		//   BELONGS (the sort order, and the page the status line names).
		//   ⚠The page is NOT how the older version's window gets aimed - that goes by story UID,
		//   because the same story can sit somewhere else entirely over there (2026-08-10; see
		//   KESCMGotoStoryFrame). Why reading this composes nothing: KESCMStoryFirstFrameUID.
		row.fFrameUID = KESCMStoryFirstFrameUID(db, row.fStoryUID);
		if (row.fFrameUID != kInvalidUID)
			row.fPageUID = KESCMFramePageUID(db, row.fFrameUID);

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
