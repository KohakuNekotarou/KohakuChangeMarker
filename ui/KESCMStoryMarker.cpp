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
bool16             gHasMark   = kFalse;		// the fast path: !gMarkDocs.empty(), kept as a flag
KESCMStoryMarkDocs gMarkDocs;				// database -> story UID -> its ranges, each list merged.
											// ⚠The database is an ADDRESS, only ever compared against
											// the run's own - never dereferenced
PMReal             gMarkOpacity(1.0);		// 1.0 for the jump; the panel's 25%/75% for the rest
bool16             gPersistent = kFalse;	// kTrue while a toggle or a held button is holding it up,
											// which is what makes the jump stand aside (see Show)
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

   @param persistent kTrue when a toggle or a held button is holding this up - see Show for what
      that changes.
*/
void KESCMStoryMarkerSetDocs(const KESCMStoryMarkDocs& docs, const PMReal& opacity, bool16 persistent)
{
	gMarkDocs.clear();
	gHasMark = kFalse;
	gPersistent = kFalse;
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
	{
		gHasMark = kTrue;
		gPersistent = persistent;
	}
}

/* KESCMStoryMarkerInstall
   Put a set up, take whatever was there down, and repaint everything involved - the half of Show
   and ShowDocs that is the same for either of them.

   ⚠EVERY DOCUMENT THAT WAS MARKED IS REPAINTED TOO, not just the ones that still are, or a mark
   would stay on screen in a window nobody is looking at any more. That is not hypothetical here:
   turning "Show Marks on Source" off leaves the target's marks up and has to wipe the source's.

   @param countdown kTrue for the jump's flash, kFalse for anything that stays up. Either way any
      countdown already running is dealt with, so a standing mark cannot inherit a jump's clock.
*/
void KESCMStoryMarkerInstall(const KESCMStoryMarkDocs& docs, const PMReal& opacity,
							 bool16 countdown, bool16 persistent)
{
	std::set<IDataBase*> toRepaint;
	for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	KESCMStoryMarkerSetDocs(docs, opacity, persistent);

	for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
		KESCMStoryMarkerRepaint(*db);

	if (countdown && gHasMark)
		KESCMStoryMarkerExpiry::Start();
	else
		KESCMStoryMarkerExpiry::Stop();
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

		if (width <= 0.0)
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

void KESCMStoryMarker::Show(IDataBase* db, UID storyUID, TextIndex from, TextIndex to)
{
	if (gShutdown)
		return;

	// ★★A STANDING MARK WINS, AND THE JUMP SAYS NOTHING (2026-08-22). While "Show Marks on ..." is
	//   on, or the tool's button is down, every changed character in that document is already lit -
	//   including the one this jump is aimed at. Putting a second mark up would replace the standing
	//   one (they are exclusive - see the header), so the reader would watch the whole document go
	//   dark to gain a pointer at something already visible.
	if (gHasMark && gPersistent)
		return;

	if (db == nil || storyUID == kInvalidUID)
	{
		KESCMStoryMarker::Clear();
		return;
	}

	if (to < from)
		to = from;

	// ★A DELETION HAS NO WIDTH HERE - the words are gone from this side, and the row is pointing at
	//   the place they used to be. One character is what makes that place visible; zero would mark
	//   nothing at all. (The jump's selection has the same problem and answers it the same way, with
	//   a leaning caret - see KESCMStoryJumpToChange.)
	//   ⚠It is widened HERE and not in the range list, which drops empty ranges: what a zero-width
	//     range should become is a decision about what the reader is being shown, and the list is
	//     numbers (KESCMStoryMarkRanges.h).
	KESCMStoryMarkDocs one;
	one[db][storyUID].push_back(KESCMMarkRange(from, (to > from) ? to : (from + 1)));

	// A flash, not a highlight - so this one gets the countdown. Restarting an already-running one
	// is that call's job, so each jump gets the mark for the full time.
	KESCMStoryMarkerInstall(one, PMReal(1.0), kTrue /*countdown*/, kFalse /*persistent*/);
}

void KESCMStoryMarker::ShowDocs(const KESCMStoryMarkDocs& docs, const PMReal& opacity)
{
	if (gShutdown)
		return;

	if (docs.empty())
	{
		KESCMStoryMarker::Clear();
		return;
	}

	// ★NO COUNTDOWN. What takes these down is a toggle going off or the mouse button coming up, not
	//   the clock (KESCMStoryPressMarks). Any countdown already running belongs to a jump that this
	//   has just replaced, and Install stops it.
	KESCMStoryMarkerInstall(docs, opacity, kFalse /*countdown*/, kTrue /*persistent*/);
}

void KESCMStoryMarker::Clear()
{
	KESCMStoryMarkerExpiry::Stop();

	if (!gHasMark)
		return;

	// ⚠The documents are collected BEFORE the flag comes down, and repainted after, so the redraw
	//   that follows is the one that takes the mark off. Repainting first would draw it again.
	std::set<IDataBase*> toRepaint;
	for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	gHasMark = kFalse;
	gPersistent = kFalse;
	gMarkDocs.clear();
	gMarkOpacity = PMReal(1.0);
	gMarkLowest = 0;
	gMarkHighest = 0;

	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
		KESCMStoryMarkerRepaint(*db);
}

bool16 KESCMStoryMarker::IsShowing()
{
	return gHasMark;
}

bool16 KESCMStoryMarker::IsShowingPersistent()
{
	return (gHasMark && gPersistent) ? kTrue : kFalse;
}

void KESCMStoryMarker::Shutdown()
{
	// ⚠The flag goes up FIRST: from here on nothing repaints, because the document the mark was in
	//   may already be half torn down. Clear() would otherwise go looking for it.
	//   ★Same door, and the same reason, as KBS's marker shutdown.
	gShutdown = kTrue;
	gHasMark = kFalse;
	gPersistent = kFalse;
	gMarkDocs.clear();
	KESCMStoryMarkerExpiry::Shutdown();
}

// End, KESCMStoryMarker.cpp.
