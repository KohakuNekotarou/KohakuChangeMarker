//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - see KCMTrackLabels.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "Utils.h"

#include "IKCMCompareFacade.h"		// GetMarkColorCyan - which of the two Mark colours the flyout chose
#include "KCMConstants.h"			// kKCMRingR.. / kKCMRingAltR.. - the Mark colour's two values (KCMDrawEventHandler::SelectedMarkColor reads the same)
#include "KCMTrackText.h"			// the words themselves, shared with the Before/After report since 2026-10-27
#include "KCMTrackLabels.h"

// ★The sign, the word, the time and the author's name are KCMTrackText's (2026-10-27): the report's Tracked Changes table
//   writes a change in the same words the panel shows it in, so they are spelt once, in a header both halves read.
PMString KCMTrackKindSign(int32 kind)		{ return KCMTrackText::KindSign(kind); }
PMString KCMTrackKindWord(int32 kind)		{ return KCMTrackText::KindWord(kind); }
PMString KCMTrackTimeLabel(uint64 t)		{ return KCMTrackText::TimeLabel(t); }

RealAGMColor KCMTrackColour(bool16 hasColour, uint8 r, uint8 g, uint8 b)
{
	if (hasColour)
		return RealAGMColor(r / 255.0, g / 255.0, b / 255.0);
	// The Mark colour: the same two constants KCMDrawEventHandler::SelectedMarkColor answers with, chosen by the same
	// flag (read through the facade - the UI half cannot reach the handler's static).
	Utils<IKCMCompareFacade> compare;
	const bool16 cyan = (compare && compare->GetMarkColorCyan()) ? kTrue : kFalse;
	return cyan ? RealAGMColor(kKCMRingAltR / 255.0, kKCMRingAltG / 255.0, kKCMRingAltB / 255.0)
				: RealAGMColor(kKCMRingR / 255.0, kKCMRingG / 255.0, kKCMRingB / 255.0);
}

PMString KCMTrackAuthorName(const PMString& name)	{ return KCMTrackText::AuthorName(name); }

// End, KCMTrackLabels.cpp.
