//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryMarker.h for what this is and why it is a global text adornment rather than a
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
#include "IGraphicsPort.h"
#include "IShape.h"					// kPrinting / kPreviewMode
#include "ITextModel.h"				// which story a wax run belongs to
#include "IWaxGlyphs.h"
#include "IWaxLine.h"
#include "IWaxRenderData.h"
#include "IWaxRun.h"

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "PMReal.h"		// the opacity, which is held in a static below. It reached this
						//   file through KCMStoryMarker.h until now, whose own comment asked
						//   for it to be moved to the .cpp that actually names one
#include <set>			// the documents to repaint when a mark moves between them
#include "UIDRef.h"
#include "Utils.h"

// Project includes:
#include "IKCMCompareFacade.h"	// GetSelectedMarkOpacity - what both kinds of mark are drawn at
#include "KCMCore.h"			// KCMIsDocDBOpen / KCMInvalidateDB. Both were written out here
									//   instead while this lived in the UI plug-in, which cannot call
									//   into the model at all
#include "KCMDrawEventHandler.h"	// SelectedMarkColor (the panel's red/cyan) and KCMSetOutputColor
									// (screen in RGB, paper in CMYK) -- both shared with the Pixel
									// mode's frames.
#include "KCMID.h"				// moved here from the UI plug-in with the adornment
#include "KCMStoryMarkBuild.h"	// KCMStoryMarkPrintAllowedFor - may THIS document go on paper
#include "KCMStoryMarker.h"
#include "KCMStoryMarkerExpiry.h"
#include "KCMThreadSafety.h"		// KCMIsSameDoc (the background's cloned DB) and the lock that
									//   guards every static below. Both became necessary the moment this
									//   file moved to the model plug-in -- see the note on the statics.

