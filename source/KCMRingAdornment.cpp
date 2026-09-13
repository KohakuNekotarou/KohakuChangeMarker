//========================================================================================
//
//  KCMRingAdornment.cpp
//
//  Draws the comparison marks as a global page item adornment. What it is for and how it works
//  is in KCMRingAdornment.h; this is the substance:
//
//    1) KCMRingAdornmentShape  ... IAdornmentShape. Calls the drawing, and only for a spread
//    2) KCMRingFlattenerUsage  ... IAdornmentFlattenerUsage. The point of the exercise: the
//                                  declaration to the transparency manager
//    3) register / unregister  ... putting it on and off the session's global list
//
//  **This is the only route that draws the marks** - the draw-event receiver
//  (kKCMDrawEventServiceBoss / KCMDrawEventSrvc / HandleDrawEvent) was removed.
//  It holds not one line of the drawing itself: it calls KCMDrawEventHandler::DrawSpreadMarks()
//  as it stands, which covers everything (rings, slashes, ticks, old-folio badges, the excluded
//  wash, Find Overset's "+", and the alpha-server path used for print and PDF). All this route
//  adds is who does the calling, and the transparency declaration.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAdornmentShape.h"
#include "IAdornmentFlattenerUsage.h"
#include "IPageItemAdornmentList.h"		// GenericID.h comes along with it, so IID_IGLOBALPAGEITEMADORNMENTLIST needs no other include
#include "ISession.h"					// GetExecutionContextSession()
#include "ISpread.h"					// "is the thing being drawn a spread"
#include "IShape.h"
#include "IDrwEvtHandler.h"				// DrawEventData, the shape the drawing takes its input in
#include "IGraphicsContext.h"			// GraphicsData
#include "IStartupShutdownService.h"	// per-execution-context registration (end of this file)
#include "IXPUtils.h"					// QueryXPManager(db)
#include "IXPManager.h"					// ItemXPChanged - makes the item-has-transparency list be rebuilt
#include "ISpreadList.h"				// walking the document's spreads
#include "IDataBase.h"					// GetRootUID
// The service that joins the transparency list for the duration of a PDF export - and joins the
// CLONE rather than the document itself (end of this file).
#include "IPDFExportSetupProvider.h"	// PDFProcessEvent (pulls in IPDFExportController.h = PDFExportEvent)
#include "IDThreading.h"				// IDThreading::ThreadLocal, holding the db being exported
// The story ID labels ("Show Story IDs", section 1.5):
#include "IStoryList.h"					// the document's stories - where the labels START (the user's instruction: from the story to its holder)
#include "ITextModel.h"					// QueryFrameList
#include "IFrameList.h"					// the story's columns
#include "IHierarchy.h"					// two steps up from a column: the multi-column frame, then the item that holds the story
#include "ITOPSplineData.h"				// text on a path: from the text-on-path spline item to the main spline the reader sees
#include "IGeometry.h"					// the item's own box, the label's place
#include "IGraphicsPort.h"				// selectfont / show / the stroke settings of the outline
#include "GraphicTypes.h"				// kPMRoundJoin / kPMRoundCap
#include "IControlView.h"				// GetContentToWindowMatrix - the zoom, for a label of fixed pixel size
#include "IViewPortAttributes.h"		// is this draw an overprint preview (the label follows the marks' rule there)
#include "OutPrvID.h"					// kSepPrvOPPEnabledVPAttr
#include "IPMFont.h"
#include "AutoGSave.h"
#include "TransformUtils.h"				// ::InnerToSpreadMatrix - the label is written in spread coordinates, upright
#include "PersistUtils.h"				// ::GetDataBase / ::GetUID off the shape being drawn
#include "PMString.h"
#include "PMMatrix.h"
#include "PMReal.h"						// abs(PMReal)

// General includes:
#include "CPMUnknown.h"
#include "UIDList.h"
#include "Utils.h"
#include <algorithm>					// std::find - one story once per holder
#include <map>
#include <vector>

// Project includes:
#include "KCMID.h"
#include "KCMConstants.h"				// kKCMStoryId* - the labels' sizes and reach
#include "KCMRingAdornment.h"
#include "KCMDrawEventHandler.h"		// DrawSpreadMarks (the drawing) / the mark-state statics / KCMQueryMarkFont
#include "KCMThreadSafety.h"			// KCMMarkStateMutex/Lock, taken to read sEntries and the story holder tables

//========================================================================================
// Registration state
//========================================================================================

/** Whether the marks **could** be translucent: the settings, and whether there are any marks.
	It does not look at whether an output is being made.
	**This is what decides whether to join the transparency list**, because at the moment of
	joining the output has not started yet. */
static bool16 KCMMarksCouldBeTranslucent();

/** **Whether the translucency of the marks lands in the output being made right now** - the
	answer `IsFlattenerRequired_` gives. That is the test above plus "is an export running".

	@warning **including "is an export running" is the essential part**: without it the document
	  can never be taken off the list again. `kXPC_RemovedSomeXP` does not mean "remove it", it
	  means "**ask again**", so for as long as this function answers kTrue the XPManager leaves
	  the entry where it is (measured: joining and leaving were both being called correctly, yet
	  the count stayed at `xp 0->4`).
	  **Answering "there is no transparency" once the output is over** is what lets it leave.
	Answering kFalse for screen drawing and for thumbnails is correct: the flattener does not run
	there, so there is nobody to ask. */
static bool16 KCMMarksDeclareTransparency();

