//========================================================================================
//
//  KCMModelNotify.h
//
//  How the model tells the UI that something changed -- the only direction that is allowed.
//
//  The model does not call into the UI. Two reasons, both real:
//    - the UI lives in a different .pln, so a free function call would not link;
//    - the UI's PDF export runs on a background thread, where a UI plug-in's bosses are not
//      visible at all (measured; see [[model-ui-plugin-separation]]).
//  So the model does not know the UI exists. It emits a notification on the application's
//  subject with a protocol IID of our own, and whoever is listening reacts. If nobody is
//  listening nothing happens, which is what makes this safe under InDesign Server.
//
//  This is the same shape KCM already uses in three places -- IID_IKCMDOCSCLOSEDOBSERVER,
//  IID_IKCMLAYOUTSYNCOBSERVER, IID_IKCMPANELVISIBILITYOBSERVER -- so no new mechanism is
//  introduced. In particular we do NOT AddIn an ISubject: the application already has one,
//  and adding a stock implementation to somebody else's boss is how you collide with another
//  vendor and fail to load.
//
//========================================================================================

#ifndef __KCMModelNotify_h__
#define __KCMModelNotify_h__

#include "BaseType.h"
#include "PMString.h"
#include "OMTypes.h"		// ClassID / UID

#include <set>			// KCMNotifyPayload::fPagesA -- which pages had their picture change

class IDataBase;

// What a notification carries besides its ClassID.
//
// A NOTIFICATION CAN CARRY MORE THAN A ClassID, whatever this header used to claim.
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

// Emit one of the kKCM*Message notifications (KCMID.h) on the application's subject.
//
// payload is handed to the listener as Update()'s changedBy. nil -- the default -- means "this
// notification carries nothing but its ClassID", and a listener must handle that: several of the
// kKCM*Message kinds are emitted with no payload at all.
//
// Safe to call when nothing is listening and safe to call during shutdown (it checks that
// the session and the application are still there and returns quietly if not).
void	KCMNotify(ClassID theChange, const KCMNotifyPayload* payload = nil);

// Emit a notification that carries WHICH DOCUMENTS it is about.
//
//   docA / docB  the target and source databases the change is about. They have to TRAVEL with
//                the notification rather than be asked for: by the time Stop notifies, the model
//                has already dropped its own pointers (KCMArmedTargetDB is nil), and the UI still
//                has to purge those two documents' thumbnails.
//   navReset     kTrue when the change invalidates the Prev/Next cursor. @warning a FULL rebuild
//                and a Stop do; an INCREMENTAL recompare does NOT -- resetting there would send
//                the cursor back to the first change every time a page is registered, which is a
//                behaviour change a user would notice.
//
// THIS FORM CANNOT BE REPLACED BY ASKING IKCMMarkData. Asking works for the CURRENT state, but the
// thumbnail refresh needs the pages that CHANGED -- the marks a recompare has just thrown away,
// the page whose flag was just cleared -- and no amount of asking recovers those.
//
// @warning it carries no page set on purpose. A FULL recompare (KCMDoMarkChangesDoc) would need
// the set of pages that carried a mark BEFORE it ran, and by the time it notifies it has already
// dropped it; supplying it means saving it on the model side first, which is not done. So this
// form redoes the whole document. The PARTIAL recompare does not have that problem -- it decides
// up front which pages it will touch -- and uses KCMNotifyDocsPages below.
void	KCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, bool16 navReset = kFalse);

// Emit a two-document notification that also carries WHICH PAGES of each document had their
// picture change. Used by the partial recompare (Refresh Page Comparison), which knows its page
// set exactly: it picks the pages to compare before it starts.
//
//   pagesA / pagesB  the caller's own sets, by reference -- they travel on the payload as pointers
//                    and are read during delivery only.
//
// @warning each set must hold EVERY page of that document whose picture can change, including
// pages that LOST something: a page whose ring disappeared, or whose tick was pruned away because
// its ring went, is in no current-state set at all, so asking cannot recover it. That is the whole
// reason the sets travel. Miss one and its stale thumbnail stays on screen -- which is exactly
// what the whole-document purge this replaces could not get wrong.
void	KCMNotifyDocsPages(ClassID theChange,
	                         IDataBase* docA, const std::set<UID>& pagesA,
	                         IDataBase* docB, const std::set<UID>& pagesB,
	                         bool16 navReset = kFalse);

// Emit a notification that carries WHICH PAGES of one document changed their picture. Used for
// kKCMPageFlagsChangedMessage: a Register or Check toggle changes the drawing of exactly the pages
// it touched, and the listener can purge just those instead of every thumbnail in the document.
//
//   pages  the caller's own set, by reference -- it travels on the payload as a pointer and is
//          read during delivery only. It must hold EVERY page whose picture can change, including
//          the ones the flag was taken OFF: a page that just lost its green "/" is not in any
//          current-state set, so asking cannot recover it (that is the whole reason this travels).
//
// @warning the single-document shape is deliberate; see KCMNotifyPayload::fPagesA.
void	KCMNotifyPages(ClassID theChange, IDataBase* doc, const std::set<UID>& pages);

