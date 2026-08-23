//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryMarker.h for what this is and why it is a global text adornment rather than a
//  draw event.
//
//  Two things live here: the handful of statics that say WHERE the mark is, and the adornment that
//  draws it. They are together because they are one question - "is anything marked, and where" -
//  and splitting them would put the answer in two places.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGlobalTextAdornment.h"
#include "IGraphicsContext.h"		// GraphicsData
#include "IDocument.h"				// the document InvalidateViews is asked about
#include "IGraphicsPort.h"
#include "ILayoutUtils.h"			// InvalidateViews - the repaint that puts the mark up / takes it down
#include "IShape.h"					// kPrinting / kPreviewMode
#include "ITextModel.h"				// which story a wax run belongs to
#include "IWaxGlyphs.h"
#include "IWaxLine.h"
#include "IWaxRenderData.h"
#include "IWaxRun.h"

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include <set>			// the documents to repaint when a mark moves between them
#include "UIDRef.h"
#include "Utils.h"

// Project includes:
#include "IKESCMCompareFacade.h"	// IsDocDBOpen - never repaint a document that has gone
#include "KCMUIID.h"
#include "KESCMStoryMarker.h"
#include "KESCMStoryMarkerExpiry.h"

namespace
{

// ---- where the mark is ------------------------------------------------------------------
//
// ★STATIC, LIKE EVERY OTHER PIECE OF THIS PLUG-IN'S TRANSIENT STATE. The mark belongs to no
//   document (it is not saved anywhere), to no panel (it shows with the panel closed) and to no
//   boss (the adornment is created by the service registry, not by us).
// ⚠These are touched from the main thread only: the jump runs there, the expiry timer is an idle
//   task there, and a global text adornment is drawn there. Unlike the comparison marks, this one
//   is deliberately NOT drawn for printing or export (see GetIsActive), which is what keeps the
//   background threads out of it entirely.
// ★★WHAT IS ASKED FOR, AND WHAT IS DRAWN, ARE DIFFERENT THINGS (2026-08-23). The two kinds of
//   caller each own one set and never touch the other's; gMarkDocs is what comes out of putting
//   them together under the per-document rule (KESCMStoryMarkDocs.h), and it is the only one the
//   drawing side ever looks at.
//   ⇒ Composing on the way IN rather than on the way out keeps the hot path exactly as it was: the
//     adornment is asked about every run on every page being drawn, and it still reads one map.
KESCMStoryMarkDocs gStandingDocs;			// the "Show Marks on ..." toggles and a held button
KESCMStoryMarkDocs gFlashDocs;				// what the newest jump asked to point at

bool16             gHasMark   = kFalse;		// the fast path: !gMarkDocs.empty(), kept as a flag
KESCMStoryMarkDocs gMarkDocs;				// database -> story UID -> its ranges, each list merged.
											// ⚠The database is an ADDRESS, only ever compared against
											// the run's own - never dereferenced
PMReal             gMarkOpacity(1.0);		// what both kinds are drawn at - the panel's radio
bool16             gShutdown  = kFalse;

// The whole marked span across every story, which is a free way to refuse most runs before
// asking any of them which story they belong to (that question costs a Query). Meaningless
// when gHasMark is kFalse.
TextIndex  gMarkLowest  = 0;
TextIndex  gMarkHighest = 0;

// How far above and below the baseline the mark reaches, as a fraction of the type size. The same
// split the product's spelling adornment uses for its own ink bounds
// (DynamicSpellCheckAdornment.cpp:1080-1081) - roughly ascent and descent.
const double kAscentFraction  = 0.85;
const double kDescentFraction = 0.10;

/* KESCMStoryMarkerRepaint
   Put the mark on screen, or take it off, now. Nothing else asks the views to redraw: a global
   adornment is only consulted while text is being drawn, so a mark that nobody repaints for would
   appear the next time the user happened to scroll.

   ⚠Asked of the compare facade first, because this runs while a document is being closed as well:
   InvalidateViews on a database that is going away is exactly the crash KBS's marker guards
   against with its own shutdown flag.
*/
void KESCMStoryMarkerRepaint(IDataBase* db)
{
	if (db == nil || gShutdown)
		return;
	if (!Utils<IKESCMCompareFacade>()->IsDocDBOpen(db))
		return;

	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(doc);
}

/* KESCMStoryMarkerFindRunRanges
   Which parts of this wax run are marked, as offsets into the run.

   ★THREE QUESTIONS, CHEAPEST FIRST, AND THE ORDER IS THE WHOLE PERFORMANCE STORY. A press marks
   every edit in the document, so this is asked of every run on every page being drawn:
     1. does the run fall within the marked span at all - two integer comparisons
     2. which story does it belong to - a Query, and the reason 1 exists
     3. which of that story's ranges it overlaps - a binary search (KESCMStoryMarkRanges.h)

   ⚠STEP 2 CANNOT BE SKIPPED even when only one story is marked. A document can be open twice over
   (target and source) and both are being drawn in their own windows, so "the right characters" is
   never enough - it has to be the right story in the right database.
*/
bool16 KESCMStoryMarkerFindRunRanges(const IWaxRun* waxRun, KESCMMarkRangeList& outRanges)
{
	outRanges.clear();

	if (!gHasMark || waxRun == nil)
		return kFalse;

	const TextIndex runStart = waxRun->TextOrigin();
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	const TextIndex runEnd = runStart + runCount;
	if (runEnd <= gMarkLowest || runStart >= gMarkHighest)
		return kFalse;						// before or after everything that is marked

	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return kFalse;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return kFalse;
	const UIDRef modelRef = ::GetUIDRef(model);
	KESCMStoryMarkDocs::const_iterator doc = gMarkDocs.find(modelRef.GetDataBase());
	if (doc == gMarkDocs.end())
		return kFalse;

	KESCMStoryMarkMap::const_iterator story = doc->second.find(modelRef.GetUID());
	if (story == doc->second.end())
		return kFalse;

	KESCMIntersectMarkRanges(story->second, runStart, runEnd, outRanges);
	return outRanges.empty() ? kFalse : kTrue;
}

/* KESCMStoryMarkerRunIsMarked
   The same question with no list built - what GetCouldDraw and GetIsActive want.
*/
bool16 KESCMStoryMarkerRunIsMarked(const IWaxRun* waxRun)
{
	if (!gHasMark || waxRun == nil)
		return kFalse;

	const TextIndex runStart = waxRun->TextOrigin();
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	const TextIndex runEnd = runStart + runCount;
	if (runEnd <= gMarkLowest || runStart >= gMarkHighest)
		return kFalse;

	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return kFalse;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return kFalse;
	const UIDRef modelRef = ::GetUIDRef(model);
	KESCMStoryMarkDocs::const_iterator doc = gMarkDocs.find(modelRef.GetDataBase());
	if (doc == gMarkDocs.end())
		return kFalse;

	KESCMStoryMarkMap::const_iterator story = doc->second.find(modelRef.GetUID());
	if (story == doc->second.end())
		return kFalse;

	return KESCMMarkRangesTouchRun(story->second, runStart, runEnd);
}

/* KESCMStoryMarkerSetDocs
   Install a set of ranges as THE mark, replacing whatever was there. Merging happens here so that
   no caller can hand in overlaps (which would invert twice and leave a hole), and the span the
   fast path tests is worked out in the same pass.

   ⚠What arrives is the COMPOSED set, not what a caller asked for - see the statics above.
*/
void KESCMStoryMarkerSetDocs(const KESCMStoryMarkDocs& docs, const PMReal& opacity)
{
	gMarkDocs.clear();
	gHasMark = kFalse;
	gMarkOpacity = opacity;
	gMarkLowest = 0;
	gMarkHighest = 0;

	bool16 first = kTrue;
	for (KESCMStoryMarkDocs::const_iterator doc = docs.begin(); doc != docs.end(); ++doc)
	{
		if (doc->first == nil)
			continue;

		KESCMStoryMarkMap kept;

		for (KESCMStoryMarkMap::const_iterator it = doc->second.begin(); it != doc->second.end(); ++it)
		{
			if (it->first == kInvalidUID)
				continue;

			KESCMMarkRangeList ranges = it->second;
			KESCMMergeMarkRanges(ranges);
			if (ranges.empty())
				continue;				// a story whose ranges were all empty is not a story to keep

			if (first || ranges.front().fFrom < gMarkLowest)
				gMarkLowest = ranges.front().fFrom;
			if (first || ranges.back().fTo > gMarkHighest)
				gMarkHighest = ranges.back().fTo;
			first = kFalse;

			kept[it->first].swap(ranges);
		}

		if (!kept.empty())
			gMarkDocs[doc->first].swap(kept);
	}

	if (!gMarkDocs.empty())
		gHasMark = kTrue;
}

/* MarkOpacityNow
   What the marks are drawn at, as the panel's 25% / 75% radio has it.

   ★★ONE PLACE ASKS, AND IT IS THIS ONE (2026-08-22). Until now the jump's pointer passed a
   hard-coded 1.0 while the standing marks were handed the selected value by their caller - so the
   same setting reached one kind of mark and not the other, and a reader who chose 25% still got a
   solid flash on every jump (user's report: "透明度の選択が反映されるようにしてほしい。今は不透明
   かな？"). Asking here rather than at each caller is what stops the two from drifting again
   ([[one-question-one-place]]).

   ⚠IT IS READ WHEN A MARK IS INSTALLED, NOT WHEN ONE IS DRAWN, and the comment here said the
   opposite until 2026-08-23 (measured: the value goes into gMarkOpacity and the drawing reads that
   static). Nothing is wrong with it - moving the radio makes the panel refresh the standing marks,
   which comes straight back through here - but a reader who believed the old sentence would look
   for a bug that is not there, or write one relying on a re-read that does not happen.
*/
PMReal MarkOpacityNow()
{
	InterfacePtr<IKESCMCompareFacade> compare(Utils<IKESCMCompareFacade>().QueryUtilInterface());
	return (compare != nil) ? compare->GetSelectedMarkOpacity() : PMReal(1.0);
}

/* KESCMStoryMarkerInstall
   Work out what is on screen from the two sets, put it up, and repaint everything involved.
   Every public call ends here, so there is one place where the rule is applied and one place that
   repaints.

   ⚠EVERY DOCUMENT THAT WAS MARKED IS REPAINTED TOO, not just the ones that still are, or a mark
   would stay on screen in a window nobody is looking at any more. That is not hypothetical here:
   turning "Show Marks on Source" off leaves the target's marks up and has to wipe the source's.

   ⚠THE CLOCK IS NOT TOUCHED HERE, AND THAT IS DELIBERATE (2026-08-23). It was a parameter of this
   function until the two sets were separated, which meant every standing mark going up or coming
   down had an opinion about the jump's clock. Both answers are wrong now that the two can be on
   screen together in different windows: stopping it would leave a pointer up for good, and starting
   it would hand the flash a fresh second every time a toggle moved. ⇒ The countdown belongs to the
   two calls that own the flash (ShowFlash / ClearFlash) and to nothing else.
*/
void KESCMStoryMarkerInstall()
{
	std::set<IDataBase*> toRepaint;
	for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	KESCMStoryMarkDocs composed;
	KESCMComposeMarkDocs(gStandingDocs, gFlashDocs, composed);
	KESCMStoryMarkerSetDocs(composed, MarkOpacityNow());

	for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
		KESCMStoryMarkerRepaint(*db);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

/** Inverts the pixels over the marked characters. Screen only. */
class KESCMStoryMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KESCMStoryMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}
	~KESCMStoryMarkerAdornment() {}

