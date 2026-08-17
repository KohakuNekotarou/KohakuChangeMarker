//========================================================================================
//
//  KESCMViewLookup.h
//
//  Questions asked of layout views: where the mouse is in a view's content coordinates,
//  which view the mouse is actually over (split windows included), and which document a
//  view belongs to.
//
//  Split out of KESCMCore.cpp on 2026-08-13. Behaviour unchanged.
//
//  UI side: every function here takes or returns an IControlView, which a model plug-in
//  must not depend on.
//
//========================================================================================

#ifndef __KESCMViewLookup_h__
#define __KESCMViewLookup_h__

#include "BaseType.h"
#include "PMReal.h"
#include "OMTypes.h"	// UID(KESCMQuerySpreadUIDForView)

class IControlView;
class IDataBase;
class IPanorama;

// Read the current mouse position in this view's content (pasteboard) coordinates
// (screen -> window -> content). kFalse when view is nil.
bool16			KESCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY);

// The layout view actually under the mouse, split windows included.
//
// ILayoutUIUtils::QueryFrontView() returns one representative view of the front
// presentation without looking at the mouse, so with a document in Split Window it always
// answers the original pane. This walks QueryWindowUnderPoint ->
// IPanelControlData::FindWidget(windowPt) instead, so the pane the mouse is really over
// wins. Same contract as QueryFrontView(): +1 ref, the caller releases (use InterfacePtr).
// nil when nothing is found.
//
// ★Measured 2026-08-17 (audit B-U7): in a split window BOTH panes' layout views carry the
// same widget id, kLayoutWidgetID. Landing on the secondary PANEL instead (its margin) is
// handled the way the product does it -- ask that panel for its kLayoutWidgetID child --
// so a widget without a panorama is never returned.
IControlView*	KESCMQueryViewUnderMouse();

// Which document's layout view this is, or nil.
//
// The layout view boss answers ILayoutControlData::GetDocument() (ILayoutControlData.h:181;
// the model is CPathCreationTracker.cpp:277-285). nil when the view has no layout control
// data, or when the document it names is no longer open.
//
// ★Until 2026-08-17 this also carried a fallback (match the view pointer against every
// document's GetAllLayoutViews, with a "last hit" hint to skip the scan). It was kept only
// because it was unknown whether a split window's second pane answers ILayoutControlData.
// It does -- measured -- so the fallback was removed; see the .cpp for the measurement.
IDataBase*		KESCMFindDocDbForView(IControlView* view);

// ★★★Which spread this view is CURRENTLY SHOWING, or kInvalidUID (2026-08-16).
//
// ILayoutControlData::GetSpreadRef() -- "current spread UIDRef, the spread this view is
// currently viewing" (ILayoutControlData.h:252-256). Same interface, same view boss, as
// KESCMFindDocDbForView above; only the question differs.
//
// ⚠ WHY THIS IS NEEDED AT ALL: a master spread and the ordinary spreads OVERLAP in pasteboard
// coordinates (measured 2026-08-16). So "which page is under the mouse" cannot be answered from
// the point alone -- the same point belongs to a page of the master AND to a page of an ordinary
// spread, and only the window knows which of them is on screen. The model therefore cannot
// answer it; the UI observes the spread and passes it down. The full story is on the model side,
// in source/KESCMCore.h (the onlySpreadUID comment) -- not in any header this plug-in can include.
UID				KESCMQuerySpreadUIDForView(IControlView* view);

// The visible region (IPanorama) behind a view: scroll position and zoom live there. Page-item
// child widgets have no panorama of their own, so this walks self -> parent (the layout widget)
// the way CTracker::QueryPanorama does. +1 ref, the caller releases (use InterfacePtr). nil for
// a nil view or a widget with no layout widget above it.
//
// Moved here from KESCMDrawEventHandler.h on 2026-08-13 (Stage 1 Task 12): it returns an
// IPanorama, which is a question about a window, and the drawing engine is model side.
IPanorama*		KESCMQueryPanorama(IControlView* view);

#endif // __KESCMViewLookup_h__