namespace
{

// ---- where the mark is ------------------------------------------------------------------
//
// **STATIC, LIKE EVERY OTHER PIECE OF THIS PLUG-IN'S TRANSIENT STATE.** The mark belongs to no
//   document (it is not saved anywhere), to no panel (it shows with the panel closed) and to no
//   boss (the adornment is created by the service registry, not by us).
// @warning **THEY ARE WRITTEN ON THE MAIN THREAD AND READ ON BACKGROUND ONES**, which is the
//   reason every access below takes KCMMarkStateMutex(). This file used to live in the UI
//   plug-in and this comment used to say "main thread only" -- true then, because a kUIPlugIn is
//   never handed the drawing during an export. It moved to the model plug-in so that the marks
//   could reach paper and PDF, and **a global text adornment is a SERVICE, which the registry
//   resolves in every execution context including background threads** (KCM.fr spells this out).
//   So the asynchronous PDF export draws these on its own thread.
//   The lock is the same one the comparison marks use (KCMThreadSafety.h), because the two are
//     never drawn at once -- Pixel mode has no washed characters and Story mode has no rings.
//   @warning the drawing side must not take it on every wax run for nothing: gHasMark is tested
//     FIRST and the lock is only taken when there is something to look up. That is the order the
//     Pixel side already uses (KCMDrawEventHandler, in its own draw path).
// **WHAT IS ASKED FOR, AND WHAT IS DRAWN, ARE DIFFERENT THINGS.** The two kinds of caller each
//   own one set and never touch the other's; gMarkDocs is what comes out of putting them
//   together under the per-document rule (KCMStoryMarkDocs.h), and it is the only one the
//   drawing side ever looks at.
//   Composing on the way IN rather than on the way out keeps the hot path exactly as it was: the
//     adornment is asked about every run on every page being drawn, and it still reads one map.
KCMStoryMarkDocs gStandingDocs;			// the "Show Marks on ..." toggles and a held button
KCMStoryMarkDocs gFlashDocs;				// what the newest jump asked to point at

bool16             gHasMark   = kFalse;		// the fast path: !gMarkDocs.empty(), kept as a flag
KCMStoryMarkDocs gMarkDocs;				// database -> story UID -> its ranges, each list merged.
											// @warning **THE KEY CANNOT BE COMPARED WITH ==.** A
											// background thread is handed a CLONED copy of the
											// database, so its pointer never equals the one stored
											// here and std::map::find always misses -- which is why
											// the lookups below walk the map and ask KCMIsSameDoc()
											// instead. Walking is free: there are at most two
											// entries, the armed target and source.
PMReal             gMarkOpacity(1.0);		// what both kinds are drawn at - the panel's radio
bool16             gShutdown  = kFalse;

// The whole marked span across every story, which is a free way to refuse most runs before
// asking any of them which story they belong to (that question costs a Query). Meaningless
// when gHasMark is kFalse.
TextIndex  gMarkLowest  = 0;
TextIndex  gMarkHighest = 0;

// How far above and below the baseline the mark reaches, as a fraction of the type size --
// roughly ascent and descent. @warning the two numbers come from the product's spelling
// adornment, where the lines that used them are COMMENTED OUT (DynamicSpellCheckAdornment.cpp,
// at the head of its GetInkBounds): it declares the glyph box instead. The split is ours; only
// the figures are borrowed.
const double kAscentFraction  = 0.85;
const double kDescentFraction = 0.10;

// How wide the caret's bar is, as a fraction of the type size.
// **WIDENED FROM 0.15** at the reader's request. At 10pt type that is 1.5pt -> 2.5pt. A deletion
//   and an insertion seen from the older side are both drawn as this bar, and at 15% it read as
//   a hairline rather than as a mark.
// TWO PLACES USED TO WRITE THE SAME NUMBER. The caret and the stand-in for a zero-width range
//   are answering one question -- "how thick does a bar have to be before it reads as *here*" --
//   so keeping two copies of the answer guarantees they eventually disagree
//   ([[one-question-one-place]]).
const double kCaretWidthFraction = 0.25;

/* KCMStoryMarkerRepaint
   Put the mark on screen, or take it off, now. Nothing else asks the views to redraw: a global
   adornment is only consulted while text is being drawn, so a mark that nobody repaints for would
   appear the next time the user happened to scroll.

   @warning the document is asked whether it is still open FIRST, because this runs while one is
    being closed as well: redrawing a database that is going away is exactly the crash KBS's
    marker guards against with its own shutdown flag.
   **BOTH QUESTIONS BELONG TO KCMCore.h**, and both were written out here instead: the first as a
    Query for IKCMCompareFacade (which is what a kUIPlugIn has to do to reach the model at all),
    the second as IDocument + ILayoutUtils::InvalidateViews. The marker is in the model plug-in
    now, where each is one call -- and the second of them is shared with ten other callers, so
    what "repaint a document" means is settled in one place.
*/
void KCMStoryMarkerRepaint(IDataBase* db)
{
	if (db == nil || gShutdown)
		return;
	if (!KCMIsDocDBOpen(db))
		return;

	KCMInvalidateDB(db);
}

/* KCMStoryMarkerRunSpan
   The characters this wax run covers, and kFalse when there is nothing to ask about it.

   **THE CHEAPEST OF THREE QUESTIONS, AND THE ONLY ONE THAT IS FREE.** A press marks every edit
   in the document, so the two lookups below are made for every run on every page being drawn:
     1. is anything marked at all, and does this run hold any characters -- here
     2. does the run fall within the whole marked span -- two integer comparisons
     3. which story does it belong to, and which of that story's ranges does the run overlap --
        a Query and a binary search (2 and 3 are KCMStoryMarkerRangesFor, KCMStoryMarkRanges.h)

   @warning **gHasMark IS READ WITHOUT THE LOCK, WHICH IS WHY THIS STEP IS ITS OWN.** It is a
    bool16 the main thread only ever sets to kTrue after the map is complete and to kFalse
    before emptying it, so testing it unlocked costs a run nothing and refuses almost all of
    them outright. Everything after it reads state the main thread rewrites, so both callers
    take the lock the moment this returns.
   @warning **both of them wrote these lines out for themselves** until this was pulled out,
    which put the order the whole hot path rests on -- flag first, lock second -- in two places.
*/
bool16 KCMStoryMarkerRunSpan(const IWaxRun* waxRun, TextIndex& outRunStart, TextIndex& outRunEnd)
{
	if (!gHasMark || waxRun == nil)
		return kFalse;

	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	outRunStart = waxRun->TextOrigin();
	outRunEnd = outRunStart + runCount;
	return kTrue;
}

/* KCMStoryMarkerRangesFor
   The ranges lit up in the story this run belongs to, or nil if none are.

   **THE DATABASE IS MATCHED BY FILE, NOT BY POINTER.** A background thread -- the asynchronous
   PDF export -- is handed a CLONED copy of the database, so its pointer never equals the one
   that was stored when the marks went up, and `gMarkDocs.find(db)` (what stood here) would miss
   every single time. The symptom would have been the worst kind: correct on screen, blank in the
   exported file. KCMIsSameDoc() asks the file instead, and it is the same call the comparison
   marks were converted to (KCMThreadSafety.h records the measurement).

   WALKING THE MAP COSTS NOTHING HERE. It holds at most two entries -- the armed target and the
   armed source -- and on the main thread KCMIsSameDoc decides on its first line (same pointer),
   so the screen path is exactly as fast as the find() it replaces.

   @warning THE CALLER MUST HOLD KCMMarkStateMutex: the returned pointer points into gMarkDocs.

   @param runStart, runEnd the run's characters, as KCMStoryMarkerRunSpan worked them out. The
       whole marked span is tested against them on the first line, which is what refuses most
       runs before the Query below is ever reached.
   @param forPrint kTrue when the drawing is going to paper or an export. The document is then
       asked whether its marks may go there at all -- and the question is asked HERE because this
       is where the database has just been worked out, so printing costs no extra lookup.
*/
const KCMMarkRangeList* KCMStoryMarkerRangesFor(const IWaxRun* waxRun, bool16 forPrint,
												 TextIndex runStart, TextIndex runEnd)
{
	if (runEnd <= gMarkLowest || runStart >= gMarkHighest)
		return nil;						// before or after everything that is marked

	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return nil;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return nil;

	const UIDRef modelRef = ::GetUIDRef(model);

	// **MAY THIS DOCUMENT GO ON PAPER.** The answer used to be a flat "no" for every document --
	//   the mark was born as a jump's pointer, which has no business being printed. Now it is per
	//   document and per toggle, exactly as the Pixel mode's frames already were
	//   (KCMStoryMarkBuild).
	if (forPrint && !KCMStoryMarkPrintAllowedFor(modelRef.GetDataBase()))
		return nil;

	for (KCMStoryMarkDocs::const_iterator doc = gMarkDocs.begin(); doc != gMarkDocs.end(); ++doc)
	{
		if (!KCMIsSameDoc(modelRef.GetDataBase(), doc->first))
			continue;

		// @warning one entry per document, so a miss here is final -- do not keep walking looking
		//   for the same document again.
		KCMStoryMarkMap::const_iterator story = doc->second.find(modelRef.GetUID());
		return (story != doc->second.end()) ? &story->second : nil;
	}

	return nil;
}

/* KCMStoryMarkerFindRunRanges
   Which parts of this wax run are marked, as offsets into the run.

   @warning the story has to be worked out even when only one of them is marked. A document can
    be open twice over (target and source) and both are being drawn in their own windows, so "the
    right characters" is never enough -- it has to be the right story in the right database.
*/
bool16 KCMStoryMarkerFindRunRanges(const IWaxRun* waxRun, bool16 forPrint, KCMMarkRangeList& outRanges)
{
	outRanges.clear();

	TextIndex runStart = 0;
	TextIndex runEnd = 0;
	if (!KCMStoryMarkerRunSpan(waxRun, runStart, runEnd))
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());

