//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryMarkerExpiry.h for why this is an idle task and not a callback timer.
//
//  **THERE IS NO "IS IT RUNNING" FLAG HERE, AND THERE MUST NOT BE.** Whether the task is
//  sitting in the idle queue is the BASE CLASS's business: CIdleTask keeps it in
//  fCurrentlyInstalled and InstallTask / UninstallTask maintain it. A second copy here could
//  only ever disagree with it -- and disagreeing in one direction is illegal, not merely untidy:
//  IIdleTaskMgr::AddTask says "it is illegal to add the same task twice", which is what a stale
//  "not running" would lead to. Calling UninstallTask when nothing is installed is free (it
//  answers kEndOfTime for a task that was not there). So every entry point below simply
//  uninstalls first and asks no questions -- the shape Adobe's own re-arming code uses
//  (spellpanel/DynSpellCheckEventWatcher.cpp does it on every keystroke). KBS learned this the
//  hard way in its own audit; the flag it
//  removed then is not being reintroduced here.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IIdleTaskMgr.h"

// General includes:
#include "CIdleTask.h"
#include "CreateObject.h"

// Project includes:
#include "KCMID.h"			// moved here from the UI plug-in with the marker
#include "KCMStoryMarker.h"
#include "KCMStoryMarkerExpiry.h"

// How long the mark stays up. The same second KBS settled on: long enough to find with the eye,
// short enough that it is plainly a pointer rather than a highlight the reader has to dismiss.
static const uint32 kKCMStoryMarkerLifetimeMs = 1000;

static IIdleTask* sTask     = nil;		// created once, reused. Released in Shutdown
static bool16     sShutdown = kFalse;	// set at application shutdown: never create/schedule again

//========================================================================================
// The task itself - RunTask and TaskName only. InstallTask / UninstallTask come from CIdleTask.
//========================================================================================

class KCMStoryMarkerExpiryTask : public CIdleTask
{
public:
	KCMStoryMarkerExpiryTask(IPMUnknown* boss) : CIdleTask(boss) {}

	virtual uint32 RunTask(uint32 flags, IdleTimer* idleTimer);
	virtual const char* TaskName() { return "KCMStoryMarkerExpiryTask"; }
};

CREATE_PMINTERFACE(KCMStoryMarkerExpiryTask, kKCMStoryMarkerExpiryImpl)

uint32 KCMStoryMarkerExpiryTask::RunTask(uint32 /*flags*/, IdleTimer* /*idleTimer*/)
{
	// Take ourselves off the queue, then clear. CIdleTask asks for exactly this -- "Don't return
	// kEndOfTime from RunTask, instead you would call UninstallTask and return any value from
	// RunTask as it will be ignored".
	//
	// @warning taking the flash down calls back into Stop(), which uninstalls again. That second
	//   call is harmless by the contract quoted at the head of this file.
	this->UninstallTask();

	// **THE FLASH, AND ONLY THE FLASH.** The standing marks a toggle is holding up have no clock,
	//   and since the two can be on screen together in different windows, a countdown that took
	//   everything down would wipe a toggle's marks a second after any jump.
	if (!sShutdown)
		KCMStoryMarker::ClearFlash();

	return 0;	// one-shot: nothing more to do
}

//========================================================================================
// Public entry points
//========================================================================================

void KCMStoryMarkerExpiry::Start()
{
	if (sShutdown)
		return;

	if (sTask == nil)
		sTask = ::CreateObject2<IIdleTask>(kKCMStoryMarkerExpiryBoss);
	if (sTask == nil)
		return;		// no timer to be had; the mark then stays up until the next jump replaces it

	// Restart rather than let a running countdown stand: the mark was just (re)shown, so it is owed
	// the full lifetime from now. Uninstall unconditionally - a pending booking has to come off
	// before a new one goes on, and if there is none this costs nothing.
	sTask->UninstallTask();
	sTask->InstallTask(kKCMStoryMarkerLifetimeMs);
}

void KCMStoryMarkerExpiry::Stop()
{
	if (sTask != nil)
		sTask->UninstallTask();
}

void KCMStoryMarkerExpiry::Shutdown()
{
	sShutdown = kTrue;	// no re-arming from here on
	if (sTask != nil)
	{
		sTask->UninstallTask();
		sTask->Release();
		sTask = nil;
	}
}

// End, KCMStoryMarkerExpiry.cpp.
