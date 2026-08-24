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
#include "KESCMConstants.h"			// kKESCMRingR/G/B - the mark colour, shared with the Pixel mode's frames
#include "KESCMDrawEventHandler.h"	// KESCMSetOutputColor - screen in RGB, paper in CMYK (2026-08-24)
#include "KESCMID.h"				// ★2026-08-23: moved here from the UI plug-in's KCMUIID.h
#include "KESCMStoryMarkBuild.h"	// KESCMStoryMarkPrintAllowedFor - may THIS document go on paper
#include "KESCMStoryMarker.h"
#include "KESCMStoryMarkerExpiry.h"
#include "KESCMThreadSafety.h"		// ★★KESCMIsSameDoc (the background's cloned DB) and the lock
									//   that guards every static below. Both became necessary the
									//   moment this file moved to the model plug-in - see the note
									//   on the statics.

namespace
{

// ---- where the mark is ------------------------------------------------------------------
//
// ★STATIC, LIKE EVERY OTHER PIECE OF THIS PLUG-IN'S TRANSIENT STATE. The mark belongs to no
//   document (it is not saved anywhere), to no panel (it shows with the panel closed) and to no
//   boss (the adornment is created by the service registry, not by us).
// ⚠★★★THEY ARE WRITTEN ON THE MAIN THREAD AND READ ON BACKGROUND ONES, WHICH IS NEW AS OF
//   2026-08-23 and is the reason every access below takes KESCMMarkStateMutex(). This file used to
//   live in the UI plug-in and this comment used to say "main thread only" - true then, because a
//   kUIPlugIn is never handed the drawing during an export. It moved to the model plug-in so that
//   the marks could reach paper and PDF, and **a global text adornment is a SERVICE, which the
//   registry resolves in every execution context including background threads** (KESCM.fr spells
//   this out). ⇒ The asynchronous PDF export draws these on its own thread.
//   ★The lock is the same one the comparison marks use (KESCMThreadSafety.h), because the two are
//     never drawn at once - Pixel mode has no inverted characters and Story mode has no rings.
//   ⚠The drawing side must not take it on every wax run for nothing: gHasMark is tested FIRST and
//     the lock is only taken when there is something to look up. That is the order the Pixel side
//     already uses (KESCMDrawEventHandler.cpp:2039-2040).
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
											// ⚠★★★THE KEY CANNOT BE COMPARED WITH == ANY MORE
											// (2026-08-23). A background thread is handed a CLONED
											// copy of the database, so its pointer never equals the
											// one stored here and std::map::find always misses -
											// which is why the lookups below walk the map and ask
											// KESCMIsSameDoc() instead. Walking is free: there are
											// at most two entries, the armed target and source.
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

// How wide the caret's bar is, as a fraction of the type size.
// ★★WIDENED FROM 0.15 ON 2026-08-23 (user's request: "もう少し幅を広く"). At 10pt type that is
//   1.5pt -> 2.5pt. A deletion and an insertion seen from the older side are both drawn as this
//   bar, and at 15% it read as a hairline rather than as a mark.
// ★TWO PLACES USED TO WRITE THE SAME NUMBER. The caret and the stand-in for a zero-width range are
//   answering one question - "how thick does a bar have to be before it reads as *here*" - so
//   keeping two copies of the answer guarantees they eventually disagree ([[one-question-one-place]]).
const double kCaretWidthFraction = 0.25;

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
/* KESCMStoryMarkerRangesFor
   The ranges lit up in the story this run belongs to, or nil if none are.

   ★★★THE DATABASE IS MATCHED BY FILE, NOT BY POINTER (2026-08-23, when this file moved to the
   model plug-in). A background thread - the asynchronous PDF export - is handed a CLONED copy of
   the database, so its pointer never equals the one that was stored when the marks went up, and
   `gMarkDocs.find(db)` (what stood here) would miss every single time. The symptom would have been
   the worst kind: correct on screen, blank in the exported file. KESCMIsSameDoc() asks the file
   instead, and it is the same call the comparison marks were converted to in 2026-08-15
   (KESCMThreadSafety.h records the measurement).

   ★WALKING THE MAP COSTS NOTHING HERE. It holds at most two entries - the armed target and the
   armed source - and on the main thread KESCMIsSameDoc decides on its first line (same pointer),
   so the screen path is exactly as fast as the find() it replaces.

   ⚠THE CALLER MUST HOLD KESCMMarkStateMutex: the returned pointer points into gMarkDocs.

   @param forPrint kTrue when the drawing is going to paper or an export. The document is then asked
       whether its marks may go there at all - and the question is asked HERE because this is where
       the database has just been worked out, so printing costs no extra lookup.
*/
const KESCMMarkRangeList* KESCMStoryMarkerRangesFor(const IWaxRun* waxRun, bool16 forPrint)
{
	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return nil;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return nil;

	const UIDRef modelRef = ::GetUIDRef(model);

	// ★★MAY THIS DOCUMENT GO ON PAPER (2026-08-23). Until this line the answer was a flat "no" for
	//   every document - the mark was born as a jump's pointer, which has no business being printed.
	//   Now it is per document and per toggle, exactly as the Pixel mode's frames already were
	//   (KESCMStoryMarkBuild).
	if (forPrint && !KESCMStoryMarkPrintAllowedFor(modelRef.GetDataBase()))
		return nil;

	for (KESCMStoryMarkDocs::const_iterator doc = gMarkDocs.begin(); doc != gMarkDocs.end(); ++doc)
	{
		if (!KESCMIsSameDoc(modelRef.GetDataBase(), doc->first))
			continue;

		// ⚠One entry per document, so a miss here is final - do not keep walking looking for the
		//   same document again.
		KESCMStoryMarkMap::const_iterator story = doc->second.find(modelRef.GetUID());
		return (story != doc->second.end()) ? &story->second : nil;
	}

	return nil;
}

bool16 KESCMStoryMarkerFindRunRanges(const IWaxRun* waxRun, bool16 forPrint, KESCMMarkRangeList& outRanges)
{
	outRanges.clear();

	if (!gHasMark || waxRun == nil)
		return kFalse;

	const TextIndex runStart = waxRun->TextOrigin();
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	const TextIndex runEnd = runStart + runCount;

	// ★THE LOCK GOES HERE AND NOT ABOVE. gHasMark is a bool16 that the main thread only ever sets
	//   to kTrue after the map is complete and to kFalse before emptying it, so testing it unlocked
	//   costs a run nothing and refuses almost all of them outright. Everything below reads state
	//   the main thread rewrites (KESCMStoryMarkerSetDocs), so it is all inside.
	KESCMMarkStateLock lock(KESCMMarkStateMutex());

	if (runEnd <= gMarkLowest || runStart >= gMarkHighest)
		return kFalse;						// before or after everything that is marked

	const KESCMMarkRangeList* ranges = KESCMStoryMarkerRangesFor(waxRun, forPrint);
	if (ranges == nil)
		return kFalse;

	KESCMIntersectMarkRanges(*ranges, runStart, runEnd, outRanges);
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

	KESCMMarkStateLock lock(KESCMMarkStateMutex());		// same order as above: gHasMark first, lock second

	if (runEnd <= gMarkLowest || runStart >= gMarkHighest)
		return kFalse;

	// ⚠kFalse = "not asked about printing". GetCouldDraw, which is what calls this, is handed no
	//   iShapeFlags at all - so this can only answer the wider question "is this run marked
	//   anywhere". A run that turns out not to be printable is refused later, in Draw. ⇒ The cost of
	//   being generous here is one Draw call that draws nothing; being strict is not possible.
	const KESCMMarkRangeList* ranges = KESCMStoryMarkerRangesFor(waxRun, kFalse);
	if (ranges == nil)
		return kFalse;

	return KESCMMarkRangesTouchRun(*ranges, runStart, runEnd);
}

/* KESCMStoryMarkerSetDocs
   Install a set of ranges as THE mark, replacing whatever was there. Merging happens here so that
   no caller can hand in overlaps (which would invert twice and leave a hole), and the span the
   fast path tests is worked out in the same pass.

   ⚠What arrives is the COMPOSED set, not what a caller asked for - see the statics above.
   ⚠THE CALLER HOLDS KESCMMarkStateMutex. There is exactly one caller (KESCMStoryMarkerInstall) and
     it takes the lock around a wider stretch than this, so taking it again here would only make the
     recursion deeper for nothing.
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

	// ★ASKED BEFORE THE LOCK IS TAKEN. MarkOpacityNow() queries a facade off the Utils boss, and
	//   holding a lock across a Query is how a short lock turns into a long one.
	const PMReal opacity = MarkOpacityNow();

	{
		// ⚠THE LOCK COVERS THE REWRITE AND NOTHING ELSE. gMarkDocs is read by the drawing side on
		//   background threads (the asynchronous PDF export), so it may not be seen half-written.
		//   ★gStandingDocs and gFlashDocs are inside only because they are read here; nothing else
		//     touches them off the main thread.
		KESCMMarkStateLock lock(KESCMMarkStateMutex());

		for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
			toRepaint.insert(it->first);

		KESCMStoryMarkDocs composed;
		KESCMComposeMarkDocs(gStandingDocs, gFlashDocs, composed);
		KESCMStoryMarkerSetDocs(composed, opacity);

		for (KESCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
			toRepaint.insert(it->first);
	}

	// ⚠★REPAINTING HAPPENS OUTSIDE THE LOCK. InvalidateViews walks the document's windows and is
	//   free to take locks of its own; doing that while holding this one is the shape a deadlock
	//   comes in. The set of documents was collected above precisely so that this loop needs
	//   nothing shared ([[avoid-timers-and-idle-tasks]] is a different rule, but the discipline
	//   "hold the lock only for the memory you are changing" is the one KESCMThreadSafety.h states).
	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
		KESCMStoryMarkerRepaint(*db);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

/** Inverts the pixels over the marked characters. On screen always; on paper and in an exported
	PDF when the document's toggle says so (KESCMStoryMarkPrintAllowedFor). ★"Screen only" until
	2026-08-23, which is why the notes below about opacity are worth reading before changing it. */
class KESCMStoryMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KESCMStoryMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}
	~KESCMStoryMarkerAdornment() {}

	/** BACKGROUND, like a highlight - ★and the comment that used to stand here called this exactly
		right before it happened. It said the mark was in the FOREGROUND because it inverted what was
		under it and the glyphs had to stay visible, and added: "a slab of colour would go in the
		background pass instead, which is where the product puts its H&J and missing-glyph
		highlights, and where this would go if the inversion turns out not to work".

		★★2026-08-24: the inversion turned out not to work on paper, and the reason had nothing to
		do with which pass it was in. **In an exported or printed page the text is drawn last**, so a
		foreground adornment lands under the glyphs out there anyway: the ground inverted and the
		glyphs did not (measured: ground 255 -> 6, glyph core 0 -> 0 - black on black). Being in the
		foreground bought nothing on paper and cost the wash its natural place.
		⇒ Moved to the background pass, where the product's own highlights live, and the drawing
		  became a coloured wash that the glyphs sit on top of - the one thing that reads the same on
		  screen and on paper. See Draw() for the whole measurement.

		The named constants for Adobe's own global adornments are in IGlobalTextAdornment.h:170-181;
		the non-global ones (underline, strikethrough, paragraph shade, ruby, kenten - and the text
		itself) are at the foot of ITextAdornment.h. */
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriBackground + 0.50); }

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
							   const IWaxGlyphs* waxGlyphs, bool16 forPrint,
							   std::vector<PMRect>& outBoxes);
};

