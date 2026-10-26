//========================================================================================
//
//  KCMPawTracker.cpp
//
//  The cat-paw stamp tool's tracker. A left press places one paw at the point pressed -- and,
//  since 2026-10-27, FOLLOWS THE DRAG before placing it: the paw points the way the button was
//  dragged before it came up (the author: "turn the paw to the angle of where the mouse was
//  released from where it was pressed"), and turns on screen while the button is down. A click
//  (under kKCMPawDragPixels of travel) places it upright, as before. The other gestures still do
//  their whole job in BeginTracking and answer kFalse ＝ the single-shot shape of
//  sdksamples/snapshot, whose tracker likewise does its whole job there.
//
//  ★FIVE GESTURES (re-cut 2026-09-07; the first cut was 2026-09-04):
//      plain press          place a paw in the colour the tool is HOLDING, pointing the way it
//                           is dragged before the release (a click: upright)
//      Alt + press          the same, then ask for a word and place the paw carrying it
//      Shift + Alt + press  SWAP that colour (red <-> blue). ★PLACES NOTHING
//      Shift + press        lift the paw under the point
//      Shift + DOUBLE       clear every paw on that page
//    ★★**THE TOOL HOLDS A COLOUR NOW** (sPawColour below), where it used to read one straight off
//      the keys. Two colours cannot be reached by three place-gestures, and the user asked for the
//      keys to SWAP rather than to select: red is the default and Shift+Alt turns it blue and back.
//      ⚠The colours are Kohaku InDesign MCP's own two -- red is what Claude marks with, blue is
//        the person's pencil -- and that pairing is the point (KCMConstants.h says why).
//    ⚠★★Alt CHANGED THE SIZE for about an hour (1.6x, then 10x, then 5x) before the user replaced
//      the idea with colour: a bigger paw is the same mark drawn larger, a different colour is a
//      different KIND of mark. Every paw is the ordinary size now.
//    ⚠★The lift is the gesture that has to test BOTH keys -- Shift alone lifts, Shift with Alt
//      places -- so `if (shift)` on its own would eat the swap.
//    ⚠★★A PLAIN PRESS NEVER LIFTS ANY MORE. It was a toggle for one day, and the fault showed
//      within minutes of first use: putting paws down in a row, the second press near the first
//      took the first one off. Placing and lifting are two intentions, so they are two gestures.
//    ⚠Ctrl is not read and cannot be: InDesign takes it for the temporary switch to the selection
//      tool, so a Ctrl + press never arrives here at all.
//    ★This is a DIFFERENT TABLE from the KCM tool's gestures (ui/KCMPeekGesture.cpp) and not a
//      second copy of it: that one belongs to the KCM tool's tracker, this to the stamp tool's,
//      and a tool's modifiers are read by its own tracker or by nobody.
//  ★The store is the model half's (KCMPawStamp.h): a stamp has to survive on the side that the
//    drawing and the saving both live on, and a kUIPlugIn's statics are not visible to the
//    background thread that exports a PDF.
//
//  ITracker (a CTracker subclass) plus its companion event handler, the same pair as
//  KCMTracker.cpp. ⚠No sprite on the boss: that is asked for by CLayoutTracker and
//  CPathCreationTracker, and this derives from CTracker directly (the full reason is at
//  kKCMTrackerBoss in KCMUI.fr).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CTracker.h"
#include "CTrackerEventHandler.h"
#include "IEvent.h"

#include "PersistUtils.h"		// ::GetUIDRef -- a page's UIDRef straight from its IGeometry
#include "IControlView.h"
#include "IGeometry.h"
#include "IGeometryFacade.h"	// GetItemBounds -- the page rectangle, in the space asked for
#include "IPasteboardUtils.h"	// QuerySpread -- the official "which spread is under this point"
#include "ISpread.h"			// QueryNearestPage / GetNumPages
#include "PMRect.h"
#include "PMReal.h"
#include "Utils.h"

