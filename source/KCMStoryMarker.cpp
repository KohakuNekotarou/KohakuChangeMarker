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
#include "IGraphicsPort.h"
#include "IShape.h"					// kPrinting / kPreviewMode
#include "ITextModel.h"				// which story a wax run belongs to
#include "IWaxGlyphs.h"
#include "IWaxLine.h"
#include "IWaxRenderData.h"
#include "IWaxRun.h"
#include "IWaxRunShape.h"			// text on a path: where a run's characters really are

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "GraphicsData.h"
#include "K2Vector.h"
#include "PMPolygon4.h"
#include "PMReal.h"
#include <atomic>		// gHasMark - read off the main thread without the lock
#include <cmath>		// std::sqrt - the direction a character on a path runs in
#include <set>			// the documents to repaint when a mark moves between them
#include "UIDRef.h"

// Project includes:
#include "KCMCore.h"			// KCMIsDocDBOpen / KCMInvalidateDB
#include "KCMDrawEventHandler.h"	// SelectedMarkColor and SelectedMarkOpacity (the panel's choices) and
									// KCMSetOutputColor (screen in RGB, paper in CMYK) -- all shared with
									// the Pixel mode's frames -- and sPrintMarks, tl_Rasterizing.
#include "KCMExternalSource.h"	// KCMIsDbAlive - a document in the list, or the lent Source (ForgetClosedDocs)
#include "KCMID.h"				// kKCMStoryMarkerAdornmentImpl
#include "KCMStoryList.h"		// KCMCaretOnTableChars - a caret in front of a table stands after the character before it
#include "KCMStoryMarkBuild.h"	// KCMStoryMarkPrintAllowedFor - may THIS document go on paper
#include "KCMStoryMarker.h"
#include "KCMStoryMarkerExpiry.h"
#include "KCMThreadSafety.h"		// KCMIsSameDoc (the background's cloned DB) and the lock over what is drawn from

