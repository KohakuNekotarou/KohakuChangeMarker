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

	// ★NOT IN THE IMPORT MODE, where the same command is offered under its own name and
	//   ActionID ("Change to Imported Text", KCMChangeRowCanImport below). Exactly one of the two is
	//   ever live, so the menu shows one name and never both.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeImport)
		return kFalse;

	// ★★**AND NOT WHEN TWO DOCUMENTS ARE COMPARED** (2026-09-16, the user's rule: the Source is
	//   there to copy from). The model decides (CanWriteToTarget) and its writes refuse on it too.
	if (!Utils<IKCMStoryEditsFacade>()->CanWriteToTarget())
		return kFalse;

	// ★**AND NOT ON ONE ALREADY RESTORED** (2026-09-15, when the Story mode started keeping those
	//   rows too, so that a Ctrl+Z has something to come back to). The older words are in the
	//   document and the row is showing them: offering to write them again would be offering to do
	//   nothing. The Import half has asked this from its first day.
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
	if (change.fReplaced)
		return kFalse;

	// The same rule as the Story mode's half above, for the same reason (fWriteBlock).
	return (change.fWriteBlock != 0) ? kFalse : kTrue;
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
	The one question both halves of the pair ask: the right mode, and a change that is STANDING as
	taken in. ⚠fReplaced is the MODEL's answer, worked out from the document's own counter - so a
	change the reader has already put back with Ctrl+Z greys the item instead of offering a second
	road to something that is done.
*/
bool16 CanUndoRestore(bool16 wantImport)
{
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
		return kFalse;

	const bool16 isImport = (Utils<IKCMCompareFacade>()->GetCompareMode() == kKCMModeImport)
						  ? kTrue : kFalse;
	if (isImport != wantImport)
		return kFalse;

	// Not when two documents are compared - the same rule and the same one answer as the take-in.
	if (!Utils<IKCMStoryEditsFacade>()->CanWriteToTarget())
		return kFalse;

	return change.fReplaced ? kTrue : kFalse;
}

}	// anonymous namespace

bool16 KCMChangeRowCanUndoRestore()
{
	return CanUndoRestore(kFalse);
}

bool16 KCMChangeRowCanUndoImport()
{
	return CanUndoRestore(kTrue);
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

// End, KCMStoryCopy.cpp.
