//========================================================================================
//
//  KCMViewLookup.h
//
//  Questions asked of layout views: where the mouse is in a view's content coordinates,
//  which view the mouse is actually over (split windows included), which document and which
//  spread a view belongs to, and the panorama behind it.
//
//  UI side: every function here takes or returns an IControlView, which a model plug-in must
//  not depend on. ★Nothing on the model side calls in here -- what is left over there are
//  comments saying "this used to be called from here" (measured across every file).
//
//========================================================================================

#ifndef __KCMViewLookup_h__
#define __KCMViewLookup_h__

#include "BaseType.h"
#include "PMReal.h"
#include "OMTypes.h"	// UID(KCMQuerySpreadUIDForView)

class IControlView;
class IDataBase;
class IPanorama;

// Read the current mouse position in this view's content (pasteboard) coordinates
// (screen -> window -> content). kFalse when view is nil.
bool16			KCMQueryMouseContentPoint(IControlView* view, PMReal& outX, PMReal& outY);

// The layout view actually under the mouse, split windows included.
//
// ILayoutUIUtils::QueryFrontView() returns one representative view of the front
// presentation without looking at the mouse, so with a document in Split Window it always
// answers the original pane. This walks QueryWindowUnderPoint ->
// IPanelControlData::FindWidget(windowPt) instead, so the pane the mouse is really over
// wins. Same contract as QueryFrontView(): +1 ref, the caller releases (use InterfacePtr).
// nil when nothing is found.
//
// ★Measured: in a split window BOTH panes' layout views carry the same widget id,
// kLayoutWidgetID. Landing on the secondary PANEL instead (its margin) is handled the way the
// product does it -- ask that panel for its kLayoutWidgetID child -- so a widget without a
// panorama is never returned.
IControlView*	KCMQueryViewUnderMouse();

// Which document's layout view this is, or nil.
//
// The layout view boss answers ILayoutControlData::GetDocument() (ILayoutControlData.h:181;
// the model is CPathCreationTracker.cpp:277-285). nil when the view has no layout control
// data, or when the document it names is no longer open.
//
// ★There is no fallback, and that is not an oversight: a split window's second pane answers
// ILayoutControlData exactly as the first does (measured -- the .cpp carries it), so asking
// the view itself is always enough.
IDataBase*		KCMFindDocDbForView(IControlView* view);

// ★★Which spread this view is CURRENTLY SHOWING, or kInvalidUID.
//
// ILayoutControlData::GetSpreadRef() -- "current spread UIDRef, the spread this view is
// currently viewing" (ILayoutControlData.h:252-256). Same interface, same view boss, as
// KCMFindDocDbForView above; only the question differs.
//
// ⚠ WHY THIS IS NEEDED AT ALL: a master spread and the ordinary spreads OVERLAP in pasteboard
// coordinates (measured). So "which page is under the mouse" cannot be answered from
// the point alone -- the same point belongs to a page of the master AND to a page of an ordinary
// spread, and only the window knows which of them is on screen. The model therefore cannot
// answer it; the UI observes the spread and passes it down. The full story is on the model side,
// in source/KCMCore.h (the onlySpreadUID comment) -- not in any header this plug-in can include.
UID				KCMQuerySpreadUIDForView(IControlView* view);

// The visible region (IPanorama) behind a view: scroll position and zoom live there. Page-item
// child widgets have no panorama of their own, so this walks self -> parent (the layout widget)
// the way CTracker::QueryPanorama does. +1 ref, the caller releases (use InterfacePtr). nil for
// a nil view or a widget with no layout widget above it.
//
// ★It sits on the UI side rather than in the drawing engine because an IPanorama answers
// "which part of the window is on screen" -- a question with no answer when there is no window.
IPanorama*		KCMQueryPanorama(IControlView* view);

#endif // __KCMViewLookup_h__