	const KCMMarkRangeList* ranges = KCMStoryMarkerRangesFor(waxRun, forPrint, runStart, runEnd);
	if (ranges == nil)
		return kFalse;

	KCMIntersectMarkRanges(*ranges, runStart, runEnd, outRanges);
	return outRanges.empty() ? kFalse : kTrue;
}

/* KCMStoryMarkerRunIsMarked
   The same question with no list built - what GetCouldDraw and GetIsActive want.

   @warning kFalse = "not asked about printing". GetCouldDraw, which is what calls this, is handed
    no iShapeFlags at all -- so this can only answer the wider question "is this run marked
    anywhere". A run that turns out not to be printable is refused later, in Draw. The cost of
    being generous here is one Draw call that draws nothing; being strict is not possible.
*/
bool16 KCMStoryMarkerRunIsMarked(const IWaxRun* waxRun)
{
	TextIndex runStart = 0;
	TextIndex runEnd = 0;
	if (!KCMStoryMarkerRunSpan(waxRun, runStart, runEnd))
		return kFalse;

	KCMMarkStateLock lock(KCMMarkStateMutex());

	const KCMMarkRangeList* ranges = KCMStoryMarkerRangesFor(waxRun, kFalse, runStart, runEnd);
	return (ranges != nil) ? KCMMarkRangesTouchRun(*ranges, runStart, runEnd) : kFalse;
}