CREATE_PMINTERFACE(KESCMStoryMarkerAdornment, kKESCMStoryMarkerAdornmentImpl)

bool16 KESCMStoryMarkerAdornment::GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
											   const IWaxGlyphs* waxGlyphs, bool16 forPrint,
											   std::vector<PMRect>& outBoxes)
{
	outBoxes.clear();

	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return kFalse;

	KESCMMarkRangeList runRanges;
	if (!KESCMStoryMarkerFindRunRanges(waxRun, forPrint, runRanges))
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
			width = size * PMReal(kCaretWidthFraction);
		}
		else if (width <= 0.0)
		{
			// ★A ZERO-WIDTH RANGE STILL HAS A PLACE. It happens where the marked characters are
			//   drawn by nothing at all - and the reader still asked "where is it". A thin bar at
			//   the start of the range answers that; an empty rectangle would answer nothing.
			width = size * PMReal(kCaretWidthFraction);
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

	// ★★★ON PAPER SINCE 2026-08-23, WHERE THIS SAID "NEVER" (user's request: 印刷 ON で印刷できる
	//   ように). The old answer came from what the mark was when it was written - a pointer at
	//   something the reader had just asked to see, which has no business being printed. It has been
	//   the Story mode's whole mark since the toggles and the tool's button started using it, and a
	//   reader who turns "Print comparison marks" on means it.
	//   ⇒ The answer is now per document, decided by the same two toggles that decide the Pixel
	//     mode's frames (KESCMStoryMarkBuild).
	// ⚠kPrinting AND kPreviewMode GET THE SAME ANSWER - the header is explicit that an adornment
	//   which does not draw when printing must not draw for print preview either
	//   (IGlobalTextAdornment.h:78-83). Asking one function keeps that true by construction.
	// ⚠★★THE ANSWER HERE IS COARSER THAN THE ONE Draw GIVES, AND IT HAS TO BE (measured 2026-08-23):
	//   **IParcelShape is not an IPMUnknown**, so there is no GetDataBase to call on it and this
	//   pass cannot name the document it is about ("指示された型は関連がありません" - the first
	//   draft tried it). ⇒ Refuse the whole pass only when NOTHING is printable; let Draw decide per
	//   run, where the run's own database is at hand. The cost of being generous is a Draw call that
	//   draws nothing; being strict here is not possible.
	if (iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode))
		return KESCMStoryMarkPrintPossibleAtAll();

	return kTrue;
}

