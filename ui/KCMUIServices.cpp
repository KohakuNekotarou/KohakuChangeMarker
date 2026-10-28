//========================================================================================
//
//  KCMUIServices.cpp
//
//  The UI half's side of IKCMUIServices.h (2026-10-28): the progress bars and the Yes / No question the model half asks
//  for. AddIn'd on kSessionBoss from KCMUI.fr, so a background thread's session - which has no UI plug-in on it - finds
//  nothing there and the model half does without. What to show, when, and in which words is the model half's
//  (KCMProgressBar.h, KCMHideUnchanged.cpp); this file only shows it.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CAlert.h"					// ModalAlert, kYesString / kNoString / kNullString, the icon enum
#include "CPMUnknown.h"
#include "ProgressBar.h"			// RangeProgressBar, SuppressProgressBarDisplay

#include <new>						// std::nothrow

#include "KCMUIID.h"				// kKCMUIServicesImpl
#include "IKCMUIServices.h"

namespace
{
	/** One RangeProgressBar, held for the model half.

		A BAR THE MANAGER TURNED AWAY IS NOT DRIVEN. Another bar's DisableChildProgressBars makes the progress-bar manager
		refuse a new one (ProgressBar.h, WasRegisterSuccessful: "Reasons for failure include another progress bar calling
		DisableChildProgressBars()" - and every KCM bar calls it, as KIDMCP's does). A refused bar never comes on screen,
		so its calls give up nothing anyone could see; what they would do is call into a bar the manager does not hold.
		The refusal is not hypothetical: measured 2026-09-05 ("the bar shows in the Pixel mode and not in the Story mode" -
		the raster loop's object had outlived its loop), and that day it showed only as a missing bar, no crash. The guard
		was written the day KIDMCP was found driving a refused bar of its own; it stood in KCMProgressBar.h until
		2026-10-28 and moved here with the bar (KFC's UI half has the same one, KFCUIServices.cpp). Asked once, as the bar
		is made. */
	class KCMUIProgressBar : public KCMProgressBarUI
	{
	public:
		KCMUIProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
			: fBar(title, startRange, endRange, showImmediate, showCancel), fRegistered(fBar.WasRegisterSuccessful() != kFalse) {}
		virtual ~KCMUIProgressBar() {}

		virtual void SetTaskText(const PMString& text, bool16 forceRedraw)
		{
			if (fRegistered)
				fBar.SetTaskText(text, forceRedraw);
		}
		virtual void SetPosition(int32 newPosition)
		{
			if (fRegistered)
				fBar.SetPosition(newPosition);
		}
		/** False for a refused bar: nobody can have pressed a Cancel that was never on screen. */
		virtual bool16 WasCancelled(bool8 setGlobalErrorState)
		{
			return fRegistered ? fBar.WasCancelled(setGlobalErrorState) : kFalse;
		}
		virtual void DisableChildProgressBars(bool16 disable)
		{
			if (fRegistered)
				fBar.DisableChildProgressBars(disable);
		}

	private:
		RangeProgressBar	fBar;			// declared first: fRegistered reads it as it is made
		const bool			fRegistered;	// the manager took the bar (WasRegisterSuccessful)
	};

	/** While this lives, no progress bar is displayed. */
	class KCMUIProgressSuppressor : public KCMProgressSuppressorUI
	{
	public:
		KCMUIProgressSuppressor() : fSuppress(kTrue) {}
		virtual ~KCMUIProgressSuppressor() {}

	private:
		SuppressProgressBarDisplay	fSuppress;
	};
}

class KCMUIServices : public CPMUnknown<IKCMUIServices>
{
public:
	KCMUIServices(IPMUnknown* boss) : CPMUnknown<IKCMUIServices>(boss) {}
	virtual ~KCMUIServices() {}

	virtual KCMProgressBarUI* NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
		bool8 showImmediate, bool8 showCancel)
	{
		return new (std::nothrow) KCMUIProgressBar(title, startRange, endRange, showImmediate, showCancel);
	}

	virtual KCMProgressSuppressorUI* NewProgressBarSuppressor()
	{
		return new (std::nothrow) KCMUIProgressSuppressor();
	}

	/** The alert Hide Unchanged Spreads raised from the model half until 2026-10-28, with the same arguments: Yes / No
		(Windows offers only the standard buttons), Yes the default, the warning icon. */
	virtual bool16 AskYesNo(const PMString& question)
	{
		const int16 clicked = CAlert::ModalAlert(question, kYesString, kNoString, kNullString,
			1,							// Yes is the default button
			CAlert::eWarningIcon);
		return (clicked == 1) ? kTrue : kFalse;
	}
};

CREATE_PMINTERFACE(KCMUIServices, kKCMUIServicesImpl)

// End, KCMUIServices.cpp.