//========================================================================================
// 1.5) The story ID labels ("Show Story IDs", 2026-09-13)
//
//  "StoryID:<the story's UID>" ABOVE every page item that carries a story - outside the item, on
//  its top edge at the left (the user's asks, after seeing a first version inside the frame: "the
//  story's number alone, outside, above, so it never lies over the text", then "StoryID:" in
//  front of it). Two stories on one item are two such labels on the one line.
//  **Found from the story, not from the item's kind** (the user's instruction: "trace from the
//  story and put the story's ID on its parent"): the document's stories -> each story's frame
//  columns -> two steps up the hierarchy -> the item. A text frame is
//  kSplineItemBoss -> kMultiColumnItemBoss -> kFrameItemBoss and text on a path is
//  kTOPSplineItemBoss -> kMulticolumnItemBoss -> kTOPFrameItemBoss (ITOPSplineData.h:52, "the
//  grand children of the main spline item"), so **the same two steps reach both**, and an anchored
//  frame stops at its own spline rather than climbing on to the frame that holds it. Text on a
//  path needs one more link, from kTOPSplineItemBoss to the main spline (KCMRebuildStoryHolders).
//
//  The table (item UID -> story UIDs) is one per database, rebuilt on every draw of a spread and
//  read on every draw of an item, so the labels follow a frame that was threaded or added a
//  moment ago. The first item drawn before any spread builds it on the spot. A background export
//  is handed a clone with a pointer of its own, so it gets a table of its own; the tables are
//  under the mark-state lock like everything else the export thread reads.
//  @warning **a table is keyed by a pointer, and a pointer outlives what it pointed at.** A
//    document closed and another opened may put the new one at the old address, and its first
//    items would then be looked up in the old document's table (the same trap as
//    [[uidref-reuse-after-close]]). So the tables are dropped when documents close
//    (KCMHandleDocsClosed -> KCMForgetStoryHolders) and when an export begins on a clone
//    (KCMBeginExportOn), and the toggle going off drops them all.
//
//  Screen: always while the toggle is on, at a fixed number of pixels whatever the zoom -
//  except in overprint preview with the marks not printing, where nothing of KCM's is shown
//  (2026-09-07, DrawSpreadMarks: the preview is a picture of the sheet).
//  Print and PDF: only with "Print comparison marks" on (the old-folio badge's rule), at a fixed
//  size in points. The Before/After report takes them on both of its sides whenever the toggle
//  is on (the user's call), which it signals with sReportExport (KCMReport.cpp) - its After side
//  is exported with sPrintMarks off, so that side would otherwise have none.
//  Thumbnails (no view, not printing): nothing - unreadable at that size.
//  **Written in spread coordinates**, upright: the port is in the item's inner coordinates when
//  an item's adornment is drawn, and a vertical text frame's inner axes stand at 90 degrees to
//  the page ([[vertical-frame-inner-axes-rotated]]), so a label written in inner coordinates would
//  lie on its side there. Concatenating spread->inner puts the label above the item's
//  spread-aligned box, the way the reader sees the frame.
//  **The label lies outside the item's box, so the adornment widens the item's painted bounds**
//  (GetPaintedAdornmentBounds): what InDesign repaints when the item moves or is redrawn is
//  computed from those bounds, and a label outside them would be left behind on the screen.
//  Opaque black with a white outline (the paw's note's two passes), so no transparency group and
//  nothing for the flattener to be told.
//========================================================================================

static bool16 sShowStoryIds = kFalse;	// default off (the flyout's "Show Story IDs")

/** item UID -> the UIDs of the stories it holds. */
typedef std::map<UID, std::vector<UID> > KCMStoryHolderTable;
/** One table per database. Read and written under KCMMarkStateMutex (recursive). */
static std::map<IDataBase*, KCMStoryHolderTable> sStoryHolders;

bool16 KCMGetShowStoryIds()
{
	return sShowStoryIds;
}

void KCMForgetStoryHolders(IDataBase* db)
{
	KCMMarkStateLock lock(KCMMarkStateMutex());
	if (db == nil)
		sStoryHolders.clear();
	else
		sStoryHolders.erase(db);
}

void KCMSetShowStoryIds(bool16 on)
{
	sShowStoryIds = on;
	if (!on)
		KCMForgetStoryHolders(nil);	// nothing reads the tables while the toggle is off; drop them (the warning in the header)
}

/** Rebuild db's table from its stories. **From the story side** (the header of this section). */
static void KCMRebuildStoryHolders(IDataBase* db)
{
	if (db == nil)
		return;

	KCMStoryHolderTable table;
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories != nil)
	{
		// User-accessible stories only, the same choice KCMStoryStamp makes: an internal story
		// (a placeholder's, an index's) has no frame the reader can point at.
		const int32 storyCount = stories->GetUserAccessibleStoryCount();
		for (int32 i = 0; i < storyCount; ++i)
		{
			const UIDRef storyRef = stories->GetNthUserAccessibleStoryUID(i);
			InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
			if (model == nil)
				continue;
			InterfacePtr<IFrameList> frames(model->QueryFrameList());
			if (frames == nil)
				continue;
			const int32 frameCount = frames->GetFrameCount();
			for (int32 f = 0; f < frameCount; ++f)
			{
				// column -> multi-column frame -> the item (two steps, both kinds of text; the
				// header of this section). kInvalidUID from GetParentUID is "no answer".
				InterfacePtr<IHierarchy> column(db, frames->GetNthFrameUID(f), UseDefaultIID());
				if (column == nil)
					continue;
				InterfacePtr<IHierarchy> multiColumn(column->QueryParent());
				if (multiColumn == nil)
					continue;
				UID holder = multiColumn->GetParentUID();
				if (holder == kInvalidUID)
					continue;
				// Text on a path: the two steps land on kTOPSplineItemBoss, which is not the item
				// the reader sees and whose adornments are never drawn (measured 2026-09-13: the
				// oval got no label). The item that IS drawn is its main spline, reached by the
				// link ITOPSplineData keeps rather than by the hierarchy (ITOPSplineData.h:45-48).
				// A frame's own text never comes this way (only kTOPSplineItemBoss carries the
				// interface), so an ordinary frame keeps the holder the two steps found.
				InterfacePtr<ITOPSplineData> textOnPath(db, holder, UseDefaultIID());
				if (textOnPath != nil && textOnPath->GetMainSplineItemUID() != kInvalidUID)
					holder = textOnPath->GetMainSplineItemUID();
				std::vector<UID>& held = table[holder];
				// A story threaded through several columns of one frame is still one story.
				if (std::find(held.begin(), held.end(), storyRef.GetUID()) == held.end())
					held.push_back(storyRef.GetUID());
			}
		}
	}

	KCMMarkStateLock lock(KCMMarkStateMutex());
	sStoryHolders[db].swap(table);
}

/** The stories iShape's item holds, copied out from under the lock; empty when it holds none
	(the ordinary answer for most items). With `buildIfMissing`, a database with no table yet (the
	first item drawn before any spread was) gets one here - the mutex is recursive, so the
	rebuild's own lock nests. The painted-bounds question passes kFalse: it is asked at
	invalidation time, and a table missing there is only ever the one the next draw is about to
	build. */
static void KCMStoriesHeldBy(IShape* iShape, bool16 buildIfMissing, std::vector<UID>& outStoryUIDs)
{
	outStoryUIDs.clear();
	IDataBase* const db = ::GetDataBase(iShape);
	const UID itemUID = ::GetUID(iShape);
	if (db == nil || itemUID == kInvalidUID)
		return;

	KCMMarkStateLock lock(KCMMarkStateMutex());
	std::map<IDataBase*, KCMStoryHolderTable>::const_iterator t = sStoryHolders.find(db);
	if (t == sStoryHolders.end())
	{
		if (!buildIfMissing)
			return;
		KCMRebuildStoryHolders(db);
		t = sStoryHolders.find(db);
		if (t == sStoryHolders.end())
			return;
	}
	KCMStoryHolderTable::const_iterator h = t->second.find(itemUID);
	if (h != t->second.end())
		outStoryUIDs = h->second;
}

