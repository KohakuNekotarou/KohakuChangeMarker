//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KESCM)
//
//  The three pieces of text one CHANGE row draws: the context before the change, the changed
//  characters themselves, and the context after.
//
//  ★WHY THE CELL NEEDS AN INTERFACE OF ITS OWN. A stock static text holds ONE string and draws it
//  in ONE colour (ITextControlData, plus the four colours its .fr names). A change row has to draw
//  the changed characters at the theme's full text colour and fade the words around them, so the
//  cell is drawn by hand - and a hand-drawn cell has to be told what to draw. This is that channel:
//  KESCMStoryTreeWidgetMgr writes it on every apply, KESCMStoryCellView reads it in Draw.
//
//  ★NOT PERSISTENT, AND IT MUST NOT BECOME SO. The list is built by one comparison and thrown away
//  by the next, and row widgets are recycled as the list scrolls, so nothing here is meant to
//  outlive the panel - which is also why every apply writes all three pieces rather than only the
//  ones it has (see the widget manager).
//
//  ★The same shape as KBS's IKBSRowData, which feeds the same kind of hand-drawn cell (its hit
//  rows draw the matched text at full strength and fade the line around it). Three strings rather
//  than five: KBS's row also carries a page locator and a flag word, and this row has neither.
//
//========================================================================================

#ifndef __IKESCMStoryCellData_h__
#define __IKESCMStoryCellData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMUIID.h"		// IID_IKESCMSTORYCELLDATA

/** Holds what one change row's text cell draws, split where the colour changes. */
class IKESCMStoryCellData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMSTORYCELLDATA };

	/** Replace all three pieces.

		★ALL THREE, ALWAYS. The cell's widget is recycled, so a piece left alone keeps whatever the
		row it used to be had in it.

		@param pre the words before the change. Carries the leading ellipsis when the excerpt was
			cut there - an ellipsis stands for words that were cut away, and those are context.
		@param mid the changed characters - what is drawn at the theme's full text colour. Empty
			when the side being shown has nothing there.
		@param post the words after it, with the trailing ellipsis on the same terms as pre.
	*/
	virtual void SetSegments(const PMString& pre, const PMString& mid, const PMString& post) = 0;

	/** Read back what was written. Answers empty strings before the first apply. */
	virtual void GetSegments(PMString& outPre, PMString& outMid, PMString& outPost) const = 0;
};

#endif // __IKESCMStoryCellData_h__

// End, IKESCMStoryCellData.h.
