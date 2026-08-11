//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison dialog: the Compare button.
//
//  Lives on the DIALOG boss, not on a button boss of its own - the same arrangement the panel
//  uses (KESCMPanelObserver): one observer, AutoAttach, and a switch on the widget ID. A custom
//  button boss would add a Class and an Impl and buy nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBooleanControlData.h"	// how a plain button is subscribed to
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage

#include <vector>

// Project includes:
#include "KESCMBookCompare.h"		// KESCMCompareBooks
#include "KESCMBookDialog.h"		// KESCMBookDialogUpdateTargets / KESCMBookDialogSetStatus
#include "KESCMBookPair.h"			// KESCMResolveBookPair
#include "KESCMBookResult.h"		// KESCMChapterResult
#include "KESCMID.h"

/** Watches the book comparison dialog's Compare button. */
class KESCMBookDialogObserver : public CObserver
{
public:
	KESCMBookDialogObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KESCMBookDialogObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject,
	                    const PMIID& protocol, void* changedBy);

private:
	void AttachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	void DetachWidget(const InterfacePtr<IPanelControlData>& pcd, const WidgetID& wid, const PMIID& iid);
	void RunComparison();
};

CREATE_PMINTERFACE(KESCMBookDialogObserver, kKESCMBookDialogObserverImpl)

void KESCMBookDialogObserver::AutoAttach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	// A plain button reports through IBooleanControlData - same as the panel's Prev / Next.
	this->AttachWidget(pcd, kKESCMBookCompareButtonWidgetID, IBooleanControlData::kDefaultIID);
}

void KESCMBookDialogObserver::AutoDetach()
{
	InterfacePtr<IPanelControlData> pcd(this, UseDefaultIID());
	if (pcd == nil)
		return;

	this->DetachWidget(pcd, kKESCMBookCompareButtonWidgetID, IBooleanControlData::kDefaultIID);
}

void KESCMBookDialogObserver::AttachWidget(const InterfacePtr<IPanelControlData>& pcd,
                                           const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;

	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil && !subject->IsAttached(this, iid))
		subject->AttachObserver(this, iid);
}

void KESCMBookDialogObserver::DetachWidget(const InterfacePtr<IPanelControlData>& pcd,
                                           const WidgetID& wid, const PMIID& iid)
{
	IControlView* cv = pcd->FindWidget(wid);
	if (cv == nil)
		return;

	InterfacePtr<ISubject> subject(cv, UseDefaultIID());
	if (subject != nil && subject->IsAttached(this, iid))
		subject->DetachObserver(this, iid);
}

void KESCMBookDialogObserver::Update(const ClassID& theChange, ISubject* theSubject,
                                     const PMIID& /*protocol*/, void* /*changedBy*/)
{
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
		return;
	}

	// ⚠ This blocks until the whole comparison is done - there is no progress bar yet (that is a
	// later stage). On a large book the dialog will sit still for a while.
	std::vector<KESCMChapterResult> chapters;
	PMString report;
	KESCMCompareBooks(target, source, chapters, report);

	KESCMBookDialogSetStatus(panelData, report);
}

// End, KESCMBookDialogObserver.cpp.
