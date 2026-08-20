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
bool16     gHasMark     = kFalse;
IDataBase* gMarkDB      = nil;		// an ADDRESS, only ever compared against the run's own database
UID        gMarkStory   = kInvalidUID;
TextIndex  gMarkFrom    = 0;
TextIndex  gMarkTo      = 0;
bool16     gShutdown    = kFalse;

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

/* KESCMStoryMarkerRunIsMarked
   Does this wax run carry any of the marked characters?

   ★TWO QUESTIONS, AND THE CHEAP ONE FIRST. Character ranges are integers and are compared first;
   only a run that overlaps is worth asking which story it belongs to, which costs a Query.
*/
bool16 KESCMStoryMarkerRunIsMarked(const IWaxRun* waxRun, int32& outCharStart, int32& outCharCount)
{
	outCharStart = 0;
	outCharCount = 0;

	if (!gHasMark || waxRun == nil)
		return kFalse;

	const TextIndex runStart = waxRun->TextOrigin();
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return kFalse;

	const TextIndex runEnd = runStart + runCount;
	if (runEnd <= gMarkFrom || runStart >= gMarkTo)
		return kFalse;						// the run is entirely before or after the mark

	// Which story. ⚠A document can be open twice over (target and source), and both are being drawn
	//   in their own windows, so "the right characters" is not enough - it has to be the right
	//   story in the right database.
	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return kFalse;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return kFalse;
	const UIDRef modelRef = ::GetUIDRef(model);
	if (modelRef.GetDataBase() != gMarkDB || modelRef.GetUID() != gMarkStory)
		return kFalse;

	// The overlap, expressed as an offset into this run.
	const TextIndex from = (gMarkFrom > runStart) ? gMarkFrom : runStart;
	const TextIndex to = (gMarkTo < runEnd) ? gMarkTo : runEnd;
	outCharStart = static_cast<int32>(from - runStart);
	outCharCount = static_cast<int32>(to - from);
	return (outCharCount > 0);
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
		int32 charStart = 0, charCount = 0;
		return KESCMStoryMarkerRunIsMarked(waxRun, charStart, charCount);
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
	/** The rectangle to invert, in the coordinates the run reports its own position in.
		kFalse for a run that cannot be measured - an inline graphic has neither glyphs nor render
		data, which all four of these methods are warned about in IGlobalTextAdornment.h. */
	static bool16 GetMarkBox(const IWaxRun* waxRun, const IWaxRenderData* renderData,
							 const IWaxGlyphs* waxGlyphs, PMRect* outBox);
};

CREATE_PMINTERFACE(KESCMStoryMarkerAdornment, kKESCMStoryMarkerAdornmentImpl)