#include "KCMUIID.h"
#include "KCMUIShared.h"			// KCMSetStatus -- the panel's status line
#include "KCMConstants.h"			// KCMPawColour -- what the tool holds and Shift+Alt swaps
#include "KCMPawWordDialog.h"		// the Alt gesture: ask for a word, then place -- NOT from in here
#include "IKCMPageFlagsFacade.h"	// ★the ONLY way across to the store: place / lift / count / size
#include "IKCMCompareFacade.h"		// InvalidateDB -- repaint the document that was pressed
#include "ILayoutController.h"		// InvalidateContent -- repaint the square of the paw being placed
#include "KCMDiag.h"				// KCM_DIAG_LOG - a placing press, release and abort, traced (test builds only)

#include <cmath>					// std::atan2 / std::floor -- which way the drag went (2026-10-27)

// The colour the tool is holding. Session state, main thread only, and deliberately NOT per
// document: it is a property of the TOOL in the reader's hand, like a pen they have picked up, so
// carrying it from one document to the next is what a person expects.
// ★Red to begin with (the user, 2026-09-07).
static int32 sPawColour = kKCMPawColourRed;

//____________________________________________________________________________________
//	Tracker event handler: forwards events to the tracker while capturing. A bare subclass of
//	CTrackerEventHandler is enough, exactly as KCMTrackerEH is -- and this tracker never
//	captures at all, so nothing but the press ever comes through it.
//____________________________________________________________________________________
class KCMPawTrackerEH : public CTrackerEventHandler
{
public:
	KCMPawTrackerEH(IPMUnknown* boss) : CTrackerEventHandler(boss) {}
	virtual ~KCMPawTrackerEH() {}
};

CREATE_PMINTERFACE(KCMPawTrackerEH, kKCMPawTrackerEHImpl)

//========================================================================================
// A page's rectangle in the coordinate space asked for, normalised.
//
//  ⚠BOTH GUARDS ARE DELIBERATE, and both are copied from KCMPagePasteboardRectRaw in
//    ui/KCMViewSync.cpp, which learnt them the same way: the facade guarantees neither that this
//    UID has geometry at all (hence the nil test) nor that the rectangle it answers has
//    Left < Right.
//========================================================================================
static bool16 KCMPawPageRect(const UIDRef& pageRef, const Transform::CoordinateSpace& space,
                             PMRect& outRect)
{
	InterfacePtr<IGeometry> geo(pageRef, UseDefaultIID());
	if (geo == nil)
		return kFalse;

	const PMRect r = Utils<Facade::IGeometryFacade>()->GetItemBounds(
		pageRef, space, Geometry::PathBounds());
	outRect = PMRect(
		(r.Left() < r.Right())  ? r.Left()  : r.Right(),
		(r.Top()  < r.Bottom()) ? r.Top()   : r.Bottom(),
		(r.Left() < r.Right())  ? r.Right() : r.Left(),
		(r.Top()  < r.Bottom()) ? r.Bottom() : r.Top());
	return kTrue;
}

