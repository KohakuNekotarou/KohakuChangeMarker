//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison dialog: the Compare button.
//
//  Lives on the DIALOG boss, not on a button boss of its own - the arrangement every dialog sample
//  in the SDK uses (basicdialog, basiclocalization, writefishprice): one observer on the dialog,
//  and a switch on the widget ID.
//
//  ***** IT DERIVES FROM CDialogObserver, AND THAT IS NOT A DETAIL. *****
//  Naming IID_IOBSERVER on the dialog boss REPLACES the stock implementation, and the stock one is
//  what makes OK, Cancel and the close box work at all: CDialogObserver attaches to those buttons
//  and to the dialog's window, validates, applies and closes (CDialogObserver.cpp:266-326 /
//  :68-185). A CObserver-derived class put here therefore does not "add" a button - it silently
//  takes the dialog's own machinery away.
//  ⚠ MEASURED 2026-08-11: while this class derived from CObserver, OK did nothing at all - a real
//  mouse click on it left the dialog open. Every override below calls its base FIRST, exactly as
//  basicdialog does (BscDlgDialogObserver.cpp:99 / :124 / :155).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBooleanControlData.h"	// how a plain button is subscribed to
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ISubject.h"				// Update's subject - a complete type is needed to ask it for
									// IID_ICONTROLVIEW (CDialogObserver.h does not bring it in)

// General includes:
#include "CDialogObserver.h"
#include "widgetid.h"				// kTrueStateMessage

#include <vector>

// Project includes:
#include "KESCMBookCompare.h"		// KESCMCompareBooks
#include "KESCMBookDialog.h"		// KESCMBookDialogUpdateTargets / SetStatus / SetRows
#include "KESCMBookPair.h"			// KESCMResolveBookPair
#include "KESCMBookResult.h"		// KESCMChapterResult
#include "KESCMBookTree.h"			// KESCMBookTreeRebuild
#include "KESCMID.h"

/** Watches the book comparison dialog's Compare button, on top of everything CDialogObserver
	already does for OK, Cancel and the close box. */
class KESCMBookDialogObserver : public CDialogObserver
{
public:
	KESCMBookDialogObserver(IPMUnknown* boss) : CDialogObserver(boss) {}
	virtual ~KESCMBookDialogObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject,
	                    const PMIID& protocol, void* changedBy);

private:
	void RunComparison();
};

CREATE_PMINTERFACE(KESCMBookDialogObserver, kKESCMBookDialogObserverImpl)

void KESCMBookDialogObserver::AutoAttach()
{
	// ***** The base FIRST. ***** It is what subscribes to OK, Cancel and the dialog's window;
	// without it this dialog would have no way of closing (measured - see the file header).
	CDialogObserver::AutoAttach();

	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	if (panelData == nil)
		return;

	// A plain button reports through IBooleanControlData - same as the panel's Prev / Next.
	// AttachToWidget is the base's own helper (AbstractDialogObserver.h:51), so this line says
	// exactly what basicdialog's line says.
	this->AttachToWidget(kKESCMBookCompareButtonWidgetID, IBooleanControlData::kDefaultIID, panelData);
}

void KESCMBookDialogObserver::AutoDetach()
{
	CDialogObserver::AutoDetach();

	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	if (panelData == nil)
		return;

	this->DetachFromWidget(kKESCMBookCompareButtonWidgetID, IBooleanControlData::kDefaultIID, panelData);
}

void KESCMBookDialogObserver::Update(const ClassID& theChange, ISubject* theSubject,
                                     const PMIID& protocol, void* changedBy)
{
	// The base first, again: OK / Cancel / the close box are its business, and the Compare button
	// is not one of the widgets it reacts to, so nothing here can be swallowed by it.
	CDialogObserver::Update(theChange, theSubject, protocol, changedBy);

	InterfacePtr<IControlView> cv(theSubject, UseDefaultIID());
	if (cv == nil)
		return;

	if (theChange == kTrueStateMessage && cv->GetWidgetID() == kKESCMBookCompareButtonWidgetID)
		this->RunComparison();
}

void KESCMBookDialogObserver::RunComparison()
{
	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	if (panelData == nil)
		return;

	// ***** Resolve again, right now. ***** The dialog is modeless, so the front tab can have
	// changed since it was opened. Refreshing here is what guarantees the names on screen and the
	// books actually compared are the same two - the labels are the user's only chance to notice
	// a mismatch, so they must not be stale.
	KESCMBookDialogUpdateTargets(panelData);

	IBook* target = nil;
	IBook* source = nil;
	if (!KESCMResolveBookPair(target, source))
	{
		PMString msg("Select a book tab in the Book panel, and open a second book.");
		msg.SetTranslatable(kFalse);
		KESCMBookDialogSetStatus(panelData, msg);

		// ★The list goes too. What is on screen has to describe the run that just happened, and
		//   this run produced nothing - leaving the previous book's chapters up there would be
		//   showing an answer to a question nobody asked any more.
		KESCMBookDialogSetRows(std::vector<KESCMChapterResult>());
		KESCMBookTreeRebuild(panelData);
		return;
	}

	// ⚠ This blocks until the whole comparison is done - there is no progress bar yet (that is a
	// later stage). On a large book the dialog will sit still for a while.
	std::vector<KESCMChapterResult> chapters;
	PMString report;
	KESCMCompareBooks(target, source, chapters, report);

	KESCMBookDialogSetStatus(panelData, report);

	// The summary says how many of each; the list says which. ★Set the rows BEFORE rebuilding:
	// the tree asks the adapter how many rows there are while ChangeRoot runs, and the adapter
	// reads exactly what was just stored.
	KESCMBookDialogSetRows(chapters);
	KESCMBookTreeRebuild(panelData);
}

// End, KESCMBookDialogObserver.cpp.