namespace
{

// ---- where the mark is ------------------------------------------------------------------
//
// **STATIC, LIKE EVERY OTHER PIECE OF THIS PLUG-IN'S TRANSIENT STATE.** The mark belongs to no
//   document (it is not saved anywhere), to no panel (it shows with the panel closed) and to no
//   boss (the adornment is created by the service registry, not by us).
// @warning **THE ONE THAT IS DRAWN FROM IS WRITTEN ON THE MAIN THREAD AND READ ON BACKGROUND ONES**, which is the
//   reason every write of it, and every read off the main thread, takes KCMMarkStateMutex(). (gStandingDocs and
//   gFlashDocs are the main thread's own.) **A global text adornment is a SERVICE, which the registry resolves in
//   every execution context including background threads** (KCM.fr spells this out), so the asynchronous PDF export
//   draws these on its own thread.
//   The lock is the same one the comparison marks use (KCMThreadSafety.h), because the two are
//     never drawn at once -- Pixel mode has no washed characters and Story mode has no rings.
//   @warning the drawing side must not take it on every wax run for nothing: gHasMark is tested
//     FIRST and the lock is only taken when there is something to look up. That is the order the
//     Pixel side already uses (KCMDrawEventHandler, in its own draw path).
// **WHAT IS ASKED FOR, AND WHAT IS DRAWN, ARE DIFFERENT THINGS.** The two kinds of caller each
//   own one set and never touch the other's; gMarkDocs is what comes out of putting them
//   together under the per-document rule (KCMStoryMarkDocs.h), and it is the only one the
//   drawing side ever looks at.
//   Composing on the way IN rather than on the way out keeps the hot path short: the
//     adornment is asked about every run on every page being drawn, and it reads one map.
KCMStoryMarkDocs gStandingDocs;			// the "Always Show Marks on ..." toggles, Print and a held button
KCMStoryMarkDocs gFlashDocs;				// what the newest jump asked to point at

// the fast path: !gMarkDocs.empty(), kept as a flag. std::atomic because it is read unlocked on the export's thread:
//   a plain bool16 read that way is a data race in the language, which MSVC on x64 only happens to forgive.
std::atomic<bool>  gHasMark(false);
KCMStoryMarkDocs gMarkDocs;				// database -> story UID -> its ranges, each list merged.
											// @warning **THE KEY CANNOT BE COMPARED WITH ==.** A
											// background thread is handed a CLONED copy of the
											// database, so its pointer never equals the one stored
											// here and std::map::find always misses -- which is why
											// the lookups below walk the map and ask KCMIsSameDoc()
											// instead. Walking is free: there are at most two
											// entries, the armed target and source.
bool16             gShutdown  = kFalse;

// How far gMarkDocs reaches, which refuses most runs before any of them is asked which story it belongs to (KCMMarkSpan).
KCMMarkSpan gMarkSpan;

// How far above and below the baseline the mark reaches, as a fraction of the type size --
// roughly ascent and descent. @warning the two numbers come from the product's spelling
// adornment, where the lines that used them are COMMENTED OUT (DynamicSpellCheckAdornment.cpp,
// in its GetInkBounds): it declares a one-point strip under the baseline instead. The split is
// ours; only the figures are borrowed.
const double kAscentFraction  = 0.85;
const double kDescentFraction = 0.10;

// How wide the caret's bar is, as a fraction of the type size.
// 0.25 AT THE AUTHOR'S REQUEST: at 0.15 (1.5pt at 10pt type) a deletion's bar, and an insertion's seen from the
//   older side, read as a hairline rather than as a mark.
// The caret, the stand-in for a zero-width range and the bar on a path all answer one question -- "how thick does a
//   bar have to be before it reads as *here*".
const double kCaretWidthFraction = 0.25;

/* KCMStoryMarkerRangesFor
   The ranges lit up in the story this run belongs to, or nil if none are.

   **THE DATABASE IS MATCHED BY FILE, NOT BY POINTER.** A background thread -- the asynchronous
   PDF export -- is handed a CLONED copy of the database, so its pointer never equals the one
   that was stored when the marks went up, and `gMarkDocs.find(db)` would miss every single time: correct on
   screen, blank in the exported file. KCMIsSameDoc() asks the file (the document ID for an unsaved one) instead, as
   the comparison marks do (KCMThreadSafety.h records the measurement).

   WALKING THE MAP COSTS LITTLE HERE. It holds at most two entries -- the armed target and the armed source -- and on
   the main thread the same pointer answers at once (the other entry, when it comes first, compares two files).

   @warning THE CALLER MUST HOLD KCMMarkStateMutex: the returned pointer points into gMarkDocs.

   @param runStart, runEnd the run's characters, as KCMStoryMarkerAskAboutRun worked them out. The
       whole marked span is tested against them on the first line, which is what refuses most
       runs before the Query below is ever reached.
   @param forPrint kTrue when the drawing is output (KCMStoryMarkerDrawIsOutput). The document is then
       asked whether its marks may go there at all -- and the question is asked HERE because this
       is where the database has just been worked out, so printing costs no extra lookup.
*/
const KCMMarkRangeList* KCMStoryMarkerRangesFor(const IWaxRun* waxRun, bool16 forPrint,
												 TextIndex runStart, TextIndex runEnd)
{
	if (gMarkSpan.Misses(runStart, runEnd))
		return nil;						// before or after everything that is marked

	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return nil;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return nil;

	const UIDRef modelRef = ::GetUIDRef(model);

	// **MAY THIS DOCUMENT GO ON PAPER.** Per document, as the Pixel mode's frames are (KCMStoryMarkBuild).
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

/* KCMStoryMarkerAskAboutRun
   Put a question to the ranges lit up in this wax run's story: `ask` is handed them and the run's characters, with the
   lock held, and what it answers is the answer. kFalse, with nothing asked, when none are lit there.

   **THE CHEAPEST OF THREE QUESTIONS COMES FIRST, AND IT IS THE ONLY ONE THAT IS FREE.** A press marks every edit
   in the document, so this is asked for every run on every page being drawn:
     1. is anything marked at all, and does this run hold any characters -- the first lines here
     2. does the run fall within the whole marked span -- two integer comparisons
     3. which story does it belong to, and which of that story's ranges does the run overlap --
        a Query and a binary search (2 and 3 are KCMStoryMarkerRangesFor, KCMStoryMarkRanges.h)

   @warning **gHasMark IS READ WITHOUT THE LOCK, WHICH IS WHY IT COMES BEFORE THE LOCK.** It is an
    atomic flag the main thread only ever sets to true after the map is complete, and a true that
    has gone out of date only sends a run on to the lock, where the map answers. So testing it
    unlocked costs a run nothing and refuses almost all of them outright. Everything after it
    reads state the main thread rewrites.
   @warning **the order the whole hot path rests on -- flag first, lock second -- is here alone**: neither of the two
    questions below holds a lock or a pointer into gMarkDocs.

   @param forPrint as KCMStoryMarkerRangesFor takes it.
   @param ask called as ask(ranges, runStart, runEnd), answering bool16. The list is only good until it returns.
*/
template <typename Ask>
bool16 KCMStoryMarkerAskAboutRun(const IWaxRun* waxRun, bool16 forPrint, Ask ask)
{
	if (!gHasMark || waxRun == nil)
		return kFalse;

	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	const TextIndex runStart = waxRun->TextOrigin();
	const TextIndex runEnd = runStart + runCount;

	KCMMarkStateLock lock(KCMMarkStateMutex());

	const KCMMarkRangeList* ranges = KCMStoryMarkerRangesFor(waxRun, forPrint, runStart, runEnd);
	return (ranges != nil) ? ask(*ranges, runStart, runEnd) : kFalse;
}

/* KCMStoryMarkerFindRunRanges
   Which parts of this wax run are marked, as offsets into the run.

   @warning the story has to be worked out even when only one of them is marked. A document can
    be open twice over (target and source) and both are being drawn in their own windows, so "the
    right characters" is never enough -- it has to be the right story in the right database.
*/
bool16 KCMStoryMarkerFindRunRanges(const IWaxRun* waxRun, bool16 forPrint, KCMMarkRangeList& outRanges)
{
	return KCMStoryMarkerAskAboutRun(waxRun, forPrint,
		[&outRanges](const KCMMarkRangeList& ranges, TextIndex runStart, TextIndex runEnd) -> bool16
		{
			KCMIntersectMarkRanges(ranges, runStart, runEnd, outRanges);
			return outRanges.empty() ? kFalse : kTrue;
		});
}

/* KCMStoryMarkerRunIsMarked
   The same question with no list built - what GetCouldDraw wants.

   @warning kFalse = "not asked about printing". GetCouldDraw, which is what calls this, is handed
    no iShapeFlags at all -- so this can only answer the wider question "is this run marked
    anywhere". A run that turns out not to be printable is refused later, in Draw. The cost of
    being generous here is one Draw call that draws nothing; being strict is not possible.
*/
bool16 KCMStoryMarkerRunIsMarked(const IWaxRun* waxRun)
{
	return KCMStoryMarkerAskAboutRun(waxRun, kFalse, KCMMarkRangesTouchRun);
}

/* KCMStoryMarkerInstall
   Work out what is on screen from the two sets, put it up, and repaint everything involved.
   Every public call ends here, so there is one place where the rule is applied and one place that
   repaints.

   **THE REPAINT IS WHAT PUTS IT ON SCREEN, OR TAKES IT OFF.** Nothing else asks the views to redraw:
   a global adornment is only consulted while text is being drawn, so a mark that nobody repaints
   for would appear the next time the user happened to scroll.

   @warning **every document that was marked is repainted too**, not just the ones that still
    are, or a mark would stay on screen in a window nobody is looking at any more. That is not
    hypothetical here: turning "Always Show Marks on Source" off leaves the target's marks up and
    has to wipe the source's.

   @warning **the clock is not touched here, and that is deliberate.** The two kinds can be on screen together in
    different windows: stopping the clock for a standing mark would leave a pointer up for good, and starting it would
    hand the flash a fresh second every time a toggle moved. The countdown belongs to ShowFlash and to nothing else.
*/
void KCMStoryMarkerInstall()
{
	// COMPOSED AND MERGED BEFORE THE LOCK, because nothing here is shared yet: the two sets are the main
	//   thread's own, and `incoming` is nobody's but this function's. A press over a few thousand edits
	//   merges a few thousand ranges, and no drawing thread has to wait for that. Merged here so that no
	//   caller can hand in overlaps.
	KCMStoryMarkDocs incoming;
	KCMComposeMarkDocs(gStandingDocs, gFlashDocs, incoming);
	const KCMMarkSpan span = KCMMergeMarkDocs(incoming);

	// @warning **THE LOCK COVERS THE REWRITE AND NOTHING ELSE.** gMarkDocs is read by the drawing
	//   side on background threads (the asynchronous PDF export), so it may not be seen
	//   half-written.
	{
		KCMMarkStateLock lock(KCMMarkStateMutex());
		gMarkDocs.swap(incoming);			// from here on, `incoming` is what was up until now
		gMarkSpan = span;
		gHasMark = !gMarkDocs.empty();
	}

	// @warning **REPAINTING HAPPENS OUTSIDE THE LOCK.** InvalidateViews walks the document's
	//   windows and is free to take locks of its own; doing that while holding this one is the
	//   shape a deadlock comes in (KCMThreadSafety.h: "Do not hold the lock through a long operation"). Reading
	//   gMarkDocs here needs none: this thread is the only one that writes it.
	// @warning the document is asked whether it is still open FIRST, because this runs while one is
	//   being closed as well, and redrawing a database that is going away is a crash. The question
	//   compares pointers, so it is safe to ask of a dead one.
	std::set<IDataBase*> toRepaint;
	for (KCMStoryMarkDocs::const_iterator it = incoming.begin(); it != incoming.end(); ++it)
		toRepaint.insert(it->first);
	for (KCMStoryMarkDocs::const_iterator it = gMarkDocs.begin(); it != gMarkDocs.end(); ++it)
		toRepaint.insert(it->first);

	for (std::set<IDataBase*>::const_iterator db = toRepaint.begin(); db != toRepaint.end(); ++db)
	{
		if (KCMIsDocDBOpen(*db))
			KCMInvalidateDB(*db);
	}
}

/* KCMStoryMarkerDropClosed
   Take the documents that are no longer open out of one set. Pointer comparison only: the key of a
   dead entry is freed memory, and KCMIsDbAlive compares addresses without touching what they point at. */
void KCMStoryMarkerDropClosed(IDocumentList* docList, KCMStoryMarkDocs& docs)
{
	for (KCMStoryMarkDocs::iterator it = docs.begin(); it != docs.end(); )
	{
		if (KCMIsDbAlive(docList, it->first))
			++it;
		else
			it = docs.erase(it);
	}
}

/** One place to fill, and what to fill it with (the Track mode colours each author). */
struct KCMMarkBox
{
	PMRect					fBox;		// what is filled; with fQuads, what they reach - the ink bounds read it either way
	KCMMarkColour			fColour;
	K2Vector<PMPolygon4>	fQuads;		// text on a path only: what is filled instead, a quadrilateral a character
};

/* KCMStoryMarkerBarOf
   The caret's bar on a character of text on a path: as tall as the character's quadrilateral, leaning the way it leans, standing
   at the edge the caret is at and running on in the reading direction - what a bar is on a straight line, turned to the path.

   The corners run with the text whichever way up it stands: 0 start and top, 1 end and top, 2 end and bottom, 3 start and bottom
   (measured on the lower half of an oval, where the text reads upside down).

   @param quad the character's quadrilateral, from IWaxRunShape::GetWaxBounds.
   @param after the far edge of the character rather than the near one.
   @param width how thick, along the text.
*/
PMPolygon4 KCMStoryMarkerBarOf(const PMPolygon4& quad, bool after, const PMReal& width)
{
	const PMPoint top = quad[after ? 1 : 0];
	const PMPoint bottom = quad[after ? 2 : 3];

	// A character with no width has no top edge to read the direction off: square to its side instead.
	double dx = ::ToDouble(quad[1].X() - quad[0].X());
	double dy = ::ToDouble(quad[1].Y() - quad[0].Y());
	if (dx == 0.0 && dy == 0.0)
	{
		dx = ::ToDouble(quad[3].Y() - quad[0].Y());
		dy = ::ToDouble(quad[0].X() - quad[3].X());
	}
	const double length = std::sqrt(dx * dx + dy * dy);
	const double scale = (length > 0.0) ? ::ToDouble(width) / length : 0.0;
	const PMReal alongX = dx * scale;
	const PMReal alongY = dy * scale;

	PMPolygon4 bar;
	bar[0] = top;
	bar[1] = PMPoint(top.X() + alongX, top.Y() + alongY);
	bar[2] = PMPoint(bottom.X() + alongX, bottom.Y() + alongY);
	bar[3] = bottom;
	return bar;
}

/* KCMStoryMarkerBoxOnPath
   One marked range of a run of text on a path.

   **A STRAIGHT BOX CANNOT DO IT, AND NOT BECAUSE IT IS STRAIGHT.** Such a run's position stands at the path's own origin and each
   glyph carries its own matrix, so the x and y the rest of this file works from are not where the characters are: the box landed
   clear of the words (measured: the box at 0,-17 to 294,0, the words at 149,300). IWaxRunShape answers in the coordinates the run
   draws in, one quadrilateral a character (IWaxRunShape.h: "Used mostly for selection and highlighting").

   @param waxRun a run IsTOPRun answers kTrue for.
   @param charStart, charCount the characters, as offsets into the run.
   @param isCaret, caretAfter what stands there: the bar, at which edge of the character; or, not a caret, the wash.
   @param barWidth how thick a bar is.
   @param box [out] its quadrilaterals and what they reach.
   @return kFalse when the run gives no shape for them, and there is nothing to draw.
*/
bool16 KCMStoryMarkerBoxOnPath(const IWaxRun* waxRun, int32 charStart, int32 charCount, bool16 isCaret, bool caretAfter,
							   const PMReal& barWidth, KCMMarkBox& box)
{
	InterfacePtr<const IWaxRunShape> shape(waxRun, UseDefaultIID());
	if (shape == nil)
		return kFalse;

	K2Vector<PMPolygon4> quads;
	shape->GetWaxBounds(charStart, charCount, &quads);
	if (quads.empty())
		return kFalse;

	if (isCaret)
	{
		const PMPolygon4 bar = KCMStoryMarkerBarOf(caretAfter ? quads.back() : quads.front(), caretAfter, barWidth);
		quads.clear();
		quads.push_back(bar);
	}

	box.fBox = quads.front().GetContainingRect();
	for (K2Vector<PMPolygon4>::const_iterator q = quads.begin(); q != quads.end(); ++q)
		box.fBox.Union(q->GetContainingRect());
	box.fQuads.swap(quads);
	return kTrue;
}

/* KCMStoryMarkerDrawIsOutput
   Is this drawing the document's output - paper, a PDF, a picture of the page, a simulation of the sheet - rather than a window?
   Output takes the marks only with "Print comparison marks" on; a window takes them as the toggles say (spec map SMK-18, SMK-19).

   **kPreviewMode ALONE DOES NOT SAY.** The screen's Preview, Bleed and Slug modes set it, and those are still a window: Always Show
   holds there, as it does for the Pixel mode's frames, whose test this is (KCMDrawEventHandler::DrawSpreadMarks: printing, isThumb,
   oppHides). A picture of a page sets it with no view, and an overprint preview sets it over a simulation of the printed sheet, where
   what will not print is not shown (spec map MK-15).
*/
bool16 KCMStoryMarkerDrawIsOutput(GraphicsData* gd, int32 iShapeFlags)
{
	if ((iShapeFlags & IShape::kPrinting) != 0 || KCMIsOverprintPreview(gd))
		return kTrue;
	return ((iShapeFlags & IShape::kPreviewMode) != 0 && gd->GetView() == nil) ? kTrue : kFalse;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

/** Lays a coloured wash under the marked characters: in a window as the toggles say, on paper and in an exported
	PDF when "Print comparison marks" is on (KCMStoryMarkPrintAllowedFor). GetDrawPriority and Draw record why it is
	not an inversion and where it has to sit; read them before changing the look. */
class KCMStoryMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KCMStoryMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}

	/** BACKGROUND, like a highlight: where the product puts its H&J and missing-glyph highlights, and a wash the
		glyphs sit on top of is the one thing that reads the same on screen and on paper (Draw has the measurement).

		The named constants for Adobe's own global adornments are in IGlobalTextAdornment.h; the
		non-global ones (underline, strikethrough, paragraph shade, ruby, kenten -- and the text
		itself) are at the foot of ITextAdornment.h.

		@warning **THE NUMBER HAS TO BE NEGATIVE.** `kTAPassPriBackground + 0.50` is the right pass by name and the
		 wrong one by arithmetic:
		  * a priority is split in two, **the whole part being the PASS and the fraction the RUN**
		    within it (TextDrawPriority.h says so at the head of the namespace), and the wax runs are
		    walked once per distinct pass;
		  * kPassBackground is **-16384** (DrawPassInfo.h), and GetPass() cuts the fraction off towards
		    zero (TextDrawPriority.h). **Every one of Adobe's own background adornments is written as
		    kTAPassPriBackground + a NEGATIVE fraction** (-0.62 to -0.49), which stays in pass -16384; a POSITIVE one
		    (-16383.50) is in pass -16383, a whole pass AFTER all of them.
		 The wash being opaque (no transparency, by design -- see Draw), `+ 0.50` paints over paragraph shading,
		   paragraph borders and rules, underlines, and the product's own missing-font / missing-glyph
		   / kinsoku / H&J highlights, in exactly the passage the reader was told to look at.
		-0.585 puts it between kTAPriParagraphRuleBelow (-0.59) and kTAPriUnderline (-0.58): after the
		  paragraph-level grounds, so the wash covers a paragraph shade the way a highlighter does, and
		  before everything drawn ON the characters, so nothing the product draws is hidden.
		Any negative pass is still below the glyphs, which are drawn at kTAPassPriText + 0.50 (pass 0). */
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriBackground + -0.585); }

	virtual bool16 GetCheckIsActive() { return kTrue; }
	virtual bool16 GetIsActive(const IParcelShape* parcelShape,
							   const ITextOptions* textOptions,
							   int32 iShapeFlags);

	/** kTrue, WHERE spellpanel ANSWERS kFalse: it draws on every run, and this draws on a handful of
		characters in one story. Answering the per-run question is what stops the text engine calling
		Draw for every run in the document while a mark is up -- the header's own example is the H&J
		adornment declining runs whose line holds no violation.
		@warning **it may also be what keeps this plug-in out of the reader's documents.** Measured:
		 the adornments answering for every run (kFalse here, ink bounds on every run) had their
		 ClassID recorded in the documents they saw, so a reader without the plug-in would get a
		 Missing Plug-in warning; this one, answering per run, in none. Which of the two answers does
		 it was not measured. */
	virtual bool16 GetCheckCouldDraw() { return kTrue; }
	virtual bool16 GetCouldDraw(const IWaxRun* waxRun, const IWaxRenderData*, const IWaxGlyphs*)
	{
		return KCMStoryMarkerRunIsMarked(waxRun);
	}

	/** The header excuses only an adornment that stays inside the run's text bounds and the line's
		vertical selection bounds. This one promises neither: a bar after its character (kCaretAfter) stands past the
		end of the run, and the reach from ascent to descent comes from the type size, not from the
		line. So the bounds are declared. */
	virtual bool16 GetHasInkBounds() { return kTrue; }
	virtual void GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
							  const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs);

	virtual void Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
					  const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs);

	virtual void StartOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
	virtual void EndOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}