//========================================================================================
// Which page is under a pasteboard point, and where on that page it falls.
//
//  outX / outY are measured from the PAGE'S TOP-LEFT, in points. ★Never pasteboard
//    coordinates: a pasteboard point is only correct within one session -- add or delete a page
//    and the spread's layout shifts, so a stamp saved that way would come back pointing
//    somewhere else.
//
//  ★THE ROUTE IS THE OFFICIAL ONE, and KCM already walks it in KCMQueryViewCenterPage
//    (ui/KCMViewSync.cpp):
//      (1) the spread under the point  = IPasteboardUtils::QuerySpread(view, pb)
//          (IPasteboardUtils.h:83 -- it answers nil for a point on no spread at all)
//      (2) the page within that spread = ISpread::QueryNearestPage(pb, &index)
//          (ISpread.h:189-195 -- the product does the same in CPathCreationTracker.cpp:341)
//    ⚠QueryNearestPage answers the NEAREST page, so it names one even for a point out on the
//      pasteboard. **The containment test is what turns that into "on a page, or not"** --
//      leave it out and every press beside a page would stamp that page's edge.
//
//  ★EVERYTHING HERE IS IN PASTEBOARD COORDINATES, because that is what the press hands us
//    (CTracker::GlobalToPasteboard) and what QueryNearestPage's contract asks for. The DRAWING
//    side works in SPREAD coordinates instead (KCMQueryPageRect in
//    source/KCMDrawEventHandler.cpp).
//
//  ★★★MEASURED ON THE RUNNING APPLICATION, 2026-09-04 -- and the answer is not the one the
//    design expected. **The two spaces are not the same.** On the document's FIRST spread the
//    difference was 0.00, 0.00, which is exactly what makes the mistake easy to make; on the
//    SECOND it was dy = -1038.90pt -- one spread's worth -- and it grows again on the third.
//    ⇒ **A pasteboard point handed straight to the drawing side would be wrong by a whole
//      spread on every spread but the first.**
//
//  ★WHAT MAKES IT WORK ANYWAY IS WHAT THE OFFSET IS MEASURED FROM. Both sides subtract THE SAME
//    PAGE'S OWN RECTANGLE, each taken in its own space, so the translation between the spaces
//    cancels: "this many points right and down from this page's top-left" names the same spot on
//    both sides whatever the spaces do. ⇒ **Storing a stamp page-relative (spec 2026-09-04 §2-3)
//    is not a preference for tidiness, it is the requirement** -- and the numbers above are what
//    turned it into one. Anyone tempted to "simplify" this by keeping a pasteboard point should
//    read those numbers first.
//========================================================================================
static bool16 KCMPawPointOnPage(IControlView* view, const PBPMPoint& pb,
                                UIDRef& outPageRef, PMReal& outX, PMReal& outY)
{
	outPageRef = UIDRef();
	if (view == nil)
		return kFalse;

	InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(view, pb));
	if (spread == nil)
		return kFalse;			// on no spread at all ＝ the empty pasteboard

	int32 pageIndex = -1;
	InterfacePtr<IGeometry> pageGeo(spread->QueryNearestPage(pb, &pageIndex));
	if (pageGeo == nil || pageIndex < 0 || pageIndex >= spread->GetNumPages())
		return kFalse;

	// ★The page's UIDRef comes from the geometry we already hold, rather than from
	//   GetNthPageUID plus a database fetched separately: one object, one question. (The sync
	//   observer next door takes the UID route because a bare UID is all it wants.)
	const UIDRef pageRef = ::GetUIDRef(pageGeo);
	if (pageRef.GetUID() == kInvalidUID)
		return kFalse;

	PMRect pr;
	if (!KCMPawPageRect(pageRef, Transform::PasteboardCoordinates(), pr))
		return kFalse;
	if (!pr.PointIn(pb))
		return kFalse;			// on the spread, but beside the page rather than on it

	outPageRef = pageRef;
	outX = pb.X() - pr.Left();
	outY = pb.Y() - pr.Top();
	return kTrue;
}

//____________________________________________________________________________________
//	The stamp tool's tracker.
//____________________________________________________________________________________
class KCMPawTracker : public CTracker
{
public:
	KCMPawTracker(IPMUnknown* boss)
		: CTracker(boss), fPlacing(kFalse), fWithWord(kFalse), fColour(kKCMPawColourRed)
	{
		fWantsToAutoScroll = kFalse;
	}
	virtual ~KCMPawTracker() {}

	/** Refuse every tracking timer. Nothing here needs one -- a drag is followed on the moves it makes --
		so a repeating idle would only cost time. The same blanket kFalse as KCMTracker::WantTimer, and
		for the same reason it is safe: mouse-up is delivered by the event handler, never by a timer. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Do NOT suppress the views' updates while following a drag (2026-10-27). The paw being placed is
		drawn by a draw event, and the base's DisableUpdates is exactly what would keep it from showing
		while the button is down -- the reason KCMTracker no-ops the same two, for its reveal. */
	virtual void DisableUpdates() {}
	virtual void EnableUpdates()  {}

	/** The point as the hand put it. The base snaps and rounds every point it hands on (to guides, to
		the ruler's increments); the direction a paw points must follow the mouse, not jump between
		round numbers. */
	virtual bool16 AdjustPoint(PBPMPoint& /*point*/) { return kFalse; }

	/** Mouse down. The gestures that take or change something (Shift: lift; Shift+Alt: swap the colour;
		Shift + double: clear the page) do it here and answer kFalse ＝ single shot, as the tool always did.
		★A PRESS THAT PLACES IS FOLLOWED (2026-10-27): the paw goes where the button went down, and the
		  drag decides which way it points -- so the base is asked to track, the paw is shown turning, and
		  the release puts it down (EndTracking). */
	virtual bool16 BeginTracking(IEvent* theEvent);

