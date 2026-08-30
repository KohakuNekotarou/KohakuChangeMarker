//========================================================================================
//
//  KCMNotifyPayload.h
//
//  What a model -> UI notification carries besides its ClassID -- the type alone.
//
//  WHY IT IS A FILE OF ITS OWN. The model fills it and the UI's observer casts changedBy back to
//  it, so both halves need the same definition; it used to be reached by including
//  KCMModelNotify.h, which is the SENDING side -- a dozen model-side free functions the UI can
//  see and cannot link to (KCMModelChangeObserver.cpp said so itself: "the type alone is
//  borrowed"). A header that crosses the boundary should carry the type and nothing else, the way
//  KCMBookResult.h already does.
//
//  HOW IT TRAVELS is stated once, on the sending side (KCMModelNotify.h): ISubject::Change takes
//  a third argument, void* changedBy, and it reaches the listener as IObserver::Update's fourth.
//
//========================================================================================
#ifndef __KCMNotifyPayload_h__
#define __KCMNotifyPayload_h__

#include "BaseType.h"		// bool16
#include "OMTypes.h"		// UID

#include <set>			// fPagesA -- which pages had their picture change

class IDataBase;

// What a notification carries besides its ClassID.
//
// A NOTIFICATION CAN CARRY MORE THAN A ClassID, whatever KCMModelNotify.h used to claim.
// ISubject::Change takes a third argument, `void* changedBy` (ISubject.h:150), and it reaches the
// listener as IObserver::Update's fourth. Adobe's own product code uses it for plain data rather
// than for the "object that caused the change" its wording suggests:
// open/components/linksui/EditOriginalResumeObserver.cpp:127 reads it back as
// `const PMIID& what = *((const PMIID*)changedBy);`
//
// WHY A PAYLOAD AND NOT STATICS, beyond this being the documented route. Change() is synchronous,
// so a struct on the emitting stack outlives the whole delivery -- which makes the payload
// per-call and per-thread. Statics in a MODEL plug-in are neither: background threads get their
// own databases but SHARE the statics ([[model-plugin-thread-safety]]). Today every emitter is
// model-side and none of them sits in KCMDrawEventHandler.cpp, the one path that runs on a
// background thread, and no listener writes back into the model from inside Update() -- so
// statics would in fact be safe. **Safe as a property of today's callers, not of the structure**,
// which is the same shape as the bug the model/UI split actually hit: KCMHandleDocsClosed was
// correct for exactly as long as this was a UI plug-in.
//
// @warning valid only while the notification is being delivered. A listener must not keep the
// pointers: a closed IDataBase* is a dangling pointer whose address gets reused.
struct KCMNotifyPayload
{
	IDataBase*	fDocA;				// the documents the change is about (see KCMNotifyDocs)
	IDataBase*	fDocB;
	IDataBase*	fDocC;				// nil unless the three-document form was used
	bool16		fNavReset;			// kTrue when the change invalidates the Prev/Next cursor
	bool16		fStatusForceRedraw;	// kTrue when a status change asks for an immediate repaint

	// WHICH PAGES of fDocA had their picture change, when the emitter knows. nil means "not known"
	// and the listener must fall back to redoing the whole document.
	//
	// @warning same lifetime rule as the pointers above: valid only while the notification is
	// being delivered. Point it at a set on the emitting stack; Change() is synchronous, so it
	// outlives the delivery and nothing has to be cleaned up.
	const std::set<UID>*	fPagesA;

	// fDocB's set. Filled by emitters that touch pages in BOTH documents -- the partial recompare
	// (Refresh Page Comparison) is the one that does.
	//
	// @warning set it only together with fDocB, and only when the set is COMPLETE for that
	// document (see KCMNotifyDocsPages). A listener told "these pages" will not look at any other.
	const std::set<UID>*	fPagesB;

	KCMNotifyPayload()
		: fDocA(nil), fDocB(nil), fDocC(nil), fNavReset(kFalse), fStatusForceRedraw(kFalse),
		  fPagesA(nil), fPagesB(nil) {}
};

#endif // __KCMNotifyPayload_h__

// End, KCMNotifyPayload.h.