	/** FOREGROUND, unlike a highlight. The mark inverts what is under it, and what the reader has
		to keep seeing is the glyphs - so it has to be applied AFTER they are painted. (A slab of
		colour would go in the background pass instead, which is where the product puts its H&J and
		missing-glyph highlights, and where this would go if the inversion turns out not to work -
		see the note in KESCMStoryMarker.h.) The named constants for Adobe's own global adornments
		are in IGlobalTextAdornment.h:170-181. */
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriForeground + 0.50); }

	virtual bool16 GetCheckIsActive() { return kTrue; }
	virtual bool16 GetIsActive(const IParcelShape* parcelShape,
							   const ITextOptions* textOptions,
							   int32 iShapeFlags);

	/** ★kTrue, WHERE KT's EXPERIMENT AND spellpanel BOTH ANSWER kFalse: they draw on every run, and
		this draws on a handful of characters in one story. Answering the per-run question is what
		stops the text engine calling Draw for every run in the document while a mark is up - the
		header's own example is the H&J adornment declining runs whose line holds no violation. */
	virtual bool16 GetCheckCouldDraw() { return kTrue; }
	virtual bool16 GetCouldDraw(const IWaxRun* waxRun, const IWaxRenderData*, const IWaxGlyphs*)
	{
		return KESCMStoryMarkerRunIsMarked(waxRun);
	}

	/** The mark is taller than the glyphs' own ink (it reaches from ascent to descent), so the
		bounds have to be declared or it would be clipped to them. */
	virtual bool16 GetHasInkBounds() { return kTrue; }
	virtual void GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
							  const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs);

	virtual void Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
					  const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs);

	virtual void StartOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
	virtual void EndOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}

