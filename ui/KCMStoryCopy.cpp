//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryCopy.h for what this is for. Three answers live here: which change the menu is
//  about, whether the item may be offered, and the copy itself.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// (The clipboard's seven includes went with "Copy Source Text" on 2026-09-15 - the header says
//  what the recipe was, so that removing the code did not take the finding with it.)

// General includes:
#include "CAlert.h"					// ModalAlert / kOKString / kCancelString / the icon enum
#include "PMString.h"
#include "Utils.h"

// Project includes:
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// GetCompareMode - the Story mode only
#include "IKCMStoryEditsFacade.h"	// GetChange - the words themselves
#include "KCMStoryCopy.h"
#include "KCMStoryRefresh.h"			// KCMStoryMenuRow - which STORY row the bulk item is about
#include "KCMUIShared.h"				// KCMSetStatus - the panel's message line

namespace
{

/* Which change the change-row menu was popped over.

   ★FILE STATICS, for the reason KCMStoryRefresh gives for its row: they belong to "the menu that
   is up right now", not to any row widget, and row widgets are recycled as the list scrolls. One
   menu is up at a time.

   ★NOT CLEARED WHEN THE MENU CLOSES, and not needing to be: every reader asks the model for the
   change afresh (GetChange), so indexes from a comparison ago answer "no such change" rather than
   naming whatever sits at those numbers now.
*/
int32 gMenuRow = -1;
int32 gMenuChange = -1;

/* StashedChange
   The change the stash names, as the model holds it NOW. kFalse when the stash names nothing,
   or the list has been rebuilt out from under it.
*/
bool16 StashedChange(IKCMStoryEditsFacade::Change& out)
{
	if (gMenuRow < 0 || gMenuChange < 0)
		return kFalse;

	// ★THE MODES WHOSE CHILD ROWS ARE TEXT CHANGES. The list is shared with the Resources mode,
	//   whose child rows carry the same node class and index shape but are not changes in a
	//   story's words at all.
	// ⚠★★**AND THE IMPORT MODE IS ONE OF THEM** (2026-09-15). This read `!= kKCMModeStory` until
	//   today, which quietly shut the whole child-row menu in the Import mode - the mode whose
	//   entire purpose is to take those changes in one at a time. The question is asked in one
	//   place for exactly this reason (KCMModeUsesStoryRows, in the boundary header), and this
	//   was the one caller still spelling it out by hand.
	if (!KCMModeUsesStoryRows(Utils<IKCMCompareFacade>()->GetCompareMode()))
		return kFalse;

	return Utils<IKCMStoryEditsFacade>()->GetChange(gMenuRow, gMenuChange, out);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStorySetMenuChange
//----------------------------------------------------------------------------------------

void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex)
{
	gMenuRow = rowIndex;
	gMenuChange = changeIndex;
}

//----------------------------------------------------------------------------------------
// KCMStoryGetMenuChange
//----------------------------------------------------------------------------------------

bool16 KCMStoryGetMenuChange(int32& outRow, int32& outChange)
{
	outRow = gMenuRow;
	outChange = gMenuChange;
	return (gMenuRow >= 0 && gMenuChange >= 0) ? kTrue : kFalse;
}

bool16 KCMChangeRowCanRestore()
{
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
		return kFalse;

	// ★NOT IN THE IMPORT MODE, where the same command is offered under its own name and
	//   ActionID ("Change to Imported Text", KCMChangeRowCanImport below). Exactly one of the two is
	//   ever live, so the menu shows one name and never both.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeImport)
		return kFalse;

	// Every kind: words, ruby and kenten (KCMStoryRestore.h). An insertion IS restorable - the
	// words come out again - so, unlike the copy item, an empty older side does not grey this
	// one. What cannot be written back (a custom kenten mark, an attribute whose paragraph's
	// words also changed) is refused by the model with a reason on the status line.
	return kTrue;
}

bool16 KCMChangeRowCanImport()
{
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
		return kFalse;

	// The Import mode's half of the pair above.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() != kKCMModeImport)
		return kFalse;

	// ★A CHANGE ALREADY TAKEN IN CANNOT BE TAKEN IN AGAIN. Its row is kept so that the reader can
	//   see what they did - not so that they can do it twice over words that already match.
	// ⚠fReplaced is the model's answer about the DOCUMENT (the story's counter has not moved
	//   since the write), so after an undo this goes live again and the model refuses with its
	//   own reason. Whether that refusal should instead be a greyed item is a thing to look at on
	//   screen; both halves of it are one line.
	return change.fReplaced ? kFalse : kTrue;
}

bool16 KCMChangeRowRestore()
{
	if (gMenuRow < 0 || gMenuChange < 0)
	{
		KCMSetStatus("restore: no change to restore.");
		return kFalse;
	}
	// The model does the work and says what happened; the words go to the status line either way.
	PMString msg;
	const bool16 ok = Utils<IKCMStoryEditsFacade>()->RestoreChange(gMenuRow, gMenuChange, msg);
	if (msg.CharCount() > 0)
		KCMSetStatus(msg);
	return ok;
}

//----------------------------------------------------------------------------------------
// The bulk items (2026-09-15)
//----------------------------------------------------------------------------------------

namespace
{

/*	BulkLive
	The one place that asks "is this the mode this item belongs to, and is there anything to act
	on" - so the four bulk items cannot answer it four different ways.

	@param wantImport kTrue for the Import mode's half of a pair.
	@param nth a row to require children on, or -1 for "the list as a whole".
*/
bool16 BulkLive(bool16 wantImport, int32 nth)
{
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare == nil || !compare->IsArmed())
		return kFalse;

