//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Show as XML" on a definition row's right-click menu (2026-09-09, the user's request: "I want
//  a Show-as-XML menu on the parent by right click; if the parent is a paragraph style, show that
//  part's XML").
//
//  ★WHAT IT IS FOR. The list says WHICH attributes differ and the band says what the older value
//  was; neither shows the definition itself. A person reading an unfamiliar difference wants the
//  surrounding XML - the element, its Properties block, the child elements the differ flattened
//  into rows - and that text already exists: the comparison keeps both sides' bodies.
//
//  ★THE TEXT IS SHOWN IN A MODAL ALERT, the way "How to Use" is (the user's call: "the display the
//  same as HowTo"). No dialog resource, no widgets, no new boss - CAlert::ModalAlert takes the
//  string and InDesign lays it out.
//
//========================================================================================

#ifndef __KCMResourceXml_h__
#define __KCMResourceXml_h__

// bool16 arrives through VCPlugInHeaders.h, which every .cpp of this plug-in includes first.

/** Whether "Show as XML" may be offered for the row the menu was popped over.

	kFalse in every case where the item would show nothing or would show the wrong thing:
	  - the panel is not in the RESOURCES mode (the other modes' rows are stories, and their text
	    is what the list itself already shows);
	  - no comparison is armed, so there are no bodies to show;
	  - no row was stashed, or the list has been rebuilt shorter since the right click.

	⚠It shares a menu with "Refresh Story Comparison", and the two are enabled in opposite modes -
	so each mode's menu has exactly one live item and InDesign hides the greyed one.
*/
bool16 KCMResourceRowHasXml();

/** Put the stashed definition's XML on screen: the Source above, the Target below.

	★THE ORDER IS THE PANEL'S ORDER (the user's call: "source on top, target below"). It is the
	same order the Target:/Source: lines are NOT in - those read newer-first - and it is the right
	one here, because reading a difference means reading the older text and then what became of it.

	⚠**An Added definition has no Source and a Removed one has no Target.** The missing half is
	  named rather than left out, so that "there was nothing here" cannot be misread as "the two
	  sides are the same".
	⚠**An enormous body is cut**, with a line saying so - a backstop at 20,000 characters a side.
	  ⚠The first version cut at 4,000 "because an alert has no scroll bar", which was never
	    measured and is false: How to Use is the same call and scrolls about 6,000 characters.
	  ★The whole text is always available to a script through app.kcmResourceDiff.

	Reports on the panel's message line and shows nothing when there is nothing to show.
*/
void KCMShowResourceXml();

#endif // __KCMResourceXml_h__

// End, KCMResourceXml.h.
