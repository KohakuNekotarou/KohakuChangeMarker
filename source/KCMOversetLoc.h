//========================================================================================
//
//  KCMOversetLoc.h
//
//  Where one overset "+" is -- the type alone.
//
//  WHY IT IS A FILE OF ITS OWN. IKCMMarkData hands these out and the navigation reads them, so
//  the UI has to know the type; it used to be reached by including KCMOversetScan.h, which also
//  declares the scan itself -- a model-side free function the UI can see and cannot link to. A
//  header that crosses the boundary should carry the type and nothing else, the way
//  KCMBookResult.h already does.
//
//  @warning a location is a PAGE plus a PASTEBOARD POINT, and it holds no database. Which
//  document it belongs to is asked of IKCMMarkData::GetOversetDB() -- one answer, in one place,
//  for a scan that only ever covers one document at a time.
//
//========================================================================================
#ifndef __KCMOversetLoc_h__
#define __KCMOversetLoc_h__

#include "UIDRef.h"		// UID
#include "PMPoint.h"	// PBPMPoint (the pasteboard coordinates of an overset "+")

// Where one overset "+" is. pageUID is the page it sits on, pb its pasteboard point -- the outport
// (bottom-right corner) of the last placed parcel, computed exactly as KBS's KBSOversetLocator
// does. This is what Prev/Next jumps to when cycling through overset.
struct KCMOversetLoc
{
	UID			pageUID;
	PBPMPoint	pb;
	KCMOversetLoc() : pageUID(kInvalidUID) {}
	KCMOversetLoc(UID p, const PBPMPoint& pt) : pageUID(p), pb(pt) {}
};

#endif // __KCMOversetLoc_h__

// End, KCMOversetLoc.h.