	const KCMCompareMode mode = compare->GetCompareMode();
	if (!KCMModeUsesStoryRows(mode))
		return kFalse;
	if ((mode == kKCMModeImport ? kTrue : kFalse) != wantImport)
		return kFalse;

	// ⚠**THE MERGED COUNT**, so a story whose changes have ALL been taken in still offers the item;
	//   pressing it then answers "nothing to take in" instead of writing. The cheap question here,
	//   the exact one in the model (BulkRun skips the rows already taken in). Walking every change
	//   to grey the item would cost that walk every time a menu opens.
	if (nth >= 0)
		return (Utils<IKCMStoryEditsFacade>()->GetChangeCount(nth) > 0) ? kTrue : kFalse;
	return (Utils<IKCMStoryEditsFacade>()->GetRowCount() > 0) ? kTrue : kFalse;
}

}	// anonymous namespace

// ⚠**-1 MEANS "NO ROW" HERE AND "THE WHOLE LIST" TO BulkLive**, so the two are not passed through
//   to each other: a stash that names no row greys the per-story item outright. (It cannot happen
//   in practice - the row is stashed as the menu is popped - but letting one -1 stand for two
//   different questions is the shape of bug this plug-in has spent the most time on.)
bool16 KCMStoryRowCanRestoreAll()
{
	const int32 nth = KCMStoryMenuRow();
	return (nth >= 0) ? BulkLive(kFalse, nth) : kFalse;
}

bool16 KCMStoryRowCanImportAll()
{
	const int32 nth = KCMStoryMenuRow();
	return (nth >= 0) ? BulkLive(kTrue, nth) : kFalse;
}

bool16 KCMStoryRowRestoreAll()
{
	// ⚠**THE STORY ROW'S OWN STASH**, not the change row's: this item hangs on the parent menu, and
	//   the two menus keep their rows apart on purpose (KCMStoryRefresh.h).
	const int32 nth = KCMStoryMenuRow();
	if (nth < 0)
	{
		KCMSetStatus("no story row to take in.");
		return kFalse;
	}
	PMString msg;
	const bool16 ok = Utils<IKCMStoryEditsFacade>()->RestoreAllInStory(nth, msg);
	if (msg.CharCount() > 0)
		KCMSetStatus(msg);
	return ok;
}

bool16 KCMCanRestoreAllStories()
{
	return BulkLive(kFalse, -1);
}

bool16 KCMCanImportAllStories()
{
	return BulkLive(kTrue, -1);
}

bool16 KCMMenuRestoreAllStories()
{
	// ***** THE COUNTS, READ BEFORE THE QUESTION IS ASKED. *****
	// ⚠The merged count again: it is what the PANEL shows, so the number in the question is the
	//   number the reader can count in the list. What actually goes in can be fewer - a change
	//   already taken in, one the model cannot write - and the status line says so afterwards.
	const int32 rows = Utils<IKCMStoryEditsFacade>()->GetRowCount();
	int32 changes = 0;
	for (int32 i = 0; i < rows; ++i)
		changes += Utils<IKCMStoryEditsFacade>()->GetChangeCount(i);

	PMString ask;
	ask.SetTranslatable(kFalse);
	ask.Append("Take in ");
	ask.AppendNumber(changes);
	ask.Append(changes == 1 ? " change in " : " changes in ");
	ask.AppendNumber(rows);
	ask.Append(rows == 1 ? " story?" : " stories?");
	ask.Append("\n\nOne Ctrl+Z undoes all of it.");

	// ★**ONLY THIS ONE ASKS** (the user's call, 2026-09-15). The per-story item on the row's menu
	//   acts on what the reader pointed at; this one reaches across the whole document at once.
	if (CAlert::ModalAlert(ask, kOKString, kCancelString, kNullString, 1, CAlert::eQuestionIcon) != 1)
	{
		KCMSetStatus("cancelled - nothing was changed.");
		return kFalse;
	}

	PMString msg;
	const bool16 ok = Utils<IKCMStoryEditsFacade>()->RestoreAllStories(msg);
	if (msg.CharCount() > 0)
		KCMSetStatus(msg);
	return ok;
}

// End, KCMStoryCopy.cpp.
