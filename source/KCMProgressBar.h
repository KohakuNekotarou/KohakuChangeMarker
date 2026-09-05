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
		@param total how many units the job has; the bar's range is 0..total. */
	KCMDeferredProgressBar(const PMString& title, int32 total)
		: fTitle(title), fTotal(total),
		  fSince(std::chrono::steady_clock::now()),
		  fSuppress(new (std::nothrow) SuppressProgressBarDisplay(kTrue))
	{}

	/** Reports that `done` units are finished and names the one about to start. The bar
		appears here, the first time the delay has passed; before that this returns at once. */
	void Step(int32 done, const PMString& text)
	{
		if (fBar.get() == nil)
		{
			const int64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - fSince).count();
			if (elapsedMs < kKCMProgressBarDelayMs)
				return;
			fSuppress.reset();		// before the bar: a bar created under the suppressor is suppressed too
			fBar.reset(new (std::nothrow) RangeProgressBar(fTitle, 0, fTotal, kTrue /*showImmediate*/));
			if (fBar.get() == nil)
				return;
			fBar->DisableChildProgressBars(kTrue);	// the rasterising internals must not subdivide it
		}
		fBar->SetTaskText(text, kFalse /*forceRedraw*/);
		fBar->SetPosition(done);
	}

	/** kTrue once the person has pressed Cancel. kFalse while no bar is up (nothing is pumped then). */
	bool16 WasCancelled()
	{
		return (fBar.get() != nil && fBar->WasCancelled(kFalse /*setGlobalErrorState*/)) ? kTrue : kFalse;
	}

private:
	PMString									fTitle;
	int32										fTotal;
	std::chrono::steady_clock::time_point		fSince;
	K2::scoped_ptr<SuppressProgressBarDisplay>	fSuppress;	// declared before fBar: destroyed after it
	K2::scoped_ptr<RangeProgressBar>			fBar;
};

#endif // __KCMProgressBar_h__

// End, KCMProgressBar.h.
