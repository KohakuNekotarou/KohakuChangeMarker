//========================================================================================
//
//  KESCMModelNotify.h
//
//  How the model tells the UI that something changed -- the only direction that is allowed.
//
//  Created 2026-08-13 for the model/UI split (Stage 1).
//
//  The model does not call into the UI. Two reasons, both real:
//    - after Stage 2 the UI lives in a different .pln, so a free function call would not link;
//    - the UI's PDF export runs on a background thread, where a UI plug-in's bosses are not
//      visible at all (measured; see [[model-ui-plugin-separation]]).
//  So the model does not know the UI exists. It emits a notification on the application's
//  subject with a protocol IID of our own, and whoever is listening reacts. If nobody is
//  listening nothing happens, which is what makes this safe under InDesign Server.
//
//  This is the same shape KESCM already uses in three places -- IID_IKESCMDOCSCLOSEDOBSERVER,
//  IID_IKESCMLAYOUTSYNCOBSERVER, IID_IKESCMPANELVISIBILITYOBSERVER -- so no new mechanism is
//  introduced. In particular we do NOT AddIn an ISubject: the application already has one,
//  and adding a stock implementation to somebody else's boss is how you collide with another
//  vendor and fail to load.
//
//========================================================================================

#ifndef __KESCMModelNotify_h__
#define __KESCMModelNotify_h__

#include "BaseType.h"
#include "PMString.h"
#include "OMTypes.h"		// ClassID / UID

#include <set>			// KESCMNotifyPayload::fPagesA(どのページの絵が変わったか)

class IDataBase;

// What a notification carries besides its ClassID.
//
// ★★2026-08-15 (API audit B2): THIS USED TO BE FOUR STATICS IN THE .cpp, on a belief that was
// written down in this very header -- "a notification can only carry a ClassID". It can carry
// more. ISubject::Change takes a third argument, `void* changedBy` (ISubject.h:150), and it
// reaches the listener as IObserver::Update's fourth. Adobe's own product code uses it for plain
// data rather than for the "object that caused the change" its wording suggests:
// open/components/linksui/EditOriginalResumeObserver.cpp:127 reads it back as
// `const PMIID& what = *((const PMIID*)changedBy);`
//
// ★WHY IT MATTERS HERE, beyond being the documented route. Change() is synchronous, so a struct
// on the emitting stack outlives the whole delivery -- which makes the payload per-call and
// per-thread. Statics in a MODEL plug-in are neither: background threads get their own databases
// but SHARE the statics ([[model-plugin-thread-safety]]). Nothing emitted a notification off the
// main thread (all 67 call sites were counted; none is in KESCMDrawEventHandler.cpp, the only
// path that runs on one) and no listener wrote back into the model from inside Update(), so the
// statics were in fact safe -- but safe as a property of TODAY'S CALLERS, not of the structure.
// That is the same shape as the bug stage 2 actually hit: KESCMHandleDocsClosed was correct for
// as long as this was a UI plug-in and became wrong the moment it was not.
//
// ⚠ Valid only while the notification is being delivered. A listener must not keep the pointers:
// a closed IDataBase* is a dangling pointer whose address gets reused.
struct KESCMNotifyPayload
{
	IDataBase*	fDocA;				// the documents the change is about (see KESCMNotifyDocs)
	IDataBase*	fDocB;
	IDataBase*	fDocC;				// nil unless the three-document form was used
	bool16		fNavReset;			// kTrue when the change invalidates the Prev/Next cursor
	bool16		fStatusForceRedraw;	// kTrue when a status change asks for an immediate repaint

