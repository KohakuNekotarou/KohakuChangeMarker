//========================================================================================
//
//  KCMViewLookup.cpp
//
//  The questions put to a layout view: where the mouse is in a view's content coordinates,
//  which view it is really over (split windows included), which document and which spread a
//  view belongs to, and the panorama behind it.
//
//  ★This file holds **no state at all**. It used to carry a "last document hit" hint, and that
//    went when KCMFindDocDbForView's fallback did (the measurement is on that function).
//
//  UI side: every function takes or returns an IControlView, so the model plug-in cannot touch
//  them. ★Nothing on the model side calls in here -- what is left over there are **comments**
//  saying "this used to be called from here", not calls (measured across every file).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "PersistUtils.h"			// ::GetUIDRef(doc) -- view -> document -> db
#include "IDataBase.h"
#include "IDocument.h"
#include "IControlView.h"
#include "IEventUtils.h"			// GetGlobalMouseLocation -- the mouse in screen coordinates
#include "IWindow.h"
#include "IWindowUtils.h"			// QueryWindowUnderPoint -- the window under the mouse
#include "IDocumentPresentation.h"
#include "IPanelControlData.h"		// FindWidget -- the hit test, and the representative view
#include "IPanorama.h"				// KCMQueryPanorama
#include "IWidgetParent.h"			// same -- walk to the parent when the view has no panorama
#include "LayoutUIID.h"				// kLayoutWidgetID / kLayoutSecondaryPanelWidgetID
#include "ILayoutControlData.h"		// GetDocument / GetSpreadRef -- the official route from a view to its document and its current spread
#include "PMPoint.h"
#include "PMReal.h"

#include "KCMViewLookup.h"
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// IsDocDBOpen -- is the db the official route answered with still open

//========================================================================================
// The shared mouse-position and hit-test helpers. The peek and the colour sampler work the
// cursor position out the same way, through these.
//========================================================================================
bool16 KCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY)
{
	outX = 0.0; outY = 0.0;
	if (view == nil)
		return kFalse;
	// The mouse: screen -> window -> content (pasteboard) coordinates.
	GSysPoint gm = Utils<IEventUtils>()->GetGlobalMouseLocation();
	PMPoint pt((PMReal)gm.x, (PMReal)gm.y);
	pt = view->GlobalToWindow(pt);
	view->WindowToContentTransform(&pt);
	outX = pt.X();
	outY = pt.Y();
	return kTrue;
}

// The layout view under the mouse, split windows included. See KCMViewLookup.h.
IControlView* KCMQueryViewUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	InterfacePtr<IPanelControlData> hitPanelData(hitPres, UseDefaultIID());
	if (hitPanelData == nil)
		return nil;

	// ★What is being looked up is a widget, so it is asked for with a widget id. kLayoutWidgetBoss
	//   is a ClassID (kClassIDSpace, kLayoutUIPrefix + 3) and kLayoutWidgetID a WidgetID
	//   (kWidgetIDSpace, the same + 3): **the numbers are equal, so either one works** -- DECLARE_PMID
	//   only declares an enum (IDFactory.h:48), and an id space is not enforced by the type.
	//   ★The product passes the ClassID for the primary pane and kLayoutWidgetID for the secondary one
	//   **inside the same function** (spellpanel, PrivateSpellingUtils::InvalTextRange), so the two are
	//   interchangeable there too; **the one that matches the question** is the widget id.
	//   KCMScrollMap.cpp asks the same way for the same reason.
	IControlView* primaryView = hitPanelData->FindWidget(kLayoutWidgetID);
	if (primaryView == nil)
		return nil;

	// primaryView is used **only** to convert global to window coordinates: every child widget shares
	// the same window coordinate system, so any of them would answer the same. The view actually under
	// the mouse is settled by the FindWidget(windowPt) hit test below, and anything that is not the
	// canvas (a ruler, say) falls back to primaryView.
	IControlView* hitView = primaryView;
	const PMPoint globalPM((PMReal)globalPt.x, (PMReal)globalPt.y);
	const PMPoint winPM = primaryView->GlobalToWindow(globalPM);
	SysPoint winPt;
	winPt.x = ::ToInt32(winPM.X());
	winPt.y = ::ToInt32(winPM.Y());

	// ★GetWidgetID() answers with a widget id, so what it is compared against is one as well (as above).
	// ★★Measured: in a split window GetAllLayoutViews returns two views, **both carrying the widget id
	//   kLayoutWidgetID**, and **both carrying ILayoutControlData whose GetDocument() answers their own
	//   document**. ⇒ landing on the secondary pane's view itself is settled by the first branch below.
	IControlView* pointHit = hitPanelData->FindWidget(winPt);
	if (pointHit != nil)
	{
		if (pointHit->GetWidgetID() == kLayoutWidgetID)
		{
			hitView = pointHit;		// primary or secondary, the view itself carries this id
		}
		else if (pointHit->GetWidgetID() == kLayoutSecondaryPanelWidgetID)
		{
			// ★The secondary **panel** was hit, not the view inside it (its margin, say). A panel has no
			//   panorama of its own, so returning it as it stands would have KCMQueryPanorama walk up to
			//   the parent and take **the PRIMARY pane's panorama**. The product looks the inner view up
			//   again at this point (splitPanelData->FindWidget(kLayoutWidgetID) in spellpanel), so this
			//   does the same. When that fails, primaryView stands ＝ what it did before.
			InterfacePtr<IPanelControlData> splitPanelData(pointHit, UseDefaultIID());
			IControlView* splitView = (splitPanelData != nil) ? splitPanelData->FindWidget(kLayoutWidgetID) : nil;
			if (splitView != nil)
				hitView = splitView;
		}
	}

	hitView->AddRef();	// the same contract as QueryFrontView(): +1 ref, the caller releases
	return hitView;
}