void KESCMStoryMarkerAdornment::GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
											 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	// ⚠kFalse: ink bounds are declared with no iShapeFlags to consult, so they are declared for the
	//   wider case. Over-declaring costs nothing (it only widens the rectangle the text engine will
	//   let us paint in); under-declaring would clip a mark that IS printable.
	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, kFalse, boxes))
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

	// ★THE PRINT DECISION IS MADE PER RUN, INSIDE GetMarkBoxes, because that is where the run's own
	//   database has just been worked out. GetIsActive answered the same question for the parcel,
	//   but Draw is reached by paths that do not consult it, so the flags are read again here
	//   (KT measured the same and kept the same guard).
	// ⚠THE FLAGS ARE NO LONGER A FLAT REFUSAL (2026-08-23). They now select WHICH question is asked:
	//   on screen every marked run draws; on paper only the documents whose toggle says so do.
	const bool16 forPrint = ((iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode)) != 0) ? kTrue : kFalse;

	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, forPrint, boxes))
		return;

	// ★COPIED OUT UNDER THE LOCK, THEN USED WITHOUT IT. gMarkOpacity is a PMReal - a struct, not a
	//   word - so a background thread reading it while the main thread writes a new one could see
	//   neither value. The copy is taken here rather than at the top so that runs which draw
	//   nothing (the overwhelming majority) never take the lock twice.
	// ⚠★★"FULL STRENGTH ON PAPER" WAS TRIED AND REMOVED THE SAME DAY (2026-08-24). It made sense
	//   while the mark was an inversion, where 25% only nudged the ground and the printed page needed
	//   the whole flip to be legible at all. A wash is the opposite: the strength IS the colour, and
	//   at 1.0 it prints as solid red over every changed word - readable, but shouting.
	//   ⇒ The panel's choice is honoured everywhere. What the reader picked for the screen is what
	//     comes out of the printer, which is the same promise the Pixel mode's frames make
	//     (KESCMDrawEventHandler::SelectedMarkOpacity is used by screen and output alike).
	//   ⚠AND IT WAS MEASURED WRONG AT FIRST: a PNG export counts as PRINTING (kPrinting is set), so
	//     the "screen" shots taken with exportFile came out at 1.0 too and both radio settings looked
	//     identical. A PNG is not the screen.
	PMReal opacity(1.0);
	{
		KESCMMarkStateLock lock(KESCMMarkStateMutex());
		opacity = gMarkOpacity;
	}

	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	// ★★★A COLOURED WASH BEHIND THE TEXT, NOT AN INVERSION (2026-08-24, after measuring the whole
	//   road). Difference blending was the original design and it is right for the screen: it shows
	//   through any ground and leaves the glyphs readable, because they invert along with the paper
	//   instead of being covered by it. It cannot survive being printed, and the reason is neither
	//   the blend nor the colour space:
	//   ⇒ **In an exported or printed page the TEXT IS DRAWN LAST.** A text adornment painting in the
	//     foreground pass lands UNDER the glyphs out there, so the ground inverts and the glyphs do
	//     not - measured 2026-08-24: ground 255 -> 6, glyph core 0 -> 0. Black on black. The words
	//     that changed become the only words that cannot be read, which is the opposite of the point.
	//   ⚠Moving the paint to the background pass does not help: the glyphs were always on top of it.
	//   ⚠Nor does painting in CMYK's maximum (0,0,0,1) instead of RGB's white - that corrects WHICH
	//     grounds invert, and never touches the glyphs. Both were built and measured before this.
	//   ⇒ So paint a wash BEHIND the text and let the glyphs keep their own colour. That is what the
	//     product's own H&J highlight does, and it is the one drawing that reads the same on screen
	//     and on paper.
	// ★NO TRANSPARENCY IS USED ANY MORE - the strength is mixed into the colour rather than asked for
	//   with setopacity. Three problems leave together: the flattener has nothing to flatten, the
	//   frame no longer has to declare transparency for the mark to survive, and PDF 1.3 behaves
	//   exactly like 1.4.
	// ★HOW STRONG - the panel's "Marks opacity 25% / 75%", mixed from paper white towards the mark
	//   colour. The jump's own mark passes 1.0, so it lands at the full colour.
	const int32 pct = ::ToInt32(opacity * PMReal(100.0));
	const int32 mix = (pct < 0) ? 0 : ((pct > 100) ? 100 : pct);

	// ★WHICH COLOUR - the panel's "Mark colour: Red / Cyan", read through the same accessor the
	//   Pixel mode's rings use, so the two modes can never disagree about it.
	uint8 baseR = 0, baseG = 0, baseB = 0;
	KESCMDrawEventHandler::SelectedMarkColor(baseR, baseG, baseB);

	const uint8 wr = (uint8)(255 - (255 - baseR) * mix / 100);
	const uint8 wg = (uint8)(255 - (255 - baseG) * mix / 100);
	const uint8 wb = (uint8)(255 - (255 - baseB) * mix / 100);

	// ★SCREEN IN RGB, PAPER IN CMYK - the same helper the Pixel mode's frames call, for the same
	//   reason: KESCM compares in CMYK, so a mark specified in RGB does not match its own frames on
	//   output. The helper lives in KESCMDrawEventHandler.cpp and was made non-static for this.
	KESCMSetOutputColor(gPort, wr, wg, wb, forPrint);

	// ★ONE FILL PER RANGE, AND THEY CANNOT OVERLAP - the ranges were merged before they were ever
	//   installed (KESCMStoryMarkRanges.h). ⚠With Difference an overlap punched a hole (two
	//   inversions cancel); a flat wash would merely paint twice, but the merge is still what keeps
	//   the drawing cheap.
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
	//   press say the same thing about the same deletion (KESCMStoryMarkBuild).
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

void KESCMStoryMarker::Shutdown()
{
	// ⚠The flag goes up FIRST: from here on nothing repaints, because the document the mark was in
	//   may already be half torn down. Taking a mark down the ordinary way would go looking for it.
	//   ★Same door, and the same reason, as KBS's marker shutdown.
	{
		// ⚠★LOCKED LIKE EVERY OTHER WRITE. Teardown is exactly when a background export may still
		//   be walking gMarkDocs, and clearing a map out from under a reader is the crash this lock
		//   exists to prevent (KESCMThreadSafety.h).
		KESCMMarkStateLock lock(KESCMMarkStateMutex());
		gShutdown = kTrue;
		gHasMark = kFalse;
		gMarkDocs.clear();
		gStandingDocs.clear();
		gFlashDocs.clear();
	}
	KESCMStoryMarkerExpiry::Shutdown();		// releases an idle task - outside the lock
}

// End, KESCMStoryMarker.cpp.
