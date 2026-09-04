//========================================================================================
//
//  KCMPawTracker.cpp
//
//  The cat-paw stamp tool's tracker. A left press places one paw at the point pressed, and
//  there is nothing to follow afterwards, so BeginTracking answers kFalse ＝ the single-shot
//  shape of sdksamples/snapshot, whose tracker likewise does its whole job in BeginTracking.
//
//  ★TASK 1 OF THE PAW STAMP PLAN ENDS HERE: a press is only REPORTED, on the panel's status
//    line. Placing and lifting arrive in Task 2 and the drawing in Task 3. What this file has to
//    get right today is the one thing all of that rests on -- turning a press into "which page,
//    and where on that page".
//
//  ★NO MODIFIER KEY IS READ. Ctrl is InDesign's own temporary tool switch; Shift and Alt belong
//    to the KCM tool's gestures, which are classified in ui/KCMPeekGesture.cpp and nowhere else.
//    A stamp that changed meaning under a modifier would be a second place to look for that
//    table.
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
#include "KCMUIShared.h"		// KCMSetStatus -- the panel's status line

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

	// CTracker converts the press for us -- the same call the SDK's own tools make
	// (snapshot/SnapTracker.cpp:211).
	PBPMPoint pb;
	this->GlobalToPasteboard(theEvent->GlobalWhere(), pb);

	UIDRef pageRef;
	PMReal x, y;
	PMString msg;

	if (KCMPawPointOnPage(fControlView, pb, pageRef, x, y))
	{
		msg = "Paw: page uid=";
		msg.AppendNumber((int32)pageRef.GetUID().Get());
		msg += ", x=";
		msg.AppendNumber(x, 2, kFalse, kFalse);
		msg += "pt, y=";
		msg.AppendNumber(y, 2, kFalse, kFalse);
		msg += "pt";

		// ★TASK 1'S MEASUREMENT (spec §4-1), and it is temporary -- Task 2 takes it out again.
		//   The question it answers: does a page sit at the same place in spread coordinates
		//   (where the marks are drawn) as in pasteboard coordinates (where a press is read)?
		//   A non-zero difference here is not a bug, it is the number the drawing side would
		//   have to add -- but it has to be SEEN before anything is built on top of it.
		PMRect pbRect, spRect;
		if (KCMPawPageRect(pageRef, Transform::PasteboardCoordinates(), pbRect) &&
		    KCMPawPageRect(pageRef, Transform::SpreadCoordinates(), spRect))
		{
			msg += " | spread-pb dx=";
			msg.AppendNumber(spRect.Left() - pbRect.Left(), 2, kFalse, kFalse);
			msg += " dy=";
			msg.AppendNumber(spRect.Top() - pbRect.Top(), 2, kFalse, kFalse);
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
