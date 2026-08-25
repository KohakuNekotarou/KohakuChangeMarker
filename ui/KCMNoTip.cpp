//========================================================================================
//
//  KCMNoTip.cpp
//
//  An ITip that says nothing. It is put on a boss to make a tooltip **not appear**.
//
//  ***** WHY REMOVING SOMETHING NEEDS AN IMPLEMENTATION *****
//
//  A static text widget brings a tooltip of its own ---- the stock kStaticTextWidgetBoss carries
//  IID_ITIP (kTextWidgetTipImpl), which pops the whole string up whenever a cell has shortened
//  it (confirmed in a boss dump from the running application). Helpful on a dialog label, in the
//  way on a row of the Story Edits list, where merely crossing the list with the pointer brings
//  boxes of text up (user's report).
//
//  ★**There is no way to REMOVE an interface from an inherited boss**, so removing the behaviour
//  means answering differently. ITip.h:37-41 states the contract: "To have no tip, return
//  PMString()". A boss names this implementation on its IID_ITIP and gets silence.
//
//  ★The base is AbstractTip for the same reason as in KCMIconTip.cpp (the base every tooltip in
//  the product code derives from, which supplies UpdateToolTipOnMouseMove and SetTipText). The
//  whole account is at the top of that file.
//
//  ★**Nothing here is particular to that one list.** Any widget of KCM’s that inherited a tooltip
//  it does not want can name this implementation, and **two do today**: the cell of a Story Edits
//  row and the cell of a book comparison row (KCMUI.fr names kKCMNoTipImpl twice).
//  ⚠An older note here named the Story row alone, while the paragraph beneath it already said
//    "any widget can" ＝ **one half of this file knew and the other did not**.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "AbstractTip.h"		// the base every product tooltip derives from

// General includes:
#include "PMString.h"

// Project includes:
#include "KCMUIID.h"

/** An ITip that always answers empty. An empty string IS "no tooltip", by the interface’s own
    definition. */
class KCMNoTip : public AbstractTip
{
public:
	KCMNoTip(IPMUnknown* boss);
	virtual ~KCMNoTip();

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KCMNoTip, kKCMNoTipImpl)

KCMNoTip::KCMNoTip(IPMUnknown* boss) : AbstractTip(boss)
{
}

KCMNoTip::~KCMNoTip()
{
}

PMString KCMNoTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	return PMString();
}

// End, KCMNoTip.cpp.