	/** A move while placing: turn the paw being shown to the mouse. */
	virtual void ContinueTracking(const PBPMPoint& where, bool16 mouseDidMove);

	/** The release: the direction from where the button came up, then the paw is placed. */
	virtual bool16 EndTracking(IEvent* theEvent);

	/** Abandoned mid-drag (Escape, a switch to another application): nothing is placed, and the paw that
		was being shown goes. */
	virtual void AbortTracking(IEvent* theEvent);

private:
	// ---- the press being followed (valid while fPlacing) -------------------------------------------
	bool16		fPlacing;
	bool16		fWithWord;	// Alt: the word is asked for after the release
	UIDRef		fPageRef;	// the page pressed on
	PMReal		fX, fY;		// where on it, from its top-left in points (what is stored)
	PBPMPoint	fPressPB;	// the same point in pasteboard coordinates (what the drag is measured from)
	PMReal		fHalf;		// half a paw's size on that page
	int32		fColour;	// the colour the tool was holding at the press
	PMReal		fAngle;		// the direction shown now: degrees clockwise from straight up

	PMReal	AngleTo(const PBPMPoint& to) const;
	void	InvalidatePlacing() const;
	void	ShowPlacing() const;
	void	HidePlacing() const;
	void	PlaceNow();
};

CREATE_PMINTERFACE(KCMPawTracker, kKCMPawTrackerImpl)

// ★HOW FAR THE BUTTON MUST TRAVEL BEFORE IT IS A DRAG, in screen pixels (2026-10-27). Under it the press is a click
//   and the paw stands upright, as every paw did before: a hand that clicks still moves a pixel or two, and a paw
//   that swung to wherever that wobble pointed would be a fault, not a feature. In SCREEN pixels, so it feels the
//   same at every zoom. ★Twice the 3 pixels the product's layout trackers take before a drag begins
//   (CLayoutTracker's kMinDragDistance): a direction wants more travel than "it moved" does.
static const double kKCMPawDragPixels = 6.0;

// Which way from the press the point `to` lies, in degrees clockwise from straight up -- 0 when it lies within the
// threshold. Whole degrees, as the label keeps them.
// ★In PASTEBOARD coordinates, where the paw is also drawn (spread coordinates differ by a translation only --
//   KCMPawPointOnPage). y runs DOWN here, which is what atan2(dx, -dy) is written for: straight up is (0, -1) -> 0,
//   to the right (1, 0) -> 90, down -> 180, to the left -> 270 (measured 2026-10-27, the paw-rotate suite).
//   ⚠A spread shown ROTATED in the view (View > Rotate Spread) should turn the drag and the paw alike, so that the
//     paw still points where the hand went on screen -- reasoned from both living in the same space, NOT measured.
PMReal KCMPawTracker::AngleTo(const PBPMPoint& to) const
{
	if (fControlView == nil)
		return PMReal(0.0);

	PMPoint a(fPressPB), b(to);
	fControlView->ContentToWindowTransform(&a);
	fControlView->ContentToWindowTransform(&b);
	const double wx = ToDouble(b.X() - a.X()), wy = ToDouble(b.Y() - a.Y());
	if (wx * wx + wy * wy < kKCMPawDragPixels * kKCMPawDragPixels)
		return PMReal(0.0);

	const double dx = ToDouble(to.X() - fPressPB.X()), dy = ToDouble(to.Y() - fPressPB.Y());
	double deg = std::atan2(dx, -dy) * 180.0 / kPMPi;
	deg = std::floor(deg + 0.5);
	while (deg < 0.0)
		deg += 360.0;
	while (deg >= 360.0)
		deg -= 360.0;
	return PMReal(deg);
}

// Repaint the square the paw being placed can cover, whichever way it points: its outlines reach 0.49 of its size
// from the centre and the white outline half its own width beyond (0.08), so 0.65 of the size plus two points for
// the anti-aliasing covers every turn -- one rectangle for the whole drag.
// ★The product's way to repaint part of a layout view: content coordinates to window ones, then the layout
//   controller's InvalidateContent (spellpanel/PrivateSpellingUtils.cpp, the spelling squiggles).
void KCMPawTracker::InvalidatePlacing() const
{
	if (fControlView == nil)
		return;
	const PMReal r = fHalf * PMReal(2.0) * PMReal(0.65) + PMReal(2.0);
	PMRect rect(fPressPB.X() - r, fPressPB.Y() - r, fPressPB.X() + r, fPressPB.Y() + r);
	fControlView->ContentToWindowTransform(&rect);
	InterfacePtr<ILayoutController> controller(fControlView, UseDefaultIID());
	if (controller != nil)
		controller->InvalidateContent(&rect, kTrue);
}

