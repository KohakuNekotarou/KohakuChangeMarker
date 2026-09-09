//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMResourceValue.h for what this band is and why it is in the upper pane.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// ----- Interfaces -----
#include "IControlView.h"
#include "ITextControlData.h"		// the band is a stock StaticMultiLineTextWidget

// ----- Project -----
#include "KCMUIID.h"				// kKCMResourceValueWidgetID
#include "Utils.h"					// Utils<IKCMResourcesFacade>()
#include "IKCMResourcesFacade.h"	// GetNthChange / GetNthAttrCount / GetNthAttr
#include "KCMUIShared.h"			// KCMFindPanelWidget
#include "KCMResourceValue.h"

namespace
{

/* WriteBand

   Put a finished string into the band, or clear it.

   ★The string is marked NOT translatable. It is a definition's name and a pair of measured
     values, and PMString hands anything translatable to the built-in table on its way to the
     screen: KCM has been bitten by exactly that once already, when a plain "Source:" came out as
     a style-source phrase in a Japanese locale. A style really can be called "Normal".

   ★SetString's second argument is invalidate (kTrue - this is the whole point of the call) and
     the third is notifyOfChange (kFalse - nothing observes this widget, and telling the world
     about a redraw would only invite one).
*/
void WriteBand(const PMString& text)
{
	InterfacePtr<ITextControlData> band(
		KCMFindPanelWidget(kKCMResourceValueWidgetID), UseDefaultIID());
	if (band == nil)
		return;		// the panel is closed, or is being torn down. Nothing to write to, and nothing wrong

	PMString finished(text);
	finished.SetTranslatable(kFalse);
	band->SetString(finished, kTrue, kFalse);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMShortResourceValue (declared in KCMResourceValue.h)
//----------------------------------------------------------------------------------------
PMString KCMShortResourceValue(const PMString& value)
{
	// ★PMString's own search, not a loop over bytes: it counts CHARACTERS and is multibyte-safe,
	//   which matters because a definition can be called "見出し/大". The product uses it the same
	//   way to cut a suffix (MediaLocation.h:84, AnimationUIManagePresetsDialogObserver.cpp:308).
	const CharCounter cut = value.LastIndexOfCharacter('/');

	// No separator, or nothing after the last one: the value stands as it is. ⚠The second case is
	// the guard that matters - shortening "a/" to "" would replace a real value with a blank cell.
	if (cut < 0 || cut + 1 >= value.CharCount())
		return value;

	PMString shortened(value);
	shortened.Remove(0, cut + 1);
	shortened.SetTranslatable(kFalse);
	return shortened;
}

//----------------------------------------------------------------------------------------
// KCMShowSelectedResource (declared in KCMResourceValue.h)
//----------------------------------------------------------------------------------------
bool16 KCMShowSelectedResource(int32 row, int32 attrIndex)
{
	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	if (row < 0 || !Utils<IKCMResourcesFacade>()->GetNthChange(row, kind, key, what))
	{
		// Out of range - the list was rebuilt under a selection, or this is the "No differences"
		// placeholder row. Clear rather than leave the previous definition's values standing:
		// a stale reading is worse than an empty band, because nothing about it looks stale.
		KCMClearResourceValue();
		return kFalse;
	}

	// ***** LINE 1: THE LABEL, AND IT IS THE STORY BOX'S. ***** The Story mode writes "Source Text:"
	// over the older wording of the edit that was clicked (KCMStoryJump.cpp); this is the same
	// sentence about the other kind of thing, so it is written the same way - a plain English
	// literal, marked NOT translatable so that the built-in table cannot swap it for something else.
	// ★NOT the definition's name. That is on the row the reader just clicked, in the two columns to
	//   the left of the value; repeating it here would spend one of the band's two lines saying
	//   what the click already said (the user's call, 2026-09-09).
	PMString text("Source Resource:");
	text.SetTranslatable(kFalse);

	// ***** LINE 2: THE OLDER VALUE, AND NOTHING ELSE. *****
	//
	// ★★★THE BAND IS THE SOURCE SIDE AND NOTHING ELSE. The list's own rows carry the TARGET's value
	//   and a sign, so the two are halves of one reading rather than the same reading twice - and
	//   this is the half that CANNOT BE READ ANYWHERE ELSE: the newer document is in front of the
	//   reader and its paragraph style can be opened and looked at. The older one cannot.
	//
	// ★★AN ATTRIBUTE ROW IS THE EXACT CASE, and that is why the rows were given children at all: one
	//   click names one attribute, so the band is a label and a single value, which is exactly the
	//   two lines it holds.
	// ★A DEFINITION ROW names no single value, so it prints one line per attribute that HAS an older
	//   one, in the order its child rows stand in. The names are not repeated - the rows below are
	//   in the same order, and the band has no room to say each thing twice.
	//
	// ⚠**AN ATTRIBUTE THE SOURCE DOES NOT HAVE CONTRIBUTES NO LINE.** There is no older value to
	//   print. The list says so instead, with the `+` on that attribute's own row.
	const int32 attrCount = Utils<IKCMResourcesFacade>()->GetNthAttrCount(row);
	const int32 first = (attrIndex >= 0) ? attrIndex : 0;
	const int32 last = (attrIndex >= 0) ? attrIndex : attrCount - 1;

	for (int32 i = first; i <= last && i < attrCount; ++i)
	{
		PMString name, source, target;
		if (!Utils<IKCMResourcesFacade>()->GetNthAttr(row, i, name, source, target))
			break;		// the count and the rows disagree; stop rather than print a blank line

		if (source.IsEmpty())
			continue;

		// ★Shortened the same way the row below it is, so the two never disagree about what the
		//   value "is" (KCMShortResourceValue carries the rule and the reason).
		PMString shown = KCMShortResourceValue(source);
		shown.SetTranslatable(kFalse);
		text.Append("\n");
		text.Append(shown);
	}

	// ★THE LABEL ALONE IS A REAL ANSWER, and it has several causes that all mean one thing to the
	//   reader: the definition is new, the difference is inside a child element this differ does not
	//   walk, or the attribute clicked is one the Source never had. "Source Resource:" with nothing
	//   under it says there is no older value, which is the truth in every one of them.

	WriteBand(text);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMClearResourceValue (declared in KCMResourceValue.h)
//----------------------------------------------------------------------------------------
void KCMClearResourceValue()
{
	WriteBand(PMString(""));
}

// End, KCMResourceValue.cpp.
