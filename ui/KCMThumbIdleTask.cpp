//========================================================================================
//
//  KCMThumbIdleTask.cpp
//
//  An idle task that rebuilds the Pages panel thumbnails once, on the next idle
//  (KCMThumbIdleTask.h). The design and the reason for the delay are in the header.
//
//  - One IIdleTask is created per session and reused (sThumbTask). A pending schedule is always
//    removed before it is put back (Remove then Add, so the fire time is the newest one). Whether
//    the task is on the queue right now is not tracked here: the CIdleTask base already holds it
//    in fCurrentlyInstalled -- the same unconditional call the shipping code makes in
//    spellpanel/DynSpellCheckEventWatcher.cpp.
//  - RunTask takes the pending databases (sPendingDBs) and calls
//    KCMTryRefreshPagesPanelThumbnails only on those still listed in IDocumentList; a db that has
//    been closed is never touched. It then takes itself off the queue with UninstallTask
//    (returning kEndOfTime would break the contract; the object itself is kept for reuse).
//  - At application shutdown KCMShutdownThumbIdleTask does RemoveTask + Release.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CIdleTask.h"

#include "IIdleTaskMgr.h"

#include <algorithm>
#include <vector>

#include "KCMUIID.h"
#include "KCMThumbIdleTask.h"
#include "KCMThumbnailRefresh.h"	// KCMTryRefreshPagesPanelThumbnails
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// IsDocDBOpen (proves a db is live rather than dereferencing a
									// closed one) / IsAppQuitting

// How long to wait for the frontmost document to settle, in ms. The switch a close causes is
// quick, so a short wait is enough; raise this if a refresh is ever missed.
static const uint32 kKCMThumbRefreshDelayMs = 150;

// ---- Shared state, all of it private to this translation unit ---------------------------
static IIdleTask*             sThumbTask = nil;	// the one idle task, reused; owned here (Release at shutdown)
static std::vector<IDataBase*> sPendingDBs;		// the databases the next RunTask will refresh
static bool16                 sShutdown  = kFalse;	// set once KCMShutdownThumbIdleTask has run.
													// A responder scheduling after that must not
													// build a new task: it would leak, and it would
													// fire during teardown. The normal shutdown
													// order never reaches it; this is the guard for
													// when it does.

//========================================================================================
// KCMThumbIdleTask -- the smallest CIdleTask subclass there is: RunTask and TaskName only.
// (CIdleTask already implements InstallTask/UninstallTask in terms of AddTask/RemoveTask.)
//========================================================================================
class KCMThumbIdleTask : public CIdleTask
{
public:
	KCMThumbIdleTask(IPMUnknown* boss) : CIdleTask(boss) {}

	virtual uint32 RunTask(uint32 flags, IdleTimer* idleTimer);
	virtual const char* TaskName() { return "KCMThumbIdleTask"; }
};

CREATE_PMINTERFACE(KCMThumbIdleTask, kKCMThumbIdleTaskImpl)

uint32 KCMThumbIdleTask::RunTask(uint32 flags, IdleTimer* /*idleTimer*/)
{
	// While the application is quitting, do not touch the Pages panel: drop the pending work and
	// take this task off the queue. This covers a document close during the quit that schedules
	// the task, with the idle firing before Shutdown gets to RemoveTask -- a purge plus
	// ForceRedraw into a panel that is being torn down is the classic crash-on-quit on the Mac.
	// ★Held once: asked here and again for every db in the loop below (Utils.h:74-80).
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare->IsAppQuitting())
	{
		sPendingDBs.clear();
		this->UninstallTask();
		return 0;
	}

	// Not while a menu is down, the mouse is being tracked, or we are in the background: the task
	// is called again once the flags change.
	if (flags & (IIdleTaskMgr::kInBackground | IIdleTaskMgr::kMenuUp | IIdleTaskMgr::kMouseTracking))
		return kOnFlagChange;

	// Take the pending databases and empty the list, so that a schedule made while RunTask runs
	// is not lost.
	std::vector<IDataBase*> dbs;
	dbs.swap(sPendingDBs);

	bool16 purgedAny = kFalse;
	for (std::vector<IDataBase*>::iterator it = dbs.begin(); it != dbs.end(); ++it)
	{
		// A db closed between the schedule and the idle must not be dereferenced.
		if (compare->IsDocDBOpen(*it))
		{
			KCMTryRefreshPagesPanelThumbnails(*it, kFalse /*redrawNow*/);	// purge only
			purgedAny = kTrue;
		}
	}
	if (purgedAny)
		KCMForceRedrawPagesPanelNow();	// one ForceRedraw once every db has been purged, not one each

	// The contract (CIdleTask.h) is to call UninstallTask rather than return kEndOfTime. On
	// kEndOfTime the IdleTaskMgr drops the task without going through UninstallTask, so the base
	// class's fCurrentlyInstalled stays true, the AddTask inside the next InstallTask is skipped,
	// and from the second close onwards the deferred thumbnail refresh never runs again.
	this->UninstallTask();
	// Do not swallow a schedule made while RunTask was running. KCMForceRedrawPagesPanelNow above
	// draws synchronously, and the draw event's safety-net cleanup (tidying up after a closed
	// document) can call KCMScheduleThumbRefresh from inside it. That schedule's AddTask would be
	// undone by the UninstallTask just above, leaving the pending databases to never fire again.
	// If any are left, put the task back (with an empty list this does nothing, as before).
	if (!sPendingDBs.empty())
		this->InstallTask(kKCMThumbRefreshDelayMs);
	return 0;	// the return value is ignored (the object is kept and reused next time)
}

//========================================================================================
// Public entry points
//========================================================================================
void KCMScheduleThumbRefresh(IDataBase* db)
{
	if (db == nil || sShutdown || Utils<IKCMCompareFacade>()->IsAppQuitting())
		return;		// no re-arming after shutdown or during a quit (leak / firing during teardown)

	if (sThumbTask == nil)
		sThumbTask = ::CreateObject2<IIdleTask>(kKCMThumbIdleTaskBoss);
	if (sThumbTask == nil)
		return;	// no task means no schedule, so do not queue the db for one either

	// Never queue the same db twice.
	if (std::find(sPendingDBs.begin(), sPendingDBs.end(), db) == sPendingDBs.end())
		sPendingDBs.push_back(db);

	// The task cannot be added twice, so always remove it before putting it back (which also
	// makes the fire time the newest one). Whether it is on the queue right now is not counted
	// here: the CIdleTask base holds that in fCurrentlyInstalled, and calling RemoveTask on a task
	// that is not installed merely returns kEndOfTime (IIdleTaskMgr::RemoveTask) and is harmless.
	// The shipping spellpanel/DynSpellCheckEventWatcher.cpp calls it just as unconditionally.
	sThumbTask->UninstallTask();
	sThumbTask->InstallTask(kKCMThumbRefreshDelayMs);
}

void KCMShutdownThumbIdleTask()
{
	sShutdown = kTrue;	// every later KCMScheduleThumbRefresh is a no-op (nothing is rebuilt)
	if (sThumbTask != nil)
	{
		sThumbTask->UninstallTask();	// RemoveTask (nothing happens if it is not installed); always before Release
		sThumbTask->Release();
		sThumbTask = nil;
	}
	sPendingDBs.clear();
}

// End of KCMThumbIdleTask.cpp
