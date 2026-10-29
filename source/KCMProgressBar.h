//========================================================================================
//  KCMProgressBar.h
//  Kohaku Change Marker - a progress bar that earns its place: it appears only after
//  kKCMProgressBarDelayMs of work, with its Cancel button, and shows nothing at all for a job
//  that is over before then.
//
//  WHY THIS EXISTS. The SDK's TaskProgressBar / RangeProgressBar decide "shown or not" at
//   construction, and showImmediate=kFalse brought no bar up at all on a 100-page comparison
//   (measured, KCMCore.cpp) - though IProgressBarManager.h:59 says "put it up after a short
//   delay", and how that run was started is not recorded. A page count cannot decide it either:
//   the Story mode rasterises no page. The author's call: every mode alike, on TIME, three seconds.
//
//  HOW. A SuppressProgressBarDisplay is held from construction so that the rasterising
//   internals raise no bar of their own during the wait (the old code did that with
//   DisableChildProgressBars on a hidden bar). At the first Step past the delay the suppressor
//   is dropped FIRST - a bar created under it would be suppressed too ("all progress bars
//   below you will not display", ProgressBar.h) - and a RangeProgressBar is created with
//   showImmediate=kTrue, positioned at the units already done.
//
//  THE SUPPRESSOR AND THE BAR ARE THE UI HALF'S (IKCMUIServices.h - the author's call: the guide
//   counts a modal bar among the UI's dialogs). This file decides everything about them - when the
//   bar comes, what it says, where Cancel is read - and asks the UI half to put them up. No UI half (a background thread, InDesign Server; the test build's fault
//   switch ui-services-off) = no suppressor and no bar: Step shows nothing, WasCancelled is false,
//   and the job runs to its end. The guard against a bar the manager refused (WasRegisterSuccessful)
//   moved with the bar: ui/KCMUIServices.cpp.
//
//  @warning **TWO OF THESE MUST NOT BE ALIVE AT ONCE.** While one exists, its suppressor holds
//   every other progress bar down - including a second KCMDeferredProgressBar, whose own bar is
//   then refused registration (measured: "the bar shows in the Pixel mode and not in the Story
//   mode" - the raster loop's object outlived its loop and sat on top of the Story comparison's).
//   Scope each one to the loop it reports on; KCMCore.cpp shows the shape.
//
//  @warning WasCancelled is taken to pump events (the SDK does not say, and it has not been
//   measured), so call it only at a safe point (a page or a story fully compared), never in the
//   middle of a rasterisation. It never raises the global error state.
//   Whether a cancel throws the work away or keeps it is the CALLER's rule, not this class's:
//   KCMCore.cpp and KCMStoryDiffRun.cpp discard (the caller goes back to Stop), KCMPeek.cpp keeps.
//========================================================================================

#ifndef __KCMProgressBar_h__
#define __KCMProgressBar_h__

#include "IKCMUIServices.h"		// KCMQueryUIServices - the bar and the suppressor, put up by the UI half
#include "K2SmartPtr.h"			// K2::scoped_ptr
#include "PMString.h"
#include "KCMConstants.h"		// kKCMProgressBarDelayMs

#include <chrono>
#include <new>					// std::nothrow

class KCMDeferredProgressBar
{
public:
	/** @param title the dialog title, already marked untranslatable by the caller.
		@param total how many units the job has; the bar's range is 0..total.
		@param delayMs how long the job may run before a bar is worth showing.

		THE DELAY IS A PARAMETER, AND THE COMPARISON MODES ALL PASS THE SAME VALUE. It became one
		when the Resources comparison wanted a bar with no wait at all, and it stayed one when the
		author - having seen that bar - asked for the three seconds back, "like the others".
		**Every mode, one rule.**
		⚠**The Before/After report passes 0, and that is not a second rule for the same case - it
		  is a different case** (measured with the user on a 60-page pair). The delay is
		  judged INSIDE Step and nowhere else, so a bar appears only when the NEXT Step comes round.
		  The report's first Step is at 0 ms and its heaviest single act - exporting the Before pages
		  - runs immediately after it with no safe point to step from, so the bar stayed invisible
		  through the whole minute it was wanted for and appeared as the work ended. A report is
		  never the "over before you see it" case the three seconds exist for: it writes two PDFs and
		  builds a document.
		⚠Zero means "at the first Step" rather than "in the constructor": nothing is drawn until the
		  caller says what it is about to do. */
	KCMDeferredProgressBar(const PMString& title, int32 total,
						   int32 delayMs = kKCMProgressBarDelayMs)
		: fTitle(title), fTotal(total), fDelayMs(delayMs),
		  fSince(std::chrono::steady_clock::now()),
		  fSuppress(NewSuppressor()),
		  fAsked(kFalse)
	{}