/* KCMStoryMarkerSetDocs
   Install a set of ranges as THE mark, replacing whatever was there. Merging happens here so that
   no caller can hand in overlaps, and the span the fast path tests is worked out in the same pass.
   @warning merging was once a matter of correctness (two Difference inversions over the same
    characters punched a hole); the wash is opaque and an overlap would merely paint twice, so
    what the merge buys now is the cost of drawing -- one fill per stretch instead of one per
    edit.

   @warning what arrives is the COMPOSED set, not what a caller asked for -- see the statics above.
   @warning THE CALLER HOLDS KCMMarkStateMutex. There is exactly one caller
     (KCMStoryMarkerInstall) and it takes the lock around a wider stretch than this, so taking it
     again here would only make the recursion deeper for nothing.
*/
void KCMStoryMarkerSetDocs(const KCMStoryMarkDocs& docs, const PMReal& opacity)
{
	gMarkDocs.clear();
	gHasMark = kFalse;
	gMarkOpacity = opacity;
	gMarkLowest = 0;
	gMarkHighest = 0;

	bool16 first = kTrue;
	for (KCMStoryMarkDocs::const_iterator doc = docs.begin(); doc != docs.end(); ++doc)
	{
		if (doc->first == nil)
			continue;

		KCMStoryMarkMap kept;

		for (KCMStoryMarkMap::const_iterator it = doc->second.begin(); it != doc->second.end(); ++it)
		{
			if (it->first == kInvalidUID)
				continue;

			KCMMarkRangeList ranges = it->second;
			KCMMergeMarkRanges(ranges);
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

   **ONE PLACE ASKS, AND IT IS THIS ONE.** The jump's pointer used to pass a hard-coded 1.0
   while the standing marks were handed the selected value by their caller -- so the same setting
   reached one kind of mark and not the other, and a reader who chose 25% still got a solid flash
   on every jump. Asking here rather than at each caller is what stops the two from drifting
   again ([[one-question-one-place]]).

   @warning **it is read when a mark is INSTALLED, not when one is DRAWN**, and the comment here
    said the opposite once (measured: the value goes into gMarkOpacity and the drawing reads that
    static). Nothing is wrong with it -- moving the radio makes the panel refresh the standing
    marks, which comes straight back through here -- but a reader who believed the old sentence
    would look for a bug that is not there, or write one relying on a re-read that does not
    happen.
*/
PMReal MarkOpacityNow()
{
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	return (compare != nil) ? compare->GetSelectedMarkOpacity() : PMReal(1.0);
}

/* KCMStoryMarkerInstall
   Work out what is on screen from the two sets, put it up, and repaint everything involved.
   Every public call ends here, so there is one place where the rule is applied and one place that
   repaints.

   @warning **every document that was marked is repainted too**, not just the ones that still
    are, or a mark would stay on screen in a window nobody is looking at any more. That is not
    hypothetical here: turning "Always Show Marks on Source" off leaves the target's marks up and
    has to wipe the source's.

   @warning **the clock is not touched here, and that is deliberate.** It was a parameter of this
    function until the two sets were separated, which meant every standing mark going up or
    coming down had an opinion about the jump's clock. Both answers are wrong now that the two
    can be on screen together in different windows: stopping it would leave a pointer up for
    good, and starting it would hand the flash a fresh second every time a toggle moved. The
    countdown belongs to the two calls that own the flash (ShowFlash / ClearFlash) and to nothing
    else.
*/
void KCMStoryMarkerInstall()
{
	std::set<IDataBase*> toRepaint;

	// ASKED BEFORE THE LOCK IS TAKEN. MarkOpacityNow() queries a facade off the Utils boss, and
	//   holding a lock across a Query is how a short lock turns into a long one.
	const PMReal opacity = MarkOpacityNow();

	{
		// @warning **THE LOCK COVERS THE REWRITE AND NOTHING ELSE.** gMarkDocs is read by the drawing
		//   side on background threads (the asynchronous PDF export), so it may not be seen
		//   half-written. gStandingDocs and gFlashDocs are inside only because they are read here;
		//   nothing else touches them off the main thread.
		KCMMarkStateLock lock(KCMMarkStateMutex());

		for (KCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
			toRepaint.insert(it->first);

		KCMStoryMarkDocs composed;
		KCMComposeMarkDocs(gStandingDocs, gFlashDocs, composed);
		KCMStoryMarkerSetDocs(composed, opacity);

		for (KCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
			toRepaint.insert(it->first);
	}

	// @warning **REPAINTING HAPPENS OUTSIDE THE LOCK.** InvalidateViews walks the document's
	//   windows and is free to take locks of its own; doing that while holding this one is the
	//   shape a deadlock comes in. The set of documents was collected above precisely so that this
	//   loop needs nothing shared -- "hold the lock only for the memory you are changing" is the
	//   discipline KCMThreadSafety.h states.
	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
		KCMStoryMarkerRepaint(*db);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

/** Lays a coloured wash under the marked characters. On screen always; on paper and in an
	exported PDF when the document's toggle says so (KCMStoryMarkPrintAllowedFor). It was
	"screen only" once, and an INVERSION before that -- both changes are recorded where they matter
	(GetDrawPriority and Draw), and both are worth reading before changing the look again. */
class KCMStoryMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KCMStoryMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}
	~KCMStoryMarkerAdornment() {}

	/** BACKGROUND, like a highlight -- and the comment that used to stand here called this exactly
		right before it happened. It said the mark was in the FOREGROUND because it inverted what was
		under it and the glyphs had to stay visible, and added: "a slab of colour would go in the
		background pass instead, which is where the product puts its H&J and missing-glyph
		highlights, and where this would go if the inversion turns out not to work".

		**The inversion turned out not to work on paper, and the reason had nothing to do with which
		pass it was in.** In an exported or printed page the text is drawn last, so a foreground
		adornment lands under the glyphs out there anyway: the ground inverted and the glyphs did not
		(measured: ground 255 -> 6, glyph core 0 -> 0 -- black on black). Being in the foreground
		bought nothing on paper and cost the wash its natural place.
		So it moved to the background pass, where the product's own highlights live, and the drawing
		  became a coloured wash that the glyphs sit on top of -- the one thing that reads the same on
		  screen and on paper. See Draw() for the whole measurement.

		The named constants for Adobe's own global adornments are in IGlobalTextAdornment.h; the
		non-global ones (underline, strikethrough, paragraph shade, ruby, kenten -- and the text
		itself) are at the foot of ITextAdornment.h.

		@warning **AND THE NUMBER HAS TO BE NEGATIVE, WHICH COST A BUG.** The move above was first
		 written as `kTAPassPriBackground + 0.50` -- the right pass by name and the wrong one by
		 arithmetic:
		  * a priority is split in two, **the whole part being the PASS and the fraction the RUN**
		    within it (TextDrawPriority.h says so at the head of the namespace), and the wax runs are
		    walked once per distinct pass;
		  * kPassBackground is **-16384** (DrawPassInfo.h), so a POSITIVE fraction carries the whole
		    part up to -16384 while **every one of Adobe's own background adornments, without
		    exception, is written as kTAPassPriBackground + a NEGATIVE fraction** (eleven of them,
		    -0.62 to -0.49) and therefore sits in pass -16385.
		 So `+0.50` did not join them; it drew a whole pass AFTER all of them. Since the wash is
		   opaque (no transparency, by design -- see Draw), it painted over paragraph shading,
		   paragraph borders and rules, underlines, and the product's own missing-font / missing-glyph
		   / kinsoku / H&J highlights, in exactly the passage the reader was told to look at.
		-0.585 puts it between kTAPriParagraphRuleBelow (-0.59) and kTAPriUnderline (-0.58): after the
		  paragraph-level grounds, so the wash covers a paragraph shade the way a highlighter does, and
		  before everything drawn ON the characters, so nothing the product draws is hidden.
		The purpose of the move is untouched: any negative pass is still below the glyphs, which are
		  drawn at kTAPassPriText + 0.50 (pass 0). */
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriBackground + -0.585); }

	virtual bool16 GetCheckIsActive() { return kTrue; }
	virtual bool16 GetIsActive(const IParcelShape* parcelShape,
							   const ITextOptions* textOptions,
							   int32 iShapeFlags);

	/** kTrue, WHERE KT's EXPERIMENT AND spellpanel BOTH ANSWER kFalse: they draw on every run, and
		this draws on a handful of characters in one story. Answering the per-run question is what
		stops the text engine calling Draw for every run in the document while a mark is up -- the
		header's own example is the H&J adornment declining runs whose line holds no violation. */
	virtual bool16 GetCheckCouldDraw() { return kTrue; }
	virtual bool16 GetCouldDraw(const IWaxRun* waxRun, const IWaxRenderData*, const IWaxGlyphs*)
	{
		return KCMStoryMarkerRunIsMarked(waxRun);
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
	/** The rectangles to wash, in the coordinates the run reports its own position in - one per
		marked range that falls in this run.
		kFalse for a run that cannot be measured - an inline graphic has neither glyphs nor render
		data, which all four of these methods are warned about in IGlobalTextAdornment.h. */
	static bool16 GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
							   const IWaxGlyphs* waxGlyphs, bool16 forPrint,
							   std::vector<PMRect>& outBoxes);
};

CREATE_PMINTERFACE(KCMStoryMarkerAdornment, kKCMStoryMarkerAdornmentImpl)

bool16 KCMStoryMarkerAdornment::GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
											   const IWaxGlyphs* waxGlyphs, bool16 forPrint,
											   std::vector<PMRect>& outBoxes)
{
	outBoxes.clear();

	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return kFalse;

	KCMMarkRangeList runRanges;
	if (!KCMStoryMarkerFindRunRanges(waxRun, forPrint, runRanges))
		return kFalse;

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphCount <= 0)
		return kFalse;

	// THE GLYPH WIDTHS ARE ADDED UP ONCE, not once per range. A press can mark several separate
	//   edits inside a single wax run, and walking the glyphs again for each of them would make
	//   the cost of a run grow with the number of edits in it.
	std::vector<PMReal> cumulative(glyphCount + 1, PMReal(0.0));
	for (int32 i = 0; i < glyphCount; ++i)
		cumulative[i + 1] = cumulative[i] + waxGlyphs->GetWidthAt(i);

	const PMReal y = waxRun->GetYPosition();				// the baseline
	const PMReal size = renderData->GetFontMatrix().GetYScale();

	for (KCMMarkRangeList::const_iterator r = runRanges.begin(); r != runRanges.end(); ++r)
	{
		const int32 charStart = static_cast<int32>(r->fFrom);
		const int32 charCount = static_cast<int32>(r->fTo - r->fFrom);

		// **CHARACTERS ARE NOT GLYPHS.** One character can be drawn by several glyphs and several
		//   characters by one, so the range has to be mapped before any width is added up -- the same
		//   call the product's spelling squiggle makes before underlining a word
		//   (DynamicSpellCheckAdornment.cpp, in its own drawing loop).
		int32 glyphIndex = -1;
		int32 glyphLength = 0;
		waxGlyphs->MapCharsToGlyphs(charStart, charCount, &glyphIndex, &glyphLength);
		if (glyphIndex < 0 || glyphLength <= 0)
			continue;						// a range that maps to nothing skips this box, not the run
		if (glyphIndex >= glyphCount)
			continue;
		if (glyphIndex + glyphLength > glyphCount)
			glyphLength = glyphCount - glyphIndex;

		// Where the range starts and how wide it is, read out of the running total above.
		// @warning added up rather than taken from GetGlyphDrawPosition because a draw position is a
		//   MATRIX -- it carries the glyph's own transform, and reading a translation out of it is
		//   only the origin of that one glyph, not the end of the range.
		const PMReal offset = cumulative[glyphIndex];
		PMReal width = cumulative[glyphIndex + glyphLength] - offset;

		if (r->fCaret)
		{
			// **A DELETION IS A CARET, NOT AN INVERTED CHARACTER.** The characters are gone from this
			//   side, so there is nothing here that IS the edit -- and inverting the character that closed
			//   up over the gap says the wrong thing about it: deleting a whole paragraph lit the first
			//   character of the NEXT one, and deleting the end of a story lit the final carriage return,
			//   which draws nothing at all.
			//   The range still covers one character so that it sorts and merges like any other
			//     (KCMStoryMarkRanges.h), but what is DRAWN is a bar standing where the caret would stand
			//     if you clicked in front of that character -- the same place the jump centres.
			width = size * PMReal(kCaretWidthFraction);
		}
		else if (width <= 0.0)
		{
			// A ZERO-WIDTH RANGE STILL HAS A PLACE. It happens where the marked characters are drawn by
			//   nothing at all -- and the reader still asked "where is it". A thin bar at the start of the
			//   range answers that; an empty rectangle would answer nothing.
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

bool16 KCMStoryMarkerAdornment::GetIsActive(const IParcelShape* /*parcelShape*/,
											  const ITextOptions* /*textOptions*/,
											  int32 iShapeFlags)
{
	if (!gHasMark)
		return kFalse;

	// **ON PAPER NOW, WHERE THIS SAID "NEVER".** The old answer came from what the mark was when it
	//   was written -- a pointer at something the reader had just asked to see, which has no
	//   business being printed. It has been the Story mode's whole mark since the toggles and the
	//   tool's button started using it, and a reader who turns "Print comparison marks" on means it.
	//   The answer is now per document, decided by the same two toggles that decide the Pixel
	//     mode's frames (KCMStoryMarkBuild).
	// @warning **kPrinting AND kPreviewMode GET THE SAME ANSWER** -- the header is explicit that an
	//   adornment which does not draw when printing must not draw for print preview either
	//   (IGlobalTextAdornment.h, at GetIsActive). Asking one function keeps that true by
	//   construction.
	// @warning **the answer here is coarser than the one Draw gives, and it has to be.**
	//   This pass cannot name the document it is about: the header says of this very parameter
	//   "the parcel this text is in. **It MAY NOT be a UID based object**"
	//   (IGlobalTextAdornment.h, at GetIsActive), so GetDataBase has nothing to answer with even
	//   when it can be called. So refuse the whole pass only when NOTHING is printable; let Draw
	//   decide per run, where the run's own database is at hand. The cost of being generous is a
	//   Draw call that draws nothing; being strict here is not possible.
	//   @warning **this said "IParcelShape is not an IPMUnknown" and that is simply false**
	//     (IParcelShape.h: `class IParcelShape : public IPMUnknown`, and GetDataBase takes a
	//     const IPMUnknown* -- PersistUtils.h -- so the call compiles). Whatever the first draft's
	//     compiler refused, it was not that. **The conclusion was right and the reason was wrong**,
	//     which is the worse of the two failures: a reason that does not hold stops warning anybody
	//     the day it stops applying. The real reason is above, and it was in the header all along.
	if (iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode))
		return KCMStoryMarkPrintPossibleAtAll();

	return kTrue;
}

void KCMStoryMarkerAdornment::GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
											 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	// @warning kFalse: ink bounds are declared with no iShapeFlags to consult, so they are declared
	//   for the wider case. Over-declaring costs nothing (it only widens the rectangle the text
	//   engine will let us paint in); under-declaring would clip a mark that IS printable.
	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, kFalse, boxes))
		return;								// leave them empty, as the header instructs

	// THE UNION OF THEM ALL, because ink bounds are declared once for the whole run. A press can
	//   mark two separate edits inside one run, and a bound that covered only the first would clip
	//   the second away.
	PMRect all = boxes.front();
	for (size_t i = 1; i < boxes.size(); ++i)
		all.Union(boxes[i]);

	*inkBounds = all;
}