	// ★★2026-08-16 (API audit B4): WHICH PAGES of fDocA had their picture change, when the
	// emitter knows. nil means "not known" and the listener must fall back to redoing the whole
	// document -- which is what EVERY page-flag notification used to do, on the same wrong belief
	// the four statics above were built on ("a notification can only carry a ClassID").
	// KESCMThumbnailRefresh.h called that fallback "a temporary regression" when it introduced it
	// (Task 10); this field is what ends it.
	//
	// ⚠ Same lifetime rule as the pointers above: valid only while the notification is being
	// delivered. Point it at a set on the emitting stack; Change() is synchronous, so it outlives
	// the delivery and nothing has to be cleaned up.
	const std::set<UID>*	fPagesA;

	// ★★2026-08-16 (API audit B5): fDocB's set. B4 wrote here "add fPagesB the day a two-document
	// emitter appears" -- and that day had already come and gone: KESCMRefreshComparisonCore
	// (Refresh Page Comparison) touches pages in BOTH documents and had been collecting both sets
	// all along, right next to a comment claiming a notification could not carry them. It was not
	// found in B4 because B4 grepped its own six files; this one lives in block B5.
	//
	// ⚠ Set it only together with fDocB, and only when the set is COMPLETE for that document (see
	// KESCMNotifyDocsPages). A listener told "these pages" will not look at any other page.
	const std::set<UID>*	fPagesB;

	KESCMNotifyPayload()
		: fDocA(nil), fDocB(nil), fDocC(nil), fNavReset(kFalse), fStatusForceRedraw(kFalse),
		  fPagesA(nil), fPagesB(nil) {}
};

// Emit one of the kKESCM*Message notifications (KESCMID.h) on the application's subject.
//
// payload is handed to the listener as Update()'s changedBy. nil -- the default -- means "this
// notification carries nothing but its ClassID", and a listener must handle that: several of the
// kKESCM*Message kinds are emitted with no payload at all.
//
// Safe to call when nothing is listening and safe to call during shutdown (it checks that
// the session and the application are still there and returns quietly if not).
void	KESCMNotify(ClassID theChange, const KESCMNotifyPayload* payload = nil);

// Emit a notification that carries WHICH DOCUMENTS it is about (2026-08-13, Task 10).
//
// The documents travel in the payload above, on Change()'s changedBy. (Until 2026-08-15 they
// travelled in statics; see the struct's comment for why that was the wrong route even though it
// worked.)
//
//   docA / docB  the target and source databases the change is about. ★They have to travel
//                with the notification rather than be asked for: by the time Stop notifies,
//                the model has already dropped its own pointers (KESCMArmedTargetDB is nil),
//                and the UI still has to purge those two documents' thumbnails.
//   navReset     kTrue when the change invalidates the Prev/Next cursor. ⚠ A FULL rebuild and
//                a Stop do; an INCREMENTAL recompare does NOT -- resetting there would send the
//                cursor back to the first change every time a page is registered, which is a
//                behaviour change a user would notice.
//
// ★2026-08-13 (Task 12) correction: this does NOT get replaced by IKESCMMarkData. Asking works
// for the CURRENT state, but the thumbnail refresh needs the pages that CHANGED -- the marks a
// recompare has just thrown away, the page whose flag was just cleared -- and no amount of asking
// recovers those. What is missing here is a page set travelling alongside the documents, in
// exactly the way the documents themselves travel. See KESCMPurgeAllPageThumbs.
//
// ★★2026-08-16 (API audit B4): that page set now exists -- for the page-flag route, below.
// ⚠ NOT for this form of the marks route. A FULL recompare (KESCMDoMarkChangesDoc) needs the set of
// pages that carried a mark BEFORE it ran, and by the time it notifies it has already dropped it;
// supplying it means saving it on the model side first (not done). So this form keeps redoing the
// whole document -- and now for a reason that is actually true.
// ★2026-08-16 (API audit B5): the PARTIAL recompare does not have that problem, because it decides
// up front which pages it is going to touch. It uses KESCMNotifyDocsPages below.
void	KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, bool16 navReset = kFalse);

