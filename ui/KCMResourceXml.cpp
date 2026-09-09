//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMResourceXml.h for what this shows and why it is an alert rather than a dialog.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// ----- Includes -----
#include "CAlert.h"					// ModalAlert - the same way How to Use is shown

// ----- Project -----
#include "KCMUIID.h"
#include "Utils.h"					// Utils<IKCMResourcesFacade>() / Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"		// IsArmed
#include "IKCMResourcesFacade.h"	// GetNthChange / GetNthValues
#include "KCMUIShared.h"			// KCMSetStatus
#include "KCMStoryTree.h"			// KCMListShowsResources - which list the stashed row belongs to
#include "KCMStoryRefresh.h"		// KCMStoryMenuRow - the row the right click was over
#include "KCMXmlPretty.h"			// KCMPrettyXml - one element per line, indented by depth
#include "KCMResourceXml.h"

namespace
{

/** How much of one side is shown.

	⚠★★**THE FIRST VERSION OF THIS SAID "an alert has NO SCROLL BAR" AND CUT AT 4000. THAT WAS
	  WRONG, AND IT WAS NEVER MEASURED** (the user caught it: "the right-click one - isn't it the
	  same as About This Plug-in? How to Use can scroll"). **How to Use is the same call** -
	  CAlert::ModalAlert - and its text is 2,841 + 3,248 = **about 6,000 characters**, which does
	  scroll. So the cut was not protecting against anything; it was making this window look like
	  the short About box instead of the long reference it was meant to resemble.

	★What is kept is a BACKSTOP, not a policy: 6,000 characters is measured to work, and a body an
	order of magnitude past that is unmeasured territory rather than something known to be safe.
	⚠It is per SIDE, not per alert: a Changed definition shows two of these.
	★When it does bite, it says so, and the whole text is always in app.kcmResourceDiff.
*/
const int32 kKCMXmlSideLimit = 20000;

/** Appends one side under its own heading, laid out, and cut if it is enormous. */
void AppendSide(PMString& out, const char* heading, const PMString& body)
{
	out.Append(heading);
	out.Append("\n");

	if (body.IsEmpty())
	{
		// ★NAMED, NOT OMITTED. An Added definition has no older side, and a blank there would read
		//   as "the two are the same" - the opposite of what the row says.
		out.Append("(not in this document)\n\n");
		return;
	}

	// ***** LAID OUT BEFORE IT IS CUT. ***** The exporter writes the whole element on one line, so
	// cutting first would take the cut from a line nobody could read anyway - and the reader would
	// lose whole elements to keep half of one.
	//
	// ⚠★★**UTF-8 BOTH WAYS, NOT GetPlatformString.** A style can be called `段落スタイル 1`, and
	//   the platform string is whatever the system code page can hold - it silently drops the rest.
	//   GetUTF8String / SetUTF8String is a lossless round trip, and the layout only ever inserts
	//   ASCII, which cannot land inside a multi-byte sequence.
	PMString pretty;
	pretty.SetUTF8String(KCMPrettyXml(body.GetUTF8String()));
	pretty.SetTranslatable(kFalse);

	if (pretty.CharCount() > kKCMXmlSideLimit)
	{
		pretty.Remove(kKCMXmlSideLimit, kMaxInt32);
		out.Append(pretty);
		out.Append("\n... (cut here. The whole text is in app.kcmResourceDiff)\n\n");
		return;
	}

	out.Append(pretty);
	out.Append("\n\n");
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMResourceRowHasXml (declared in KCMResourceXml.h)
//----------------------------------------------------------------------------------------
bool16 KCMResourceRowHasXml()
{
	// ⚠THE MODE IS ASKED FIRST. The stashed row is an index into whichever list was on screen, so
	//   in the Story mode it names a story and reading it as a definition would answer about
	//   whatever definition happens to sit at that number (KCMStoryRefresh.h says the same).
	if (!KCMListShowsResources())
		return kFalse;

	Utils<IKCMCompareFacade> compare;
	if (!compare || !compare->IsArmed())
		return kFalse;

	const int32 row = KCMStoryMenuRow();
	if (row < 0)
		return kFalse;

	// The row has to still be there: the list is rebuilt whole by every comparison, and a right
	// click may be followed by a menu the reader leaves open.
	Utils<IKCMResourcesFacade> resources;
	if (!resources)
		return kFalse;

	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	return resources->GetNthChange(row, kind, key, what);
}

//----------------------------------------------------------------------------------------
// KCMShowResourceXml (declared in KCMResourceXml.h)
//----------------------------------------------------------------------------------------
void KCMShowResourceXml()
{
	// ★THE SAME TEST THE MENU WAS ENABLED BY, run again here. The menu's state was decided when it
	//   was opened, and a comparison can stop while it stands open - so the action asks once more
	//   rather than trusting that it could not have been offered wrongly.
	if (!KCMResourceRowHasXml())
	{
		KCMSetStatus("Show as XML: nothing to show.");
		return;
	}

	const int32 row = KCMStoryMenuRow();

	PMString kind, key;
	KCMResourceChangeKind what = kKCMResourceChanged;
	Utils<IKCMResourcesFacade>()->GetNthChange(row, kind, key, what);

	PMString source, target;
	if (!Utils<IKCMResourcesFacade>()->GetNthValues(row, source, target))
	{
		KCMSetStatus("Show as XML: the comparison no longer holds this definition.");
		return;
	}

	// ★The heading is the definition's WHOLE key, not the shortened name the row shows: this window
	//   stands on its own, away from the Kind column that would otherwise say what sort of thing it
	//   is (KCMResourceValue.h carries the shortening rule and why the list may shorten).
	// ★The escapes are read here too, so the heading of this window and the row that opened it say
	//   the same thing (`%3a` is a colon - KCMXmlPretty.h). ⚠The BODIES below are left exactly as
	//   the exporter wrote them: this window is where a reader goes to see the real XML, and
	//   quietly rewriting it here would defeat the one thing it is for.
	PMString text;
	text.SetUTF8String(KCMDecodePercentEscapes(key.GetUTF8String()));
	text.SetTranslatable(kFalse);
	text.Append("\n\n");

	// ⚠Source first (the user's call). The Target:/Source: lines at the top of the panel read the
	//   other way round; here the older text comes first because that is the order a difference is
	//   read in - what it was, then what it became.
	AppendSide(text, "----- Source (older) -----", source);
	AppendSide(text, "----- Target (newer) -----", target);

	// ★A finished sentence rather than a key. Left translatable, PMString hands it to the built-in
	//   table on the way to the screen, and a definition really can be called "Normal".
	text.SetTranslatable(kFalse);

	CAlert::ModalAlert(text, kOKString, kNullString, kNullString, 1, CAlert::eInformationIcon);
}

// End, KCMResourceXml.cpp.