/** The label of one item, if it holds a story. Called for every item the global list reaches. */
static void KCMDrawStoryIdLabel(IShape* iShape, GraphicsData* gd, int32 flags)
{
	if (!sShowStoryIds)
		return;
	// Print and PDF only with "Print comparison marks" on (kPrinting is set for both:
	// DrawSpreadMarks) - or when the export is the Before/After report's, on either side (the
	// user's call: with the toggle on, the report carries the numbers too; sReportExport is up
	// for the length of its two exports, KCMReport.cpp).
	const bool16 printing = (flags & IShape::kPrinting) != 0;
	if (printing && !KCMDrawEventHandler::sPrintMarks && !KCMDrawEventHandler::sReportExport)
		return;
	// Without a view and not printing this is a thumbnail, or some other viewless draw - nothing
	// is readable at that size, and there is no zoom to size the text by.
	IControlView* const view = gd->GetView();
	if (!printing && view == nil)
		return;
	// Overprint preview shows what will be printed, so only what would be printed is shown
	// (2026-09-07, the user's decision, applied to every mark in DrawSpreadMarks): with the marks
	// not printing, no label either. The viewport attribute is exact; kPreviewMode is not (it
	// shares its bit with PDF export).
	if (!printing)
	{
		IViewPortAttributes* const vpa = gd->GetViewPortAttributes();
		if (vpa != nil && vpa->GetAttr(kSepPrvOPPEnabledVPAttr, 0) != 0 && !KCMDrawEventHandler::sPrintMarks)
			return;
	}

	std::vector<UID> storyUIDs;
	KCMStoriesHeldBy(iShape, kTrue, storyUIDs);
	if (storyUIDs.empty())
		return;

	IPMFont* const font = KCMQueryMarkFont();	// nil on a background thread before the main thread fetched it: no label then (the badge's caveat)
	IGraphicsPort* const gPort = gd->GetGraphicsPort();
	if (font == nil || gPort == nil)
		return;
	InterfacePtr<IGeometry> geometry(iShape, UseDefaultIID());
	if (geometry == nil)
		return;

	// The size: a fixed number of screen pixels on screen (points = pixels / the view's scale,
	// the way the rings pick their radius), a fixed number of points in print and PDF.
	PMReal size = kKCMStoryIdPrintPt;
	if (!printing)
	{
		const PMReal sxr = abs(view->GetContentToWindowMatrix().GetXScale());	// can be negative (PMReal's abs)
		if (sxr <= PMReal(0.0))
			return;
		size = kKCMStoryIdFontPx / sxr;
	}
	const PMReal pad = size * kKCMStoryIdPadRatio;

	// The item's box in spread coordinates, and the port turned to spread coordinates to match
	// (the header of this section: upright whatever the frame's own axes). concat is a
	// pre-concatenation, so what is handed to it maps the coordinates written AFTER it (spread)
	// into the ones the port was in (inner).
	const PMMatrix innerToSpread = ::InnerToSpreadMatrix(geometry);
	if (innerToSpread.IsSingular())
		return;
	PMRect box = geometry->GetPathBoundingBox();
	innerToSpread.Transform(&box);		// the smallest spread-aligned rectangle around the item (PMMatrix.h:176)

	// One line: "StoryID:<uid>", a space between two. Neither the prefix nor a digit has a
	// descender that matters, so the baseline can sit the gap above the item's top edge and the
	// whole label is outside the box.
	PMString text;
	text.SetTranslatable(kFalse);
	for (size_t i = 0; i < storyUIDs.size(); ++i)
	{
		if (i > 0)
			text.Append(" ");
		text.Append(kKCMStoryIdPrefix);
		text.AppendNumber((int32)storyUIDs[i].Get());
	}
	int32 n16 = 0;
	const UTF16TextChar* const buf16 = text.GrabUTF16Buffer(&n16);
	if (buf16 == nil || n16 <= 0)
		return;
	const PMReal x = box.Left();
	const PMReal y = box.Top() - pad;

	AutoGSave ag(gPort);
	gPort->concat(innerToSpread.Inverse());
	gPort->selectfont(font, size);

	// The white outline first, the black digits over it. A round join and cap, or the outline
	// grows horns at a glyph's sharp corners (KCMDrawPawWord). Two show calls, never one with
	// the flags combined (the same, measured). Print and PDF are CMYK.
	KCMSetOutputColor(gPort, 255, 255, 255, printing);
	gPort->setlinejoin(kPMRoundJoin);
	gPort->setlinecap(kPMRoundCap);
	gPort->setlinewidth(size * kKCMStoryIdHaloRatio);
	gPort->show(x, y, (uint32)n16, buf16, IGraphicsPort::kStrokeText);
	KCMSetOutputColor(gPort, 0, 0, 0, printing);
	gPort->show(x, y, (uint32)n16, buf16, IGraphicsPort::kFillText);
}

//========================================================================================
// 1) The adornment itself
//========================================================================================

/** Calls the drawing of the comparison marks, and only for a spread. */
class KCMRingAdornmentShape : public CPMUnknown<IAdornmentShape>
{
public:
	KCMRingAdornmentShape(IPMUnknown* boss) : CPMUnknown<IAdornmentShape>(boss) {}
	~KCMRingAdornmentShape() {}

	/** @warning **it is never called at all unless this is a single bit** (measured). Returning
		`kBeforeShape | kAfterShape` gives zero calls: the distribution is done by
		IAdornmentIterator(paintOrderMask) (CShape.cpp:127) and a combined value matches no pass.
		The symptom is "nothing appears on screen", which is indistinguishable from a fault in the
		drawing code. Leave this alone.
		The marks go on top of the content, hence kAfterShape (after the shape is drawn). */
	virtual AdornmentDrawOrder GetDrawOrderBits() { return kAfterShape; }

	virtual void DrawAdornment(IShape* iShape, AdornmentDrawOrder drawOrder,
							   GraphicsData* gd, int32 flags);

	/** `itemBounds` is **a copy of the rectangle the caller has already transformed**, and the
		return value is Union'd into it (CShape::UnionPageItemAdornmentPaintedBBox). So **returning
		it untouched is the right answer** where nothing sticks out, and the marks are drawn inside
		the page or the spread.
		@warning applying `innertoview` here transforms it a second time. framelabel's
		  GetPaintedAdornmentBounds does exactly that (the other way round, with the inverse).
		  **The authority is the calling code** - the side that does the Union. */
	virtual PMRect GetPaintedAdornmentBounds(IShape* iShape, AdornmentDrawOrder /*drawOrder*/,
											 const PMRect& itemBounds, const PMMatrix& /*innertoview*/);

	/** The same contract. This one is only reached when WillPrint() is kTrue (CShape.cpp:93). */
	virtual PMRect GetPrintedAdornmentBounds(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/,
											 const PMRect& itemBounds, const PMMatrix& /*innertoview*/)
		{ return itemBounds; }