// Which spread this view is currently showing. KCMViewLookup.h carries the whole reason the
// question has to be put to the window at all. Same boss and same interface as
// KCMFindDocDbForView below; only the question differs.
// ⚠There is no fallback -- when it cannot be read the answer is kInvalidUID and the caller drops
//   back to walking everything. That is wrong only while a master spread is on screen; for an
//   ordinary spread it lands on the same page.
UID KCMQuerySpreadUIDForView(IControlView* view)
{
	if (view == nil)
		return kInvalidUID;
	InterfacePtr<ILayoutControlData> layoutData(view, IID_ILAYOUTCONTROLDATA);
	if (layoutData == nil)
		return kInvalidUID;
	return layoutData->GetSpreadRef().GetUID();
}

// Which document's layout view this is (see KCMViewLookup.h). A shared helper: the colour
// sampler's window guard puts the same question.
IDataBase* KCMFindDocDbForView(IControlView* view)
{
	if (view == nil)
		return nil;

	// ★The official route: the layout view boss carries ILayoutControlData, which answers with the
	//   document that view is showing. The view itself is asked, so the answer does not depend on what
	//   an enumeration API happens to return. Contract: ILayoutControlData.h:181; the product does the
	//   same in CPathCreationTracker.cpp:277-285 (and in CusDtLnkUIDDTargetFlavorHelper.cpp:197 /
	//   BscDNDCustomFlavorHelper.cpp:194). That the interface really is on the boss was confirmed
	//   against a live dump (kLayoutWidgetBoss + IID_ILAYOUTCONTROLDATA).
	//   ★GetDocument() answers with a raw pointer it does NOT AddRef ＝ nothing to release.
	//
	// ★★**This one route is the whole function.** It used to be followed by a fallback -- try the db
	//   that was hit last, and failing that match the view pointer against every document's
	//   GetAllLayoutViews -- kept for one reason only: it was unknown whether the second pane of a
	//   split window answers ILayoutControlData. **A diagnostic build settled it**: GetAllLayoutViews
	//   returns both panes, both carry ILayoutControlData, and GetDocument() answers each pane's own
	//   document. The fallback could therefore never run, and it went.
	//   ⚠**If it ever has to come back**: the only thing it guaranteed beyond this was "the db handed
	//   back always belongs to an open document" (its IDocumentList walk assured that silently), and
	//   the IsDocDBOpen below now says so explicitly.
	InterfacePtr<ILayoutControlData> layoutData(view, IID_ILAYOUTCONTROLDATA);
	if (layoutData == nil)
		return nil;
	IDocument* doc = layoutData->GetDocument();
	if (doc == nil)
		return nil;

	// ★Check that it is alive, explicitly. Asking the view does not say whether that document is
	//   still open, so it is asked here, and KCM's rule across the whole plug-in -- never carry or
	//   dereference a closed db -- holds.
	IDataBase* db = ::GetUIDRef(doc).GetDataBase();
	return Utils<IKCMCompareFacade>()->IsDocDBOpen(db) ? db : nil;
}


// The IPanorama behind a view. Page-item child widgets carry no panorama of their own, so this
// walks self -> parent (the layout widget) exactly as CTracker::QueryPanorama does. The caller
// releases.
//
// ★It belongs on the UI side rather than in the drawing engine because an IPanorama answers "which
//   part of the window is on screen" -- a question with no answer when there is no window.
IPanorama* KCMQueryPanorama(IControlView* view)
{
	if (view == nil)
		return nil;
	IPanorama* pano = (IPanorama*)view->QueryInterface(IID_IPANORAMA);
	if (pano != nil)
		return pano;
	InterfacePtr<IWidgetParent> parent(view, IID_IWIDGETPARENT);
	if (parent == nil)
		return nil;
	return (IPanorama*)parent->QueryParentFor(IID_IPANORAMA);
}

// end of KCMViewLookup.cpp
