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
IControlView*	KESCMQueryViewUnderMouse();

// Which document's layout view this is, or nil.
//
// The official route is asked first: the layout view boss answers ILayoutControlData
// ::GetDocument() (ILayoutControlData.h:181; the model is CPathCreationTracker.cpp:277-285).
// Only when that fails does it fall back to matching pointers across every document's
// GetAllLayoutViews.
IDataBase*		KESCMFindDocDbForView(IControlView* view);

// Drop the fallback route's "document that matched last time" hint. The hint never decides
// the answer -- it only picks which database to try first -- so correctness is unaffected,
// but after a document close, an arm change, or sync being turned off it is known stale.
// (While the official route works the fallback never runs, so this is effectively a no-op.)
void			KESCMForgetViewDbHint();

// The visible region (IPanorama) behind a view: scroll position and zoom live there. Page-item
// child widgets have no panorama of their own, so this walks self -> parent (the layout widget)
// the way CTracker::QueryPanorama does. +1 ref, the caller releases (use InterfacePtr). nil for
// a nil view or a widget with no layout widget above it.
//
// Moved here from KESCMDrawEventHandler.h on 2026-08-13 (Stage 1 Task 12): it returns an
// IPanorama, which is a question about a window, and the drawing engine is model side.
IPanorama*		KESCMQueryPanorama(IControlView* view);

#endif // __KESCMViewLookup_h__
