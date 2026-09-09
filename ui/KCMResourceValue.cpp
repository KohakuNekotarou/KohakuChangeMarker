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
#include "IControlView.h"			// what KCMFindPanelWidget hands back
#include "IKCMStatusTextData.h"		// reading back WHOSE message is standing in the shared box

// ----- Project -----
#include "KCMUIID.h"				// kKCMStatusTextWidgetID
#include "Utils.h"					// Utils<IKCMResourcesFacade>()
#include "IKCMResourcesFacade.h"	// GetNthChange / GetNthAttrCount / GetNthAttr
#include "KCMUIShared.h"			// KCMFindPanelWidget
#include "KCMXmlPretty.h"			// KCMDecodePercentEscapes - `%3a` is a colon, not a mojibake
#include "KCMResourceValue.h"

namespace
{

/* The label this band writes -- SPELLED IN ONE PLACE, because two things need it.

   ★★IT IS ALSO HOW THE BAND RECOGNISES ITS OWN WRITING. Since the band moved into the message
     area it shares that box with the panel's ordinary status messages, and KCMClearResourceValue
     is called in ALL THREE MODES by design (KCMStorySection.cpp says why). Clearing without
     asking would wipe a message this file never wrote.
*/
const char* const kKCMResourceBandLabel = "Source Resource:";

/* WriteBand

   Put a label and its values into the panel's MESSAGE AREA -- the very box the Story mode writes
   "Source Text:" into (2026-09-09, the user's call: "show it in the same display area as the
   Story mode"). Until then this band had a widget of its own, and that widget is what the panel
   had grown 45px to hold.

   ★The message area takes six pieces (label, pre, mid, post, ruby, attrKind) and colours only
     pre/post faded; an ordinary status message is (empty, empty, s, empty, empty, 0). A label
     with a body is therefore (label, empty, body, empty, empty, 0) -- nothing faded here, and
     nothing with a reading over it.
   ★Both pieces are marked NOT translatable for the reason the old band was: these are measured
     values and a definition's name, and PMString hands anything translatable to the built-in
     table on its way to the screen. KCM has been bitten by exactly that once, when a plain
     "Source:" came out as a style-source phrase in a Japanese locale.
   ★KCMSetStatusSegments also stores the pieces on the model side, which is a second thing gained
     by moving: **app.kcmStatus now answers with this band's contents** ("heading + newline +
     body"), where the old widget could be read from nowhere outside the panel.
*/
void WriteBand(const PMString& label, const PMString& body)
{
	PMString finishedLabel(label);
	finishedLabel.SetTranslatable(kFalse);

	PMString finishedBody(body);
	finishedBody.SetTranslatable(kFalse);

	const PMString kNothing;
	KCMSetStatusSegments(finishedLabel, kNothing, finishedBody, kNothing, kNothing, 0);
}

/* Is the message standing in the box the one THIS file put there?

   ★Asked of the box rather than of the mode. The mode can be switched between the write and the
     clear, and what has to be protected is the TEXT, not the state that produced it
     (the same reasoning as check-a-place-not-a-state: read something nobody else can move).
   ⚠kFalse when the panel is closed, and that is right -- there is nothing on screen to clear.
*/
bool16 BandHoldsOurText()
{
	InterfacePtr<IKCMStatusTextData> data(
		KCMFindPanelWidget(kKCMStatusTextWidgetID), UseDefaultIID());
	if (data == nil)
		return kFalse;

	PMString label, pre, mid, post, ruby;
	int32 attrKind = 0;
	data->GetSegments(label, pre, mid, post, ruby, attrKind);

	// ★BOTH PIECES ARE ASKED, because this file writes the label into either one: as the HEADING
	//   when a value goes under it, and as the BODY when none does (KCMShowSelectedResource carries
	//   the reason - an empty body is drawn as a caret). Asking only about the heading would leave
	//   a label-only band standing after the list was rebuilt underneath it.
	return (label == kKCMResourceBandLabel) || (mid == kKCMResourceBandLabel);
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

	// No separator, or nothing after the last one: the value stands as it is (but still gets its
	// escapes read - see below). ⚠The second case is the guard that matters: shortening "a/" to ""
	// would replace a real value with a blank cell.
	PMString shortened(value);
	if (cut >= 0 && cut + 1 < value.CharCount())
		shortened.Remove(0, cut + 1);

	// ★★TRIMMED FIRST, DECODED SECOND, and the order is not arbitrary: the `/` this cut at is the
	//   exporter's own separator, written plain, while a `/` INSIDE a name arrives as `%2f`.
	//   Decoding first would manufacture a separator that the exporter deliberately escaped, and
	//   the cut would then land inside somebody's style name.
	// ★What this undoes: `スタイルグループ 1%3a段落スタイル 1` reads as `スタイルグループ 1:段落スタイル 1`
	//   (KCMXmlPretty.h carries the measurement and why it is not an encoding fault).
	shortened.SetUTF8String(KCMDecodePercentEscapes(shortened.GetUTF8String()));
	shortened.SetTranslatable(kFalse);
	return shortened;
}

//----------------------------------------------------------------------------------------
// KCMShowSelectedResource (declared in KCMResourceValue.h)
//----------------------------------------------------------------------------------------
bool16 KCMShowSelectedResource(int32 row, int32 attrIndex)
{
	// ⚠★★THE GUARD GOES ON THE Utils OBJECT, NOT ON WHAT IT HANDS BACK. `Utils<T>()->M()`
	//  dereferences before there is anything to test, so a facade that is not registered takes the
	//  panel down with it rather than returning nil ([[utils-boss-facade-access]]).
	// ★AND THIS FACADE HAS BEEN UNREGISTERED ONCE, in this very feature: 2026-09-09, KCMFactoryList.h
	//  missing its line (fdc3b39). Every other caller in KCM reads it this way already - KCMChangeNav,
	//  KCMCmykCursor, KCMPawTracker, KCMPawWordDialog, KCMStoryPressMarks all say so in their own
	//  comments - and these three calls were simply the ones that had not been brought into line.
	Utils<IKCMResourcesFacade> resources;
	if (!resources)
	{
		KCMClearResourceValue();
		return kFalse;
	}

	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	if (row < 0 || !resources->GetNthChange(row, kind, key, what))
	{
		// Out of range - the list was rebuilt under a selection, or this is the "No differences"
		// placeholder row. Clear rather than leave the previous definition's values standing:
		// a stale reading is worse than an empty band, because nothing about it looks stale.
		KCMClearResourceValue();
		return kFalse;
	}

	// ***** LINE 1: THE LABEL, AND IT IS NOW LITERALLY THE STORY BOX'S. ***** The Story mode writes
	// "Source Text:" over the older wording of the edit that was clicked (KCMStoryJump.cpp); this
	// says the same sentence about the other kind of thing, into THE SAME BOX, through the same
	// call (2026-09-09, the user: "show it in the same display area as the Story mode").
	// ★The label itself is spelled at the top of this file, not here, because the clear has to
	//   recognise its own writing (BandHoldsOurText).
	// ★NOT the definition's name. That is on the row the reader just clicked, in the two columns to
	//   the left of the value; repeating it here would spend one of the box's lines saying
	//   what the click already said (the user's call, 2026-09-09).
	PMString body;
	body.SetTranslatable(kFalse);

	// ***** LINE 2: THE OLDER VALUE, AND NOTHING ELSE. *****
	//
	// ★★★THE BAND IS THE SOURCE SIDE AND NOTHING ELSE. The list's own rows carry the TARGET's value
	//   and a sign, so the two are halves of one reading rather than the same reading twice - and
	//   this is the half that CANNOT BE READ ANYWHERE ELSE: the newer document is in front of the
	//   reader and its paragraph style can be opened and looked at. The older one cannot.
	//
	// ★★AN ATTRIBUTE ROW IS THE ONLY CASE, and that is why the rows were given children at all: one
	//   click names one attribute, so the band is a label and a single value.
	// ★A DEFINITION ROW names no single value, so it shows the label alone - the code below carries
	//   what printing them all cost.
	//
	// ⚠**AN ATTRIBUTE THE SOURCE DOES NOT HAVE CONTRIBUTES NO LINE.** There is no older value to
	//   print. The list says so instead, with the `+` on that attribute's own row.
	// ★★★ONLY AN ATTRIBUTE ROW PUTS A VALUE HERE (2026-09-09, the user's call: "when the parent
	//   is selected, just Source Resource: and nothing below it").
	//   ⚠It used to print one line per attribute that had an older value. **The names were not
	//     printed beside them** - the child rows below stand in the same order, and there was no
	//     room to say each thing twice - so a definition with several changed attributes put a
	//     column of bare values on screen with nothing tying each to its own attribute. One value
	//     under a label is a reading; a stack of them is a puzzle.
	//   ⇒ The band answers ONE question, "what was this attribute before?", and only an ATTRIBUTE
	//     row asks it. A definition row's own answer is the list of children underneath it.
	if (attrIndex >= 0)
	{
		const int32 attrCount = resources->GetNthAttrCount(row);
		PMString name, source, target;
		if (attrIndex < attrCount
			&& resources->GetNthAttr(row, attrIndex, name, source, target)
			&& !source.IsEmpty())
		{
			// ★Shortened the same way the row below it is, so the two never disagree about what the
			//   value "is" (KCMShortResourceValue carries the rule and the reason).
			body = KCMShortResourceValue(source);
			body.SetTranslatable(kFalse);
		}
	}

	// ★THE LABEL ALONE IS A REAL ANSWER, and it has several causes that all mean one thing to the
	//   reader: the definition is new, the difference is inside a child element this differ does not
	//   walk, the attribute clicked is one the Source never had, or a DEFINITION row was clicked -
	//   which shows the label and nothing else since the user's call above. "Source Resource:" with
	//   nothing under it says there is no older value, which is the truth in every one of them.
	//
	// ⚠★★★AND IT GOES IN AS THE BODY, NOT AS THE HEADING. **The message area draws an EMPTY middle
	//   piece as a vertical BAR** - a caret - because in the Story mode an empty older side means
	//   "the new words were inserted HERE" (kKCMCaretWidth in KCMStatusTextView.cpp, 2026-09-08).
	//   Measured 2026-09-10, the user: "when the parent is selected something is shown in the source
	//   part - a vertical bar". It reads as a value that failed to draw.
	//   ★A heading with nothing under it is not a heading at all, it is one sentence, so sending it
	//     as the body is what it actually IS as well as what the box needs. **The line looks
	//     identical either way**: heading and body are drawn at the same full text colour, and the
	//     heading occupies a line of its own exactly as a first body line would.
	//   ⚠**The caret itself is not the bug and is not touched.** It is right for the Story mode;
	//     what was wrong is this file handing the box a shape it reserves for something else.
	PMString label(kKCMResourceBandLabel);
	if (body.IsEmpty())
	{
		WriteBand(PMString(""), label);
		return kTrue;
	}

	WriteBand(label, body);
	return kTrue;
}

//----------------------------------------------------------------------------------------
// KCMClearResourceValue (declared in KCMResourceValue.h)
//----------------------------------------------------------------------------------------
void KCMClearResourceValue()
{
	// ★★★ONLY WHAT THIS FILE WROTE, and that test is new with the move. The band used to own a
	//   widget, so clearing it could not touch anything else; it now shares the message area with
	//   every status message the panel writes. ⚠**The caller clears in ALL THREE MODES on
	//   purpose** (KCMStorySection.cpp: "in Pixel and Story the band is empty anyway"), which was
	//   free before and would now wipe the Story mode's "Source Text:" and every ordinary message
	//   -- including the result of the comparison that has just finished.
	if (!BandHoldsOurText())
		return;

	WriteBand(PMString(""), PMString(""));
}

// End, KCMResourceValue.cpp.