void KCMStoryMarkerAdornment::Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
									 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	if (gd == nil || !gHasMark)
		return;

	// THE PRINT DECISION IS MADE PER RUN, INSIDE GetMarkBoxes, because that is where the run's own
	//   database has just been worked out. GetIsActive answered the same question for the parcel,
	//   but Draw is reached by paths that do not consult it, so the flags are read again here.
	// @warning the flags are no longer a flat refusal. They now select WHICH question is asked: on
	//   screen every marked run draws; on paper only the documents whose toggle says so do.
	const bool16 forPrint = ((iShapeFlags & (IShape::kPrinting | IShape::kPreviewMode)) != 0) ? kTrue : kFalse;

	std::vector<PMRect> boxes;
	if (!GetMarkBoxes(waxRun, renderData, waxGlyphs, forPrint, boxes))
		return;

	// COPIED OUT UNDER THE LOCK, THEN USED WITHOUT IT. gMarkOpacity is a PMReal -- a struct, not a
	//   word -- so a background thread reading it while the main thread writes a new one could see
	//   neither value. The copy is taken here rather than at the top so that runs which draw
	//   nothing (the overwhelming majority) never take the lock twice.
	// @warning **"full strength on paper" was tried and removed the same day.** It made sense while
	//   the mark was an inversion, where 25% only nudged the ground and the printed page needed the
	//   whole flip to be legible at all. A wash is the opposite: the strength IS the colour, and at
	//   1.0 it prints as solid red over every changed word -- readable, but shouting.
	//   The panel's choice is honoured everywhere. What the reader picked for the screen is what
	//     comes out of the printer, which is the same promise the Pixel mode's frames make
	//     (KCMDrawEventHandler::SelectedMarkOpacity is used by screen and output alike).
	//   @warning and it was measured wrong at first: a PNG export counts as PRINTING (kPrinting is
	//     set), so the "screen" shots taken with exportFile came out at 1.0 too and both radio
	//     settings looked identical. A PNG is not the screen.
	PMReal opacity(1.0);
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		opacity = gMarkOpacity;
	}

	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	// **A COLOURED WASH BEHIND THE TEXT, NOT AN INVERSION** (after measuring the whole road).
	//   Difference blending was the original design and it is right for the screen: it shows through
	//   any ground and leaves the glyphs readable, because they invert along with the paper instead
	//   of being covered by it. It cannot survive being printed, and the reason is neither the blend
	//   nor the colour space:
	//   **In an exported or printed page the TEXT IS DRAWN LAST.** A text adornment painting in the
	//     foreground pass lands UNDER the glyphs out there, so the ground inverts and the glyphs do
	//     not -- measured: ground 255 -> 6, glyph core 0 -> 0. Black on black. The words that changed
	//     become the only words that cannot be read, which is the opposite of the point.
	//   @warning moving the paint to the background pass does not help: the glyphs were always on
	//     top of it. Nor does painting in CMYK's maximum (0,0,0,1) instead of RGB's white -- that
	//     corrects WHICH grounds invert, and never touches the glyphs. Both were built and measured
	//     before this.
	//   So paint a wash BEHIND the text and let the glyphs keep their own colour. That is what the
	//     product's own H&J highlight does, and it is the one drawing that reads the same on screen
	//     and on paper.
	// NO TRANSPARENCY IS USED -- the strength is mixed into the colour rather than asked for with
	//   setopacity. Three problems leave together: the flattener has nothing to flatten, the frame no
	//   longer has to declare transparency for the mark to survive, and PDF 1.3 behaves exactly like
	//   1.4.
	// HOW STRONG -- the panel's "Marks opacity 25% / 75%", mixed from paper white towards the mark
	//   colour. @warning **both kinds of mark get the same strength**, the jump's flash included:
	//   the value is read once by KCMStoryMarkerInstall (MarkOpacityNow) and every caller ends
	//   there. A sentence here once said the jump "passes 1.0 and lands at the full colour", which
	//   described the very bug that was fixed by moving the question to one place (see
	//   MarkOpacityNow above, which says so).
	const int32 pct = ::ToInt32(opacity * PMReal(100.0));
	const int32 mix = (pct < 0) ? 0 : ((pct > 100) ? 100 : pct);

	// WHICH COLOUR -- the panel's "Mark colour: Red / Cyan", read through the same accessor the
	//   Pixel mode's rings use, so the two modes can never disagree about it.
	uint8 baseR = 0, baseG = 0, baseB = 0;
	KCMDrawEventHandler::SelectedMarkColor(baseR, baseG, baseB);

	const uint8 wr = (uint8)(255 - (255 - baseR) * mix / 100);
	const uint8 wg = (uint8)(255 - (255 - baseG) * mix / 100);
	const uint8 wb = (uint8)(255 - (255 - baseB) * mix / 100);

	// SCREEN IN RGB, PAPER IN CMYK -- the same helper the Pixel mode's frames call, for the same
	//   reason: KCM compares in CMYK, so a mark specified in RGB does not match its own frames on
	//   output. The helper lives in KCMDrawEventHandler.cpp and was made non-static for this.
	KCMSetOutputColor(gPort, wr, wg, wb, forPrint);

	// ONE FILL PER RANGE, AND THEY CANNOT OVERLAP -- the ranges were merged before they were ever
	//   installed (KCMStoryMarkRanges.h). @warning with Difference an overlap punched a hole (two
	//   inversions cancel); a flat wash would merely paint twice, but the merge is still what keeps
	//   the drawing cheap.
	for (std::vector<PMRect>::const_iterator b = boxes.begin(); b != boxes.end(); ++b)
		gPort->rectfill(b->Left(), b->Top(), b->Width(), b->Height());

	gPort->newpath();
}

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

