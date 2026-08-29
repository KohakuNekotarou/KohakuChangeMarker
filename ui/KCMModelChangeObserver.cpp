//========================================================================================
//
//  KCMModelChangeObserver.cpp
//
//  The **UI side** of "the model raised a notification, rebuild the display". The sender is
//  KCMModelNotify.cpp, and the notifications are the kKCM*Message of KCMBoundaryID.h
//  (★boundary IDs, so both halves hold the same copy -- not the model-only KCMID.h).
//
//  ★It lives on kActiveContextBoss, the same proven arrangement as the three observers already
//    there (layout sync / batch close / panel visibility). **No new mechanism was introduced.**
//
//  ★**All seven notifications are connected.**
//    ⚠**Seven kinds, six branches** ＝ Marks Rebuilt and Marks Cleared share one branch (the
//      kKCM*Message in KCMBoundaryID.h number seven). Do not mix the two counts.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CObserver.h"
#include "ISession.h"
#include "IApplication.h"
#include "IActiveContext.h"
#include "ISubject.h"
#include "PMString.h"

#include "KCMUIID.h"
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// GetSessionStatus / IsAppQuitting
#include "KCMModelNotify.h"		// KCMNotifyPayload ---- ★**the type alone is borrowed**
									// ⚠The **free functions** this header declares have bodies only on the
									//   model side, so another .pln cannot link them. **A struct definition
									//   needs no linking**, which is why the type may be borrowed -- the same
									//   shape as IKCMMarkData.h borrowing KCMOversetLoc from KCMOversetScan.h.
									//   **Do not call the functions.**
#include "KCMUIShared.h"			// KCMSetStatus (display; UI-internal) / KCMRefreshPanel
// ★Everything below is **a UI-side header**. This observer exists to turn a notification back
//   into UI work, so calling the UI is its job -- not reverse flow. Reading the model through
//   KCMCore.h is the permitted direction as well (UI -> model).
#include "IKCMMarkData.h"			// GetOversetOn (is Find Overset on by itself? = reading the model’s state)
#include "KCMPeekGesture.h"		// KCMResetPeekGestureState / KCMBatchCloseInProgress / KCMDeferCloseUi
#include "KCMThumbIdleTask.h"		// KCMScheduleThumbRefresh (defer the post-close rebuild to the next idle)
#include "KCMThumbnailRefresh.h"	// KCMPurgeAllPageThumbs / KCMRefreshThumbnailsForPages /
									// KCMForceRedrawPagesPanelNow
#include "KCMChangeNav.h"			// KCMResetNav / KCMRefreshNavPosition
#include "KCMScrollMap.h"			// KCMScrollMapAttach / DetachAll / InvalidateAll
#include "KCMStoryTree.h"			// KCMStoryTreeRebuild
#include "KCMStorySection.h"		// KCMUpdateStorySectionLabel
#include "KCMStoryPressMarks.h"	// KCMStoryMarksRefresh (keep the always-on marks in step with the result)
#include "KCMViewSync.h"			// KCMInvalidateSyncCaches

#include <set>						// combining two page sets when a document is compared with itself

/* The UI-side observer of the model's notifications. It shares kActiveContextBoss under
   IID_IKCMMODELCHANGEOBSERVER (the same host, and the same proven reason, as the layout sync
   observer), and it subscribes to the application's subject. */
class KCMModelChangeObserver : public CObserver
{
public:
	KCMModelChangeObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMMODELCHANGEOBSERVER) {}
	~KCMModelChangeObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMModelChangeObserver, kKCMModelChangeObserverImpl)

void KCMModelChangeObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	// Look only at what was sent under our own protocol, so InDesign’s notifications never mix in.
	if (protocol != IID_IKCMMODELCHANGEOBSERVER)
		return;

	// ★★The payload of the notification. What the model put in the third argument of
	//   ISubject::Change arrives as the fourth argument of Update (ISubject.h:150; InDesign’s own
	//   example is linksui's EditOriginalResumeObserver.cpp:127). It used to be kept in a model-side
	//   static and fetched back through the five facades, on the **mistaken premise that a
	//   notification can carry nothing but a ClassID**.
	// ⚠**It can be nil** (a notification with nothing attached). One empty value is prepared and the
	//   reference is pointed at it, so no branch has to test for nil.
	const KCMNotifyPayload  kEmptyPayload;
	const KCMNotifyPayload& n = (changedBy != nil) ? *(const KCMNotifyPayload*)changedBy
													 : kEmptyPayload;

	if (theChange == kKCMStatusTextMessage)
	{
		// ★The string itself belongs to the model side (it is what app.kcmStatus answers with at any
		//   time = the session’s state). "Repaint right now", on the other hand, is peculiar to this
		//   one notification, so it comes from the payload.
		PMString s;
		Utils<IKCMCompareFacade>()->GetSessionStatus(s);
		KCMSetStatus(s, n.fStatusForceRedraw);
		return;
	}

	// ★★**None of the branches below can tell WHICH DOCUMENTS from the ClassID.** They come from
	//   the payload’s fDocA / fDocB.
	//   ⚠After a Stop, GetArmedTargetDB is nil, so "the two documents to clean up" can only come
	//   from the payload ---- which is what sets these apart from the Attach cases.

	if (theChange == kKCMMarksRebuiltMessage || theChange == kKCMMarksClearedMessage)
	{
		IDataBase* const docA = n.fDocA;		// Target
		IDataBase* const docB = n.fDocB;		// Source (the same as docA when a document is compared with itself)

		// The view sync’s page-rectangle and exclusion caches are stale once the pair being compared
		// changes. (A 250ms TTL would catch up even if this were forgotten; saying so explicitly makes
		// it right from the very next notification.)
		KCMInvalidateSyncCaches();

		// ★The always-on marks of Story mode ARE the comparison, so they are rebuilt here too. On a
		//   Stop the armed state is down, so they disappear inside; on a successful comparison they are
		//   redrawn from the new result.
		//   ⚠A comparison in Pixel mode does not always raise the StoryEditsRebuilt below, so **both
		//     doors call this** ＝ switching to Pixel and comparing, with Story marks still shown,
		//     leaves no stale marks behind.
		KCMStoryMarksRefresh();

		if (theChange == kKCMMarksRebuiltMessage)
		{
			// A comparison succeeded ＝ put the strip into both windows. Attach refuses a window that
			// already has one, so passing through here on every comparison does not multiply them.
			if (docA != nil)                 KCMScrollMapAttach(docA);
			if (docB != nil && docB != docA) KCMScrollMapAttach(docB);
			KCMScrollMapInvalidateAll();
		}
		else if (docA != nil || docB != nil)
		{
			// Stop: take the strip out of every window.
			// ⚠★★**Do nothing on a Cleared that carries no document.** The sender (the tail of
			//   KCMStopComparison) posts docA/docB as nil to rebuild nothing but the panel’s appearance
			//   after disarming. The order of a Stop is:
			//     ① the Cleared that KCMDoClearMarks posts (with documents) ...... strips removed here
			//     ② KCMApplyOversetForDoc ...... if Find Overset is on by itself, it **puts one back** on
			//        the scanned document
			//     ③ this second Cleared (without documents)
			//   Without the test, ③ tears off what ② had just restored ---- everything was already
			//   removed at ①, so **the only effect ③ could have had was to undo ②** (not redundant work
			//   but a fault in itself).
			//   ★The test has the same shape as the Attach side above (the nil checks on docA/docB).
			//     **Attach was filtered by document and Detach was not** -- that was the whole fault.
			KCMScrollMapDetachAll();
		}

		// The Pages panel thumbnails. ★★**A partial recomparison (Refresh Page Comparison) carries the
		// set of pages it touched**, so only those are purged per UID.
		// ⚠**A full purge remains for exactly two cases**:
		//   ① a full recomparison (KCMDoMarkChangesDoc) and a Stop ---- narrowing them down would need
		//     "the OLD set that carried marks before the recomparison", and by the time this
		//     notification goes out that has already been thrown away. ⇒ **Not that it cannot be
		//     carried, but that there is nothing in hand to carry** (the model side would have to
		//     stash it first; it does not).
		//   ② a notification with no set attached (the way out for a sender that has none).
		// ⚠**No notification carries a set for one side only** (KCMNotifyDocsPages always attaches
		//   both). Narrowing docA while leaving docB whole would make it unreadable which is right.
		// The ForceRedraw is folded into one call after both documents (it must not break the batching).
		if (n.fPagesA != nil && n.fPagesB != nil)
		{
			if (docB == docA)
			{
				// A document compared with itself. Both sets are pages of the same document, so they are
				// merged and purged once (dropping docB as the branch below does would **miss the pages
				// touched as the Source side**).
				std::set<UID> both(*n.fPagesA);
				both.insert(n.fPagesB->begin(), n.fPagesB->end());
				if (docA != nil) KCMRefreshThumbnailsForPages(docA, both, kFalse /*redrawNow*/);
			}
			else
			{
				if (docA != nil) KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
				if (docB != nil) KCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
			}
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();

		KCMRefreshPanel();				// the Target/Source names, the icon, and whether Prev/Next are enabled

		// ⚠The navigation anchor is dropped only on a full recomparison and on a Stop. Dropping it on
		//   an incremental one (a registration toggle) would send Prev/Next back to the first page
		//   every time a page is registered ＝ a visible change in behaviour.
		if (n.fNavReset)
			KCMResetNav();
		KCMRefreshNavPosition();
		return;
	}

	if (theChange == kKCMPageFlagsChangedMessage)
	{
		// A Register (Added/Removed) or a Check changed ＝ only the thumbnails and the dots on the map
		// change. Neither what the panel shows nor the Prev/Next position moves on this notification.
		IDataBase* const docA = n.fDocA;
		IDataBase* const docB = n.fDocB;

		// ★★**The notification carries which pages changed**, so only those are purged per UID. This
		//   branch rebuilt every page of the db until then ---- on the **mistaken premise** that a
		//   notification can carry nothing but a ClassID (which had already been overturned; the
		//   correction had simply not reached this branch).
		//   ⚠A notification whose fPagesA is nil still means every page (the way out for a sender that
		//     has no set).
		//   ⚠★The nil test on docA is **for the reader, not for the callee**: KCMRefreshThumbnailsForPages
		//     returns on a nil db of its own accord, as do KCMPurgeAllPageThumbs, KCMScrollMapAttach and
		//     KCMScheduleThumbRefresh (measured 2026-08-29). It is written because **every other call
		//     site in this function writes it**, and this one, alone in not writing it, read as a
		//     missing guard.
		if (n.fPagesA != nil)
		{
			if (docA != nil) KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();
		KCMScrollMapInvalidateAll();
		return;
	}

	if (theChange == kKCMStoryEditsRebuiltMessage)
	{
		// The list's model (KCMStoryList) has already been rebuilt on the model side; this only brings
		// the screen into line.
		// ★The count in the heading is decided by KCMUpdateStorySectionLabel from the armed state, so
		//   calling them in order is all that is needed. With the panel closed or the section folded,
		//   both give up quietly inside.
		KCMStoryTreeRebuild();
		KCMUpdateStorySectionLabel();

		// ★The always-on marks ARE the comparison result, so they are rebuilt when it is.
		//   ⚠Without this, "the row I just fixed with Refresh Story Comparison still shows its old
		//     mark" ＝ the breakage the person who fixed it is least likely to notice. It is
		//     idempotent, so it does nothing when no marks are shown.
		KCMStoryMarksRefresh();

		// ★★Rebuild the "k/N" of Prev/Next as well. **What Story mode walks are the leaves of this
		//   list**, so a changed list changes the denominator ---- which is exactly what happens when
		//   Refresh Story Comparison adds or removes the children of one row, leaving the number stale
		//   on the screen of the person who fixed it.
		//   ⚠A Start or a Stop rebuilds the list too, but there the marks notification (the branch
		//     above) calls the same function immediately afterwards ＝ calling it twice gives the same
		//     value (it is idempotent, rebuilding from the current state).
		KCMRefreshNavPosition();
		return;
	}

	if (theChange == kKCMOversetRescannedMessage)
	{
		// The overset scan produced a new result. ⚠It is a feature independent of the comparison, so
		//   the subject may be one document or two (right after the scan moves, the "+" of **the
		//   previous document** has to go ＝ docB).
		IDataBase* const docA = n.fDocA;	// the document now being scanned
		IDataBase* const docB = n.fDocB;	// the one scanned before (only when it moved; nil otherwise)
		// ★★The sender attaches "the pages whose + may have changed", so the purge is per UID. For a
		//   rescan of the same document fPagesA is **new ∪ old**; when the scan moved to another
		//   document, fPagesB is the previous one’s old set (KCMOversetApply.cpp). ⚠A notification
		//   with no set still means every page.
		if (n.fPagesA != nil && n.fPagesB != nil)
		{
			if (docA != nil) KCMRefreshThumbnailsForPages(docA, *n.fPagesA, kFalse /*redrawNow*/);
			if (docB != nil && docB != docA)
				KCMRefreshThumbnailsForPages(docB, *n.fPagesB, kFalse /*redrawNow*/);
		}
		else
		{
			if (docA != nil)                 KCMPurgeAllPageThumbs(docA);
			if (docB != nil && docB != docA) KCMPurgeAllPageThumbs(docB);
		}
		KCMForceRedrawPagesPanelNow();

		if (docA != nil)
			KCMScrollMapAttach(docA);	// the map is shown even without a comparison (the standalone check)
		KCMScrollMapInvalidateAll();

		if (n.fNavReset)
			KCMResetNav();
		KCMRefreshNavPosition();		// rebuild how many things Prev/Next walks (changes plus overset)
		return;
	}

	if (theChange == kKCMComparisonDocsClosedMessage)
	{
		// ★★**This branch alone decides WHEN to act.** Cleaning up after a closed document must not
		//   happen at all during teardown, and must wait for the last of them during a batch close --
		//   and both of those are **the UI’s business**. The model only says "a document closed and I
		//   dropped its state".

		// Needed whichever document closed ---- neither the page structure nor the db pointer can be
		// relied on any more, so the view sync’s page-rectangle and exclusion caches go. It only
		// empties containers, so it is safe during teardown too.
		KCMInvalidateSyncCaches();

		// navReset tells whether "the comparison ended" (the Stop-equivalent full clean-up ran). A
		// document closing with no bearing on the comparison arrives with kFalse, and the heavy
		// clean-up below is not needed.
		// ★This branch asks **IKCMCompareFacade** exactly once (IsAppQuitting), so it is not worth
		//   holding an InterfacePtr for (three or more calls and it would be ＝
		//   [[utils-boss-facade-access]]).
		const bool16 comparisonEnded = n.fNavReset;
		if (comparisonEnded)
		{
			KCMResetPeekGestureState();	// the state of an in-flight peek
			KCMResetNav();				// the navigation anchor (do not carry a closed document’s page UID forward)
		}

		// ★Touch no widget while the application is quitting: the window and the panel may both be
		//   coming apart ---- touching a widget mid-teardown is the classic shape of a Mac-only
		//   crash-on-quit. The window goes with it, so there is no point removing the strip either.
		if (Utils<IKCMCompareFacade>()->IsAppQuitting())
			return;

		// ★During a batch close (several documents in a row), hold it and let the "all of them have
		//   closed" notification flush it once.
		if (KCMBatchCloseInProgress())
		{
			KCMDeferCloseUi();
			return;
		}

		if (comparisonEnded)
		{
			// The strip: ★if Find Overset is on by itself (and its scanned document is still alive),
			//   **keep it** and only repaint the red bands. If the scanned document is the one that
			//   closed, the model has already done DropOverset ＝ sOversetOn is false, and it is removed
			//   as usual.
			if (Utils<IKCMMarkData>()->GetOversetOn())
				KCMScrollMapInvalidateAll();
			else
				KCMScrollMapDetachAll();

			// ★The thumbnails are **not rebuilt here but deferred to the next idle**. When the Target is
			//   the one that closed and the survivor is about to be activated, the regeneration does not
			//   complete during that transition however hard ForceRedraw is called, and the frames stay
			//   behind (measured in the running application). nil is rejected by the scheduler and
			//   duplicate dbs are folded together.
			KCMScheduleThumbRefresh(n.fDocA);
			KCMScheduleThumbRefresh(n.fDocB);
			KCMScheduleThumbRefresh(n.fDocC);
		}

		KCMRefreshPanel();	// bring the panel’s ON/OFF display into line with the real state (this is what clears a stuck "ON")
		return;
	}
}

// Subscribe to, or unsubscribe from, the application's subject.
// ★**One function with a flag**, which is the shape this plug-in already settled on for the same
//   job: KCMLayoutSyncAttachContext in KCMViewSync.cpp. The two directions differ in **one call**,
//   and everything ahead of it -- the session, the active context, the observer, the subject --
//   was written out twice here (2026-08-29).
// ★IsAttached is asked either way, so calling either direction twice is a no-op.
// ⚠The session can be nil while the application is quitting, which is why the detach direction
//   has to survive it.
static void KCMSetModelChangeObserverAttached(bool16 attach)
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMMODELCHANGEOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	const bool16 attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER);
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IKCMMODELCHANGEOBSERVER, IID_IKCMMODELCHANGEOBSERVER);
}

