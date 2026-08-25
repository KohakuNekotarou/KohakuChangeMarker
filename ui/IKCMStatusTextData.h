//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What the panel's MESSAGE AREA draws: an optional heading line, and a body split into the three
//  pieces where its colour changes.
//
//  ★WHY FOUR PIECES AND NOT ONE STRING. Every message the panel raises used to be one string in a
//  stock multi-line static text, which draws its whole string in ONE colour. When a change row is
//  clicked, this box shows the OTHER side of that edit (KCMStoryJump.cpp), and the reader's
//  question there is "which characters are the ones that differ" - a question one colour cannot
//  answer. So the box is drawn by hand, and a hand-drawn box has to be told where the colour
//  changes: fLabel / fPre / fMid / fPost is that channel (2026-08-20).
//
//  ★AN ORDINARY MESSAGE IS THE SAME SHAPE, NOT A SPECIAL CASE: the other three pieces are empty and
//  fMid carries the whole sentence, which comes out as one run at the theme's text colour - exactly
//  what the stock widget drew. That is why the 72 places that call KCMSetStatus needed no change.
//
//  ★THE HEADING IS ITS OWN FIELD RATHER THAN THE HEAD OF fPre, and the reason is the overflow rule:
//  when the text does not fit, the CONTEXT gives way from its outer ends - so a heading living at
//  the head of fPre would be the first thing cut away. It is the one piece that must survive.
//
//  ★NOT PERSISTENT, AND IT MUST NOT BECOME SO. This is the last message raised in this session;
//  what has to outlive the panel is remembered on the MODEL side instead (KCMModelNotify.cpp),
//  which is also where app.kcmStatus answers from.
//
//  ★Same shape and the same reasoning as IKCMStoryCellData, which feeds the change row's cell.
//  Four strings rather than three: a row has no room for a heading and no need of one.
//
//========================================================================================

#ifndef __IKCMStatusTextData_h__
#define __IKCMStatusTextData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMUIID.h"		// IID_IKCMSTATUSTEXTDATA

/** Holds what the panel's message area draws, split where the colour changes. */
class IKCMStatusTextData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMSTATUSTEXTDATA };

	/** Replace all four pieces.

		★ALL FOUR, ALWAYS. There is one message area and one message in it; writing only the pieces
		a caller happens to have would leave the rest of the previous message standing beside it.

		@param label a heading on a line of its own - "Source Text:" / "Target Text:" when the box is
			showing the other side of an edit. Empty for an ordinary message, and then it costs no
			line. Drawn at the full text colour: only pre/post are faded (user's call, 2026-08-21).
		@param pre the words before the changed characters - faded. Empty for an ordinary message.
		@param mid for an ordinary message, the whole message; for the other side of an edit, the
			characters that differ - drawn at the theme's full text colour.
		@param post the words after them, on the same terms as pre. Empty for an ordinary message.
		@param ruby the READING that belongs over mid, drawn on a line of its own above it
			(2026-08-22). Empty for an ordinary message and for an edit that has no ruby in it.

			★THIS IS THE OTHER SIDE'S READING, and that is what makes it worth drawing at all: the
			row in the list shows the NEWER version, so a reading that was REMOVED appears nowhere
			else. The decision that the row's own upper line stays empty for those (user, 2026-08-22)
			only holds because this box shows what went.

			★NO "twoLines" FLAG HERE, UNLIKE THE ROW'S CELL. The cell needs one because its row has
			a fixed height that must match its neighbours' even when there is no reading to draw.
			This box has no neighbours to line up with, so an empty reading simply costs no line.
	*/
	virtual void SetSegments(const PMString& label, const PMString& pre,
							 const PMString& mid, const PMString& post,
							 const PMString& ruby) = 0;

	/** Read back what was written. Answers empty strings before the first message. */
	virtual void GetSegments(PMString& outLabel, PMString& outPre,
							 PMString& outMid, PMString& outPost, PMString& outRuby) const = 0;
};

#endif // __IKCMStatusTextData_h__

// End, IKCMStatusTextData.h.