	/** An empty implementation is the correct one. **The contract exempts it explicitly** -
		IAdornmentShape.h:138-140: "This is only used by adornments for which the inking bounds are
		based on **the content**. Adornments for which inking bboxes are based **solely on the
		frame** do not need to implement this routine."
		The marks are drawn against the box of a page or a spread (GetPaintedAdornmentBounds returns
		itemBounds untouched), so they are frame-based, the exempt side. The SDK agrees: only
		transparencyeffect, which bleeds outside the frame, implements it, while
		framelabel/FrmLblAdornment.cpp:160 is an empty `{}`. It is not a flattener interface either -
		TranFxAdornment.cpp:483 comments it as **"used for resizing textframe etc."**

		@warning it was **settled by measurement as well**. "Could declaring ink bounds stand in for
		  the `ItemXPChanged` notification?" was A/B'd, this being the one interface spellpanel had
		  and KCM did not. Same document, same preset (the magazine ad delivery one = PDF 1.3), same
		  script (work/kescm-adorn/isolate-doc.ps1):

		| | pixels on the changed page |
		|---|---|
		| notification (= what is here) | **`red 0` / pale red 40,847** (translucent) |
		| notification off, ink bounds declared | **`red 862,283`** (solid) |

		**Ink bounds play no part in the flattener's decision. `ItemXPChanged` cannot be replaced.** */
	virtual void AddToContentInkBounds(IShape* /*iShape*/, PMRect* /*inOutBounds*/) {}

	virtual PMReal GetPriority() { return 0; }

	/** Invalidation is done where it always was, by the mark side (KCMInvalidate...) across the
		document's views, so there is nothing to do here. */
	virtual void Inval(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/, GraphicsData* /*gd*/,
					   ClassID /*reasonForInval*/, int32 /*flags*/) {}

	/** It has to be kTrue. @warning **not so that it gets drawn, but so that it counts towards the
		printed bbox**: DrawPageItemAdornments does not gate the drawing on this value (in a DEBUG
		build an assert reads it, and nothing else - CShape.cpp:117-143), while
		UnionPrintingPageItemAdornmentPaintedBBox does (CShape.cpp:93). kFalse drops the adornment
		out of the drawing area computed for print and export.
		Whether the marks go into print at all is still decided by the drawing itself, from
		sPrintMarks. Not here. */
	virtual bool16 WillPrint() { return kTrue; }

	/** Whether to interrupt offscreen text drawing (for adornments that draw behind the text).
		The marks go on top, so kFalse. */
	virtual bool16 WillDraw(IShape* /*iShape*/, AdornmentDrawOrder /*drawOrder*/,
							GraphicsData* /*gd*/, int32 /*flags*/) { return kFalse; }

	/** The marks take no clicks; they are there to be looked at. */
	virtual bool16 HitTest(IShape* /*iShape*/, AdornmentDrawOrder /*adornmentDrawOrder*/,
						   IControlView* /*layoutView*/, const PMRect& /*mouseRect*/) { return kFalse; }
};

CREATE_PMINTERFACE(KCMRingAdornmentShape, kKCMRingAdornmentImpl)

PMRect KCMRingAdornmentShape::GetPaintedAdornmentBounds(IShape* iShape, AdornmentDrawOrder /*drawOrder*/,
														const PMRect& itemBounds, const PMMatrix& /*innertoview*/)
{
	// The marks are drawn inside the page or the spread, so for them the untouched itemBounds is
	// the right answer (the declaration). **The story ID label is the exception**: it is written
	// ABOVE the item's box (section 1.5), so the item's painted bounds are widened by the band the
	// label occupies - upwards, and to the right for an item narrower than the label. Without
	// this, moving the item leaves its label behind on the screen, and a repaint of the region
	// above it alone does not reach the item and loses the label there. itemBounds is in the
	// view's pixels (the caller has transformed it), which is why the band is in pixels too.
	// Asked for every item on every invalidation, so the answer for an item holding no story is
	// found in the table without building one (KCMStoriesHeldBy, kFalse).
	if (!sShowStoryIds || iShape == nil)
		return itemBounds;
	std::vector<UID> storyUIDs;
	KCMStoriesHeldBy(iShape, kFalse, storyUIDs);
	if (storyUIDs.empty())
		return itemBounds;
	PMRect widened = itemBounds;
	widened.Top(widened.Top() - kKCMStoryIdBandAbovePx);
	if (widened.Right() < widened.Left() + kKCMStoryIdReachRightPx)
		widened.Right(widened.Left() + kKCMStoryIdReachRightPx);
	return widened;
}

void KCMRingAdornmentShape::DrawAdornment(IShape* iShape, AdornmentDrawOrder drawOrder,
											GraphicsData* gd, int32 flags)
{
	if (drawOrder != kAfterShape || iShape == nil || gd == nil)
		return;

	// **Being on the global list, this is reached once for every single page item on the spread.**
	//   The marks are drawn once per spread, so everything that is not a spread is dropped here -
	//   after the one thing that IS drawn per item, the story UID label (section 1.5; it answers
	//   at once for an item that holds no story, which is most of them).
	// @warning **the reason for taking the spread (kSpreadBoss) rather than the page (kPageBoss) is
	//   the coordinate system**: the drawing is written to take a page's box in spread coordinates
	//   and draw it, and a spread's inner coordinates are spread coordinates, so it can be handed
	//   over as it is. Take the page and everything is out by the page's offset.
	InterfacePtr<ISpread> spread(iShape, UseDefaultIID());
	if (spread == nil)
	{
		KCMDrawStoryIdLabel(iShape, gd, flags);
		return;
	}

	// A spread's draw is where the story holder table of its document is brought up to date
	// (section 1.5): the items drawn after this read it.
	if (sShowStoryIds)
		KCMRebuildStoryHolders(::GetDataBase(iShape));

	// Build the shape the drawing takes its input in. changedBy is the iShape as it stands: the
	// drawing reads an ISpread and an IDataBase off changedBy and nothing else, and both come off
	// the same boss (iShape and spread are one boss).
	DrawEventData ded(iShape, gd, flags);
	KCMDrawEventHandler::DrawSpreadMarks(&ded);
}

//========================================================================================
// 2) The point of it all -- the declaration to the transparency manager
//========================================================================================