	/** Reports that `done` units are finished and names the one about to start. The bar
		appears here, the first time the delay has passed; before that this returns at once. */
	void Step(int32 done, const PMString& text)
	{
		if (!fAsked)
		{
			const int64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - fSince).count();
			if (elapsedMs < fDelayMs)
				return;
			// ASKED ONCE, past the delay: a UI half that is not there now is not there before the job
			// ends (no UI = no bar, and every Step after this returns below).
			fAsked = kTrue;
			fSuppress.reset();		// before the bar: a bar created under the suppressor is suppressed too
			InterfacePtr<IKCMUIServices> ui(KCMQueryUIServices());
			if (ui != nil)
				fBar.reset(ui->NewProgressBar(fTitle, 0, fTotal, kTrue /*showImmediate*/, kTrue /*showCancel*/));
			if (fBar.get() == nil)
				return;
			fBar->DisableChildProgressBars(kTrue);	// the rasterising internals must not subdivide it
			// (A bar the manager refused - another bar's DisableChildProgressBars, measured - comes back all
			//  the same and drives nothing, its WasCancelled false: that guard stands with the bar, in
			//  ui/KCMUIServices.cpp.)
		}
		if (fBar.get() == nil)
			return;
		fBar->SetTaskText(text, kFalse /*forceRedraw*/);
		fBar->SetPosition(done);
	}

	/** kTrue once the person has pressed Cancel. kFalse while no bar is up (nothing is pumped then),
		and for a bar the manager refused (the UI half answers no for it: nobody can have pressed it). */
	bool16 WasCancelled()
	{
		return (fBar.get() != nil && fBar->WasCancelled(kFalse /*setGlobalErrorState*/)) ? kTrue : kFalse;
	}

private:
	/** The suppressor the UI half holds through the wait, or nil when there is no UI half (and
		nothing to suppress). */
	static KCMProgressSuppressorUI* NewSuppressor()
	{
		InterfacePtr<IKCMUIServices> ui(KCMQueryUIServices());
		return (ui != nil) ? ui->NewProgressBarSuppressor() : nil;
	}

	PMString									fTitle;
	int32										fTotal;
	int32										fDelayMs;
	std::chrono::steady_clock::time_point		fSince;
	K2::scoped_ptr<KCMProgressSuppressorUI>		fSuppress;	// declared before fBar: destroyed after it
	K2::scoped_ptr<KCMProgressBarUI>			fBar;
	bool16										fAsked;		// the delay has passed and the UI half was asked for the bar (once)
};

/*	KCMProgressBar
	A modal bar for a job that wants one at once and drives it itself - the book comparison
	(KCMBookCompare.cpp). RangeProgressBar's calls, the bar the UI half's (IKCMUIServices.h): no UI
	half = no bar, every call does nothing and WasCancelled answers false. KFC's KFCProgressBar,
	the same shape.
*/
class KCMProgressBar
{
public:
	/** RangeProgressBar's first five arguments (ProgressBar.h), with its default for showCancel. */
	KCMProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate,
				   bool8 showCancel = kTrue)
	{
		InterfacePtr<IKCMUIServices> ui(KCMQueryUIServices());
		if (ui != nil)
			fBar.reset(ui->NewProgressBar(title, startRange, endRange, showImmediate, showCancel));
	}

	void SetTaskText(const PMString& text, bool16 forceRedraw = kTrue)
	{
		if (fBar.get() != nil)
			fBar->SetTaskText(text, forceRedraw);
	}
	void SetPosition(int32 newPosition)
	{
		if (fBar.get() != nil)
			fBar->SetPosition(newPosition);
	}
	/** False when there is no bar: nobody can have pressed a Cancel that is not there. */
	bool16 WasCancelled(bool8 setGlobalErrorState = kTrue)
	{
		return (fBar.get() != nil) ? fBar->WasCancelled(setGlobalErrorState) : kFalse;
	}
	void DisableChildProgressBars(bool16 disable)
	{
		if (fBar.get() != nil)
			fBar->DisableChildProgressBars(disable);
	}

