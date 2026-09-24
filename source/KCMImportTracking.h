//========================================================================================
//
//  KCMImportTracking.h
//
//  The import writes under Track Changes, signed "KohakuChangeMarker" (2026-09-24, the user's
//  decision - design docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md, section 11).
//
//========================================================================================

#pragma once
#ifndef __KCMImportTracking_h__
#define __KCMImportTracking_h__

#include "PMString.h"
#include "UIDRef.h"

/** The name the import's changes are recorded under. */
extern const char* const kKCMImportAuthorName;

/** The application's user name switched to kKCMImportAuthorName for as long as this lives, and put
	back when it dies - on success, cancel and failure alike, which is why it is an object rather than
	two calls. ★The change history records the name the application has AT THE MOMENT OF EACH EDIT
	(measured 2026-09-24, work/kcm-table-spike/track.jsx), so this is what signs the import's changes,
	and what lets a reader reject all of them at once by user. */
class KCMImportAuthor
{
public:
	KCMImportAuthor();
	~KCMImportAuthor();
private:
	KCMImportAuthor(const KCMImportAuthor&);
	KCMImportAuthor& operator=(const KCMImportAuthor&);
	PMString fOld;
	bool16 fSwitched;
};

/** Change tracking ON for one story for as long as this lives, and back to what it was when it dies.
	★A story the reader was already tracking is left alone both ways, so their own setting survives
	 the import (design 11-1 item 3); the changes recorded meanwhile stay either way. */
class KCMStoryTrackingOn
{
public:
	explicit KCMStoryTrackingOn(const UIDRef& story);
	~KCMStoryTrackingOn();
private:
	KCMStoryTrackingOn(const KCMStoryTrackingOn&);
	KCMStoryTrackingOn& operator=(const KCMStoryTrackingOn&);
	UIDRef fStory;
	bool16 fWas;
};

#endif // __KCMImportTracking_h__
