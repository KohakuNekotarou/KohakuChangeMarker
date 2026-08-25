//========================================================================================
//
//  KCMDocResponder.cpp
//
//  Document-close responder for the kAfterCloseDocSignalResponderService signal, which fires
//  only when a document close has actually completed (a close cancelled from the save-changes
//  dialog does NOT fire it). On that signal we hand off to KCMHandleDocsClosed(), which sweeps
//  every tracked database (marks / original images / toast / peek arm) against the live document
//  list and cleans up whichever ones vanished, then refreshes the panel ON->OFF.
//
//  There is no ServiceProvider here on purpose: a boss that answers ONE signal names the API's own
//  provider implementation in the .fr (kAfterCloseDocSignalRespServiceImpl, DocumentID.h) and
//  writes only the responder. CServiceProvider + HasMultipleIDs is for a boss that answers several
//  signals at once, the shape linksui/ClosingDocumentsResponder.cpp needs for its three.
//  (A hand-written KCMDocServiceProvider did this until 2026-08-06 - it did exactly what the
//  API's implementation does.)
//
//  We deliberately use AfterClose rather than BeforeClose: BeforeClose can be followed by a user
//  cancel, so DROPPING the marks there would drop them for a close that never happened. AfterClose
//  gives no usable document in the signal data either, so identifying "which db closed" is done by
//  a liveness sweep (KCMHandleDocsClosed) rather than from the signal.
//
//  ***** THE OFFICIAL SHAPE IS A TWO-STEP, AND IT DOES NOT HAVE THAT PROBLEM. ***** The argument
//  above only rules out DROPPING on BeforeClose - it does not rule out LISTENING on it. Adobe's own
//  linksui does precisely that (ClosingDocumentsResponder.cpp:150-186): on kBeforeCloseDoc it builds
//  a key from the document (path + IDataBase::GetDocumentID) and merely REMEMBERS it; on
//  kAfterCloseDoc - where its own comment states "Document is null in the signal data at this point
//  of time" - it uses the remembered key. A cancelled close just leaves a stale key that the next
//  one overwrites, so nothing is dropped early. That names the closed document exactly, no sweep.
//
//  WHY WE STILL SWEEP - a trade, not an oversight. Adopting the two-step means answering two
//  signals from one boss (the CServiceProvider + HasMultipleIDs shape) and rebuilding
//  KCMHandleDocsClosed around "drop this one" instead of "drop whatever vanished". The sweep also
//  does something the two-step cannot: it catches databases that went away without a close signal
//  this plug-in saw. Revisit if the sweep ever becomes expensive.
//  Full reasoning: docs/ai-notes/kescm-api-audit-b9-2026-08-16.md
//
//  The sweep's own hazard is known and already closed: on a background thread every database is a
//  CLONE with a different pointer, so "not in the document list" is always true there and the sweep
//  would drop everything. The guard sits at the single entry of KCMHandleDocsClosed
//  (IsMainThreadDomain), not here - it has three callers and this is one question
//  ([[one-question-one-place]]).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "isignalmgr.h"
#include "IDocumentSignalData.h"	// GetDocument() - the UIDRef of the document about to close

// Implementation includes:
#include "CResponder.h"
#include "KCMCore.h"				// KCMHandleDocsClosed
#include "KCMHideUnchanged.h"		// KCMResetHideUnchanged / the two hidden-side getters
#include "KCMID.h"
#include "KCMThreadSafety.h"		// KCMIsMainThread

