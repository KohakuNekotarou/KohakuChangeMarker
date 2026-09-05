//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What one CHANGE row's text cell draws: the context before the change, the changed characters
//  themselves, the context after - and, since 2026-08-22, the READING that stands above them when
//  the change is a ruby.
//
//  ★WHY THE CELL NEEDS AN INTERFACE OF ITS OWN. A stock static text holds ONE string and draws it
//  in ONE colour (ITextControlData, plus the four colours its .fr names). A change row has to draw
//  the changed characters at the theme's full text colour and fade the words around them, so the
//  cell is drawn by hand - and a hand-drawn cell has to be told what to draw. This is that channel:
//  KCMStoryTreeWidgetMgr writes it on every apply, KCMStoryCellView reads it in Draw.
//
//  ★NOT PERSISTENT, AND IT MUST NOT BECOME SO. The list is built by one comparison and thrown away
//  by the next, and row widgets are recycled as the list scrolls, so nothing here is meant to
//  outlive the panel - which is also why every apply writes all three pieces rather than only the
//  ones it has (see the widget manager).
//
//  ★The same shape as KBS's IKBSRowData (declared in KBSColorTextView.h), which feeds the same
//  kind of hand-drawn cell: its hit rows draw the matched text at full strength and fade the line
//  around it. **The two do not carry the same pieces** -- KBS's row also has a page locator and
//  an accent-coloured flag word, and this one has a reading instead.
//  ⚠**Do not write how many strings either of them has here.** It said "three rather than five"
//    until ruby was added and made it four, and the same one addition put the same kind of stale
//    count into IKCMStatusTextData.h and KCMStatusTextView.cpp. **The parameter list below is the
//    answer.**
//
//========================================================================================

#ifndef __IKCMStoryCellData_h__
#define __IKCMStoryCellData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMUIID.h"		// IID_IKCMSTORYCELLDATA

/** Holds what one change row's text cell draws, split where the colour changes. */
class IKCMStoryCellData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMSTORYCELLDATA };

	/** Replace everything the cell draws.

		★ALL OF IT, ALWAYS. The cell's widget is recycled, so a piece left alone keeps whatever the
		row it used to be had in it. That is also why this is ONE call rather than one per piece:
		two setters could be half-called, and the half that was not called is exactly the stale
		half ([[one-question-one-place]]). There is a single caller - the widget manager's apply.

		@param pre the words before the change. Carries the leading ellipsis when the excerpt was
			cut there - an ellipsis stands for words that were cut away, and those are context.
		@param mid the changed characters - what is drawn at the theme's full text colour. Empty
			when the side being shown has nothing there.
		@param post the words after it, with the trailing ellipsis on the same terms as pre.
		@param ruby the READING that goes above mid, for a ruby change (2026-08-22). Empty for an
			ordinary text change - and also for a ruby that was REMOVED, which is the whole reason
			the next parameter exists.
		@param twoLines draw this cell on two lines, the reading above the characters it belongs
			to. ★NOT "ruby is not empty": a ruby that was taken away leaves nothing to put on the
			upper line, and the row still has to be laid out on two lines or the base text would
			jump up half a row against its neighbours (user's call, 2026-08-22: the upper line
			stays empty and the old reading is read in the message area). The row's HEIGHT is
			decided from the same fact, in KCMStoryTreeWidgetMgr.
	*/
	/** @param attrKind WHICH attribute the upper line belongs to - KCMStoryAttrKind as a plain
		 int32, the same way it crosses the model/UI boundary.

		★★★IT IS NOT ENOUGH TO KNOW THAT THERE IS AN UPPER LINE. Ruby's value is a READING and is
		written out as it stands; kenten's is a KIND ("Bullseye") and is drawn as a MARK. Handing
		the cell only the string and a two-line flag is exactly the state this panel was in when
		kenten was withdrawn in August: the name of a mark was painted over the base text as though
		somebody could read it aloud. **The cell has to be told which it is looking at**, and
		IKCMStoryEditsFacade.h says the same thing about every other place that asks. */
	virtual void SetSegments(const PMString& pre, const PMString& mid, const PMString& post,
							 const PMString& ruby, bool16 twoLines, int32 attrKind) = 0;

	/** Read back what was written. Answers empty strings, kFalse and 0 before the first apply. */
	virtual void GetSegments(PMString& outPre, PMString& outMid, PMString& outPost,
							 PMString& outRuby, bool16& outTwoLines, int32& outAttrKind) const = 0;
};

#endif // __IKCMStoryCellData_h__

// End, IKCMStoryCellData.h.
