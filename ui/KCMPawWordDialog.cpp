//========================================================================================
//
//  KCMPawWordDialog.cpp
//
//  The word after a press. See KCMPawWordDialog.h.
//
//  ★★★**BROUGHT OVER FROM KOHAKU INDESIGN MCP'S BLUE PENCIL, DELIBERATELY AND ALMOST WHOLE**
//    (the user, 2026-09-07: "just bring it over as it is"). KIDMCPUIPencilDialog.cpp is the same
//    problem already solved -- a tool that asks for a line of text and then writes to the document
//    -- and it carries two findings that cost a night to get and that neither a build nor a
//    casual test would have shown. Both are reproduced below, with their reasons, because a copy
//    that keeps the code and drops the reasons is a copy that gets "simplified" back into the bug.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "ICallbackTimer.h"			// the one-shot delay between the release and the dialog
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IIdleTask.h"				// kEndOfTime - what a one-shot callback MUST return
#include "ISession.h"

// General includes:
#include "CDialogController.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "CreateObject.h"
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"
#include "ShuksanID.h"				// kCallbackTimerBoss / IID_ICALLBACKTIMER
#include "Utils.h"

// Project includes:
#include "KCMUIID.h"
#include "KCMPawWordDialog.h"
#include "KCMUIShared.h"			// KCMSetStatus
#include "IKCMPageFlagsFacade.h"	// the crossing to the model half
#include "IKCMCompareFacade.h"		// InvalidateDB

namespace
{

// The press waiting for its word. UI code is main-thread only, so nothing here is locked.
// ⚠gTimer is an owned reference (::CreateObject hands one over) and MUST be released -- a timer
//  left holding a raw function pointer into a plug-in that then unloads is a crash
//  (ICallbackTimer.h).
IDataBase*		gDb      = nil;
UID				gPage    = kInvalidUID;
PMReal			gX       = 0.0;
PMReal			gY       = 0.0;
int32			gColour  = 0;
PMReal			gHalf    = 0.0;
ICallbackTimer*	gTimer   = nil;

// ★THE WORD WAITS UNTIL THE DIALOG HAS CLOSED. OK only keeps it and says so; the paw is placed
//  afterwards. The reason is with PlaceFired.
PMString		gText;
bool16			gApply   = kFalse;

void StopTimer()
{
	if (gTimer != nil)
	{
		gTimer->StopTimer();
		gTimer->Release();
		gTimer = nil;
	}
}

void Forget()
{
	gDb     = nil;
	gPage   = kInvalidUID;
	gColour = 0;
	gApply  = kFalse;
	gText.Clear();
}

/* Place
   Puts the waiting paw down, with the words, and says so on the status line.
   ★THE CROSSING: the store lives in the other DLL, so this goes through the facade like every
    other call the tool makes (IKCMPageFlagsFacade.h).
*/
void Place(const PMString& text)
{
	if (gDb == nil || gPage == kInvalidUID)
		return;

	// ⚠★★**NOT `InterfacePtr<IKCMPageFlagsFacade> flags(Utils<IKCMPageFlagsFacade>());`** -- that
	//   is the most vexing parse, and the compiler reads it as a FUNCTION DECLARATION. It was
	//   written that way here first and the error it gives says nothing about the cause
	//   ("'placed': const object must be initialized", pointing at the line below).
	// ★The guard goes on the Utils object, not on what it hands back: QueryUtilInterface has no nil
	//   check of its own, so testing the result is already too late ([[utils-boss-facade-access]]).
	Utils<IKCMPageFlagsFacade> flags;
	if (!flags)
		return;					// the model half is not there -- nothing to place into

	const bool16 placed = flags->PawStampPlaceAt(gDb, gPage, gX, gY, gColour, gHalf, text);

	PMString msg;
	msg.SetTranslatable(kFalse);
	// ⚠The same four outcomes the plain press reports, and worded the same way: a press that lands
	//   on a paw already there places nothing, and saying "placed" then would be a lie the count
	//   does not correct.
	msg = placed ? (text.IsEmpty() ? "Paw placed (" : "Paw and word placed (")
	             : "Paw: one is already there (";
	msg.AppendNumber(flags->PawStampCount(gDb));
	msg += " on this document)";
	msg.SetTranslatable(kFalse);
	KCMSetStatus(msg);

	// ⚠**No repaint from here.** The model's marks observer does it when the write lands, and it
	//   does it for undo and redo too, which nothing on this side could (KCMMarksObserver.h). The
	//   UI asks for the change; the model decides what moved and shows it.
}

/* TimerFired
   The first timer's callback: the tracker has let go, so the box can be opened.

   ⚠★★★MUST RETURN kEndOfTime. Returning 0 means "call me again at once", not "done"
    (IIdleTask.h); this machine's plug-ins froze InDesign that way once already.
   ★THE TIMER IS LET GO FIRST, then the dialog opens: Open() runs a modal loop, and anything
    wanting to stop the timer during it would find nothing to stop, which is the honest state.
*/
uint32 TimerFired(void* /*refPtr*/)
{
	ICallbackTimer* fired = gTimer;
	gTimer = nil;
	if (fired != nil)
		fired->Release();

	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDialogMgr> dialogMgr(app, UseDefaultIID());
	if (dialogMgr == nil)
	{
		Forget();
		return IIdleTask::kEndOfTime;
	}

	RsrcSpec spec(LocaleSetting::GetLocale(), kKCMUIPluginID, kViewRsrcType,
	              kKCMPawWordDialogRsrcID, kTrue);
	// kMovableModal (the reader may want to see the page under it), one copy, NOT cached (the box
	// starts empty every time, and InitializeDialogFields is only sure to run on a fresh dialog),
	// not resizable (one line).
	IDialog* dialog = dialogMgr->CreateNewDialog(spec, IDialog::kMovableModal,
		IDialogMgr::kDontAllowMultipleCopies, IDialogMgr::kDontCacheDialog,
		IDialogMgr::kDontAllowUserResize);
	if (dialog == nil)
	{
		Forget();
		return IIdleTask::kEndOfTime;
	}

	// ⚠**NO COMMAND BUFFERING.** IDialog.h: by default the dialog command manager BUFFERS every
	//  command fired while the dialog is up and runs them as one compound command when OK is
	//  pressed -- **a compound with no name**. What this file cares about is the NAME: the step
	//  this places has to read "Place Cat Paw" in the Edit menu, and a step swallowed by a nameless
	//  compound does not. This dialog fires no command of its own (one edit box), so there is
	//  nothing to buffer and nothing is lost.
	dialog->SetBufferCommands(kFalse);
	dialog->Open();			// waits: the controller below keeps the word (OK) or forgets (Cancel)

	if (gApply)
		Place(gText);		// OK, with or without a word
	Forget();				// Cancel falls here having placed nothing, which is what Cancel means
	return IIdleTask::kEndOfTime;
}

/* ⚠★**THE BLUE PENCIL HAS A SECOND TIMER HERE AND KCM DELIBERATELY DOES NOT** (the user,
   2026-09-07: "that part is there to keep it from going dirty, so it is not needed -- dirty is
   fine"). Over there, recording right after Open() returned put the step in the same transaction
   as InDesign's own work of closing a modal, and undoing it MARKED THE DOCUMENT MODIFIED -- which
   for a plug-in whose marks never touch the .indd is a defect worth a night's hunting.
   ★**Here it is not a defect at all.** A cat paw IS written into the document, on purpose; the
   file is meant to be modified the moment one is placed, exactly as it is for a plain press. So
   the second timer would buy nothing and cost a tick of delay before the paw appeared.
   ⇒ **If this file is ever compared with KIDMCPUIPencilDialog.cpp again, this difference is
   intended.** Copying that timer back in would not break anything; it would just be answering a
   question KCM does not have. */

}	// anonymous namespace