private:
	/** What to wash, in the coordinates the run draws in - one box per marked range that falls in this run.
		None for a run with nothing marked, and for a run that cannot be measured - an inline graphic has
		neither glyphs nor render data, as IGlobalTextAdornment.h warns of each method that takes them. */
	static std::vector<KCMMarkBox> GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
												const IWaxGlyphs* waxGlyphs, bool16 forPrint);
};

CREATE_PMINTERFACE(KCMStoryMarkerAdornment, kKCMStoryMarkerAdornmentImpl)

std::vector<KCMMarkBox> KCMStoryMarkerAdornment::GetMarkBoxes(const IWaxRun* waxRun, const IWaxRenderData* renderData,
																 const IWaxGlyphs* waxGlyphs, bool16 forPrint)
{
	std::vector<KCMMarkBox> boxes;

	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return boxes;

	KCMMarkRangeList runRanges;
	if (!KCMStoryMarkerFindRunRanges(waxRun, forPrint, runRanges))
		return boxes;

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphCount <= 0)
		return boxes;

	const PMReal y = waxRun->GetYPosition();				// the baseline
	const PMReal size = renderData->GetFontMatrix().GetYScale();
	const bool16 onPath = waxRun->IsTOPRun();

	for (KCMMarkRangeList::const_iterator r = runRanges.begin(); r != runRanges.end(); ++r)
	{
		const int32 charStart = r->fFrom;
		const int32 charCount = r->fTo - r->fFrom;

		// **CHARACTERS ARE NOT GLYPHS.** One character can be drawn by several glyphs and several
		//   characters by one, so the range has to be mapped before any width is added up -- the same
		//   call the product's spelling squiggle makes before underlining a word
		//   (DynamicSpellCheckAdornment.cpp, in its own drawing loop).
		int32 glyphIndex = -1;
		int32 glyphLength = 0;
		int32 shapeStart = charStart;		// the characters a shape is asked for on a path
		int32 shapeCount = charCount;
		bool caretAfter = (r->fShape == KCMMarkRange::kCaretAfter);
		waxGlyphs->MapCharsToGlyphs(charStart, charCount, &glyphIndex, &glyphLength);
		const auto mapsToNothing = [&]() { return glyphIndex < 0 || glyphLength <= 0 || glyphIndex >= glyphCount; };

		// **A CARET WHOSE CHARACTER DRAWS NOTHING STANDS AFTER THE ONE BEFORE IT.** A deletion at the end of a
		//   PARAGRAPH puts the caret in front of that paragraph's return, and a return maps to no glyph. The
		//   character before it is in this same run (a return ends the run it is in), and the end of that character
		//   is the very place the reader would click. The end of the STORY is the same case decided one step
		//   earlier (KCMStoryMarker::CaretAt), because there the caret's own character may not reach any run at all.
		if (r->IsCaret() && mapsToNothing() && charStart > 0)
		{
			waxGlyphs->MapCharsToGlyphs(charStart - 1, 1, &glyphIndex, &glyphLength);
			caretAfter = true;
			shapeStart = charStart - 1;
			shapeCount = 1;
		}

		if (mapsToNothing())
			continue;						// a range that maps to nothing skips this box, not the run
		if (glyphIndex + glyphLength > glyphCount)
			glyphLength = glyphCount - glyphIndex;

		if (onPath)
		{
			KCMMarkBox box;
			box.fColour = r->fColour;
			if (KCMStoryMarkerBoxOnPath(waxRun, shapeStart, shapeCount, r->IsCaret(), caretAfter, size * PMReal(kCaretWidthFraction), box))
				boxes.push_back(box);
			continue;
		}

		// Where the range starts and how wide it is: an escapement is the distance from the start of
		//   the run THROUGH a glyph (IWaxGlyphs.h).
		// @warning not from GetGlyphDrawPosition: a draw position is a MATRIX -- it carries the glyph's
		//   own transform, and reading a translation out of it is only the origin of that one glyph,
		//   not the end of the range.
		PMReal offset = (glyphIndex > 0) ? waxGlyphs->GetEscapementAt(glyphIndex - 1) : PMReal(0.0);
		PMReal width = waxGlyphs->GetEscapementAt(glyphIndex + glyphLength - 1) - offset;

		// **A CARET IS A BAR, NOT A MARKED CHARACTER** (KCMStoryMarkRanges.h, at kCaretBefore). The range covers one
		//   character so that it sorts and merges like any other, but what is DRAWN is a bar standing where the caret
		//   would stand if you clicked in front of that character -- the same place the jump centres.
		// kCaretAfter stands at the far edge of its character instead (the end of the story, in front of a table, or a
		//   character whose successor draws nothing - see above).
		if (caretAfter)
			offset += width;

		// A ZERO-WIDTH RANGE STILL HAS A PLACE. It happens where the marked characters are drawn by
		//   nothing at all -- and the reader still asked "where is it". A thin bar at the start of the
		//   range answers that; an empty rectangle would answer nothing.
		if (r->IsCaret() || width <= 0.0)
			width = size * PMReal(kCaretWidthFraction);

		const PMReal x = waxRun->GetXPosition() + offset;
		KCMMarkBox box;
		box.fBox = PMRect(x, y - size * PMReal(kAscentFraction), x + width, y + size * PMReal(kDescentFraction));
		box.fColour = r->fColour;
		boxes.push_back(box);
	}

	return boxes;
}

