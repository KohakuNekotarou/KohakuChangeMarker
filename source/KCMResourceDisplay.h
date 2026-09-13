//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  What a Resources attribute's value READS AS - the one place that turns the export's own text
//  into what the panel, the band and the PDF report all show.
//
//  ★★WHY IT IS ONE FUNCTION (2026-09-13). Three call sites were each doing the same two steps in
//  the same order - shorten, then add the document's unit - and a third step was about to be
//  added to all three. **A value that reads one way in the panel and another in the report is a
//  bug nobody would think to look for**, and the way to make that impossible is to have one
//  function rather than an agreement between three ([[one-question-one-place]]).
//
//  ★SHARED BY BOTH PLUG-INS AS INLINE FUNCTIONS, like the two headers it calls, and for the same
//  reason: the model builds the report and the UI draws the panel.
//
//========================================================================================
#ifndef __KCMResourceDisplay_h__
#define __KCMResourceDisplay_h__

#include <cstdlib>
#include <string>

#include "IShortcutUtils.h"			// GetShortcutString - InDesign's own spelling of a shortcut
#include "KBSCModifierDefs.h"		// kControl / kShift / kOption - what the first number IS
#include "PMString.h"
#include "Utils.h"
#include "VirtualKey.h"

#include "KCMResourceShortValue.h"	// KCMShortResourceValue
#include "KCMResourceUnits.h"		// KCMResourceValueWithUnit

/** A keyboard-shortcut attribute as INDESIGN spells it: "Shift+Alt+Z" for the export's "272 90 1".

	★★★THE THREE NUMBERS WERE MEASURED, NOT GUESSED (2026-09-13, on shortcuts the user typed into
	a character style and read back out of this very list):

	    Shift+Alt+Z   ->  272 90 1
	    Shift+F1      ->   16  7 0

	  * **The first is the modifier mask of KBSCModifierDefs.h** - kControl 0x1, kShift 0x10 (16),
	    kOption 0x100 (256), kMacControl 0x1000. Shift alone is 16 and Shift+Alt is 16+256=272,
	    which is what came back. It is NOT the ICursorMgr scheme (Shift there is 0x02), and reading
	    it as that one is the mistake this note exists to prevent.
	  * **The third says WHICH KIND OF KEY the second is**, which is exactly the two constructors
	    VirtualKey offers: 1 = a character (Z = 90), 0 = a drover key code (F1 = 7). A virtual key
	    code would have made F1 112; it is 7, so the second number is not one.
	  ⚠**The order was read wrong twice before it was read right.** The first reading took the
	    LAST number for the modifiers because it happened to be 1 where Ctrl is 1; the second took
	    the first number for a single modifier. Both survived one sample and died on the next. ⇒
	    **one sample cannot tell a coincidence from a rule.**

	★★INDESIGN DOES THE SPELLING, NOT THIS. Utils<IShortcutUtils>()->GetShortcutString takes the
	  very pair VirtualKey + modifiers and returns the shortcut as the application writes it
	  everywhere else - so the words match the Keyboard Shortcuts editor, in whatever language the
	  interface is in. A table of our own would have to be right about every key on every keyboard
	  in every locale.

	@param attrName the IDML attribute's name. Only "...KeyboardShortcut" is looked at.
	@param value the export's text.
	@param out [out] the shortcut, when this returns kTrue.
	@return kFalse when this is not a shortcut, or not one this can read - the caller then shows
		the value as it stands rather than a guess.
*/
inline bool16 KCMResourceShortcutText(const PMString& attrName, const PMString& value, PMString& out)
{
	// ⚠BY NAME AS WELL AS BY SHAPE. Three whitespace-separated integers is not a rare shape - it
	//   could as easily be three of something else - so the name has to agree before the numbers
	//   are read as a shortcut.
	if (!KCMNameEndsWith(attrName.GetUTF8String(), "KeyboardShortcut"))
		return kFalse;

	// Exactly three integers, the whole string consumed.
	const std::string text = value.GetUTF8String();
	long parts[3] = { 0, 0, 0 };
	const char* at = text.c_str();
	for (int i = 0; i < 3; ++i)
	{
		char* end = nil;
		parts[i] = std::strtol(at, &end, 10);
		if (end == at)
			return kFalse;		// not a number where one was wanted
		at = end;
		while (*at == ' ' || *at == '\t')
			++at;
	}
	if (*at != '\0')
		return kFalse;			// a fourth thing: this is not the shape measured

	// ★"0 0 0" IS A REAL ANSWER - no shortcut - and it turns up whenever a definition exists on one
	//   side only, because an Added or Removed row lists every attribute it has. Said in words
	//   rather than left as three zeroes.
	if (parts[0] == 0 && parts[1] == 0 && parts[2] == 0)
	{
		out = PMString("(none)");
		out.SetTranslatable(kFalse);
		return kTrue;
	}

	const VirtualKey key = (parts[2] != 0)
		? VirtualKey(static_cast<SysChar>(parts[1]))
		: VirtualKey(static_cast<uint32>(parts[1]));

	// ⚠THE GUARD GOES ON THE Utils OBJECT, not on what it hands back ([[utils-boss-facade-access]]).
	Utils<IShortcutUtils> shortcuts;
	if (!shortcuts)
		return kFalse;
	PMString spelled = shortcuts->GetShortcutString(key, static_cast<int16>(parts[0]));
	if (spelled.IsEmpty())
		return kFalse;			// it could not name this one; the numbers are better than nothing

	spelled.SetTranslatable(kFalse);
	out = spelled;
	return kTrue;
}

/** What the reader sees for one Resources attribute value.

	A shortcut becomes InDesign's own spelling of it; a length becomes the document's own unit;
	everything else is shortened the way the list has always shortened it.

	@param db the Target's database - whose units are used. nil leaves a length in points.
*/
inline PMString KCMResourceDisplayValue(const PMString& attrName, const PMString& value, IDataBase* db)
{
	PMString shortcut;
	if (KCMResourceShortcutText(attrName, value, shortcut))
		return shortcut;
	return KCMResourceValueWithUnit(attrName, KCMShortResourceValue(value), db);
}

#endif // __KCMResourceDisplay_h__

// End, KCMResourceDisplay.h.
