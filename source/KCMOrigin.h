//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What a document looks like in numbers.
//
//  ⛔**THIS FILE HELD THE TASK START ORIGIN UNTIL 2026-09-21.** Task Start took the document's
//  INX into memory, and every comparison against it rebuilt a copy from those bytes. Task Start
//  saves a copy to a FILE now and Start opens it (KCMTaskStartSave.h), so the whole of that -
//  the bytes, the park and unpark, the peek document, the IDML wrapper, the script writers - is
//  gone.
//
//  ★**WHAT SURVIVES IS THE ONE PIECE THAT WAS NEVER ABOUT THE ORIGIN**: a count of what a
//  document holds, which KCMRehydrate checks an import against ("did the whole thing come
//  back?"). It kept the name it had, because renaming it would touch every caller for no
//  behaviour - a rename is its own change, not a rider on a removal.
//
//========================================================================================

#ifndef __KCMOrigin_h__
#define __KCMOrigin_h__

#include "BaseType.h"

class IDataBase;

struct KCMOriginShape
{
	int32 fSpreads;
	int32 fPages;
	int32 fStories;		// user-accessible stories
	int32 fTextLen;		// ITextModel::TotalLength summed over them
	KCMOriginShape() : fSpreads(0), fPages(0), fStories(0), fTextLen(0) {}
	bool16 operator==(const KCMOriginShape& o) const
	{ return (fSpreads == o.fSpreads && fPages == o.fPages && fStories == o.fStories && fTextLen == o.fTextLen) ? kTrue : kFalse; }
};

void KCMMeasureShape(IDataBase* db, KCMOriginShape& out);

#endif // __KCMOrigin_h__

// End, KCMOrigin.h.