bool16 KESCMStoryMarkerAdornment::GetMarkBox(const IWaxRun* waxRun, const IWaxRenderData* renderData,
											 const IWaxGlyphs* waxGlyphs, PMRect* outBox)
{
	if (waxRun == nil || renderData == nil || waxGlyphs == nil || outBox == nil)
		return kFalse;

	int32 charStart = 0, charCount = 0;
	if (!KESCMStoryMarkerRunIsMarked(waxRun, charStart, charCount))
		return kFalse;

	// ★CHARACTERS ARE NOT GLYPHS. One character can be drawn by several glyphs and several
	//   characters by one, so the range has to be mapped before any width is added up - the same
	//   call the product's spelling squiggle makes before underlining a word
	//   (DynamicSpellCheckAdornment.cpp:793).
	int32 glyphIndex = -1;
	int32 glyphLength = 0;
	waxGlyphs->MapCharsToGlyphs(charStart, charCount, &glyphIndex, &glyphLength);
	if (glyphIndex < 0 || glyphLength <= 0)
		return kFalse;

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphIndex >= glyphCount)
		return kFalse;
	if (glyphIndex + glyphLength > glyphCount)
		glyphLength = glyphCount - glyphIndex;

	// Where the range starts and how wide it is, by adding up the glyph widths the run itself
	// reports. ⚠Added up rather than taken from GetGlyphDrawPosition because a draw position is a
	//   MATRIX - it carries the glyph's own transform, and reading a translation out of it is only
	//   the origin of that one glyph, not the end of the range.
	PMReal offset(0.0);
	for (int32 i = 0; i < glyphIndex; ++i)
		offset += waxGlyphs->GetWidthAt(i);

	PMReal width(0.0);
	for (int32 i = glyphIndex; i < glyphIndex + glyphLength; ++i)
		width += waxGlyphs->GetWidthAt(i);

	if (width <= 0.0)
	{
		// ★A ZERO-WIDTH RANGE STILL HAS A PLACE. It happens where the marked characters are drawn
		//   by nothing at all - and the reader still asked "where is it". A thin bar at the start
		//   of the range answers that; an empty rectangle would answer nothing.
		width = renderData->GetFontMatrix().GetYScale() * PMReal(0.15);
	}

	const PMReal x = waxRun->GetXPosition() + offset;
	const PMReal y = waxRun->GetYPosition();				// the baseline
	const PMReal size = renderData->GetFontMatrix().GetYScale();

	outBox->Left(x);
	outBox->Right(x + width);
	outBox->Top(y - size * PMReal(kAscentFraction));
	outBox->Bottom(y + size * PMReal(kDescentFraction));
	return kTrue;
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
	PMRect box;
	if (!GetMarkBox(waxRun, renderData, waxGlyphs, &box))
		return;								// leave them empty, as the header instructs

	*inkBounds = box;
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

	PMRect box;
	if (!GetMarkBox(waxRun, renderData, waxGlyphs, &box))
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
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	gPort->rectfill(box.Left(), box.Top(), box.Width(), box.Height());
	gPort->newpath();
}

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

void KESCMStoryMarker::Show(IDataBase* db, UID storyUID, TextIndex from, TextIndex to)
{
	if (gShutdown)
		return;

	// The old mark's document has to be repainted too when the jump crossed documents, or the
	// previous mark would stay on screen in a window nobody is looking at any more.
	IDataBase* const previousDB = gHasMark ? gMarkDB : nil;

	if (db == nil || storyUID == kInvalidUID)
	{
		KESCMStoryMarker::Clear();
		return;
	}

	if (to < from)
		to = from;

	gMarkDB = db;
	gMarkStory = storyUID;
	gMarkFrom = from;
	// ★A DELETION HAS NO WIDTH HERE - the words are gone from this side, and the row is pointing at
	//   the place they used to be. One character is what makes that place visible; zero would mark
	//   nothing at all. (The jump's selection has the same problem and answers it the same way, with
	//   a leaning caret - see KESCMStoryJumpToChange.)
	gMarkTo = (to > from) ? to : (from + 1);
	gHasMark = kTrue;

	if (previousDB != nil && previousDB != db)
		KESCMStoryMarkerRepaint(previousDB);
	KESCMStoryMarkerRepaint(db);

	// A flash, not a highlight. Restarting an already-running countdown is that call's job, so each
	// jump gets the mark for the full time.
	KESCMStoryMarkerExpiry::Start();
}

void KESCMStoryMarker::Clear()
{
	KESCMStoryMarkerExpiry::Stop();

	if (!gHasMark)
		return;

	IDataBase* const db = gMarkDB;
	gHasMark = kFalse;
	gMarkDB = nil;
	gMarkStory = kInvalidUID;
	gMarkFrom = 0;
	gMarkTo = 0;

	// ⚠The flag is down BEFORE the repaint, so the redraw it asks for is the one that takes the
	//   mark off. Repainting first would draw it again.
	KESCMStoryMarkerRepaint(db);
}

bool16 KESCMStoryMarker::IsShowing()
{
	return gHasMark;
}

void KESCMStoryMarker::Shutdown()
{
	// ⚠The flag goes up FIRST: from here on nothing repaints, because the document the mark was in
	//   may already be half torn down. Clear() would otherwise go looking for it.
	//   ★Same door, and the same reason, as KBS's marker shutdown.
	gShutdown = kTrue;
	gHasMark = kFalse;
	gMarkDB = nil;
	gMarkStory = kInvalidUID;
	KESCMStoryMarkerExpiry::Shutdown();
}

// End, KESCMStoryMarker.cpp.
