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
	//   story's words at all. ★The question is asked in ONE place (KCMModeUsesStoryRows, in the
	//   boundary header) - this was once the one caller that spelled it out by hand, and doing so
	//   quietly shut the whole child-row menu in a mode that needed it.
	if (!KCMModeUsesStoryRows(Utils<IKCMCompareFacade>()->GetCompareMode()))
		return kFalse;

	if (!Utils<IKCMStoryEditsFacade>()->GetChange(gMenuRow, gMenuChange, out))
		return kFalse;

	// ★A "!" CHILD - one thing an import could not put in (2026-09-19) - OFFERS NOTHING. It has no
	//   words to copy, no position to write at, no other side to restore. Every item of the child
	//   row's menu asks this function first, so answering kFalse here greys them all, and a menu
	//   with nothing live does not open (the existing rule).
	if (out.fWhat == IKCMStoryEditsFacade::Change::kWhatRefused)
		return kFalse;

	return kTrue;
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

	// (⛔Until 2026-09-20 this also refused in the fourth mode, where the same command was offered
	//  under its own name - "Change to Imported Text". The mode and the second name are gone.)

	// ★**AND ONLY WITH A SOURCE THE OLDER WORDS CAN BE READ OUT OF.** The model decides
	//   (CanWriteToTarget) and its writes refuse on the same answer.
	//   ⚠(Until 2026-09-21 this also refused whenever two documents were compared - the user's
	//    rule of 2026-09-16, withdrawn when a Task Start became a document like any other.)
	if (!Utils<IKCMStoryEditsFacade>()->CanWriteToTarget())
		return kFalse;

	// ★**AND NOT ON ONE ALREADY RESTORED** (2026-09-15, when the Story mode started keeping those
	//   rows too, so that a Ctrl+Z has something to come back to). The older words are in the
	//   document and the row is showing them: offering to write them again would be offering to do
	//   nothing.
	if (change.fReplaced)
		return kFalse;

	// ★★**AND NOT ON ONE THAT CANNOT BE WRITTEN BACK** (2026-09-16, the user's rule: "offer it only
	//   when the range holds no special character"). A table, a note, an anchored object: text
	//   commands put back the character without the object. The model decided it (fWriteBlock), and
	//   its write refuses the same change again if it is reached some other way.
	if (change.fWriteBlock != 0)
		return kFalse;

	// Every kind: words, ruby and kenten (KCMStoryRestore.h). An insertion IS restorable - the
	// words come out again - so, unlike the copy item, an empty older side does not grey this
	// one. What cannot be written back (a custom kenten mark, an attribute whose paragraph's
	// words also changed) is refused by the model with a reason on the status line.
	return kTrue;
}

bool16 KCMChangeRowRestore()
{
	if (gMenuRow < 0 || gMenuChange < 0)
	{
		KCMSetStatus("restore: no change to restore.");
		return kFalse;
	}
	// ★★**THE SECOND "+" OF "+ +", TAKEN IN ALONE, IS ASKED ABOUT FIRST** (2026-09-17 afternoon, the user's
	//   request). A paragraph taken in gets the next style of the paragraph standing before it in the
	//   document at that moment, so taking the later of two new paragraphs first can style it differently
	//   from taking them in order - which the story row's take-in-all does.
	{
		IKCMStoryEditsFacade::Change change;
		if (StashedChange(change) && change.fAfterNewParagraph && !change.fReplaced)
		{
			PMString question(kKCMParagraphOrderConfirmKey);
			question.Translate();			// from the enUS table, like every other English string here
			question.SetTranslatable(kFalse);
			if (CAlert::ModalAlert(question, kOKString, kCancelString, kNullString,
					2,							// Cancel is the default: the careful answer
					CAlert::eQuestionIcon) != 1)
			{
				KCMSetStatus("restore: not taken in.");
				return kFalse;
			}
		}
	}

	// The model does the work and says what happened; the words go to the status line either way.
	PMString msg;
	const bool16 ok = Utils<IKCMStoryEditsFacade>()->RestoreChange(gMenuRow, gMenuChange, msg);
	if (msg.CharCount() > 0)
		KCMSetStatus(msg);
	return ok;
}

//----------------------------------------------------------------------------------------
// Putting one change back (2026-09-16)
//----------------------------------------------------------------------------------------

namespace
{

/*	CanUndoRestore
	A change that is STANDING as taken in. ⚠fReplaced is the MODEL's answer, worked out from the
	document's own counter - so a change the reader has already put back with Ctrl+Z greys the item
	instead of offering a second road to something that is done.
	⚠It took a `wantImport` flag until 2026-09-20, when the item was a pair (this one and "Change
	 Back to the Original"). The fourth mode is gone and so is the second name.
*/
bool16 CanUndoRestore()
{
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
		return kFalse;

	// The same rule and the same one answer as the take-in: a Source to read the older words from.
	if (!Utils<IKCMStoryEditsFacade>()->CanWriteToTarget())
		return kFalse;

	return change.fReplaced ? kTrue : kFalse;
}

}	// anonymous namespace

bool16 KCMChangeRowCanUndoRestore()
{
	return CanUndoRestore();
}

bool16 KCMChangeRowUndoRestore()
{
	if (gMenuRow < 0 || gMenuChange < 0)
	{
		KCMSetStatus("restore: no change to put back.");
		return kFalse;
	}
	PMString msg;
	const bool16 ok = Utils<IKCMStoryEditsFacade>()->UndoRestoreChange(gMenuRow, gMenuChange, msg);
	if (msg.CharCount() > 0)
		KCMSetStatus(msg);
	return ok;
}

/*	StoryBulkLive
	The one place that asks "is this the mode this item belongs to, and is there anything to act
	on" - so the item's grey state and what pressing it does cannot answer differently.

	⚠It took a `wantImport` flag and a "-1 means the whole list" convention until 2026-09-20, when
	 there were four bulk items. One is left (2026-09-21) and the fourth mode is gone, so both went.
*/
static bool16 StoryBulkLive(int32 nth)
{
	if (nth < 0)
		return kFalse;

	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare == nil || !compare->IsArmed())
		return kFalse;
	if (!KCMModeUsesStoryRows(compare->GetCompareMode()))
		return kFalse;

	// Asked of the model, which refuses the write on the same answer.
	if (!Utils<IKCMStoryEditsFacade>()->CanWriteToTarget())
		return kFalse;

	// ⚠**THE MERGED COUNT**, so a story whose changes have ALL been taken in still offers the item;
	//   pressing it then answers "nothing to take in" rather than writing. It is the cheap question
	//   here and the exact one in the model (BulkRun skips what is already taken in). Walking every
	//   change to grey the item would cost that walk every time a menu opens.
	return (Utils<IKCMStoryEditsFacade>()->GetChangeCount(nth) > 0) ? kTrue : kFalse;
}

bool16 KCMStoryRowCanRestoreAll()
{
	return StoryBulkLive(KCMStoryMenuRow());
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

// End, KCMStoryCopy.cpp.