void KCMPawWordDialog::AskAndPlaceLater(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
                                        int32 colour, const PMReal& baseHalf)
{
	StopTimer();
	gDb     = db;
	gPage   = pageUID;
	gX      = x;
	gY      = y;
	gColour = colour;
	gHalf   = baseHalf;
	gApply  = kFalse;
	gText.Clear();

	gTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (gTimer == nil)
	{
		Forget();
		return;
	}
	// One millisecond, not zero: ICallbackTimer's callback treats 0 as "call me again at once"
	// (the flyout's own comment records freezing InDesign that way), and a millisecond is the same
	// gesture to a person.
	gTimer->StartTimer(TimerFired, 1, nil);
}

void KCMPawWordDialog::Shutdown()
{
	StopTimer();
	Forget();
}


// ---------------------------------------------------------------------------------------------
// The dialog's controller. FILE SCOPE, not the anonymous namespace: CREATE_PMINTERFACE defines a
// factory the plug-in's NoStrip file refers to by name, and internal linkage would fail the link.
// ---------------------------------------------------------------------------------------------

class KCMPawWordDialogController : public CDialogController
{
public:
	KCMPawWordDialogController(IPMUnknown* boss) : CDialogController(boss) {}

	/** An empty box every time: the word is about THIS paw. */
	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		PMString empty;
		empty.SetTranslatable(kFalse);
		this->SetTextControlData(kKCMPawWordEditWidgetID, empty);
	}

	/** OK: the word is kept for PlaceFired, which places once the dialog is down (gApply).
		★**An empty box is still an OK**: an Alt press that changed its mind about the word still
		 meant to put a paw down. That is why the flag is set here rather than derived from the
		 text -- the text cannot tell an empty OK from a Cancel, and this can. */
	virtual void ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId)
	{
		CDialogController::ApplyDialogFields(context, widgetId);
		gText  = this->GetTextControlData(kKCMPawWordEditWidgetID);
		gApply = kTrue;
	}

	/** Cancel: nothing is placed. */
	virtual void UserCancelled()
	{
		CDialogController::UserCancelled();
		Forget();
	}
};

CREATE_PMINTERFACE(KCMPawWordDialogController, kKCMPawWordDialogControllerImpl)

// End, KCMPawWordDialog.cpp.
