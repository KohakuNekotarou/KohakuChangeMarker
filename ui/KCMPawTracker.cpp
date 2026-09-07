//========================================================================================
//
//  KCMPawTracker.cpp
//
//  The cat-paw stamp tool's tracker. A left press places one paw at the point pressed, and
//  there is nothing to follow afterwards, so BeginTracking answers kFalse ＝ the single-shot
//  shape of sdksamples/snapshot, whose tracker likewise does its whole job in BeginTracking.
//
//  ★FIVE GESTURES (re-cut 2026-09-07; the first cut was 2026-09-04):
//      plain press          place a paw in the colour the tool is HOLDING
//      Alt + press          ask for a word, then place the paw carrying it
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
	KCMPawTracker(IPMUnknown* boss) : CTracker(boss) { fWantsToAutoScroll = kFalse; }
	virtual ~KCMPawTracker() {}

	/** Refuse every tracking timer. Nothing here is continuous -- the whole gesture is one
		press -- so a repeating idle would only cost time. The same blanket kFalse as
		KCMTracker::WantTimer, and for the same reason it is safe: mouse-up is delivered by the
		event handler, never by a timer. */
	virtual bool16 WantTimer(ClassID /*trackerTimerBoss*/) { return kFalse; }

	/** Mouse down. Reports which page was pressed and where on it.
		★It answers kFalse ＝ "do not start tracking": the gesture is over the instant it began.
		⚠That also means the base's BeginTracking is never called, so none of what it sets up
		 (the modal cursor, the update suppression, the timers) is entered in the first place --
		 which is why this file has nothing to undo and no EndTracking. */
	virtual bool16 BeginTracking(IEvent* theEvent);
};

CREATE_PMINTERFACE(KCMPawTracker, kKCMPawTrackerImpl)

bool16 KCMPawTracker::BeginTracking(IEvent* theEvent)
{
	if (theEvent == nil)
		return kFalse;

	// ★Left press only. ⚠Deliberately not kLButtonDn alone: press, release, press again inside
	//   the system double-click time and the second press arrives as **kDoubleClick**, which
	//   IEvent documents as "double click on ANY mouse button" -- LButtonDn() is what narrows it
	//   back to the left one. The same two lines as KCMTracker.cpp, and the reason matters here
	//   more than there: stamping twice in one spot is exactly what a user does when they meant
	//   to place and then lift.
	const IEvent::EventType evType = theEvent->GetType();
	const bool16 leftPress =
		(evType == IEvent::kLButtonDn) ||
		(evType == IEvent::kDoubleClick && theEvent->LButtonDn());
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
		const PMReal half = flags->PawHalfSizeForPage(db, pageUID);
		if (half <= PMReal(0.0))
		{
			// The page could not be measured, so there is no honest size to stamp at. Saying so
			// beats stamping at a guessed one.
			msg = "Paw: cannot measure that page";
		}
		else
		{
			// ★★THE FOUR GESTURES (re-cut 2026-09-07 at the user's request; the colours were
			//   pink / cyan / green until then):
			//       plain press      place, in whichever colour the tool is holding
			//       Alt              ask for a word, then place -- the word goes beside the paw
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
			else if (altDown)
			{
				// ⚠★★★**NOTHING IS PLACED HERE.** The word is asked for in a modal dialog, and a
				//   modal must not be opened from inside a tracker -- the mouse is still captured
				//   and the dialog's loop would run under it (KCMPawWordDialog.h carries the
				//   reason, and this plug-in's tool-button flyout already obeys it). So the press
				//   is handed to a one-shot timer, and the placing, the count and the status line
				//   all happen over there once the box has been answered.
				KCMPawWordDialog::AskAndPlaceLater(db, pageUID, x, y, sPawColour, half);
				return kFalse;			// the message and the redraw belong to the dialog
			}
			else
			{
				changed = flags->PawStampPlaceAt(db, pageUID, x, y, sPawColour, half, PMString());
				// ⚠Three outcomes, not two: a press that lands on a paw already there places
				//   nothing, and saying "placed" then would be a lie the count does not correct
				//   (the count is unchanged, which is exactly what a slip looks like).
				if (!changed)
					msg = "Paw: one is already there (";
				else
					msg = "Paw placed (";
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

// End, KCMPawTracker.cpp.