/** Answers "this adornment is using transparency". Modelled on
	`sdksamples/transparencyeffect/TranFxFlattenerUsage.cpp`, whose body is a bare kTrue.

	**This is the key to the solid block at PDF 1.3.** 1.3 has no transparency, so the only way
	to anything translucent is through the flattener, and the flattener **gathers artwork and
	then rasterises it** - so the only candidates are the ones that declared transparency while
	being gathered. A draw event is called during the drawing that follows the gathering and so
	never had a chance to declare anything, which is the whole reason this class was added.

	@warning **there are two declarations and they are different interfaces** (different IIDs):
	  - `IFlattenerUsage` (`IsFlattenerRequired`)           ... for **page items**. Not one
	                                                            implementation exists in the SDK
	  - `IAdornmentFlattenerUsage` (`IsFlattenerRequired_`) ... for **adornments** <- this one
	The trailing underscore is the tell. Pick the wrong one and nobody Queries it, silently.

	**A/B measured**, same document, same preset (the magazine ad delivery one = Acrobat 4), on
	**a page containing no transparency at all**:

	| declaration | what the page looks like | dominant colour |
	|---|---|---|
	| **made (= what is here)** | **the ring is translucent** (74,503 px) | `240,192,176` = 25% red on white |
	| not made | **the whole page is a solid red block** (850,175 px) | `224,0,16` = nearly pure red |

	Synchronous (main thread) and asynchronous (background, the UI's export) gave the same
	numbers, so **the declaration is Queried on both threads**: a boss registered globally is
	asked as well.
	**Making the marks an adornment is not what fixes this; the declaration is.** The adornment
	is needed because **the interface to declare through (`IID_IADORNMENTFLATTENERUSAGE`) goes on
	no other kind of boss**, not because it changes the drawing. */
class KCMRingFlattenerUsage : public CPMUnknown<IAdornmentFlattenerUsage>
{
public:
	KCMRingFlattenerUsage(IPMUnknown* boss) : CPMUnknown<IAdornmentFlattenerUsage>(boss) {}
	~KCMRingFlattenerUsage() {}

	virtual bool32 IsFlattenerRequired_(IPMUnknown* iThing, const PMMatrix* masterSpread2LayoutSpreadMatrix,
										int32 nFlags);
};

CREATE_PMINTERFACE(KCMRingFlattenerUsage, kKCMRingFlattenerUsageImpl)

bool32 KCMRingFlattenerUsage::IsFlattenerRequired_(IPMUnknown* /*iThing*/,
													 const PMMatrix* /*masterSpread2LayoutSpreadMatrix*/,
													 int32 /*nFlags*/)
{
	// **Never return kTrue unconditionally.** A page that declares transparency is rasterised by
	//   the flattener and passes through a CMYK/blend space conversion, which **dulls its colours**
	//   (measured: RGB(255,0,0) -> (230,0,20)). Dragging in the cases where no mark is drawn changes
	//   how the document looks while nothing has been drawn on it.
	//   **Declare only when a translucent mark is really going into the output.**
	//   TranFxFlattenerUsage.cpp:79-83 writes down this very design decision: if adding and removing
	//   the adornment is what adds and removes the transparency, a constant kTrue is fine; if a
	//   setting can make the effect disappear, read that setting and answer from it. This is the
	//   second case - a toggle makes it disappear.

	return KCMMarksDeclareTransparency();
}

static bool16 KCMMarksCouldBeTranslucent()
{
	// If the marks are set not to go into print or export, no transparency arises in the output.
	// ★**ONE SWITCH DECIDES IT: "Print comparison marks"** (sPrintMarks). Since 2026-09-07 the
	//   Source side follows it too (spec map MK-14), so the Source toggle no longer has to be
	//   consulted here -- with sPrintMarks off, neither side reaches the output.
	if (!KCMDrawEventHandler::sPrintMarks)
		return kFalse;

	// No mark to draw means no transparency either.
	// @warning sEntries is written by the main thread and read by a background thread, so the lock
	//   is taken even to read it (the discipline is in KCMThreadSafety.h; the mutex is recursive,
	//   so nesting does not deadlock).
	KCMMarkStateLock lock(KCMMarkStateMutex());
	return !KCMDrawEventHandler::sEntries.empty();
}

//========================================================================================
// 3) On and off the session's global list
//========================================================================================

/** The session's global page item adornment list, or nil.

	@warning **there is no `IGlobalPageItemAdornmentList.h`.** The interface is the ordinary
	  IPageItemAdornmentList, and **taking it off the session under a different IID** is the whole
	  of it (kSessionBoss / IID_IGLOBALPAGEITEMADORNMENTLIST / kGlobalPageItemAdornmentListImpl).
	  Written out at both call sites, that is the kind of thing only one of the two copies ends up
	  saying.
	@warning the session itself is nil while the application is shutting down, which is an ordinary
	  outcome here rather than a failure -- Unregister is called from exactly that moment. */
static IPageItemAdornmentList* KCMQueryGlobalAdornmentList()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	return (IPageItemAdornmentList*)session->QueryInterface(IID_IGLOBALPAGEITEMADORNMENTLIST);
}

void KCMRingAdornmentRegister()
{
	// @warning **do not remember "already registered" in a static and return early.** **This
	//   function has to be called once per execution context** - the main thread and the background
	//   threads. A static gate means only the first call, on the main thread, registers anything and
	//   the background threads walk straight past. Registering twice is prevented by HasAdornment
	//   below, so that is the only guard needed.
	//
	// **Why a static is the wrong place for it.** One line of guide vol1-07 explains both halves -
	//   "Threads do not share object-model instances. **They do share globals and statics**":
	//     (1) **the first half**: what is registered is **an interface instance on the session**, so
	//         what AddAdornment did on the main thread **is not visible from a background thread's
	//         execution context**. Without registering again there, nothing calls DrawAdornment.
	//     (2) **the second half**: remembering "it is registered" in a static makes it **read kTrue
	//         on the background thread too**, and the draw-event route that existed then stood down,
	//         believing the adornment would draw.
	//   **So neither of them drew.** The symptom was "the PDF from the UI's File > Export, and only
	//     that one, has no frames at all" (measured at PDF 1.4: synchronous 77,240 px,
	//     **asynchronous 0**).
	//   Generally: **an arrangement of "one of us takes this" held in a static turns into "neither of
	//     us takes it" the moment it crosses a thread.** Ask the place where the arrangement actually
	//     lives - here, the session.
	//   The draw-event route has since been removed and **this is the only route that draws**, so the
	//     standing down in (2) no longer exists. (1) is unchanged: **registration does not cross
	//     threads.**

	InterfacePtr<IPageItemAdornmentList> globalList(KCMQueryGlobalAdornmentList());
	if (globalList == nil)
		return;

	// The second argument kFalse means "do not dirty the document". The global list lives on the
	//   session, so the document's data is not touched at all and nothing persists into the .indd.
	if (!globalList->HasAdornment(kKCMRingAdornmentBoss))
		globalList->AddAdornment(kKCMRingAdornmentBoss, kFalse);
}

void KCMRingAdornmentUnregister()
{
	InterfacePtr<IPageItemAdornmentList> globalList(KCMQueryGlobalAdornmentList());
	if (globalList == nil)
		return;	// the session is gone: the application is shutting down, which is when this is called

	if (globalList->HasAdornment(kKCMRingAdornmentBoss))
		globalList->RemoveAdornment(kKCMRingAdornmentBoss, kFalse);
}

