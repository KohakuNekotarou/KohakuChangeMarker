//========================================================================================
//
//  KCMUIDrawEvent.cpp
//
//  The UI half's draw service. It carries **only what is drawn on screen alone** -- the things
//  the model side cannot do by construction.
//
//  It holds two:
//    (1) the on-press HUD ＝ while the tool's left button is held, the top-left of the pressed
//        window says what that window is to the comparison (Target / Source / ...).
//        ★**Whether the button is held is the tool's state, which is UI**; the model cannot see
//        it.
//    (2) noticing a manual Hide/Show Spread (moved here from the model-side handler). The
//        scrollbar map is **a strip injected into a document window** ＝ UI, so noticing what
//        should update it belongs on this side too.
//
//  ★kDrawEventService **expects several providers** (InDesign itself registers over twenty), so
//    one more here breaks nothing. ★Keeping this service screen-only is deliberate: a
//    `kUIPlugIn` boss cannot be created from a background thread, and the UI’s PDF export runs on
//    one, so nothing drawn here reaches an exported file ([[model-ui-plugin-separation]]). For a
//    HUD that is the right answer.
//    ⚠★★**The comparison marks, which DO have to reach an export, are on the model side -- and
//      they are not drawn from a draw event at all.** They were consolidated into the **global
//      page item adornment** (KCMRingAdornment.cpp), which builds a DrawEventData and calls
//      KCMDrawEventHandler::DrawSpreadMarks. ★**The model half registers no draw-event handler
//      of any kind**; KCMDrawEventHandler is no longer an IDrwEvtHandler implementation and says
//      so at the top of its own header (the class name and the file name are historical).
//
//  ★GetThreadingPolicy is **not written by hand**. CServiceProvider derives the default from the
//    plug-in type, so this side -- a UI plug-in -- gets kMainThreadOnly automatically (guide
//    vol1-07 L245-253).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CServiceProvider.h"
#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "GraphicsData.h"			// GraphicsData (GetGraphicsPort / GetView) and DrawEventData
#include "IGraphicsPort.h"
#include "IShape.h"					// IShape::kPrinting (the print / PDF-export context flag)
#include "GraphicsID.h"				// kDrawEventService
#include "DocumentContextID.h"		// kEndSpreadMessage / kAfterLastSpreadDrawMessage
#include "ISpread.h"
#include "IGeometry.h"
#include "IDataBase.h"
#include "PersistUtils.h"			// ::GetDataBase (the db of the spread being drawn)
#include "TransformUtils.h"			// ::InnerToSpreadMatrix / ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include "PMPoint.h"

#include "KCMUIID.h"
#include "KCMTrackerHud.h"		// the on-press HUD (moved here from the model-side handler)
#include "KCMScrollMap.h"			// KCMScrollMapNoticeDrawEvent (detects a manual Hide/Show Spread)

//========================================================================================
// The offset from pasteboard coordinates to this spread’s coordinates (= pasteboard - spread).
//   Pasteboard coordinates are one system for the whole document, while spreads are stacked on
//   the pasteboard (mostly vertically) and each carries an offset (spread[0] happens to be 0).
//   Mapping the same inner origin (0,0) through both InnerToSpreadMatrix and
//   InnerToPasteboardMatrix and taking the difference gives this spread’s offset; subtracting it
//   from a point in pasteboard coordinates gives that point in the spread’s.
//   ★It lives here because **its only caller, the HUD, does** -- the model side no longer
//     references it. Not a line of it changed in the move. It reads page geometry and touches no
//     view, but it exists only to line drawing up, so the drawing side is where it belongs.
//========================================================================================
static PMPoint KCMSpreadOffsetFromPasteboard(IDataBase* db, ISpread* spread)
{
	PMPoint off(0.0, 0.0);
	if (db == nil || spread == nil || spread->GetNumPages() < 1)
		return off;
	InterfacePtr<IGeometry> pg(db, spread->GetNthPageUID(0), UseDefaultIID());
	if (pg == nil)
		return off;
	PMMatrix mS = ::InnerToSpreadMatrix(pg);
	PMMatrix mP = ::InnerToPasteboardMatrix(pg);
	PMPoint ps(0.0, 0.0), pp(0.0, 0.0);
	mS.Transform(&ps);
	mP.Transform(&pp);
	return PMPoint(pp.X() - ps.X(), pp.Y() - ps.Y());
}

//========================================================================================
// KCMUIDrawEventHandler
//========================================================================================
class KCMUIDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KCMUIDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KCMUIDrawEventHandler() {}

	virtual void	Register(IDrwEvtDispatcher* d);
	virtual void	UnRegister(IDrwEvtDispatcher* d);
	virtual bool16	HandleDrawEvent(ClassID eventID, void* eventData);
};

CREATE_PMINTERFACE(KCMUIDrawEventHandler, kKCMUIDrawEventHandlerImpl)