void KCMPawTracker::ShowPlacing() const
{
	Utils<IKCMPageFlagsFacade> flags;
	if (!flags)
		return;
	flags->PawPreviewSet(fPageRef.GetDataBase(), fPageRef.GetUID(), fX, fY, fColour, fAngle, fControlView);
	this->InvalidatePlacing();
}

void KCMPawTracker::HidePlacing() const
{
	Utils<IKCMPageFlagsFacade> flags;
	if (flags)
		flags->PawPreviewClear();
	this->InvalidatePlacing();
}

// Put the followed paw down, at the direction now held, and say so on the status line. Alt hands it to the word box
// instead, which places it once the word is in (the same one-shot timer as before -- a modal is not opened from a
// tracker -- now armed at the release rather than at the press).
void KCMPawTracker::PlaceNow()
{
	IDataBase* const db = fPageRef.GetDataBase();
	const UID pageUID = fPageRef.GetUID();
	if (fWithWord)
	{
		KCMPawWordDialog::AskAndPlaceLater(db, pageUID, fX, fY, fColour, fHalf, fAngle);
		return;			// the message and the redraw belong to the dialog
	}

	PMString msg;
	InterfacePtr<IKCMPageFlagsFacade> flags(Utils<IKCMPageFlagsFacade>().QueryUtilInterface());
	if (flags == nil)
	{
		msg = "Paw: the model side did not answer";
	}
	else
	{
		const bool16 placed = flags->PawStampPlaceAtAngle(db, pageUID, fX, fY, fColour, fHalf, PMString(), fAngle);
		// ⚠Three outcomes, not two: a press that lands on a paw already there places nothing (it was asked at the
		//   press, but a paw could have arrived since -- a script, another view), and saying "placed" then would be
		//   a lie the count does not correct.
		if (!placed)
			msg = "Paw: one is already there (";
		else if (fAngle != PMReal(0.0))
		{
			msg = "Paw placed, turned ";
			msg.AppendNumber(ToInt32(fAngle));
			msg += " degrees (";
		}
		else
			msg = "Paw placed (";
		msg.AppendNumber(flags->PawStampCount(db));
		msg += " on this document)";
	}
	// (The flag comes down by hand -- the reason is at the end of BeginTracking.)
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);
	// ⚠No repaint from here for the paw itself: the model's marks observer repaints when the write lands, for
	//   undo and redo as well (KCMMarksObserver.h). HidePlacing has already asked for the square it stood in.
}