void KCMStoryMarker::AddFlashRange(KCMStoryMarkDocs& docs, IDataBase* db, UID storyUID,
									TextIndex from, TextIndex to)
{
	if (db == nil || storyUID == kInvalidUID)
		return;			// a window that is not open, or a story there is none of - nothing to add

	if (to < from)
		to = from;

	// **A DELETION HAS NO WIDTH on the side it was deleted from** -- the words are gone from there,
	//   and what the jump is pointing at is the PLACE they used to be. That place is shown as a
	//   CARET, which is also what the standing marks do, so a jump and a press say the same thing
	//   about the same deletion (KCMStoryMarkBuild).
	//   @warning it used to be widened to one character here, which inverted whatever had closed up
	//     over the gap -- a different character claiming to be the edit.
	//   The decision is made HERE and not inside the range list: what a zero-width range should
	//     look like is about what the reader is being shown, and that list is numbers
	//     (KCMStoryMarkRanges.h).
	// **AND AN INSERTION IS THE SAME THING SEEN FROM THE OLDER SIDE.** The characters exist only in
	//   the newer document, so the range handed over for the older one is empty and comes out as the
	//   caret standing where they went in -- which is exactly where the reader is looking. Nothing
	//   here has to know which of the two cases it is.
	docs[db][storyUID].push_back((to > from) ? KCMMarkRange(from, to)
											 : KCMMarkRange::Caret(from));
}

