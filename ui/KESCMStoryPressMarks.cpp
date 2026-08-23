//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  See KESCMStoryPressMarks.h for what the Story mode's marks are and what decides them.
//
//  ★★★THERE IS NOTHING LEFT HERE BUT THE DOOR (2026-08-23). Everything this file used to do -
//  reading the Story Edits list, turning changes into ranges, remembering whether the button is
//  down - moved to source/KESCMStoryMarkBuild.cpp when the adornment moved to the model plug-in.
//  The move was forced by one measured fact: **the UI's File > Export > PDF runs in the background
//  and a kUIPlugIn is never handed the drawing**, so marks living on this side could not reach an
//  exported PDF no matter what their print guard said.
//
//  ★THE THREE FUNCTIONS KEPT THEIR NAMES AND THEIR SIGNATURES ON PURPOSE. Six callers across four
//  files (the action component, the model-change observer, the peek gesture) were left untouched by
//  the migration, so "the behaviour did not change" is something a reader can check by looking at
//  the diff rather than by trusting it.
//
//  ⚠DO NOT PUT DECISIONS BACK IN HERE. Anything that answers "should a mark be showing" belongs on
//  the model side, where the print toggle, the compare mode and the two documents already live;
//  a test written here would be the second place that question is answered
//  ([[one-question-one-place]]).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "Utils.h"

// Project includes:
#include "IKESCMStoryMarkFacade.h"		// the model side of all three of these
#include "KESCMStoryPressMarks.h"

// ⚠★★★NIL IS A REAL ANSWER, AND TESTING FOR IT IS NOT DEFENSIVE PADDING (2026-08-23, found by the
//   bug recheck of this very migration). KESCMStoryMarksRefresh is hung off a model notification,
//   so it can arrive while the application is closing - and by then kUtilsBoss may already be gone.
//   ★THE VERSION OF THIS FILE THAT STOOD HERE BEFORE THE MIGRATION HELD AN InterfacePtr AND TESTED
//     IT (`if (compare != nil && compare->IsArmed() ...`). Going through a facade instead must not
//     quietly drop that guard - the same protection the CMYK sampler's shutdown path keeps, and for
//     the same reason (KESCMPeek.cpp writes it down).
//   ⚠`InterfacePtr<IXxx> p(Utils<IXxx>());` does not compile - most vexing parse - which is why
//     every one of these says QueryUtilInterface() ([[utils-boss-facade-access]]).

void KESCMStoryMarksRefresh()
{
	InterfacePtr<IKESCMStoryMarkFacade> marks(Utils<IKESCMStoryMarkFacade>().QueryUtilInterface());
	if (marks != nil)
		marks->Refresh();
}

void KESCMStoryPressMarksBegin(bool16 useSourceDocument)
{
	InterfacePtr<IKESCMStoryMarkFacade> marks(Utils<IKESCMStoryMarkFacade>().QueryUtilInterface());
	if (marks != nil)
		marks->SetPress(kTrue, useSourceDocument);
}

void KESCMStoryPressMarksEnd()
{
	// ⚠No "was anything pressed" test here, and that is not an omission: the model keeps the press
	//   state, so it is the only side that can answer, and it refuses a release it never saw a press
	//   for. Testing here as well would be the same fact written down twice.
	InterfacePtr<IKESCMStoryMarkFacade> marks(Utils<IKESCMStoryMarkFacade>().QueryUtilInterface());
	if (marks != nil)
		marks->SetPress(kFalse, kFalse);
}

// End, KESCMStoryPressMarks.cpp.
