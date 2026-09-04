//========================================================================================
//
//  KCMPawTracker.cpp
//
//  The cat-paw stamp tool's tracker. A left press places one paw at the point pressed, and
//  there is nothing to follow afterwards, so BeginTracking answers kFalse ＝ the single-shot
//  shape of sdksamples/snapshot, whose tracker likewise does its whole job in BeginTracking.
//
//  ★A press PLACES a paw, or LIFTS the one it landed on, and says which it did on the panel's
//    status line. ⚠Nothing is drawn yet -- the drawing arrives in Task 3 of the plan -- so until
//    then the status line is the only sign a press did anything.
//  ★The store is the model half's (KCMPawStamp.h): a stamp has to survive on the side that the
//    drawing and the saving both live on, and a kUIPlugIn's statics are not visible to the
//    background thread that exports a PDF.
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
#include "KCMUIShared.h"			// KCMSetStatus -- the panel's status line
#include "IKCMPageFlagsFacade.h"	// ★the ONLY way across to the store: place / lift / count / size
#include "IKCMCompareFacade.h"		// InvalidateDB -- repaint the document that was pressed

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
			const bool16 placed = flags->PawStampToggleAt(db, pageUID, x, y, half);
			msg = placed ? "Paw placed (" : "Paw lifted (";
			msg.AppendNumber(flags->PawStampCount(db));
			msg += " on this document)";

			// ★Repaint THIS document -- deliberately not the comparison's Target. A paw can be
			//   put on a document that is not being compared at all, which is the point of the
			//   tool, so KCMInvalidateMarksDoc next door (which repaints the Target) would be the
			//   wrong call. The facade ignores nil, so no test is needed.
			Utils<IKCMCompareFacade>()->InvalidateDB(db);
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