//========================================================================================
// 3.5) Telling the transparency manager to ask again -- the other half of the PDF 1.3 fix
//========================================================================================

/** Join the list, or leave it. **The direction is a parameter.**
	@warning it used to be one function serving both directions and **sending
	  `kXPC_MayHaveAddedSomeXP` for both** - and as the name says, that kind only works in the
	  direction of adding. So although the callers were told to "call these symmetrically" and did,
	  **the leaving side never once took effect** (A/B on the same document: `MayHaveAdded` gives
	  `1->1`, `RemovedSomeXP` gives `1->0`).
	**If one function serves both directions, take the direction as a parameter.** Symmetry in the
	calls does not guarantee symmetry in the meaning. */
enum KCMXPListAction
{
	kKCMXPListAdd,		///< transparency arises in the output - join the list (kXPC_AddedSomeXP)
	kKCMXPListRemove		///< it does not - leave the list (kXPC_RemovedSomeXP)
};

/** db's transparency manager, or nil. The two callers want different things when it cannot be had
	(one returns, one answers -1), so the nil is handed back rather than acted on here. */
static IXPManager* KCMQueryXPManagerFor(IDataBase* db)
{
	if (db == nil)
		return nil;
	Utils<IXPUtils> xpUtils;
	if (!xpUtils)
		return nil;
	return xpUtils->QueryXPManager(db);
}

static void KCMSetItemXPState(IDataBase* db, KCMXPListAction action)
{
	InterfacePtr<IXPManager> xpManager(KCMQueryXPManagerFor(db));
	if (xpManager == nil)
		return;

	InterfacePtr<ISpreadList> spreadList(db, db->GetRootUID(), UseDefaultIID());
	if (spreadList == nil)
		return;

	// **One item per spread is enough.** The list is the material for answering "are there items with
	//   transparency on this spread", and location plays no part in it (IXPManager.h:114-116: "Info
	//   is maintained solely on the presence of transparent items on spread, **not based on
	//   location**").
	// @warning **a spread holding no items at all cannot be put on the list**, so this does nothing
	//   for a document of empty pages: the adornment draws frames on an empty page, but the only
	//   thing that can answer for transparency is an item.
	UIDList items(db);
	const int32 spreadCount = spreadList->GetSpreadCount();
	for (int32 i = 0; i < spreadCount; ++i)
	{
		InterfacePtr<ISpread> spread(db, spreadList->GetNthSpreadUID(i), UseDefaultIID());
		if (spread == nil)
			continue;
		const int32 pageCount = spread->GetNumPages();
		for (int32 p = 0; p < pageCount; ++p)
		{
			UIDList onPage(db);
			spread->GetItemsOnPage(p, &onPage, kFalse /*bIncludePage - the page itself is not wanted*/);
			if (onPage.Length() > 0)
			{
				items.Append(onPage[0]);
				break;			// this spread has its representative
			}
		}
	}
	if (items.Length() == 0)
		return;

	// **The kind is chosen by the direction** (IXPManager.h:95-105).
	//   - joining = `kXPC_AddedSomeXP`    ... "some transparency was added". The same change type
	//                                         transparencyeffect passes (TranFxUtils.cpp:451-457,
	//                                         "update the item-has-xp list") - but not through the
	//                                         same call; see the second warning below.
	//   - leaving = `kXPC_RemovedSomeXP`  ... "some transparency was removed".
	//   @warning **`kXPC_MayHaveAddedSomeXP` is not an "either direction" kind.** Measured on one
	//     document: `MayHaveAdded` gives **1->1, it does not leave**; `RemovedSomeXP` gives **1->0,
	//     it leaves**. Reading the header's "will ask the item(s) for their new XP state, and if it
	//     changes, will update" on its own misleads - **as the name says, `MayHave**Added**` only
	//     works in the direction of adding.**
	//   Either way, what finally decides whether it joins or leaves is `IsFlattenerRequired_` (=
	//     `KCMMarksDeclareTransparency`): the XPManager asks the items again and an item asks its
	//     adornments. **So this does not amount to declaring a transparency that is not there.**
	// @warning **the command form (`ProcessItemXPChangedCmd` / `kXPItemXPPrePostCmdBoss`) is not
	//   used.** The SDK sample uses it because **adding and removing its adornment is itself a change
	//   to the document's data and has to be undoable**. KCM registers on the session side and changes
	//   not one byte of the document, so there is nothing to put on the undo stack - putting something
	//   there would break what Ctrl+Z means.
	//   @warning **which leaves this call without a precedent in the SDK.** `ItemXPChanged` is
	//     declared at IXPManager.h:106-107 and **called from nowhere in the shipped source**;
	//     `ProcessItemXPChangedCmd` (:110) is called from nowhere either (counted 2026-08-30 across
	//     source/: declarations and comments only). Every official path is the command boss, by way
	//     of TranFxUtils::Inval. **What is taken from transparencyeffect is the change type and the
	//     moment, not the call** - so this line is on the "no official road" side of the audit, and
	//     the reasoning above is all there is to stand on.
	//
	// @warning **this call dirties the document** (measured with the guard removed: one press of the
	//   flyout was enough for `modified=true`). **The list is data on the document side**, which is
	//   also why the official route is a command: there the dirtying goes with a real change to the
	//   document and is correct. **KCM has changed nothing, so it puts it back.**
	//   KCM does that with `IDataBase::SaveRestoreModifiedState` - clean going in, clean coming out.
	//   The same guard is in KCMCore.cpp, KCMPeek.cpp, KCMOversetScan.cpp and others.
	//   @warning **the guard only stops the document asking to be saved; it does not stop it being
	//     saved.** The update to the list is in the database, so a save made for any other reason
	//     writes it into the .indd along with everything else (measured: **it is still there after a
	//     reopen, and is not re-validated**).
	//     **Which is why this function is called only for the duration of an export** (section 5).
	{
		IDataBase::SaveRestoreModifiedState dirtyGuard(db);
		xpManager->ItemXPChanged(items,
								 (action == kKCMXPListAdd) ? IXPManager::kXPC_AddedSomeXP
															 : IXPManager::kXPC_RemovedSomeXP);
	}
}

//========================================================================================
// 4) Per-execution-context registration -- the startup/shutdown service
//========================================================================================

