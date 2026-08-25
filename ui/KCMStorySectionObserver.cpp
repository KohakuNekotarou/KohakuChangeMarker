//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Watches the "Story Edits" toggle button and opens or closes the section when it is pressed.
//  Deliberately thin: the observer knows only that the button went down, and KCMStorySection
//  owns every decision about what that means. Same split as the product's Links panel
//  (ToggleLinkInfoButtonObserver.cpp), which this is copied from.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// ----- Interfaces -----
#include "ISubject.h"
#include "IControlView.h"
#include "ITriStateControlData.h"

// ----- Includes -----
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage

// ----- Project -----
#include "KCMUIID.h"
#include "KCMStorySection.h"

//========================================================================================
// CLASS KCMStorySectionToggleObserver
//========================================================================================
class KCMStorySectionToggleObserver : public CObserver
{
public:
	KCMStorySectionToggleObserver(IPMUnknown* boss);
	virtual ~KCMStorySectionToggleObserver();

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);

	virtual void AutoAttach();
	virtual void AutoDetach();
};

CREATE_PMINTERFACE(KCMStorySectionToggleObserver, kKCMStorySectionToggleObserverImpl)

//----------------------------------------------------------------------------------------
KCMStorySectionToggleObserver::KCMStorySectionToggleObserver(IPMUnknown* boss) :
	CObserver(boss)
{
}

//----------------------------------------------------------------------------------------
KCMStorySectionToggleObserver::~KCMStorySectionToggleObserver()
{
}

//----------------------------------------------------------------------------------------
void KCMStorySectionToggleObserver::AutoAttach()
{
	InterfacePtr<ISubject> subject(this, UseDefaultIID());
	if (subject != nil)
		subject->AttachObserver(this, IID_ITRISTATECONTROLDATA);

	// The panel rebuilds its widgets every time it is shown, so the triangle has to be pointed at
	// the section's real state here rather than left at whatever the resource drew.
	KCMUpdateStorySectionButtonState();
}

//----------------------------------------------------------------------------------------
void KCMStorySectionToggleObserver::AutoDetach()
{
	InterfacePtr<ISubject> subject(this, UseDefaultIID());
	if (subject != nil)
		subject->DetachObserver(this, IID_ITRISTATECONTROLDATA);
}

//----------------------------------------------------------------------------------------
void KCMStorySectionToggleObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/,
                                             const PMIID& /*protocol*/, void* /*changedBy*/)
{
	if (theChange == kTrueStateMessage)
		KCMToggleStorySection();
}

// End, KCMStorySectionObserver.cpp.
