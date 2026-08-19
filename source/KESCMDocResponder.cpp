//========================================================================================
//
//  KESCMDocResponder.cpp
//
//  Document-close responder for the kAfterCloseDocSignalResponderService signal, which fires
//  only when a document close has actually completed (a close cancelled from the save-changes
//  dialog does NOT fire it). On that signal we hand off to KESCMHandleDocsClosed(), which sweeps
//  every tracked database (marks / original images / toast / peek arm) against the live document
//  list and cleans up whichever ones vanished, then refreshes the panel ON->OFF.
//
//  There is no ServiceProvider here on purpose: a boss that answers ONE signal names the API's own
//  provider implementation in the .fr (kAfterCloseDocSignalRespServiceImpl, DocumentID.h) and
//  writes only the responder. CServiceProvider + HasMultipleIDs is for a boss that answers several
//  signals at once, the shape linksui/ClosingDocumentsResponder.cpp needs for its three.
//  (A hand-written KESCMDocServiceProvider did this until 2026-08-06 - it did exactly what the
//  API's implementation does.)
//
//  We deliberately use AfterClose rather than BeforeClose: BeforeClose can be followed by a user
//  cancel, so DROPPING the marks there would drop them for a close that never happened. AfterClose
//  gives no usable document in the signal data either, so identifying "which db closed" is done by
//  a liveness sweep (KESCMHandleDocsClosed) rather than from the signal.
//
//  ***** THE OFFICIAL SHAPE IS A TWO-STEP, AND IT DOES NOT HAVE THAT PROBLEM. ***** The argument
//  above only rules out DROPPING on BeforeClose - it does not rule out LISTENING on it. Adobe's own
//  linksui does precisely that (ClosingDocumentsResponder.cpp:150-186): on kBeforeCloseDoc it builds
//  a key from the document (path + IDataBase::GetDocumentID) and merely REMEMBERS it; on
//  kAfterCloseDoc - where its own comment states "Document is null in the signal data at this point
//  of time" - it uses the remembered key. A cancelled close just leaves a stale key that the next
//  one overwrites, so nothing is dropped early. That names the closed document exactly, no sweep.
//
//  ***** BUT BeforeClose IS ALREADY TOO LATE TO CHANGE WHAT GETS WRITTEN. ***** MEASURED
//  2026-08-19 with a temporary diagnostic build: on entry to a kBeforeCloseDoc responder,
//  IDataBase::IsModified() was 0 - the document was already clean, i.e. THE SAVE HAD ALREADY
//  HAPPENED. Editing the model there put the change on screen and made the document dirty again,
//  but the bytes on disk were the old ones, and nothing saved them a second time.
//  => "Before the close" is, as far as the FILE is concerned, after. Anything that has to be true
//  of the saved bytes belongs on kBeforeSaveDoc (which fires both for File > Save and for the save
//  a close performs), not here. Listening on BeforeClose is still fine for anything that only
//  needs to know a close is coming.
//  (Context: this was found while trying to un-hide Hide Unchanged's spreads before they reached
//  the file. That turned out not to be wanted - see KESCMHideUnchanged.h - but the timing fact
//  stands and is worth keeping, because the name of the signal suggests the opposite.)
//
//  WHY WE STILL SWEEP - a trade, not an oversight. Adopting the two-step means answering two
//  signals from one boss (the CServiceProvider + HasMultipleIDs shape) and rebuilding
//  KESCMHandleDocsClosed around "drop this one" instead of "drop whatever vanished". The sweep also
//  does something the two-step cannot: it catches databases that went away without a close signal
//  this plug-in saw. Revisit if the sweep ever becomes expensive.
//  Full reasoning: docs/ai-notes/kescm-api-audit-b9-2026-08-16.md
//
//  The sweep's own hazard is known and already closed: on a background thread every database is a
//  CLONE with a different pointer, so "not in the document list" is always true there and the sweep
//  would drop everything. The guard sits at the single entry of KESCMHandleDocsClosed
//  (IsMainThreadDomain), not here - it has three callers and this is one question
//  ([[one-question-one-place]]).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "isignalmgr.h"

// Implementation includes:
#include "CResponder.h"
#include "KESCMCore.h"		// KESCMHandleDocsClosed
#include "KESCMID.h"

/** KESCMDocResponder
	Responds to the "after close document" signal by cleaning up any of KESCM's
	tracked, now-closed documents. Implements IResponder via the CResponder partial
	implementation.
*/
class KESCMDocResponder : public CResponder
{
public:
	KESCMDocResponder(IPMUnknown* boss) : CResponder(boss) {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KESCMDocResponder, kKESCMDocResponderImpl)

void KESCMDocResponder::Respond(ISignalMgr* /*signalMgr*/)
{
	// The service provider only registers kAfterCloseDocSignalResponderService, so any
	// call here means a document just finished closing. We do not need the signal's
	// document UIDRef (it is invalid after close); KESCMHandleDocsClosed() figures out
	// which tracked databases are gone by checking them against the live document list.
	KESCMHandleDocsClosed();
}

// End, KESCMDocResponder.cpp.