private:
	/** The rectangles to invert, in the coordinates the run reports its own position in - one per
		marked range that falls in this run.
		kFalse for a run that cannot be measured - an inline graphic has neither glyphs nor render
		data, which all four of these methods are warned about in IGlobalTextAdornment.h. */
	static bool16 GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
							   const IWaxGlyphs* waxGlyphs, std::vector<PMRect>& outBoxes);
};

CREATE_PMINTERFACE(KESCMStoryMarkerAdornment, kKESCMStoryMarkerAdornmentImpl)

bool16 KESCMStoryMarkerAdornment::GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
											   const IWaxGlyphs* waxGlyphs, std::vector<PMRect>& outBoxes)
{
	outBoxes.clear();

	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return kFalse;

	KESCMMarkRangeList runRanges;
	if (!KESCMStoryMarkerFindRunRanges(waxRun, runRanges))
		return kFalse;

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphCount <= 0)
		return kFalse;

	// ★THE GLYPH WIDTHS ARE ADDED UP ONCE, not once per range. A press can mark several separate
	//   edits inside a single wax run, and walking the glyphs again for each of them would make
	//   the cost of a run grow with the number of edits in it.
	std::vector<PMReal> cumulative(glyphCount + 1, PMReal(0.0));
	for (int32 i = 0; i < glyphCount; ++i)
		cumulative[i + 1] = cumulative[i] + waxGlyphs->GetWidthAt(i);

	const PMReal y = waxRun->GetYPosition();				// the baseline
	const PMReal size = renderData->GetFontMatrix().GetYScale();

	for (KESCMMarkRangeList::const_iterator r = runRanges.begin(); r != runRanges.end(); ++r)
	{
		const int32 charStart = static_cast<int32>(r->fFrom);
		const int32 charCount = static_cast<int32>(r->fTo - r->fFrom);

		// ★CHARACTERS ARE NOT GLYPHS. One character can be drawn by several glyphs and several
		//   characters by one, so the range has to be mapped before any width is added up - the same
		//   call the product's spelling squiggle makes before underlining a word
		//   (DynamicSpellCheckAdornment.cpp:793).
		int32 glyphIndex = -1;
		int32 glyphLength = 0;
		waxGlyphs->MapCharsToGlyphs(charStart, charCount, &glyphIndex, &glyphLength);
		if (glyphIndex < 0 || glyphLength <= 0)
			continue;						// ⚠a range that maps to nothing skips this box, not the run
		if (glyphIndex >= glyphCount)
			continue;
		if (glyphIndex + glyphLength > glyphCount)
			glyphLength = glyphCount - glyphIndex;

		// Where the range starts and how wide it is, read out of the running total above.
		// ⚠Added up rather than taken from GetGlyphDrawPosition because a draw position is a
		//   MATRIX - it carries the glyph's own transform, and reading a translation out of it is
		//   only the origin of that one glyph, not the end of the range.
		const PMReal offset = cumulative[glyphIndex];
		PMReal width = cumulative[glyphIndex + glyphLength] - offset;

		if (r->fCaret)
		{
			// ★★★A DELETION IS A CARET, NOT AN INVERTED CHARACTER (2026-08-22, user's call:
			//   "細いバーにするがいいです、キャレットの位置で"). The characters are gone from this
			//   side, so there is nothing here that IS the edit - and inverting the character that
			//   closed up over the gap says the wrong thing about it: deleting a whole paragraph lit
			//   the first character of the NEXT one, and deleting the end of a story lit the final
			//   carriage return, which draws nothing at all.
			//   ⇒ The range still covers one character so that it sorts and merges like any other
			//     (KESCMStoryMarkRanges.h), but what is DRAWN is a bar standing where the caret would
			//     stand if you clicked in front of that character - the same place the jump centres.
			width = size * PMReal(0.15);
		}
		else if (width <= 0.0)
		{
			// ★A ZERO-WIDTH RANGE STILL HAS A PLACE. It happens where the marked characters are
			//   drawn by nothing at all - and the reader still asked "where is it". A thin bar at
			//   the start of the range answers that; an empty rectangle would answer nothing.
			width = size * PMReal(0.15);
		}

		const PMReal x = waxRun->GetXPosition() + offset;

		PMRect box;
		box.Left(x);
		box.Right(x + width);
		box.Top(y - size * PMReal(kAscentFraction));
		box.Bottom(y + size * PMReal(kDescentFraction));
		outBoxes.push_back(box);
	}

	return outBoxes.empty() ? kFalse : kTrue;
}

