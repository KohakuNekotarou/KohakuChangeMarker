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
#include "OMTypes.h"		// ClassID

class IDataBase;

// Emit one of the kKESCM*Message notifications (KESCMID.h) on the application's subject.
// Safe to call when nothing is listening and safe to call during shutdown (it checks that
// the session and the application are still there and returns quietly if not).
void	KESCMNotify(ClassID theChange);

// Emit a notification that carries WHICH DOCUMENTS it is about (2026-08-13, Task 10).
//
// A notification can only carry a ClassID, so anything else the UI needs has to be left here
// for it to pick up. That is not a workaround bolted on: it is the same shape KESCMNotifyStatus
// already uses for the status text, and it keeps the state on the model side, which is where
// the split says it belongs. The UI reads these inside Update(), and Change() is synchronous,
// so what it reads is always what this call just stored.
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
void	KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, bool16 navReset = kFalse);

// Three-document form. Closing a document can leave THREE survivors that all need their
// thumbnails rebuilt -- the compare target, the document the "original" overlay came from, and
// the one carrying the source-side frames -- and they are not always the same document.
// ⚠ Only ever pass documents that have been checked to be still open: the receiver dereferences
// them (a closed IDataBase* is a dangling pointer whose address gets reused).
void	KESCMNotifyDocs(ClassID theChange, IDataBase* docA, IDataBase* docB, IDataBase* docC, bool16 navReset);

// The values left by the last KESCMNotifyDocs. Meaningful only while handling that
// notification -- do not cache them (a database pointer outlives nothing here).
IDataBase*	KESCMNotifiedDocA();
IDataBase*	KESCMNotifiedDocB();
IDataBase*	KESCMNotifiedDocC();		// nil unless the three-document form was used
bool16		KESCMNotifiedNavReset();

// Set the status text and emit kKESCMStatusTextMessage.
//
// The string is KEPT HERE, on the model side, not in the panel. Two reasons: app.kcmStatus
// (KESCMScriptProvider.cpp, also model side) must be able to answer while the panel is
// CLOSED, and the panel's widgets are rebuilt every time it is re-shown, so a value stored
// in a widget would not survive. The UI reads the string back with KESCMGetSessionStatus
// when it receives the notification, and again from AutoAttach when the panel re-appears.
//
// forceRedrawNow is passed through to the listener: kTrue means "paint before you return",
// which the comparison loop needs because it is about to block.
void	KESCMNotifyStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// kTrue when the last KESCMNotifyStatus asked for an immediate repaint. The UI observer reads
// this to decide whether to force the paint; it is part of the notification, not of the text.
bool16	KESCMStatusWantsForceRedraw();

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