// Three-document form. Closing a document can leave THREE survivors that all need their
// thumbnails rebuilt -- the compare target, the document the "original" overlay came from, and
// the one carrying the source-side frames -- and they are not always the same document.
// @warning only ever pass documents that have been checked to be still open: the receiver
// dereferences them (a closed IDataBase* is a dangling pointer whose address gets reused).
void	KCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, IDataBase* docC, bool16 navReset);

// Set the status text and emit kKCMStatusTextMessage.
//
// The string is KEPT HERE, on the model side, not in the panel. Two reasons: app.kcmStatus
// (KCMScriptProvider.cpp, also model side) must be able to answer while the panel is
// CLOSED, and the panel's widgets are rebuilt every time it is re-shown, so a value stored
// in a widget would not survive. The UI reads the string back with KCMGetSessionStatus
// when it receives the notification, and again from AutoAttach when the panel re-appears.
//
// forceRedrawNow travels in the payload's fStatusForceRedraw: kTrue means "paint before you
// return", which the comparison loop needs because it is about to block. It is part of the
// NOTIFICATION, not of the text -- which is exactly why it belongs on the payload and the text
// does not (the text is session state that app.kcmStatus answers from at any time).
void	KCMNotifyStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// The same, for a message that is a plain literal -- which is what almost every one of them is.
//
// **Every message KCM raises is fixed English** (there is no jaJP string table; the few strings
// that have a Japanese form are switched at run time in KCMLoc.h), so every one of them has to be
// marked untranslatable. @warning **forgetting that mark is silent**: the string goes through the
// translation table and the reader is shown the key itself. Thirteen call sites wrote out the same
// three lines, which is thirteen chances to leave the mark off the fourteenth.
// forceRedrawNow is KCMNotifyStatus's and defaults the same way.
void	KCMSayStatus(const char* text, bool16 forceRedrawNow = kFalse);

// Store the status text WITHOUT emitting a notification.
//
// This is what the UI's own KCMSetStatus calls. A message raised by a UI action (a menu item, a
// button, a row click) is painted by the UI directly -- it does not need to travel through the
// notification -- but it still has to be REMEMBERED here, because app.kcmStatus answers from this
// string and because the panel is rebuilt on every re-show.
//
// @warning it must not notify: KCMSetStatus is what the observer calls when a notification
// arrives, so notifying from here would loop.
void	KCMStoreSessionStatus(const PMString& s);

// Store the status text SPLIT WHERE ITS COLOUR CHANGES, without emitting a notification.
//
// The panel's message area is drawn by hand and can show two colours, so that the other side of a
// clicked edit can have its differing characters at full strength and the words around them faded
// (KCMStatusTextView.cpp). This is the same store as above, told where the pieces begin: label = a
// heading on its own line, mid = the characters that differ, pre/post = the context on either side.
//
// WHY THE MODEL HOLDS THE SPLIT AND NOT THE PANEL. The panel's widgets are rebuilt on every
// re-show, so a split kept there would be lost and the message would come back in one colour --
// the same sentence looking different depending on the route it took. And the split cannot be
// re-derived by the panel: the boundary is a code point index into text already cut at both ends,
// and PMString counts UTF-16. One place answers "what does the message area say", and it is the
// place that already answered it ([[one-question-one-place]]).
//
// The fifth piece, `ruby`, is the READING drawn above the changed characters when the edit is a
// ruby one. It is stored here for exactly the reason the other four are: a reading kept anywhere
// else would come back missing while the words it belongs to came back intact.
//
// @warning it must not notify, for the same reason as above.
// @warning the ruby is NOT part of what KCMGetSessionStatus assembles -- see there.
void	KCMStoreSessionStatusSegments(const PMString& label, const PMString& pre,
										const PMString& mid, const PMString& post,
										const PMString& ruby);

// The last string given to KCMNotifyStatus or KCMStoreSessionStatus. This is what
// app.kcmStatus returns.
//
// It is ASSEMBLED from the pieces: label, a line break, then the body. A message stored as one
// string is the case where the others are empty, so the answer is that string itself -- which is
// why app.kcmStatus reads exactly as it did before the split existed.
// @warning THE RUBY IS DELIBERATELY LEFT OUT of the assembly. This answers "what does the message
// area SAY", and a reading is not part of the sentence -- it sits above it. Splicing it in would
// change what every existing script reads out of app.kcmStatus.
void	KCMGetSessionStatus(PMString& out);

// The same message, in the five pieces it was stored in. The UI reads this back when the panel
// re-appears, so that a coloured message comes back coloured.
// A message stored as one string answers with that string in `outMid` and four empty pieces.
void	KCMGetSessionStatusSegments(PMString& outLabel, PMString& outPre,
									  PMString& outMid, PMString& outPost, PMString& outRuby);

// Shutdown only: empty the stored message, so the static PMStrings' destructors have no live
// heap buffer to free when the plug-in unloads (Mac unload order differs from Windows).
// @warning ALL FIVE PIECES. A static PMString added beside its fellows and left out of this list
// is the exact shape of two defects already found in this plug-in.
void	KCMClearSessionStatus();

#endif // __KCMModelNotify_h__
