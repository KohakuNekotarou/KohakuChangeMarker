//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The value band: the selected definition's OLDER value (the Resources mode - design section
//  6-2 / 6-3).
//
//  ⚠**"BAND" IS A NICKNAME, NOT A WIDGET, SINCE 2026-09-09.** It had one of its own for a day;
//  it now writes into the panel's MESSAGE AREA, the same box the Story mode puts "Source Text:"
//  in. The name is kept because the whole file is written around it - what changed is that the
//  box is SHARED, which is why clearing it has to ask whose text is standing there first.
//
//  WHY THE BAND EXISTS. The Target's value can be read off the document in front of the reader.
//  **The Source's cannot be read anywhere at all** - the older document's paragraph style is not
//  on screen, and its XML is not something a person reads. Putting the two side by side is the
//  whole point of the mode, and this is where that happens.
//
//  ★★★THE BAND AND THE LIST ARE TWO HALVES, NOT TWO COPIES (2026-09-09, the user's call). A
//  definition row now HAS children - one per attribute that differs - and each of those carries
//  the TARGET's value and a sign. This band carries the SOURCE's, and only the source's. So the
//  reader looks left for the older value and right for the newer one, and neither is written
//  twice. (⚠This supersedes design 6-1b, which had no child rows and put both values here.)
//
//  ★THE BAND IS THERE IN EVERY MODE AND IS EMPTY IN TWO OF THEM. Pixel and Story pay 45px of
//  panel height for a blank strip - the user's call, because resizing the panel on every mode
//  switch runs a full palette re-layout while docked.
//
//========================================================================================

#ifndef __KCMResourceValue_h__
#define __KCMResourceValue_h__

// bool16 and int32 arrive through VCPlugInHeaders.h, which every .cpp of this plug-in includes
// first (KCMStoryTree.h and KCMStorySection.h are written the same way). PMString is named in a
// signature below, so it is included rather than assumed.
#include "PMString.h"

/** Write the OLDER values of the selected row into the panel's MESSAGE AREA.

	★★★**IT IS THE STORY MODE'S BOX, WORD FOR WORD** (2026-09-09, the user's call: "make it like
	Story's Source Text - Source Resource - and the second line just the source's value"). Clicking
	a change in the Story list puts this in the message area:

	    Source Text:
	    the older wording

	and clicking a definition or one of its attributes now puts this in the band:

	    Source Resource:
	    8.503937007874015

	★A LABEL AND A VALUE, AND NOTHING ELSE. The name of the definition and the name of the attribute
	are both already on the row the reader just clicked, in the Kind and Definition columns; the one
	thing the list cannot show is the value the OLDER document had, because its column holds the
	newer one. So that is all this says.

	⚠**Values are verbatim** - no unit and no rounding. The export writes bare numbers and nothing
	  knows which attributes are lengths (IKCMResourcesFacade.h states the same contract).
	⚠**An attribute the Source does not have contributes no line.** There is no older value to
	  print. The list's own row says so instead, with a `+`.
	⚠**What does not fit is clipped**: the message area holds 4 lines on a Japanese UI (KCMUI.fr
	  carries the measurement next to the widget) - MORE than the retired band's 2. ★A click on an
	  ATTRIBUTE row is still the exact case: a label and one value.

	@param row        0 .. IKCMResourcesFacade::GetChangeCount()-1. Out of range clears the band.
	@param attrIndex  which attribute the reader clicked, or -1 for the definition row itself.
	                  ★A definition row has no single older value, so it shows **the label and
	                  nothing under it** (the user's call, 2026-09-09).
	@return kTrue when something was written. ★kFalse is not a failure worth reporting: the panel
	        may simply not be open, and every caller is a gesture the reader made in the list.
*/
bool16 KCMShowSelectedResource(int32 row, int32 attrIndex);

// KCMShortResourceValue (what a reader sees of one attribute value) moved to the shared header
// source/KCMResourceShortValue.h on 2026-09-13, when the PDF report (model side) needed the same
// rule. The callers here keep including this file and get it through this line.
#include "KCMResourceShortValue.h"

/** Empty the band.

	Called wherever the selection stops meaning anything - a mode switch, a rebuilt list, the
	panel's AutoAttach. ⚠**Those are the same places that rewrite the section heading**, so the one
	call sits inside KCMUpdateStorySectionLabel rather than being repeated at each of them: a
	second home for "the list just changed" is a second chance to forget one.

	Safe with the panel closed.
*/
void KCMClearResourceValue();

#endif // __KCMResourceValue_h__

// End, KCMResourceValue.h.
