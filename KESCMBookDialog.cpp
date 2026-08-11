//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: the dialog. See KESCMBookDialog.h for why it is modeless and fixed-size.
//
//  Shape copied from KESCL's Jump Offset dialog (KESCLActionComponent.cpp:384-417), which is the
//  closest working example in reach - same author, same stock kDialogBoss, also fixed-size. The
//  one thing changed is the dialog type: kModeless instead of kMovableModal.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IControlView.h"		// Enable / Disable - the Compare button
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IPanelControlData.h"	// FindWidget - reaching the dialog's own widgets
#include "ISession.h"
#include "ITextControlData.h"	// SetString - the Target/Source lines and the status area

// General includes:
#include "CDialogController.h"
#include "CoreResTypes.h"		// kViewRsrcType - the dialog's RsrcSpec
#include "LocaleSetting.h"		// the dialog's RsrcSpec locale
#include "RsrcSpec.h"

// Project includes:
#include "KESCMBookDialog.h"
#include "KESCMBookPair.h"		// KESCMResolveBookPair / KESCMBookDisplayName
#include "KESCMID.h"

/** The dialog's controller.

    There is nothing to fill in or apply yet - the Target/Source labels, the Compare button and the
    chapter list all arrive in the next tasks. The class exists now because the boss names this
    implementation, and a boss naming an implementation that is not registered simply fails to
    produce the interface (silently - which is the whole difficulty of that class of bug). */
class KESCMBookDialogController : public CDialogController
{
public:
	KESCMBookDialogController(IPMUnknown* boss) : CDialogController(boss) {}

	virtual void InitializeDialogFields(IActiveContext* context);
	virtual void ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId);
};

CREATE_PMINTERFACE(KESCMBookDialogController, kKESCMBookDialogControllerImpl)

void KESCMBookDialogController::InitializeDialogFields(IActiveContext* context)
{
	CDialogController::InitializeDialogFields(context);

	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	KESCMBookDialogUpdateTargets(panelData);
}

//----------------------------------------------------------------------------------------
// Filling the dialog in
//----------------------------------------------------------------------------------------

namespace
{

/** Put text into one of the dialog's static text widgets. Does nothing if it is not there. */
void SetLine(IPanelControlData* panelData, const WidgetID& widgetID, const PMString& text)
{
	if (panelData == nil)
		return;

	IControlView* view = panelData->FindWidget(widgetID);
	if (view == nil)
		return;

	InterfacePtr<ITextControlData> textData(view, UseDefaultIID());
	if (textData != nil)
		textData->SetString(text);		// SetString invalidates by itself - no Invalidate() here
}

}	// anonymous namespace

void KESCMBookDialogUpdateTargets(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	IBook* target = nil;
	IBook* source = nil;
	const bool16 resolved = KESCMResolveBookPair(target, source);

	PMString targetLine("Target: ");
	PMString sourceLine("Source: ");
	if (resolved)
	{
		targetLine.Append(KESCMBookDisplayName(target));
		sourceLine.Append(KESCMBookDisplayName(source));
	}
	else
	{
		// ***** Say WHICH half is missing, and never guess. ***** The front tab is deliberately
		// not allowed to fall back to InDesign's active book (see KESCMBookPair.h), so when it
		// cannot be identified the honest thing is to say so and refuse to run.
		targetLine.Append("(no book tab in front)");
		sourceLine.Append("(no second open book)");
	}
	targetLine.SetTranslatable(kFalse);
	sourceLine.SetTranslatable(kFalse);

	SetLine(panelData, kKESCMBookTargetTextWidgetID, targetLine);
	SetLine(panelData, kKESCMBookSourceTextWidgetID, sourceLine);

	// Compare is only pressable when there is something to compare.
	IControlView* compareButton = panelData->FindWidget(kKESCMBookCompareButtonWidgetID);
	if (compareButton != nil)
	{
		if (resolved)
			compareButton->Enable();
		else
			compareButton->Disable();
	}
}

void KESCMBookDialogSetStatus(IPanelControlData* panelData, const PMString& message)
{
	SetLine(panelData, kKESCMBookStatusTextWidgetID, message);
}

void KESCMBookDialogController::ApplyDialogFields(IActiveContext* context, const WidgetID& widgetId)
{
	CDialogController::ApplyDialogFields(context, widgetId);
}

void KESCMOpenBookDialog()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;

	InterfacePtr<IApplication> application(session->QueryApplication());
	if (application == nil)
		return;

	InterfacePtr<IDialogMgr> dialogMgr(application, UseDefaultIID());
	if (dialogMgr == nil)
		return;

	RsrcSpec dialogSpec
	(
		LocaleSetting::GetLocale(),		// Locale index
		kKESCMPluginID,					// This plug-in
		kViewRsrcType,
		kKESCMBookDialogRsrcID,			// The dialog's view resource
		kTrue							// Initially visible
	);

	// ***** kModeless / kDontAllowUserResize / kCacheDialog - each for its own reason: *****
	//   - kModeless is the point of the whole thing (see the header)
	//   - kDontAllowUserResize because a modeless dialog's panel does NOT follow its window: a
	//     resize handle would move the frame and leave the contents behind
	//   - kCacheDialog so that closing and reopening keeps the contents and the position. That
	//     matters here more than in most dialogs: the results took real time to compute, and
	//     closing the window must not be the same as throwing them away
	// kDontAllowMultipleCopies is belt and braces - IDialogMgr already allows only one copy of a
	// modeless dialog (IDialogMgr.h:67) - but it states the intent rather than relying on it.
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kModeless,
		IDialogMgr::kDontAllowMultipleCopies, IDialogMgr::kCacheDialog,
		IDialogMgr::kDontAllowUserResize);
	if (dialog == nil)
		return;

	// ***** doWait = kFalse. ***** Open() waits by default (IDialog.h:59-65), which would make a
	// modeless dialog behave like a modal one - the exact thing this dialog exists not to do.
	dialog->Open(nil, kFalse);
}

// End, KESCMBookDialog.cpp.