bool16 KESCMStoryMarkerAdornment::GetIsActive(const IParcelShape* /*parcelShape*/,
											  const ITextOptions* /*textOptions*/,
											  int32 iShapeFlags)
{
	if (!gHasMark)
		return kFalse;

	// ★★NEVER ON PAPER. This is a pointer at something the reader just asked to see, not part of
	//   the document - and the header is explicit that an adornment which does not draw when
	//   printing must not draw for print preview either (IGlobalTextAdornment.h:78-83), so both
	//   flags are answered together.
	//   ⚠This is the OPPOSITE of the comparison marks, which exist to come out on paper - see
	//     KESCMDrawEventHandler. Two marks, two answers, for two different questions.
	if (iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode))
		return kFalse;

	return kTrue;
}

void KESCMStoryMarkerAdornment::GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
											 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, boxes))
		return;								// leave them empty, as the header instructs

	// ★THE UNION OF THEM ALL, because ink bounds are declared once for the whole run. A press can
	//   mark two separate edits inside one run, and a bound that covered only the first would clip
	//   the second away.
	PMRect all = boxes.front();
	for (size_t i = 1; i < boxes.size(); ++i)
		all.Union(boxes[i]);

	*inkBounds = all;
}

void KESCMStoryMarkerAdornment::Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
									 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	if (gd == nil || !gHasMark)
		return;

	// GetIsActive answered this for the parcel, but Draw is reached by paths that do not consult
	// it, so the flags are tested again here (KT measured the same and kept the same guard).
	if (iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode))
		return;

	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, boxes))
		return;

	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	// ★★INVERT, THE WAY KBS DOES IT. Difference blending against white flips whatever is
	//   underneath, so the mark is visible on any background - white paper, a coloured box, an
	//   image - and the glyphs stay readable because they are inverted along with the paper rather
	//   than covered by a slab (KBSDrawEventHandler.cpp:533-546, which also records that a plain
	//   red rectangle and IRasterPort::SetXORMode were both tried first and rejected).
	gPort->setblendingmode(kPMBlendDifference);

	// ★HOW MUCH OF THE INVERSION TO APPLY - the panel's "Marks opacity 25% / 75%", which the press
	//   reads once and hands over (KESCMStoryPressMarks). The jump's own mark passes 1.0, so it
	//   still lands at full strength.
	// ★★★AND IT WORKS ON SCREEN, WHERE THE EXPORT PATH DOES NOT (measured 2026-08-22). setopacity
	//   is silently ignored by a global text adornment when the drawing is going to PDF - KT asked
	//   three different ways on 2026-08-19 and all three came out pixel-identical - and that was
	//   the only measurement anyone had. It was of the EXPORT path. This mark is screen-only by
	//   GetIsActive, and switching the panel between 25% and 75% visibly changes how strong the
	//   inversion is (confirmed on the running application).
	//   ⇒ The line is not "opacity does nothing on a text adornment" but "it does not survive being
	//     written out". ★The fallback written for the other outcome is recorded here rather than
	//     deleted, because the same question returns the moment anything asks for this mark on
	//     paper: with Difference, painting (a,a,a) lands on 1-a over white and a over black, which
	//     is where an alpha of a would have put it.
	if (gMarkOpacity < PMReal(1.0))
		gPort->setopacity(gMarkOpacity, kFalse);

	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));

	// ★ONE FILL PER RANGE, AND THEY CANNOT OVERLAP - the ranges were merged before they were ever
	//   installed (KESCMStoryMarkRanges.h). Two Difference fills over the same pixels would invert
	//   them twice and leave a hole exactly where both said "look here".
	for (std::vector<PMRect>::const_iterator b = boxes.begin(); b != boxes.end(); ++b)
		gPort->rectfill(b->Left(), b->Top(), b->Width(), b->Height());

	gPort->newpath();
}

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

