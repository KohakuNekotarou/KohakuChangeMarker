//========================================================================================
//
//  IKCMUIServices.h
//
//  WHAT THE MODEL HALF ASKS THE UI HALF FOR, AND DOES WITHOUT WHEN IT IS NOT THERE (the author's call,
//  "follow the guide strictly: the alerts and the progress bars are the UI's too", made for KCM:
//  docs/superpowers/specs/2026-10-28-kcm-ui-services-design.md).
//
//  NOT A LINK MATTER. RangeProgressBar, SuppressProgressBarDisplay and CAlert are PUBLIC_DECL, in the library every
//  plug-in links (ProgressBar.h, CAlert.h), so the model half could call them. They are the UI's because the guide
//  lists DIALOGS among the user-interface components (vol1-06, "UI component content") and a modal progress bar and a
//  Yes / No alert are read as dialogs - KFC's reading and KFC's shape (KFC/model/IKFCUIServices.h), copied. So the model
//  asks THIS interface - pure virtual, declared here on the model's side - and the UI half implements it
//  (ui/KCMUIServices.cpp) and AddIns it to kSessionBoss from its own resource (vol1-07, Object-Model Rule 2: a UI
//  implementation reaches a model boss only through an AddIn in a UI plug-in's resource). kSessionBoss, as KFC's: the
//  guide gives every thread a session of its own, and a background thread's has no UI half on it.
//
//  NIL IS AN ANSWER. On a background thread and under InDesign Server the UI plug-in is not loaded and the Query comes
//  back nil - "It is critical that you write model code that expects to be able to receive nil pointers" (vol1-07,
//  "Rules for thread safety"). Every caller reads nil as "no UI" and does without: no bar, nothing to cancel, no
//  question asked - and a write that waits for a Yes is not made (KCMHideUnchanged.cpp).
//
//  WHAT THE MODEL DECIDES STAYS IN THE MODEL: the words (KCMLoc), whether and when to ask, the bar's wait and its steps,
//  where a Cancel is read and what it throws away (KCMProgressBar.h). What crosses is the showing.
//
//  THE SLOTS ARE A PROMISE TO THE OTHER HALF: the model half calls the UI half's vtable. A new method goes below the
//  last one, never between, and both halves are built together ([[facade-vtable-slot-append-only]]). There is no build
//  stamp, unlike KFC's (KFCOtherHalfIsThisBuild): KCM ships its two .pln together, and nothing outside KCM calls this.
//
//========================================================================================

#ifndef __IKCMUIServices_h__
#define __IKCMUIServices_h__

#include "IPMUnknown.h"
#include "ISession.h"			// GetExecutionContextSession - KCMQueryUIServices
#include "PMString.h"

#include "KCMBoundaryID.h"		// IID_IKCMUISERVICES
#include "KCMDiag.h"			// KCM_DIAG_FAULT - test builds only (ui-services-off)

/** One modal progress bar the UI half has put up for the model half - a RangeProgressBar's calls, and nothing else.
	Deleted by the model half when its run is over, and the bar comes down with it (KCMProgressBar.h). A bar the
	progress-bar manager turned away comes back all the same and drives nothing: its WasCancelled answers false. */
class KCMProgressBarUI
{
public:
	virtual ~KCMProgressBarUI() {}
	virtual void	SetTaskText(const PMString& text, bool16 forceRedraw) = 0;
	virtual void	SetPosition(int32 newPosition) = 0;
	virtual bool16	WasCancelled(bool8 setGlobalErrorState) = 0;
	virtual void	DisableChildProgressBars(bool16 disable) = 0;
};

/** While one of these lives, no progress bar is displayed - a SuppressProgressBarDisplay(kTrue) the UI half holds
	("all progress bars below you will not display", ProgressBar.h). Deleted by the model half. */
class KCMProgressSuppressorUI
{
public:
	virtual ~KCMProgressSuppressorUI() {}
};

class IKCMUIServices : public IPMUnknown
{
public:
	// No kDefaultIID, on purpose: the model half asks through KCMQueryUIServices and nowhere else.

	/** A modal RangeProgressBar with these arguments (ProgressBar.h), or nil when one could not be made. The caller
		owns it and deletes it when its run is over. */
	virtual KCMProgressBarUI*			NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
											bool8 showImmediate, bool8 showCancel) = 0;

	/** A SuppressProgressBarDisplay(kTrue) for as long as the returned object lives, or nil. The caller owns it. */
	virtual KCMProgressSuppressorUI*	NewProgressBarSuppressor() = 0;

	/** The Yes / No question asked before a document is changed: a warning alert with Yes the default button. kTrue =
		Yes. The words are the model's, finished in the UI language and marked untranslatable (KCMLoc::Text). */
	virtual bool16						AskYesNo(const PMString& question) = 0;

	// Appended from here on - below the last slot, never between (the head of this file says why).
};

/** HOW THE MODEL HALF ASKS FOR IKCMUIServices - the only way. AddRef'd, or nil: no UI half (a background thread's session,
	InDesign Server) - or, in a test build, the fault switch ui-services-off (KCMDiag.h), which stands for that case on
	the desktop. Asked every time, never kept: the main thread's session has the UI half, a background thread's has not. */
inline IKCMUIServices* KCMQueryUIServices()
{
	if (KCM_DIAG_FAULT("ui-services-off"))
		return nil;
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IKCMUIServices> ui(session, IID_IKCMUISERVICES);
	return ui.forget();
}

#endif // __IKCMUIServices_h__

// End, IKCMUIServices.h.