// ★The HUD uses **both** draw-event routes: one alone cannot cover the whole window (the reason
//   is at HandleDrawEvent below). The model-side handler registered both once, and
//   kAfterLastSpreadDrawMessage **was registered for the HUD alone**, so the registrations left
//   the model side together with the HUD.
void KCMUIDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// The per-spread draw event; the port is in spread coordinates.
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
	// Per-window (once, after every spread has been drawn); the port is in pasteboard coordinates.
	d->RegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this, kDEHLowestPriority);
}

void KCMUIDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
	d->UnRegisterHandler(ClassID(kAfterLastSpreadDrawMessage), this);
}

/* HandleDrawEvent - draws the on-press HUD, and nothing else.

   ★★Why both routes are used (the measurement is inherited from the model-side handler’s
     Register, where it was first written down):
     kEndSpreadMessage           ... clipped to the band (spread / pasteboard) but **in front**
     kAfterLastSpreadDrawMessage ... not clipped but **behind** (= visible only on the canvas
                                    where nothing covers it)
   ∴ together, each pixel is served by exactly one of them, and **the whole view is covered with
     no double drawing**.

   ★The return value is always kFalse ＝ pass it on (the established practice).
     ⚠**Nothing of KCM’s runs after this.** The comparison marks are an adornment, not a
       draw-event handler, so what follows are InDesign’s own handlers.
*/
bool16 KCMUIDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;

	// Screen only: do nothing in a print or PDF-export context.
	// ★This service is on the UI side, so an export is never handed to it anyway; the test is here
	//   for the print contexts that DO appear on screen (print preview and the like). Same shape as
	//   the model side’s test.
	if ((ded->flags & IShape::kPrinting) != 0)
		return kFalse;

	// Scrollbar map: a light check that detects a manual Hide/Show Spread from the Pages panel (a
	// fingerprint comparison, throttled to 250ms). Hiding or showing by hand does not pass through
	// any hook of KCM’s, but it always causes a redraw, so the check rides on the spread draw event
	// (an Undo/Redo of the same change arrives the same way). KCMScrollMap.cpp.
	// ★★**It has to stand BEFORE the HUD test.** It must run on every non-print draw, whether or
	//   not a button is held -- the HUD test below returns immediately when nothing is pressed, so
	//   putting this after it would mean **the map only updated while the button was down**. In the
	//   model-side handler it likewise stood near the top, before the mark test.
	// ⚠★**One behaviour did change in the move**: over there it sat **after** the re-entrancy guard
	//   at the top of that function (do not draw while we are rasterising ourselves -- the model
	//   side’s `tl_Rasterizing`, an `IDThreading::ThreadLocal<bool16>`), so the detection did not
	//   run during a comparison’s rasterisation.
	//   This handler does not consult that flag, so **it runs during rasterisation too**.
	//   ★Why that was judged harmless: the check only compares a fingerprint of the hidden state and
	//     invalidates the strip when it differs, and the hidden state does not change while
	//     rasterising ⇒ effectively a no-op (and throttled to 250ms besides). ⚠It is a behavioural
	//     difference all the same, so it is on the list of things to confirm in the application.
	KCMScrollMapNoticeDrawEvent();

	// Do nothing unless a button is held AND this is the view of the window it was pressed in.
	// ★Draws with a nil view (a Pages panel thumbnail being generated, for instance) are rejected by
	// KCMTrackerHudWantsDraw.
	if (!KCMTrackerHudWantsDraw(ded->gd->GetView()))
		return kFalse;

	IGraphicsPort* gPort = ded->gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// The per-window event: the port is already in pasteboard coordinates, so no offset.
	if (eventID == ClassID(kAfterLastSpreadDrawMessage))
	{
		KCMTrackerHudDraw(gPort, ded->gd->GetView(), PMPoint(0.0, 0.0));
		return kFalse;
	}

	// The per-spread event: changedBy is the spread being drawn. The port is in spread coordinates,
	// so the pasteboard-to-spread offset is passed along.
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil)
		return kFalse;

	KCMTrackerHudDraw(gPort, ded->gd->GetView(), KCMSpreadOffsetFromPasteboard(db, spread));
	return kFalse;
}

//========================================================================================
// KCMUIDrawEventSrvc
//   Registers itself as a kDrawEventService provider. The service is found at application
//   startup, and the IDrwEvtHandler on the same boss is registered with the draw event
//   dispatcher.
//========================================================================================
class KCMUIDrawEventSrvc : public CServiceProvider
{
public:
	KCMUIDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KCMUIDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// An internal name, not translated, so SetCString. Same practice as the model side’s
	// KCMDrawEventSrvc.
	virtual void GetName(PMString* pName) { pName->SetCString("KCMUIDrawEventSrvc"); }
	// ★GetThreadingPolicy is **not written** (the reason is at the top of this file).
};

CREATE_PMINTERFACE(KCMUIDrawEventSrvc, kKCMUIDrawEventSrvcImpl)

// End, KCMUIDrawEvent.cpp.