void KCMStoryMarker::ShowFlash(const KCMStoryMarkDocs& docs)
{
	if (gShutdown)
		return;

	// **WHERE THE "A STANDING MARK WINS" TEST USED TO BE.** It stood here as a single early return:
	//   while any toggle was on, or the tool's button was down, the jump said nothing at all. That
	//   was right about the document the toggle was for and wrong about the other one, which had
	//   nothing standing in it and was where the reader had just asked to be shown something (bug
	//   A3).
	//   The rule is now applied per document, once, in the composition -- so there is nothing to
	//     decide here and no second place for it to be decided differently
	//     ([[one-question-one-place]]).
	gFlashDocs = docs;
	KCMStoryMarkerInstall();

	// A flash, not a highlight - so this one gets the countdown, and a jump that lands while an
	// older one is still up restarts it, so the newest always gets the full time.
	// @warning **armed even when the composition hid it.** A flash asked for in a document that has
	//   a standing mark shows nothing, and the clock then takes down something invisible -- which is
	//   right: if the reader turns that toggle off a moment later, what is left is the pointer they
	//   asked for, with the time it has left rather than for ever.
	if (!gFlashDocs.empty())
		KCMStoryMarkerExpiry::Start();
	else
		KCMStoryMarkerExpiry::Stop();
}