/** KCMDocResponder
	Responds to the "after close document" signal by cleaning up any of KCM's
	tracked, now-closed documents. Implements IResponder via the CResponder partial
	implementation.
*/
class KCMDocResponder : public CResponder
{
public:
	KCMDocResponder(IPMUnknown* boss) : CResponder(boss) {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KCMDocResponder, kKCMDocResponderImpl)

void KCMDocResponder::Respond(ISignalMgr* /*signalMgr*/)
{
	// The service provider only registers kAfterCloseDocSignalResponderService, so any
	// call here means a document just finished closing. We do not need the signal's
	// document UIDRef (it is invalid after close); KCMHandleDocsClosed() figures out
	// which tracked databases are gone by checking them against the live document list.
	KCMHandleDocsClosed();
}

/** KCMBeforeSaveDocResponder
	Undoes this plug-in's one edit to the USER'S document before that document reaches the disk.

	***** WHY THE SAVE AND NOT THE CLOSE. ***** Everything else KCM does on a close is dropping
	its OWN state, and that belongs on AfterClose (see the top of this file). Hide Unchanged is the
	exception because it changed the user's document: kHideSpreadCmdBoss is a persistent edit, so
	spreads left hidden are hidden in the saved .indd - and hidden spreads do not print and do not
	export, which is a page silently missing from whatever gets sent out.

	⚠ MEASURED, in this order (2026-08-19, critical recheck axis 1):

	  1. With Hide Unchanged ON, closing the documents left them hidden in the file. Closing them
	     one at a time broke whichever was closed FIRST (reversing the order reversed which one
	     broke); "Close All" broke BOTH. Stopping the comparison first restored them correctly, so
	     the close was the only broken path. The immediate cause is that KCMResetHideUnchanged
	     asks KCMIsDocDBOpen before restoring, and by kAfterCloseDoc the answer is no - it then
	     drops the remembered spread list without using it, and returns void, so nothing says so.

	  2. ★So a kBeforeCloseDoc responder was written - and it did NOT fix it. A diagnostic build
	     reported IDataBase::IsModified() == 0 on entry to that signal: ***** BY kBeforeCloseDoc THE
	     SAVE HAS ALREADY HAPPENED. ***** Restoring there put the spreads back on screen and made
	     the document dirty again, while the bytes on disk still had them hidden.

	⇒ The hook belongs on the SAVE. kBeforeSaveDoc covers both ways a document reaches the disk:
	File > Save, and the save that a close performs on the way out.

	★The spreads are NOT hidden again afterwards (user's call, 2026-08-19). Re-hiding would need
	kAfterSaveDoc and would immediately re-dirty the document, so every save would leave unsaved
	changes behind and saving again would repeat it. Restoring and stopping there means what is on
	disk is always the honest document, and the user turns the toggle back on if they still want it.

	***** SAVE AS IS DELIBERATELY NOT COVERED, AND THAT IS THE FEATURE. ***** (User's call,
	2026-08-19.) Save As and Save a Copy raise their own signals
	(kBeforeSaveAsDocSignalResponderService / kBeforeSaveACopyDocSignalResponderService,
	DocumentID.h:318,321), which this responder does not answer, so those paths write the document
	with its spreads STILL HIDDEN. That gives the user a clean division:

	    Save / closing save  -> spreads restored, the original file stays honest
	    Save As              -> spreads kept hidden, for when a hidden-spread copy is what you want

	★MEASURED, not inferred (2026-08-19): with Hide Unchanged ON, document.save(File) produced a
	file that reopened as [H,-,H,H] - hidden on disk - while the same state saved over the original
	reopened as [-,-,-,-]. ⚠If a signal is ever added here, that division disappears; say so first.
*/
class KCMBeforeSaveDocResponder : public CResponder
{
public:
	KCMBeforeSaveDocResponder(IPMUnknown* boss) : CResponder(boss) {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KCMBeforeSaveDocResponder, kKCMBeforeSaveResponderImpl)

void KCMBeforeSaveDocResponder::Respond(ISignalMgr* signalMgr)
{
	// ⚠ Restoring runs a command (kHideSpreadCmdBoss with kFalse), and this plug-in is
	// kModelPlugIn, so this responder can be reached on a background thread with a CLONE of the
	// database. A clone never matches the pointers we remembered, so the test below would be false
	// anyway - but a model change must not be attempted from there at all, and the guard says why
	// rather than relying on that. The same question is asked once, at the entry of
	// KCMHandleDocsClosed, for the sweep ([[one-question-one-place]]).
	if (!KCMIsMainThread())
		return;

	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;

	IDataBase* const db = signalData->GetDocument().GetDataBase();
	if (db == nil)
		return;

	// Restore only when the document being saved is one of the two we hid into. An unrelated third
	// document must not clear the toggle.
	// ★Both sides are restored, not just this one: they were hidden as one undo step and the
	// remembered lists are dropped together, so putting back only half would leave the other half
	// hidden with nothing remembering it - the very state this responder exists to prevent.
	// The other side is usually not being saved in the same breath, but it is still open, and
	// leaving it hidden would just move the problem to its own save. KCMResetHideUnchanged
	// checks liveness per side internally, so a side that has already gone is skipped.
	if (db == KCMGetHideUnchangedDB() || db == KCMGetHideUnchangedSrcDB())
		KCMResetHideUnchanged(kTrue);
}

// End, KCMDocResponder.cpp.