private:
	K2::scoped_ptr<KCMProgressBarUI>	fBar;	// the UI half's bar, or nil when there is no UI

	KCMProgressBar(const KCMProgressBar&);
	KCMProgressBar& operator=(const KCMProgressBar&);
};

//========================================================================================
//  ONE BAR FOR A LONGER JOB (the author's call: an import is covered by one bar from reading the
//  files to comparing the stories - KCMImportStoryTextAsSource).
//
//  **WHY A SLOT AND NOT A PARAMETER.** The comparison an import runs is reached through the
//    ordinary Start (KCMToggleStartStop -> ... -> KCMStoryDiffRun::Run, and the raster loop in
//    KCMCore.cpp on the way), and those loops make their OWN bar. Two alive at once is the one thing
//    this class must never allow (the warning above), so the loops ask the slot first: while a
//    longer job has registered its bar there, they step a SLICE of it instead of making their own.
//  ⚠**MAIN THREAD ONLY**, like every bar: the slot is read by the comparison loops, which run on the
//    main thread; nothing on a background thread (a PDF export) goes near them.
//========================================================================================

/** The bar a longer job has registered, and the slice of it the next inner loop reports into. */
struct KCMOuterProgress
{
	KCMDeferredProgressBar*	fBar;
	int32					fFrom;		// the unit of fBar an inner loop's "0 done" lands on
	int32					fTo;		// and its "all done"
};

/** The one slot. An inline function's static is one object for the whole plug-in. */
inline KCMOuterProgress& KCMOuterProgressSlot()
{
	static KCMOuterProgress sSlot = { nil, 0, 0 };
	return sSlot;
}

/** Registers `bar` for its lifetime and puts back whatever was there before. */
class KCMOuterProgressScope
{
public:
	explicit KCMOuterProgressScope(KCMDeferredProgressBar& bar)
		: fSaved(KCMOuterProgressSlot())
	{
		// The whole slot, as the destructor puts the whole one back: a member added to it starts empty here too.
		const KCMOuterProgress mine = { &bar, 0, 0 };
		KCMOuterProgressSlot() = mine;
	}
	~KCMOuterProgressScope() { KCMOuterProgressSlot() = fSaved; }

	/** The units of the bar the NEXT inner loop's whole range maps onto. */
	void Slice(int32 from, int32 to)
	{
		KCMOuterProgressSlot().fFrom = from;
		KCMOuterProgressSlot().fTo = to;
	}

private:
	KCMOuterProgress	fSaved;

	KCMOuterProgressScope(const KCMOuterProgressScope&);
	KCMOuterProgressScope& operator=(const KCMOuterProgressScope&);
};

/*	KCMProgressStepper
	What a loop that reports progress holds: a KCMDeferredProgressBar of its OWN, or - while a longer
	job has registered one (KCMOuterProgressScope) - a slice of that one. The same two calls either way.

	**THE OWN BAR IS NOT EVEN CONSTRUCTED WHEN THERE IS AN OUTER ONE**: its constructor alone raises a
	  suppressor, and a suppressor is what refuses the outer bar its registration.
*/
class KCMProgressStepper
{
public:
	KCMProgressStepper(const PMString& title, int32 total)
		: fOuter(KCMOuterProgressSlot()), fTotal(total)
	{
		if (fOuter.fBar == nil)
			fOwn.reset(new (std::nothrow) KCMDeferredProgressBar(title, total));
	}

	void Step(int32 done, const PMString& text)
	{
		if (fOuter.fBar != nil)
			fOuter.fBar->Step(OuterUnit(done), text);
		else if (fOwn.get() != nil)
			fOwn->Step(done, text);
	}

	bool16 WasCancelled()
	{
		if (fOuter.fBar != nil)
			return fOuter.fBar->WasCancelled();
		return (fOwn.get() != nil) ? fOwn->WasCancelled() : kFalse;
	}

private:
	int32 OuterUnit(int32 done) const
	{
		if (fTotal <= 0)
			return fOuter.fFrom;
		const int64 span = static_cast<int64>(fOuter.fTo) - fOuter.fFrom;
		return fOuter.fFrom + static_cast<int32>(span * done / fTotal);
	}

	const KCMOuterProgress					fOuter;		// copied at construction: the slice this loop was given
	int32									fTotal;
	K2::scoped_ptr<KCMDeferredProgressBar>	fOwn;

	KCMProgressStepper(const KCMProgressStepper&);
	KCMProgressStepper& operator=(const KCMProgressStepper&);
};

#endif // __KCMProgressBar_h__

// End, KCMProgressBar.h.