bool16 KCMStoryMarkerAdornment::GetIsActive(const IParcelShape* /*parcelShape*/,
											  const ITextOptions* /*textOptions*/,
											  int32 iShapeFlags)
{
	if (!gHasMark)
		return kFalse;

	// **ON PAPER WHEN THE READER SAYS SO.** A reader who turns "Print comparison marks" on means it: the answer is per
	//   document, as the Pixel mode's frames are (KCMStoryMarkBuild).
	// @warning **kPreviewMode IS LEFT TO Draw.** The flag is set for the screen's preview modes as well as for
	//   a page's picture, and this pass has no GraphicsData to tell them apart (KCMStoryMarkerDrawIsOutput).
	//   The header's "an adornment that does not draw when printing should not draw for this either" is about
	//   marks that never print; these print when the reader says so, and in a window the toggles decide.
	// @warning **the answer here is coarser than the one Draw gives, and it has to be.**
	//   This pass cannot name the document it is about: the header says of this very parameter
	//   "the parcel this text is in. **It MAY NOT be a UID based object**"
	//   (IGlobalTextAdornment.h, at GetIsActive), so GetDataBase has nothing to answer with even
	//   when it can be called. So refuse the whole pass only when NOTHING is printable; let Draw
	//   decide per run, where the run's own database is at hand. The cost of being generous is a
	//   Draw call that draws nothing; being strict here is not possible.
	//   (The call itself compiles: IParcelShape is an IPMUnknown. It is the UID that may not be there.)
	if (iShapeFlags & IShape::kPrinting)
		return KCMDrawEventHandler::sPrintMarks;

	return kTrue;
}