void KESCMStoryMarker::AddFlashRange(KESCMStoryMarkDocs& docs, IDataBase* db, UID storyUID,
									TextIndex from, TextIndex to)
{
	if (db == nil || storyUID == kInvalidUID)
		return;			// a window that is not open, or a story there is none of - nothing to add

	if (to < from)
		to = from;

	// ★★A DELETION HAS NO WIDTH on the side it was deleted from - the words are gone from there, and
	//   what the jump is pointing at is the PLACE they used to be. Since 2026-08-22 that place is
	//   shown as a CARET (user's call), which is also what the standing marks do, so a jump and a
	//   press say the same thing about the same deletion (KESCMStoryPressMarks).
	//   ⚠It used to be widened to one character here, which inverted whatever had closed up over the
	//     gap - a different character claiming to be the edit.
	//   ★The decision is made HERE and not inside the range list: what a zero-width range should
	//     look like is about what the reader is being shown, and that list is numbers
	//     (KESCMStoryMarkRanges.h).
	// ★★AND AN INSERTION IS THE SAME THING SEEN FROM THE OLDER SIDE (2026-08-23). The characters
	//   exist only in the newer document, so the range handed over for the older one is empty and
	//   comes out as the caret standing where they went in - which is exactly where the reader is
	//   looking. Nothing here has to know which of the two cases it is.
	docs[db][storyUID].push_back((to > from) ? KESCMMarkRange(from, to)
											 : KESCMMarkRange::Caret(from));
}