// Attach to the application subject (once, from the UI’s Startup). The same shape as
// KCMAttachDocsClosedObserver.
void KCMAttachModelChangeObserver()
{
	KCMSetModelChangeObserverAttached(kTrue);
}

// Detach on the way out. ★Call it **before the panel is taken down** (the order in
// KCMUIStartup.cpp). While it is attached, what the session holds is **a pointer into this
// .pln**, and a notification during teardown runs Update inside code that is going away ---- the
// same reasoning as KCMDetachPanelVisibilityObserver.
//
// ★★**Why this one needs a detach and KCMDocsClosedObserver does not** (two observers of the
//   same construction follow two different policies, and the difference was written down
//   nowhere):
//   **Update above has six branches, and only the kKCMComparisonDocsClosedMessage branch carries
//   an IsAppQuitting guard** ---- the other five would touch widgets if they ran during
//   teardown.
//   ⇒ **What cannot be guarded at the entrance is stopped at the source.**
//   The other one has a single-bodied Update whose entrance is guarded twice (see the comment on
//   KCMAttachDocsClosedObserver in KCMPeekGesture.cpp). **The difference is not the host boss or
//   the subject, but what Update does.**
void KCMDetachModelChangeObserver()
{
	KCMSetModelChangeObserverAttached(kFalse);
}

// End, KCMModelChangeObserver.cpp.