/** Registers on the startup of each execution context, and unregisters on its shutdown.

	**Why a service of its own is needed.**
	  A global **text** adornment needs none of this. It is declared in the .fr AddIn as
	  `IID_IK2SERVICEPROVIDER, kGlobalTextAdornmentServiceImpl` - spellpanel's dynamic spelling
	  squiggle is exactly that (SpellPanelClass.fr:773-774) - and there is not one line of
	  registration code, because **a service is resolved separately in every execution context and
	  so comes up on a background thread by itself.**

	  @warning **the page item side has no such door.** Of the services naming an adornment in
	  `kServiceIDSpace` there are only `kGlobalTextAdornmentService` and InCopy's two galley ones,
	  **all of them text**. What the page item side ships is `kGlobalPageItemAdornmentListImpl`,
	  the implementation of the list that sits on the session, and that is not a service provider.
	  **So this class does by hand what a service would have done for free.**

	@warning **it must not be folded into KCMPeekStartup.** That one is deliberately pinned to the
	  main thread in the .fr (kCMainThreadStartupShutdownProviderImpl), because its Shutdown()
	  throws away the whole comparison state: called on every background thread teardown, **the
	  marks vanish on every PDF export** (a bug that was hit and fixed). This one only registers
	  and unregisters, so it is safe on every thread - and **useless unless it runs on them**.
	  Hence a boss of its own. */
class KCMRingAdornmentStartup : public CPMUnknown<IStartupShutdownService>
{
public:
	KCMRingAdornmentStartup(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	~KCMRingAdornmentStartup() {}

	virtual void Startup()  { KCMRingAdornmentRegister(); }
	virtual void Shutdown() { KCMRingAdornmentUnregister(); }
};

CREATE_PMINTERFACE(KCMRingAdornmentStartup, kKCMRingAdornmentStartupImpl)

//========================================================================================
// 5) Joining the transparency list, for the duration of an export only
//
//  WHY ONLY FOR THE DURATION
//    `IXPManager`'s list is **data on the document, and it persists into the .indd** (measured: a
//    document that was compared and then saved still holds one entry when reopened, and **opening
//    it re-validates nothing**). Held for the length of a comparison, the moment the reader saves
//    for any reason at all **a record with nothing behind it is baked in** - and it stays there
//    for whoever opens that .indd without KCM. **So join when it is needed and leave afterwards.**
//    The flattener is only wanted **during an export**; screen drawing and thumbnails have no use
//    for the list, and that print has none either was settled by measurement (below).
//
//  MODELLED ON `customconditionaltext`, the one sample implementing "change it before, put it back
//  after" for both PDF and print
//    - PDF   ... CusCondTxtResponder::RespondExport (changes on Before, restores on After and on
//                **Failed**)
//    - print ... CusCondTxtPrintSetupProvider (BeforePrintGatherCmd -> EndPrint)
//
//  WHY THE PRINT SIDE IS **NOT** IMPLEMENTED
//    @warning **not because it has no effect. It has.** Join the list while printing and the
//    flattener runs there too, and the marks come out denser:
//
//      | joins the list when printing | coloured pixels on the changed pages (p2/p3) | how it looks |
//      |---|--:|---|
//      | yes (the shape the sample has; the implementation is in `bd44eec`) | **16,076 / 13,635** | the clear pale red seen on screen |
//      | no (= what is here) | **8,407 / 7,379** | much fainter, the way 1.5.0 looked with the draw-event route |
//
//      (A/B: the same document work/kescm-selftest/kescm-target.indd, which **holds no
//       transparency**; Microsoft Print to PDF; work/kescm-adorn/verify16-print.ps1. The PDFs are
//       92,702 against 153,221 bytes. **Neither of them is `red=0` solid** - the difference is
//       density, which is a different breakage from the solid block at PDF 1.3.)
//
//    **It was left out because print does not need that precision.** Print is not the final
//      output; **what goes to the printing company is the PDF**, so the strictness is wanted on
//      the export side only. To match the density in print as well, bring
//      `KCMPrintXPSetupProvider` back from `bd44eec` (together with `kKCMPrintXPSetupProviderBoss`
//      and `kKCMPrintXPSetupProviderImpl`).
//
//  WHY NOT AROUND THE SAVE
//    `kBeforeSaveDocSignalResponderService` would serve the same purpose, but **failing there costs
//    the reader their document** - InDesign goes down and they get a recovery. Failing an export
//    only costs them the export. **Put it where failing is allowed.**
//
//  WHY THE CLONE, AND NOT THE EXPORT SIGNALS
//    The earlier implementation used `kBeforeExport`/`kAfterExport`/`kFailedExport` and joined the
//    list on **the original document**. @warning **that had a hole**: the signals arrive **on the
//    main thread carrying the original db**, so there is a stretch of time in which the original is
//    on the list, and **a save made during an asynchronous (background) export bakes the list into
//    the .indd** (measured: save() during an export, and the reopened document reports
//    `document.kcmTransparencyItemCount` = **4**, where the normal path gives 0).
//    `kPDFExportSetupService`'s `BeginExport` is handed **the clone db the export draws from**, so
//    **the output can be changed without the original being touched once**. The details and the
//    measurements are in the implementation below and in
//    docs/ai-notes/pdf-export-setup-service-and-clone-db-2026-08-20.md.
//    What the earlier implementation established is still true and worth keeping:
//      - the export signals fire on **both** the synchronous and the asynchronous route, but
//        **both of them on the main thread, with the original `IDataBase`**
//      - @warning **the format name differs between the routes**: synchronous `Adobe PDF`,
//        asynchronous `Adobe PDF (Print)`. (That is why the sample lists three of them; look at
//        one and you miss the other route.)
//========================================================================================

// The db this execution context is exporting; nil when it is not exporting.
//
// **Being thread-local is the essence of the design.** Joining (`BeginExport`), being asked
//   (`IsFlattenerRequired_`) and leaving (`EndExport`) all happen on one thread - all on the
//   background thread when the export is asynchronous, all on the main thread when it is
//   synchronous - **so no mutex is needed**.
//   @warning **it must not be a static.** Guide vol1-07's "Threads do not share object-model
//     instances. **They do share globals and statics**" applies to it directly: a background
//     export would make the drawing on the main thread answer "an output is being made". The same
//     trap caught this code once already (the comment in Register).
// One thread never exports two documents at once, the events being the straight line
//   `BeginExport -> drawing -> EndExport`, so one of these is enough.
static IDThreading::ThreadLocal<IDataBase*> tl_ExportingDB(nil);


/** Leave the db being exported. Called from `EndExport`, and when a book export moves on to the
	next document. */
static void KCMEndExportOnThisThread()
{
	IDataBase* const db = tl_ExportingDB.Get();
	if (db == nil)
		return;

	// **Clear the flag before notifying.** `kXPC_RemovedSomeXP` does not mean "remove it", it means
	//   "**ask again**", so if `IsFlattenerRequired_` still answers "there is transparency" at this
	//   point **the entry does not leave the list** (measured: it stayed at `xp 0->4`).
	tl_ExportingDB.Set(nil);
	KCMSetItemXPState(db, kKCMXPListRemove);
}

/** An export has begun, or a book export has moved to another document. Join the list, but only
	where transparency is going to arise. */
static void KCMBeginExportOn(IDataBase* db)
{
	// A book's `NewDocument` comes here, so leave the previous chapter's db first.
	// @warning forget this and every chapter from the second on **leaves the previous chapter's db on
	//   the list**.
	KCMEndExportOnThisThread();

	if (db == nil)
		return;
	// The story ID labels' table of this db, if one is lying about from an earlier export whose
	// clone stood at the same address, is dropped here so the first page drawn does not read it
	// (section 1.5, the warning on pointers). The first item drawn builds a fresh one.
	KCMForgetStoryHolders(db);
	// Whether to join is decided by "could the marks be translucent".
	// @warning **not by `KCMMarksDeclareTransparency()`**: that one includes "is an export running",
	//   and since tl_ExportingDB is not set yet at this point it is always kFalse there, so **nothing
	//   would ever join**.
	if (!KCMMarksCouldBeTranslucent())
		return;

	// **Set the flag before notifying.** The notification KCMSetItemXPState() sends below makes the
	//   XPManager come back and ask `IsFlattenerRequired_`, and unless "an export is running" is true
	//   by then the answer is kFalse and nothing joins.
	tl_ExportingDB.Set(db);
	KCMSetItemXPState(db, kKCMXPListAdd);
}

// The answer `IsFlattenerRequired_` gives. Forward-declared at the head of this file, with the
// reasoning.
static bool16 KCMMarksDeclareTransparency()
{
	// **Outside an export, answer "there is no transparency".** The reason is at
	//   KCMEndExportOnThisThread() above.
	if (tl_ExportingDB.Get() == nil)
		return kFalse;

	return KCMMarksCouldBeTranslucent();
}

//----------------------------------------------------------------------------------------
// Joining **the clone only**, through kPDFExportSetupService
//
//  WHY THIS AND NOT THE EXPORT SIGNAL (kBeforeExport)
//    `kBeforeExport` arrives **on the main thread, with the original document's db**. Join there
//    and the original is on the list for a stretch of time, and **a save made in that window bakes
//    the list into the .indd** (measured: save() during an asynchronous export, and the reopened
//    document reports `kcmTransparencyItemCount` = **4**, where the normal path gives 0).
//    `kPDFExportSetupService`'s `BeginExport`, by contrast, is handed **the clone db of the
//    export** when the export is asynchronous (measured: a pointer different from both the
//    original Target and the original Source). **Join that and the original is never touched.**
//    **Joining the clone still reaches the output**: the translucency at PDF 1.3 came out not one
//    dot different from when the original was used. Full record in
//    docs/ai-notes/pdf-export-setup-service-and-clone-db-2026-08-20.md.
//    @warning **a synchronous export makes no clone** - the db is the original. There is nothing to
//    do there but join and leave, but a synchronous export blocks until it returns, so **the window
//    for a save does not exist**.
//
//  MODELLED ON `sdksamples/pdfvt` - PDFVTExportProvider::PDFProcessEvent and PDFVT.fr:113-121
//  @warning **the ServiceProvider side names Adobe's own `kPDFExportSetupServiceImpl` in the .fr**,
//    so this is the only implementation to write. (The service ID comes from that implementation,
//    so the .fr needs no ServiceID either.)
//----------------------------------------------------------------------------------------

class KCMPDFExportSetup : public CPMUnknown<IPDFExportSetupProvider>
{
public:
	KCMPDFExportSetup(IPMUnknown* boss) : CPMUnknown<IPDFExportSetupProvider>(boss) {}
	~KCMPDFExportSetup() {}