void KCMStoryMarker::ShowStanding(const KCMStoryMarkDocs& docs)
{
	if (gShutdown)
		return;

	// NO COUNTDOWN. What takes these down is a toggle going off or the mouse button coming up, not
	//   the clock -- and a clock that a jump has running belongs to the jump, so this leaves it
	//   alone (KCMStoryMarkerInstall).
	gStandingDocs = docs;
	KCMStoryMarkerInstall();
}

void KCMStoryMarker::ClearFlash()
{
	if (gFlashDocs.empty())
		return;

	gFlashDocs.clear();
	KCMStoryMarkerExpiry::Stop();
	KCMStoryMarkerInstall();		// repaints whatever went dark
}

void KCMStoryMarker::ClearStanding()
{
	if (gStandingDocs.empty())
		return;

	gStandingDocs.clear();
	KCMStoryMarkerInstall();
}

void KCMStoryMarker::Shutdown()
{
	// @warning the flag goes up FIRST: from here on nothing repaints, because the document the mark
	//   was in may already be half torn down. Taking a mark down the ordinary way would go looking
	//   for it. Same door, and the same reason, as KBS's marker shutdown.
	{
		// @warning **LOCKED LIKE EVERY OTHER WRITE.** Teardown is exactly when a background export may
		//   still be walking gMarkDocs, and clearing a map out from under a reader is the crash this
		//   lock exists to prevent (KCMThreadSafety.h).
		KCMMarkStateLock lock(KCMMarkStateMutex());
		gShutdown = kTrue;
		gHasMark = kFalse;
		gMarkDocs.clear();
		gStandingDocs.clear();
		gFlashDocs.clear();
	}
	KCMStoryMarkerExpiry::Shutdown();		// releases an idle task - outside the lock
}

// End, KCMStoryMarker.cpp.