bool16 KCMPawTracker::BeginTracking(IEvent* theEvent)
{
	fPlacing = kFalse;
	if (theEvent == nil)
		return kFalse;

	// ★A LEFT PRESS, IN EITHER OF THE TWO SHAPES IT ARRIVES IN: kLButtonDn for the first press of a
	//   sequence, and **kDoubleClick for the second** -- press, release, press again inside the
	//   system double-click time and Windows sends WM_LBUTTONDBLCLK in place of the second
	//   WM_LBUTTONDOWN, which reaches here as kDoubleClick (10).
	//
	// ⚠★★★**`theEvent->LButtonDn()` MUST NOT BE ASKED HERE, AND THAT WAS MEASURED** (2026-09-07,
	//   on the running application, with a probe in this very spot and with the user's own hand on
	//   the mouse):
	//       plain double click   ev=10  L=0  S=0
	//       Shift + double click ev=10  L=0  S=1
	//   **L is 0 both times.** IEvent.h:39-41 promises "kTrue if left mouse button was pressed when
	//   this event was generated" and that promise is not kept for kDoubleClick. The modifiers ARE
	//   kept (S follows Shift exactly), so everything below may read them as it always has.
	//   ⇒ The old guard `(evType == kDoubleClick && theEvent->LButtonDn())` threw every double
	//     click away, and this function's ONE exit that writes no message at all is that guard --
	//     which is why Shift + double click looked like nothing whatsoever had happened, with the
	//     status line still showing the first press's "Paw lifted". The clearing code was right the
	//     whole time and was never reached.
	//
	// ★NOTHING NARROWS kDoubleClick TO THE LEFT BUTTON, AND THAT IS A DECISION (the user's, made on
	//   2026-09-07 after the measurement above: "it is probably fine without it"). Three things say
	//   no narrowing is needed:
	//     - **kDoubleClick is raised for the left button only** -- measured in this plug-in's
	//       earlier life and kept as [[overprint-simulation-and-mouse-timing]]: a MIDDLE button
	//       raises none at all, which is why KESCM had to time its middle-button presses by hand
	//       with IEvent::GetTime;
	//     - **the SDK's only tracker that acts on a double click does exactly this**, and asks
	//       nothing else -- snapshot/SnapTracker.cpp:238-244 compares the type and stops;
	//     - **not one of the sixteen ButtonDblClk implementations in the SDK consults LButtonDn()**
	//       (counted 2026-09-07, product code and samples alike).
	//   ⚠WHAT IS NOT MEASURED IS THE RIGHT BUTTON. The middle one is; the right one nobody has
	//     tried. If it ever does raise a kDoubleClick here, a plain right double click would place
	//     one paw -- a single undo step -- and the cure is one term:
	//     `&& Utils<IEventUtils>()->IsMouseButtonDown()`, whose contract is "the current state of
	//     the LEFT mouse button" (IEventUtils.h:65-69, and the only door to the button state now
	//     that the event's has been found lying). It was written, then taken back out on purpose:
	//     put it back with a measurement in hand, not on a hunch.
	const IEvent::EventType evType = theEvent->GetType();
	const bool16 leftPress =
		(evType == IEvent::kLButtonDn) ||
		(evType == IEvent::kDoubleClick);
	if (!leftPress)
		return kFalse;
	// ★The SECOND press of a pair, which Shift turns into "clear this page" below.
	const bool16 isDouble = (evType == IEvent::kDoubleClick) ? kTrue : kFalse;

	// CTracker converts the press for us -- the same call the SDK's own tools make
	// (snapshot/SnapTracker.cpp:211).
	PBPMPoint pb;
	this->GlobalToPasteboard(theEvent->GlobalWhere(), pb);

	UIDRef pageRef;
	PMReal x, y;
	PMString msg;

	if (KCMPawPointOnPage(fControlView, pb, pageRef, x, y))
	{
		IDataBase* const db = pageRef.GetDataBase();
		const UID pageUID = pageRef.GetUID();

		// ★★EVERY CROSSING TO THE MODEL IS A FACADE CALL. model and UI are two DLLs, so calling
		//   KCMPawStampToggleAt() straight from here does not link -- measured on 2026-09-04
		//   (LNK2019, three unresolved symbols), which is how this arrived at its proper shape.
		// ⚠The utility is taken through QueryUtilInterface() and nil-tested: writing
		//   `InterfacePtr<T> p(Utils<T>());` does not compile (most vexing parse), and
		//   QueryUtilInterface() itself has no nil guard inside ([[utils-boss-facade-access]]).
		InterfacePtr<IKCMPageFlagsFacade> flags(Utils<IKCMPageFlagsFacade>().QueryUtilInterface());
		if (flags == nil)
		{
			msg = "Paw: the model side did not answer";
			KCMSetStatus(msg.SetTranslatable(kFalse));
			return kFalse;
		}

		// ★THE HIT BOX IS THE PAW'S OWN SQUARE, so what can be seen is what can be lifted. The
		//   size is asked of the one place that owns it rather than worked out again here -- put
		//   the ratio in two places and the picture and the target drift apart, and the drift
		//   would only ever show as "sometimes the paw will not come off".
		//   ★A turned paw keeps the same square: its outlines stay within it whichever way it points.
		const PMReal half = flags->PawHalfSizeForPage(db, pageUID);
		if (half <= PMReal(0.0))
		{
			// The page could not be measured, so there is no honest size to stamp at. Saying so
			// beats stamping at a guessed one.
			msg = "Paw: cannot measure that page";
		}
		else
		{
			// ★★THE GESTURES (re-cut 2026-09-07 at the user's request; the colours were pink / cyan /
			//   green until then; the drag added 2026-10-27):
			//       plain press      place, in whichever colour the tool is holding -- pointing the way
			//                        the button is dragged before it comes up (a click: upright)
			//       Alt              the same, and the word is asked for after the release
			//       Shift + Alt      SWAP the colour (red <-> blue). ★It places NOTHING
			//       Shift            lift the paw under the point
			//       Shift + DOUBLE   clear every paw on that page
			// ⚠SHIFT ALONE LIFTS, but Shift WITH Alt places -- so the lift is the one combination
			//   that has to test BOTH keys. Reading Shift on its own would swallow the swap before
			//   it ever happened.
			const bool16 shiftDown = theEvent->ShiftKeyDown();
			const bool16 altDown   = theEvent->OptionAltKeyDown();

			bool16 changed = kFalse;
			if (shiftDown && !altDown)
			{
				if (isDouble)
				{
					// ★★**SHIFT + DOUBLE CLICK CLEARS THE PAGE** (2026-09-07, the user asked for it).
					//   ⚠**The single press has ALREADY happened** and lifted the paw under the point:
					//     Windows delivers kLButtonDn first and kDoubleClick second, and nothing here
					//     can know a second click is coming. So the reader gets what they asked for --
					//     an empty page -- in TWO undo steps rather than one, and Ctrl+Z twice puts it
					//     all back. That is the honest cost of the gesture, not a defect to hunt.
					const int32 gone = flags->PawStampClearPage(db, pageUID);
					changed = (gone > 0) ? kTrue : kFalse;
					msg = changed ? "Cat paws cleared from this page (" : "Paw: none left on this page (";
				}
				else
				{
					changed = flags->PawStampLiftAt(db, pageUID, x, y, half);
					msg = changed ? "Paw lifted (" : "Paw: none under that point (";
				}
			}
			else if (shiftDown && altDown)
			{
				// ★★**IT ONLY SWAPS THE COLOUR. NOTHING IS PLACED** (the user, 2026-09-07, after
				//   using the first cut: "when Shift+Alt is pressed it stamps -- make it only
				//   change the colour setting").
				//   ⚠The first cut swapped AND placed, on the reasoning that a press which leaves
				//     the page unmarked reads as a press that did nothing. Wrong reasoning, and the
				//     use showed why: **the reader swaps the colour when they are about to mark
				//     something ELSE**, so the swap left a paw where they had merely been choosing.
				//     Choosing a pen is not writing with it.
				sPawColour = (sPawColour == kKCMPawColourRed) ? kKCMPawColourBlue : kKCMPawColourRed;
				// ⚠**This one says its piece and leaves**, because the count appended to every other
				//   message below belongs to PAWS. "Cat paw colour: blue (2 on this document)" reads
				//   as "there are two blue paws", which is not what the number counts.
				msg = (sPawColour == kKCMPawColourBlue) ? "Cat paw colour: blue"
				                                        : "Cat paw colour: red";
				msg.SetTranslatable(kFalse);
				KCMSetStatus(msg);
				// Nothing was placed, so there is nothing to undo and nothing to redraw.
				return kFalse;
			}
			else if (flags->PawStampIsAt(db, pageUID, x, y, half))
			{
				// ★★NO STACKING, ASKED AT THE PRESS (2026-10-27). The store refuses a paw on a paw (the
				//   user's request of 2026-09-04: two on one spot look like one, and only the top comes
				//   off), and the press is where to find out -- before a drag is followed and a paw is
				//   shown turning over one that will never be placed.
				msg = "Paw: one is already there (";
			}
			else
			{
				// ★★FOLLOW THE DRAG (2026-10-27, the author: "turn the paw to the angle of where the mouse
				//   was released from where it was pressed"). The paw is shown at once, upright, and
				//   turns as the mouse moves; the release puts it down (EndTracking).
				// ⚠Alt no longer opens the word box from here: the direction is not known until the
				//   release, so the box comes after it (PlaceNow -> KCMPawWordDialog, the same one-shot
				//   timer -- a modal is still never opened from inside a tracker).
				fPlacing  = kTrue;
				fWithWord = altDown;
				fPageRef  = pageRef;
				fX        = x;
				fY        = y;
				fPressPB  = pb;
				fHalf     = half;
				fColour   = sPawColour;
				fAngle    = 0.0;
				this->ShowPlacing();
				// (test builds: one run of the paw-rotate suite placed nothing for one press and showed no paw
				//  while it was held -- the first run after a restart, 2026-10-27, not seen again in four. The
				//  three lines below say which way such a press went the next time.)
				KCM_DIAG_LOG("PAW-PRESS placing x=%d y=%d alt=%d", ToInt32(x), ToInt32(y), altDown ? 1 : 0);
				if (CTracker::BeginTracking(theEvent))
					return kTrue;
				KCM_DIAG_LOG("PAW-PRESS the base would not track - placed upright at once");
				// The base would not follow (it always does in a layout view; a refusal must not lose
				// the press): the paw goes down now, upright, the way a click puts it.
				this->HidePlacing();
				fPlacing = kFalse;
				this->PlaceNow();
				return kFalse;
			}
			msg.AppendNumber(flags->PawStampCount(db));
			msg += " on this document)";

			// ⚠★★**NOTHING IS REPAINTED FROM HERE ANY MORE** (2026-09-07). This used to call
			//   IKCMCompareFacade::InvalidateDB when something moved, and it had to: the UI was
			//   what knew a paw had been placed. It is not any more -- a paw goes into the
			//   DOCUMENT now, and the model's marks observer repaints when the write lands,
			//   for undo and redo as well as for the press (KCMMarksObserver.h).
			//   ★Leaving the call in would have been harmless and wrong: two answers to "who
			//     redraws after a paw changes", of which only one is right for Ctrl+Z
			//     ([[one-question-one-place]]). **The UI asks for the change; the model decides
			//     what moved and shows it.**
			//   ⚠`changed` is now read for ONE thing only -- how the status line is worded. If it ever
			//     stops being read, delete it rather than leaving a flag nobody acts on.
		}
	}
	else
	{
		msg = "Paw: not over a page";
	}

	// ★THE FLAG HAS TO BE TAKEN DOWN BY HAND. KCMSetStatus's const char* overload does it for
	//   its caller -- that is the whole difference between the two overloads -- but the PMString
	//   one must not, because a message that IS a key has to stay translatable. What is built
	//   above is a finished sentence, and a finished sentence left translatable turns into
	//   something else the moment it matches an entry of the built-in table: KCM has been bitten
	//   by exactly that ("Source:" came back as a style-source phrase in a Japanese locale),
	//   which is why every call site of this kind takes the flag down itself.
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);

	return kFalse;			// single shot -- there is nothing to keep tracking
}