void KCMStoryMarkerAdornment::GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
											 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	// @warning kFalse: ink bounds are declared with no iShapeFlags to consult, so they are declared
	//   for the wider case. Over-declaring costs nothing (it only widens the rectangle the text
	//   engine will let us paint in); under-declaring would clip a mark that IS printable.
	const std::vector<KCMMarkBox> boxes = GetMarkBoxes(waxRun, renderData, waxGlyphs, kFalse);
	if (boxes.empty())
		return;								// leave them empty, as the header instructs

	// THE UNION OF THEM ALL, because ink bounds are declared once for the whole run. A press can
	//   mark two separate edits inside one run, and a bound that covered only the first would clip
	//   the second away.
	PMRect all = boxes.front().fBox;
	for (size_t i = 1; i < boxes.size(); ++i)
		all.Union(boxes[i].fBox);

	*inkBounds = all;
}

void KCMStoryMarkerAdornment::Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
									 const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
{
	if (gd == nil)
		return;

	// **NOTHING GOES INTO KCM'S OWN PICTURES**, the rule of the Pixel mode's marks (DrawSpreadMarks) and for its
	//   reasons. Such a picture asks as a picture of the page, and with Print on it would be allowed (measured): a
	//   Pixel comparison started from the Story mode finds the wash as a change on a page where nothing shows (a space
	//   at a story's end), the Alt read answers the wash's colour, and the Shift peek lays the older words over with
	//   their wash.
	if (KCMDrawEventHandler::tl_Rasterizing.Get())
		return;

	// THE PRINT DECISION IS MADE PER RUN, INSIDE GetMarkBoxes, because that is where the run's own
	//   database has just been worked out. GetIsActive answered the same question for the parcel,
	//   but Draw is reached by paths that do not consult it, so the flags are read again here.
	// @warning the flags are not a refusal. They select WHICH question is asked: in a window every marked run draws;
	//   in the output only what "Print comparison marks" allows (KCMStoryMarkerDrawIsOutput).
	const bool16 forPrint = KCMStoryMarkerDrawIsOutput(gd, iShapeFlags);

	const std::vector<KCMMarkBox> boxes = GetMarkBoxes(waxRun, renderData, waxGlyphs, forPrint);
	if (boxes.empty())
		return;

	IGraphicsPort* gPort = gd->GetGraphicsPort();
	if (gPort == nil)
		return;

	AutoGSave autoGSave(gPort);

	// **A COLOURED WASH BEHIND THE TEXT, NOT AN INVERSION** (after measuring the whole road).
	//   Difference blending is right for the screen: it shows through any ground and leaves the glyphs
	//   readable, because they invert along with the paper instead of being covered by it. It cannot survive
	//   being printed, and the reason is neither the blend nor the colour space:
	//   **In an exported or printed page the TEXT IS DRAWN LAST.** A text adornment painting in the
	//     foreground pass lands UNDER the glyphs out there, so the ground inverts and the glyphs do
	//     not -- measured: ground 255 -> 6, glyph core 0 -> 0. Black on black. The words that changed
	//     become the only words that cannot be read, which is the opposite of the point.
	//   @warning moving the paint to the background pass does not help: the glyphs are on top of it either way. Nor
	//     does painting in CMYK's maximum (0,0,0,1) instead of RGB's white -- that corrects WHICH grounds invert, and
	//     never touches the glyphs. Both were built and measured.
	//   So paint a wash BEHIND the text and let the glyphs keep their own colour. That is what the
	//     product's own H&J highlight does, and it is the one drawing that reads the same on screen
	//     and on paper.
	// NO TRANSPARENCY IS USED -- the strength is mixed into the colour rather than asked for with
	//   setopacity. Three problems leave together: the flattener has nothing to flatten, the frame no
	//   longer has to declare transparency for the mark to survive, and PDF 1.3 behaves exactly like
	//   1.4.
	// HOW STRONG -- the panel's "Marks opacity 25% / 75%", mixed from paper white towards the mark
	//   colour, for both kinds of mark. Read here, at the moment of drawing, so that the radio cannot
	//   reach one kind and miss the other.
	// @warning **not at full strength on paper.** The strength IS the colour, and at 1.0 a wash prints as solid red
	//   over every changed word -- readable, but shouting. What the reader picked for the screen is what comes out of
	//   the printer, the promise the Pixel mode's frames make (KCMDrawEventHandler::SelectedMarkOpacity serves screen
	//   and output alike).
	//   @warning a PNG export counts as PRINTING (kPrinting is set): a shot taken with exportFile is not the screen.
	const int32 mix = ::ToInt32(KCMDrawEventHandler::SelectedMarkOpacity() * PMReal(100.0));

	// WHICH COLOUR -- the panel's "Mark colour: Red / Cyan", read through the same accessor the
	//   Pixel mode's rings use, so the two modes can never disagree about it - UNLESS THE BOX HAS A
	//   COLOUR OF ITS OWN (the Track mode: the author's tracked-change colour). Every box of
	//   the other modes has none.
	KCMMarkColour panelColour;
	KCMDrawEventHandler::SelectedMarkColor(panelColour.fR, panelColour.fG, panelColour.fB);

	// ONE FILL PER BOX, AND THEY CANNOT OVERLAP -- the ranges were merged before they were ever
	//   installed (KCMStoryMarkRanges.h; ranges of two colours are cut, never laid over each other).
	// SCREEN IN RGB, PAPER IN CMYK -- the same helper the Pixel mode's frames call, for the same
	//   reason: KCM compares in CMYK, so a mark specified in RGB does not match its own frames on
	//   output.
	const auto washed = [mix](uint8 channel) { return static_cast<uint8>(255 - (255 - channel) * mix / 100); };
	for (std::vector<KCMMarkBox>::const_iterator b = boxes.begin(); b != boxes.end(); ++b)
	{
		const KCMMarkColour& base = b->fColour.fHasOwn ? b->fColour : panelColour;
		KCMSetOutputColor(gPort, washed(base.fR), washed(base.fG), washed(base.fB), forPrint);
		if (b->fQuads.empty())
		{
			gPort->rectfill(b->fBox);
			continue;
		}
		// Text on a path: a quadrilateral a character. They overlap where the path bends; the wash is opaque, so that shows nothing.
		for (K2Vector<PMPolygon4>::const_iterator q = b->fQuads.begin(); q != b->fQuads.end(); ++q)
		{
			gPort->newpath();
			gPort->moveto((*q)[0].X(), (*q)[0].Y());
			gPort->lineto((*q)[1].X(), (*q)[1].Y());
			gPort->lineto((*q)[2].X(), (*q)[2].Y());
			gPort->lineto((*q)[3].X(), (*q)[3].Y());
			gPort->closepath();
			gPort->fill();
		}
	}
}

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