// Emit a two-document notification that also carries WHICH PAGES of each document had their
// picture change (2026-08-16, API audit B5). Used by the partial recompare (Refresh Page
// Comparison), which knows its page set exactly: it picks the pages to compare before it starts.
//
//   pagesA / pagesB  the caller's own sets, by reference -- they travel on the payload as pointers
//                    and are read during delivery only.
//
// ⚠ Each set must hold EVERY page of that document whose picture can change, including pages that
// LOST something: a page whose ring disappeared, or whose ✓ was pruned away because its ring went,
// is in no current-state set at all, so asking cannot recover it. That is the whole reason the sets
// travel rather than being asked for. Miss one and its stale thumbnail stays on screen -- which is
// exactly what the whole-document purge this replaces could not get wrong.
void	KESCMNotifyDocsPages(ClassID theChange,
	                         IDataBase* docA, const std::set<UID>& pagesA,
	                         IDataBase* docB, const std::set<UID>& pagesB,
	                         bool16 navReset = kFalse);

// Emit a notification that carries WHICH PAGES of one document changed their picture
// (2026-08-16, API audit B4). Used for kKESCMPageFlagsChangedMessage: a Register or Check toggle
// changes the drawing of exactly the pages it touched, and the listener can purge just those
// instead of rebuilding every thumbnail in the document.
//
//   pages  ★the caller's own set, by reference -- it travels on the payload as a pointer and is
//          read during delivery only. It must hold EVERY page whose picture can change, including
//          the ones the flag was taken OFF: a page that just lost its green "/" is not in any
//          current-state set, so asking cannot recover it (that is the whole reason this travels).
//
// ⚠ The single-document shape is deliberate; see KESCMNotifyPayload::fPagesA.
void	KESCMNotifyPages(ClassID theChange, IDataBase* doc, const std::set<UID>& pages);

// Three-document form. Closing a document can leave THREE survivors that all need their
// thumbnails rebuilt -- the compare target, the document the "original" overlay came from, and
// the one carrying the source-side frames -- and they are not always the same document.
// ⚠ Only ever pass documents that have been checked to be still open: the receiver dereferences
// them (a closed IDataBase* is a dangling pointer whose address gets reused).
void	KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, IDataBase* docC, bool16 navReset);

// Set the status text and emit kKESCMStatusTextMessage.
//
// The string is KEPT HERE, on the model side, not in the panel. Two reasons: app.kcmStatus
// (KESCMScriptProvider.cpp, also model side) must be able to answer while the panel is
// CLOSED, and the panel's widgets are rebuilt every time it is re-shown, so a value stored
// in a widget would not survive. The UI reads the string back with KESCMGetSessionStatus
// when it receives the notification, and again from AutoAttach when the panel re-appears.
//
// forceRedrawNow travels in the payload's fStatusForceRedraw: kTrue means "paint before you
// return", which the comparison loop needs because it is about to block. ★It is part of the
// NOTIFICATION, not of the text -- which is exactly why it belongs on the payload and the text
// does not (the text is session state that app.kcmStatus answers from at any time).
void	KESCMNotifyStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// Store the status text WITHOUT emitting a notification.
//
// ★This is what the UI's own KESCMSetStatus calls. A message raised by a UI action (a menu
// item, a button, a row click) is painted by the UI directly -- it does not need to travel
// through the notification -- but it still has to be REMEMBERED here, because app.kcmStatus
// answers from this string and because the panel is rebuilt on every re-show.
//
// ⚠It must not notify: KESCMSetStatus is what the observer calls when a notification
// arrives, so notifying from here would loop.
void	KESCMStoreSessionStatus(const PMString& s);

// The last string given to KESCMNotifyStatus or KESCMStoreSessionStatus. This is what
// app.kcmStatus returns.
void	KESCMGetSessionStatus(PMString& out);

// Shutdown only: empty the stored string, so the static PMString's destructor has no live
// heap buffer to free when the plug-in unloads (Mac unload order differs from Windows).
void	KESCMClearSessionStatus();

#endif // __KESCMModelNotify_h__