	virtual bool16 PDFProcessEvent(PDFExportEvent* pdfExportEvent, int32 pageNum);
};

CREATE_PMINTERFACE(KCMPDFExportSetup, kKCMPDFExportSetupImpl)

bool16 KCMPDFExportSetup::PDFProcessEvent(PDFExportEvent* ev, int32 /*pageNum*/)
{
	if (ev != nil)
	{
		switch (ev->id)
		{
			// The second case is a book export moving on to another document (measured with a
			// two-chapter book).
			// @warning without kPDFExportEventNewDocument, using only the db from BeginExport,
			//   **every chapter from the second on works against the wrong document**.
			case kPDFExportEventBeginExport:
			case kPDFExportEventNewDocument:
				KCMBeginExportOn(ev->db);
				break;

			// @warning **the db on this event is always nil** (measured on both routes), so what
			//   leaves the list is the one that was kept.
			case kPDFExportEventEndExport:
				KCMEndExportOnThisThread();
				break;

			default:
				break;
		}
	}

	// @warning **always return kTrue.** kFalse means "**skip the default processing for this event**"
	//   (IPDFExportSetupProvider.h:52-53), which breaks the export itself.
	return kTrue;
}

//----------------------------------------------------------------------------------------
// 5-2) Reading the count from outside -- document.kcmTransparencyItemCount
//----------------------------------------------------------------------------------------

/** How many items are on the item-has-transparency list. What it is for is at the declaration in
	KCMRingAdornment.h.

	**This one function is what makes the joining and leaving above checkable without counting
	pixels.** The list persists into the .indd, so reading `0 -> n -> 0` across an export says the
	leaving works, and **save -> close -> reopen -> 0 says nothing was written**.
	@warning **the dirty flag cannot stand in for it**: the `SaveRestoreModifiedState` guard only
	  stops the document asking to be saved, it does not stop it being saved. **What has to be
	  measured is the list itself.** */
int32 KCMGetNumItemsWithXP(IDataBase* db)
{
	InterfacePtr<IXPManager> xpManager(KCMQueryXPManagerFor(db));
	if (xpManager == nil)
		return -1;

	return xpManager->GetNumItemsWithXP();
}

/* KCMRevalidateItemXPList -- declared and explained in KCMRingAdornment.h. */
void KCMRevalidateItemXPList(IDataBase* db)
{
	if (db == nil || !KCMIsMainThread())
		return;
	// Mid-export on this thread the declarations standing are the right ones (KCMBeginExportOn put
	// them up for the flattener); asking again now would take them down under the export.
	if (tl_ExportingDB.Get() != nil)
		return;

	InterfacePtr<IXPManager> xpManager(KCMQueryXPManagerFor(db));
	if (xpManager == nil)
		return;

	// The list as it stands, read before anything is sent: ItemXPChanged rebuilds it, so reading
	// and sending cannot be interleaved.
	const int32 count = xpManager->GetNumItemsWithXP();
	if (count <= 0)
		return;
	UIDList items(db);
	for (int32 i = 0; i < count; ++i)
	{
		const UID uid = xpManager->GetNthItemWithXP(i);
		if (uid != kInvalidUID)
			items.Append(uid);
	}
	if (items.Length() == 0)
		return;

	{
		IDataBase::SaveRestoreModifiedState dirtyGuard(db);
		xpManager->ItemXPChanged(items, IXPManager::kXPC_RemovedSomeXP);	// "ask again", not "take off"
	}
}

// End, KCMRingAdornment.cpp.