KCMMarkRange KCMStoryMarker::CaretAt(IDataBase* db, UID storyUID, TextIndex at)
{
	if (at > 0)
	{
		InterfacePtr<ITextModel> model(UIDRef(db, storyUID), UseDefaultIID());
		if (model != nil && at >= model->TotalLength() - 1)
			return KCMMarkRange::CaretAfter(at);
	}

	TextIndex afterTable = 0;
	if (KCMCaretOnTableChars(db, storyUID, at, afterTable))
		return KCMMarkRange::CaretAfter(afterTable);

	return KCMMarkRange::Caret(at);
}

void KCMStoryMarker::AddFlashRange(KCMStoryMarkDocs& docs, IDataBase* db, UID storyUID,
									TextIndex from, TextIndex to, const KCMMarkColour& colour)
{
	if (db == nil || storyUID == kInvalidUID)
		return;			// a window that is not open, or a story there is none of - nothing to add

	// **A DELETION HAS NO WIDTH on the side it was deleted from** -- the words are gone from there,
	//   and what the jump is pointing at is the PLACE they used to be.
	//   The decision is made HERE and not inside the range list: what a zero-width range should
	//     look like is about what the reader is being shown, and that list is numbers
	//     (KCMStoryMarkRanges.h).
	// **AND AN INSERTION IS THE SAME THING SEEN FROM THE OLDER SIDE.** The characters exist only in
	//   the newer document, so the range handed over for the older one is empty and comes out as the
	//   caret standing where they went in -- which is exactly where the reader is looking. Nothing
	//   here has to know which of the two cases it is.
	const KCMMarkRange range = (to > from) ? KCMMarkRange(from, to) : CaretAt(db, storyUID, from);
	docs[db][storyUID].push_back(range.WithColour(colour));
}