void KCMPawTracker::ContinueTracking(const PBPMPoint& where, bool16 mouseDidMove)
{
	CTracker::ContinueTracking(where, mouseDidMove);
	if (!fPlacing || !mouseDidMove)
		return;
	const PMReal angle = this->AngleTo(where);
	if (angle != fAngle)
	{
		fAngle = angle;
		this->ShowPlacing();		// a turn only when the whole degree changed: no repaint for a move that did not
	}
}

bool16 KCMPawTracker::EndTracking(IEvent* theEvent)
{
	const bool16 result = CTracker::EndTracking(theEvent);
	if (fPlacing)
	{
		// The direction from where the button CAME UP, read the way the press was read -- the last move
		// seen may lie a little short of it.
		if (theEvent != nil)
		{
			PBPMPoint pb;
			this->GlobalToPasteboard(theEvent->GlobalWhere(), pb);
			fAngle = this->AngleTo(pb);
		}
		this->HidePlacing();
		fPlacing = kFalse;
		KCM_DIAG_LOG("PAW-RELEASE angle=%d word=%d", ToInt32(fAngle), fWithWord ? 1 : 0);
		this->PlaceNow();
	}
	return result;
}

void KCMPawTracker::AbortTracking(IEvent* theEvent)
{
	CTracker::AbortTracking(theEvent);
	if (fPlacing)
	{
		this->HidePlacing();
		fPlacing = kFalse;
		KCM_DIAG_LOG("PAW-ABORT the drag was interrupted - nothing placed");
		PMString msg;
		msg = "Paw: not placed (the drag was interrupted)";
		msg.SetTranslatable(kFalse);
		KCMSetStatus(msg);
	}
}

// End, KCMPawTracker.cpp.