void KESCMStoryMarker::ShowFlash(const KESCMStoryMarkDocs& docs)
{
	if (gShutdown)
		return;

	// ★★WHERE THE "A STANDING MARK WINS" TEST USED TO BE (2026-08-22 - 2026-08-23). It stood here
	//   as a single early return: while any toggle was on, or the tool's button was down, the jump
	//   said nothing at all. That was right about the document the toggle was for and wrong about
	//   the other one, which had nothing standing in it and was where the reader had just asked to
	//   be shown something (bug A3).
	//   ⇒ The rule is now applied per document, once, in the composition - so there is nothing to
	//     decide here and no second place for it to be decided differently
	//     ([[one-question-one-place]]).
	gFlashDocs = docs;
	KESCMStoryMarkerInstall();

	// A flash, not a highlight - so this one gets the countdown, and a jump that lands while an
	// older one is still up restarts it, so the newest always gets the full time.
	// ⚠ARMED EVEN WHEN THE COMPOSITION HID IT. A flash asked for in a document that has a standing
	//   mark shows nothing, and the clock then takes down something invisible - which is right: if
	//   the reader turns that toggle off a moment later, what is left is the pointer they asked for,
	//   with the time it has left rather than for ever.
	if (!gFlashDocs.empty())
		KESCMStoryMarkerExpiry::Start();
	else
		KESCMStoryMarkerExpiry::Stop();
}

void KESCMStoryMarker::ShowStanding(const KESCMStoryMarkDocs& docs)
{
	if (gShutdown)
		return;

	// ★NO COUNTDOWN. What takes these down is a toggle going off or the mouse button coming up, not
	//   the clock - and a clock that a jump has running belongs to the jump, so this leaves it alone
	//   (KESCMStoryMarkerInstall).
	gStandingDocs = docs;
	KESCMStoryMarkerInstall();
}

void KESCMStoryMarker::ClearFlash()
{
	if (gFlashDocs.empty())
		return;

	gFlashDocs.clear();
	KESCMStoryMarkerExpiry::Stop();
	KESCMStoryMarkerInstall();		// repaints whatever went dark
}

void KESCMStoryMarker::ClearStanding()
{
	if (gStandingDocs.empty())
		return;

	gStandingDocs.clear();
	KESCMStoryMarkerInstall();
}

bool16 KESCMStoryMarker::IsShowing()
{
	return gHasMark;
}

void KESCMStoryMarker::Shutdown()
{
	// ⚠The flag goes up FIRST: from here on nothing repaints, because the document the mark was in
	//   may already be half torn down. Taking a mark down the ordinary way would go looking for it.
	//   ★Same door, and the same reason, as KBS's marker shutdown.
	gShutdown = kTrue;
	gHasMark = kFalse;
	gMarkDocs.clear();
	gStandingDocs.clear();
	gFlashDocs.clear();
	KESCMStoryMarkerExpiry::Shutdown();
}

// End, KESCMStoryMarker.cpp.
