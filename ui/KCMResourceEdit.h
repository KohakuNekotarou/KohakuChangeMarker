//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Double-click on a Resources row: select what it names in the panel that edits it, and open
//  that editor (2026-09-13, the user's request: "selecting a result row should select the part of
//  the panel that edits that change - first, character styles: the parent row opens that style's
//  edit screen").
//
//  ★WHAT IT IS FOR. The list says WHICH definition differs and the band says what the older value
//  was. The next thing a reader does with that is go and CHANGE the definition - and until now
//  that meant leaving the panel, finding the style by name in its own panel, and opening its
//  options. This does the finding.
//
//  ***** THE ROUTE, AND THE THREE THAT WERE MEASURED AND REJECTED (2026-09-13). *****
//
//  What it does: bring the Target to the front, show the Character Styles panel, ask its tree for
//  the row by name, select that row WITHOUT broadcasting the change, and fire the product's own
//  "Style Options..." action - which reads that selection and opens its own complete dialog.
//  Every call is a public SDK interface and nothing of another plug-in's private state is touched.
//
//  ⚠★★★notifyOfChange = kFalse IS THE FEATURE, NOT A DETAIL. An ordinary click on a style row sets
//  the DOCUMENT'S default character style - measured, [なし] became KCMEditProbe - because the
//  panel's observer applies whatever the selection broadcasts. A comparison panel that changes the
//  document it is comparing would put a difference into its own list that the reader never made.
//  The hilite is the selection; the notification is what applies.
//
//  The three that were tried and measured to the end, so that none of them is tried again:
//    1. ★**Build the dialog ourselves.** The resource DOES hand back the product's very panel
//       (kTextSelectableCharDialogBoss, widget kStyleCharParentWidgetID) with the right service id,
//       and SetDialogServiceID fills in its three widget ids - but its sixteen pages never arrive.
//       They are driven by an ITextTargetServer the product starts (seen in the Debug build's Spy
//       trace: kMsgCreateTargetImpl / kMsgTargetToWidgetImpl, once per page), which nothing public
//       reaches. **A dialog opened in that state shows "[None]" with an empty list and ends the
//       process when it is cancelled** - CSelectableDialogSwitcher::QueryGroupPanelControlData,
//       which does not answer nil for a switcher that was never set up, it reads a null.
//    2. **Write the dialog's data onto the service boss the registry hands out.** Measured: the
//       product never touches that instance - it was all zeroes before, during and after its own
//       dialog was up. The instance that carries the data is the DIALOG'S OWN PANEL, a second
//       instance of the same boss class.
//    3. **Fire the right-click twin, kContextMenuStyleOptionsActionID.** It works beautifully by
//       hand and changes nothing - it reads the row the menu was popped over rather than the
//       selection - but the styles panel stashes that row privately, so nothing outside it can say
//       which row the action should be about.
//
//  ★ONLY CHARACTER STYLES SO FAR (the user's staging). Every other kind of row says so on the
//  status line rather than doing nothing.
//
//========================================================================================

#ifndef __KCMResourceEdit_h__
#define __KCMResourceEdit_h__

// bool16 and int32 arrive through VCPlugInHeaders.h, which every .cpp of this plug-in includes
// first (KCMResourceValue.h and KCMResourceXml.h are written the same way).

/** Select the definition row `row` names in the panel that edits it, and open that editor.

	What happens, in order:
	  1. the row is read back from IKCMResourcesFacade (a rebuilt list or the "No differences"
	     placeholder answers nothing, silently - the same rule as KCMShowSelectedResource);
	  2. a row that is not a CharacterStyle, or one that exists only in the Source, is refused on
	     the status line - there is nothing in the Target to edit;
	  3. the Target is brought to the front, because the Character Styles panel shows the ACTIVE
	     document's styles and the action edits the style selected in it;
	  4. the panel is shown if it was not (never by firing its menu item, which is a toggle);
	  5. its tree is asked for the row BY NAME - the only way in, since the nodes are a class of
	     the styles panel's own and nothing outside can build one;
	  6. the row is scrolled into view (which opens the groups above it), checked to be the right
	     one (the search is a prefix match), selected without notifying, and the product's
	     "Style Options..." action is fired.

	★★AN ATTRIBUTE ROW OPENS THE DIALOG AT THE PAGE THAT HOLDS IT (2026-09-13, the user's request:
	"after the panel opens, the place in the list on the left that holds that Kind should be
	selected - AppliedFont first, second from the top, Basic Character Formats"). The dialog is the
	same one either way; the child row only says which of its sixteen pages to show.
	⚠**The page is chosen by POSITION**, from a table this file carries, because the pages come from
	  twelve different plug-ins and their widget ids are not published. One line per attribute, and
	  an attribute the table does not know simply opens the dialog at its own first page.

	@param row  0 .. IKCMResourcesFacade::GetChangeCount()-1, the DEFINITION row.
	@param attrIndex  which of that definition's differing attributes was double-clicked, or -1 for
	            the definition row itself. Only decides the page the dialog opens at.
	@return kTrue when the action was fired. kFalse for every refusal, each of which has already
	        been written to the status line - except the silent ones listed under 1.
*/
bool16 KCMEditSelectedResource(int32 row, int32 attrIndex = -1);

//----------------------------------------------------------------------------------------
// The two "Edit..." items on the row menus
//----------------------------------------------------------------------------------------
//
// ★★★IT IS A RIGHT-CLICK ITEM AND NOT A DOUBLE CLICK (2026-09-13, the user's call, reversing the
// morning's design). A double click is silent about what it is going to do; a menu says "Edit...",
// and the reader chooses it. ⇒ **The double click on a Resources row does nothing again.**
//
// ★TWO ITEMS, ONE PER MENU, BECAUSE THERE ARE TWO STASHES. The right click on a definition row
// records the row (KCMStorySetMenuRow) and pops kKCMStoryRowMenuName; the right click on an
// attribute row records the row AND the attribute (KCMStorySetMenuChange) and pops
// kKCMChangeRowMenuName. Each item reads its own stash, which is what stops a stale value in one
// from pointing the other at the wrong thing - the rule the existing items on those menus already
// follow.
//
// Both sit at the TOP of their menu (the user's call).

/** Whether "Edit..." may be offered for the DEFINITION row the menu was popped over. */
bool16 KCMResourceRowCanEdit();

/** Open the editor for that definition. */
void KCMEditMenuResourceRow();

/** Whether "Edit..." may be offered for the ATTRIBUTE row the menu was popped over. */
bool16 KCMResourceAttrCanEdit();

/** Open the editor for that definition, at the page that holds that attribute. */
void KCMEditMenuResourceAttr();

#endif // __KCMResourceEdit_h__

// End, KCMResourceEdit.h.
