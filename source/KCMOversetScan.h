//========================================================================================
//
//  KCMOversetScan.h
//
//  The detection behind the Find Overset flyout item. It scans one document and collects the UIDs
//  of the pages holding overset text -- text that does not fit its frame, which InDesign marks
//  with a red "+". This is a check of its own and has nothing to do with the new/old comparison.
//  The implementation is KBS's overset locator logic (find the last placed frame, then climb the
//  chain of table anchors) copied inline into the .cpp, so nothing here builds against KBS.
//
//========================================================================================
#ifndef __KCMOversetScan_h__
#define __KCMOversetScan_h__

#include <vector>
#include "UIDRef.h"		// UID
#include "PMPoint.h"	// PBPMPoint (the pasteboard coordinates of an overset "+")

class IDataBase;

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

// Scan db and append one entry per piece of overset text to outLocs (the caller clears it first).
// Both kinds are detected: a story's primary thread overflowing an ordinary frame (the red "+")
// and a single table cell overflowing on its own (the red dot), each recorded as one "+"
// pasteboard point plus the page UID it belongs to -- the same per-location shape KBS uses.
// Overset in a frame that is on no page (out on the pasteboard) is skipped, having no page to
// record.
// **Before asking about overset, any stale composition is brought up to date**
// (RecomposeThruLastFrame). Overset is the *result* of composition, so asking without composing
// reports overflow the user has already fixed and stays silent about overflow that has just
// appeared. @warning composing dirties the document, so the whole scan runs inside an
// IDataBase::SaveRestoreModifiedState -- clean in, clean out. The details are at (0) in the .cpp.
void KCMCollectOversetLocations(IDataBase* db, std::vector<KCMOversetLoc>& outLocs);

// (A thin wrapper returning only the set of page UIDs, KCMCollectOversetPages, was removed. Its
//  one intended caller, KCMApplyOversetForDoc, needs **both** the locations and the pages, so
//  calling the wrapper as well would have scanned the document twice -- which is why that caller
//  folded the set inline from the start, leaving the wrapper with no caller at all.)

#endif // __KCMOversetScan_h__