void KCMStoryMarker::ShowFlash(KCMStoryMarkDocs docs)
{
	if (gShutdown)
		return;

	// "A STANDING MARK WINS" IS NOT TESTED HERE: it is per document, once, in the composition (KCMComposeMarkDocs). An
	//   early return here would silence the flash in the other document too (KCMStoryMarkDocs.h).
	gFlashDocs.swap(docs);
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

void KCMStoryMarker::ShowStanding(KCMStoryMarkDocs docs)
{
	if (gShutdown)
		return;

	// NO COUNTDOWN. What takes these down is a toggle going off or the mouse button coming up, not
	//   the clock -- and a clock that a jump has running belongs to the jump, so this leaves it
	//   alone (KCMStoryMarkerInstall).
	gStandingDocs.swap(docs);
	KCMStoryMarkerInstall();
}

void KCMStoryMarker::ClearFlash()
{
	if (gFlashDocs.empty())
		return;

	gFlashDocs.clear();
	KCMStoryMarkerInstall();		// repaints whatever went dark
}

void KCMStoryMarker::ForgetClosedDocs(IDocumentList* docList)
{
	if (docList == nil)
		return;

	// All three sets lose the same documents, so the drawn map stays what composing the two others
	//   would give (KCMComposeMarkDocs decides each document from its own entries alone) - which is
	//   also why no window has anything to repaint. The span the fast path tests may now be wider
	//   than what is left, which costs a run one more question and nothing else.
	KCMMarkStateLock lock(KCMMarkStateMutex());
	KCMStoryMarkerDropClosed(docList, gStandingDocs);
	KCMStoryMarkerDropClosed(docList, gFlashDocs);
	KCMStoryMarkerDropClosed(docList, gMarkDocs);
	gHasMark = !gMarkDocs.empty();
}

void KCMStoryMarker::Shutdown()
{
	// @warning the flag goes up FIRST: from here on nothing repaints, because the document the mark
	//   was in may already be half torn down. Taking a mark down the ordinary way would go looking
	//   for it.
	{
		// @warning **LOCKED LIKE EVERY OTHER WRITE.** Teardown is exactly when a background export may
		//   still be walking gMarkDocs, and clearing a map out from under a reader is the crash this
		//   lock exists to prevent (KCMThreadSafety.h).
		KCMMarkStateLock lock(KCMMarkStateMutex());
		gShutdown = kTrue;
		gHasMark = false;
		gMarkDocs.clear();
		gStandingDocs.clear();
		gFlashDocs.clear();
	}
	KCMStoryMarkerExpiry::Shutdown();		// releases an idle task - outside the lock
}

// End, KCMStoryMarker.cpp.
