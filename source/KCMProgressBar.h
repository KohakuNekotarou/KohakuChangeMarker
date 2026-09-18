//========================================================================================
//  KCMProgressBar.h
//  Kohaku Change Marker - a progress bar that earns its place: it appears only after
//  kKCMProgressBarDelayMs of work, with its Cancel button, and shows nothing at all for a job
//  that is over before then.
//
//  WHY THIS EXISTS. The SDK's TaskProgressBar / RangeProgressBar decide "shown or not" at
//   construction: showImmediate=kFalse means the bar never appears (measured on a 100-page
//   comparison, KCMCore.cpp), not "appears once this takes a while". Until 2026-09-05 the
//   comparison guessed from a page count (10 or more) whether a bar was worth it, and the
//   Story mode - which rasterises no page - had no bar however many stories it read. The
//   user's call: Pixel and Story alike, on TIME, three seconds.
//
//  HOW. A SuppressProgressBarDisplay is held from construction so that the rasterising
//   internals raise no bar of their own during the wait (the old code did that with
//   DisableChildProgressBars on a hidden bar). At the first Step past the delay the suppressor
//   is dropped FIRST - a bar created under it would be suppressed too ("all progress bars
//   below you will not display", ProgressBar.h) - and a RangeProgressBar is created with
//   showImmediate=kTrue, positioned at the units already done.
//
//  @warning **TWO OF THESE MUST NOT BE ALIVE AT ONCE.** While one exists, its suppressor holds
//   every other progress bar down - including a second KCMDeferredProgressBar, whose own bar is
//   then refused registration (measured 2026-09-05: "the bar shows in the Pixel mode and not in
//   the Story mode" - the raster loop's object outlived its loop and sat on top of the Story
//   comparison's). Scope each one to the loop it reports on; KCMCore.cpp shows the shape.
//
//  @warning WasCancelled pumps events, so call it only at a safe point (a page or a story fully
//   compared), never in the middle of a rasterisation. It never raises the global error state.
//   Whether a cancel throws the work away or keeps it is the CALLER's rule, not this class's:
//   KCMCore.cpp and KCMStoryDiffRun.cpp discard (the caller goes back to Stop), KCMPeek.cpp keeps.
//========================================================================================

#ifndef __KCMProgressBar_h__
#define __KCMProgressBar_h__

#include "ProgressBar.h"		// RangeProgressBar, SuppressProgressBarDisplay
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

		★★THE DELAY IS A PARAMETER, AND THE THREE COMPARISON MODES ALL PASS THE SAME VALUE
		(2026-09-09). It became one when the Resources comparison wanted a bar with no wait at all,
		and it stayed one when the user - having seen that bar - asked for the three seconds back,
		"like the others". ⇒ **Three modes, one rule.**
		⚠★★**The Before/After report passes 0, and that is not a second rule for the same case - it
		  is a different case** (2026-09-14, measured with the user on a 60-page pair). The delay is
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
		  fSuppress(new (std::nothrow) SuppressProgressBarDisplay(kTrue)),
		  fBarUp(kFalse)
	{}

	/** Reports that `done` units are finished and names the one about to start. The bar
		appears here, the first time the delay has passed; before that this returns at once. */
	void Step(int32 done, const PMString& text)
	{
		if (fBar.get() == nil)
		{
			const int64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - fSince).count();
			if (elapsedMs < fDelayMs)
				return;
			fSuppress.reset();		// before the bar: a bar created under the suppressor is suppressed too
			fBar.reset(new (std::nothrow) RangeProgressBar(fTitle, 0, fTotal, kTrue /*showImmediate*/));
			if (fBar.get() == nil)
				return;
			fBar->DisableChildProgressBars(kTrue);	// the rasterising internals must not subdivide it

			// ***** AND NOW ASK WHETHER IT ACTUALLY WENT UP. *****
			//
			// ⚠**THE HEADER SAYS THIS IS "not usually necessary to call"** (ProgressBar.h:58) - and it
			//  names the exact reason it is necessary here in the next sentence: "another progress bar
			//  calling DisableChildProgressBars()", which is the call one line above, made by every
			//  other KCMDeferredProgressBar and by KIDMCP's bar as well.
			// ★THE REFUSAL IS NOT HYPOTHETICAL. It was measured on 2026-09-05 - "the bar shows in the
			//  Pixel mode and not in the Story mode", the raster loop's object having outlived its
			//  loop (see the warning at the head of this file).
			// ⚠**AND IT DID NOT CRASH THAT DAY.** The symptom was a missing bar, nothing more. This
			//  guard is prevention, not a repair: it was written the same day KIDMCP was found driving
			//  a refused bar of its own, so that the two plug-ins answer the question the same way.
			fBarUp = fBar->WasRegisterSuccessful();
		}

		// ⚠ONE RETURN FOR THREE CALLS - SetTaskText, SetPosition, and WasCancelled below. Driving a
		//  bar that never registered pumps nothing and shows nothing, so what is given up by stopping
		//  here is exactly nothing; what is avoided is calling into a window the manager does not know
		//  about.
		if (!fBarUp)
			return;

		fBar->SetTaskText(text, kFalse /*forceRedraw*/);
		fBar->SetPosition(done);
	}

	/** kTrue once the person has pressed Cancel. kFalse while no bar is up (nothing is pumped then).

		⚠fBarUp, not just a nil test: a refused bar is an object that exists and cannot be pressed, so
		 asking it whether it was cancelled can only ever answer no - and it is one of the three calls
		 the guard in Step() exists to cover. */
	bool16 WasCancelled()
	{
		return (fBarUp && fBar.get() != nil && fBar->WasCancelled(kFalse /*setGlobalErrorState*/))
			   ? kTrue : kFalse;
	}

private:
	PMString									fTitle;
	int32										fTotal;
	int32										fDelayMs;
	std::chrono::steady_clock::time_point		fSince;
	K2::scoped_ptr<SuppressProgressBarDisplay>	fSuppress;	// declared before fBar: destroyed after it
	K2::scoped_ptr<RangeProgressBar>			fBar;

	/** Whether fBar is a bar the manager actually registered. ⚠Declared AFTER fBar so that the
		initialiser list stays in declaration order; it is set in Step(), never here. */
	bool16										fBarUp;
};

//========================================================================================
//  ONE BAR FOR A LONGER JOB (2026-09-17, the user: the Import mode is covered by one bar from
//  reading the files to comparing the stories).
//
//  ★★**WHY A SLOT AND NOT A PARAMETER.** The comparison an import runs is reached through the
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
		KCMOuterProgress& slot = KCMOuterProgressSlot();
		slot.fBar = &bar;
		slot.fFrom = 0;
		slot.fTo = 0;
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

	★**THE OWN BAR IS NOT EVEN CONSTRUCTED WHEN THERE IS AN OUTER ONE**: its constructor alone raises a
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
