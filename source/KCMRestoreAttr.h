//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  "Restore from Source" on an attribute change row (2026-09-24, stage 2 B - design section 14 of
//  docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md): the ruby, kenten, warichu or tate-chu-yoko over
//  a row's characters is made what the SOURCE document has there.
//
//  ★★★**READ FROM THE SOURCE STORY ITSELF, NEVER FROM WHAT THE ROW REMEMBERS** (the user: "the Source document is
//   there, so take the information from there and put it back"). The row carries a reading as a string; the Source
//   story carries the mark as it is - a custom kenten's own character among other things the row has no field for.
//   Both stories are read with KCMTextRead::ReadStory, the same reader the comparison runs.
//
//  ★**ONE KIND, THE ROW'S** (design 14-1 item 3): a tate-chu-yoko row inside a warichu restores the tate-chu-yoko
//   and leaves the warichu as it stands.
//
//  ★★**THE CHARACTERS UNDER THE MARK HAVE TO BE THE SOURCE'S** (design 14-1 item 4), or the mark is put over the
//   wrong words in silence. Two tests, both before anything is written: an EMPTY side (the diff's way of saying the
//   words of the paragraph differ as well - CompareParagraphAttr's textDiffered), and the characters of the window
//   compared on both sides. A refusal writes nothing, and names the way out (fix the words first; after an import,
//   "Reject This Import Change").
//
//  ★**PLANNED FIRST, WRITTEN SECOND**, as two calls: the caller (the facade) begins its command sequence only once
//   the plan has been made, so a refusal never lands an empty step on the undo stack - the same discipline as the
//   reject's count-first (KCMRejectImport.h).
//
//========================================================================================

#ifndef __KCMRestoreAttr_h__
#define __KCMRestoreAttr_h__

#include "BaseType.h"		// bool16 / int32 / TextIndex
#include "PMString.h"

#include "KCMAttrRestorePlan.h"

class UIDRef;

/** One restore, planned: the kind, its window and its writes (KCMAttrRestorePlan), and how many characters the
	window covers (for the message). */
struct KCMAttrRestoreJob
{
	int32				fKind;			// KCMStoryAttrKind: ruby / kenten / warichu / tcy
	KCMAttrRestorePlan	fPlan;
	int32				fCharacters;
	KCMAttrRestoreJob() : fKind(0), fCharacters(0) {}
};

/** Reads BOTH stories (KCMTextRead::ReadStory, under IDataBase::SaveRestoreModifiedState - nothing is written
	here), plans the window (KCMPlanAttrRestore) and refuses when the characters under it are not the same on
	both sides (design 14-1 item 4), when a side has no characters (the words differ as well), when the kind
	is not one that is written back, or when a Source kenten names a mark this build cannot write.
	@return kTrue with outJob filled; kFalse with outWhy (non-translatable) and nothing to do. */
bool16 KCMPlanRestoreAttrFromSource(const UIDRef& targetStory, const UIDRef& sourceStory, int32 kind,
									TextIndex tFrom, TextIndex tTo, TextIndex sFrom, TextIndex sTo,
									KCMAttrRestoreJob& outJob, PMString& outWhy);

/** Whether the marks of `kind` over the Target's [tFrom, tTo) are the Source's over [sFrom, sTo) - the same
	positions (aligned by tFrom - sFrom), values and, for ruby, the same group setting, once each side's marks
	are clipped to its window. Both stories are read under IDataBase::SaveRestoreModifiedState; nothing is
	written. ★What a taken-back attribute row's "=" is a comparison of (KCMStoryList::RejectedStateOf, 2026-09-24
	night). kFalse when a story cannot be read or the windows differ in length. */
bool16 KCMAttrMarksSame(const UIDRef& targetStory, const UIDRef& sourceStory, int32 kind,
						TextIndex tFrom, TextIndex tTo, TextIndex sFrom, TextIndex sTo);

/** Takes the kind OFF the whole Target window, then puts the Source's marks on (KCMStoryRestore's writers).
	⚠The CALLER wraps this in a command sequence - one undo step. A write that fails half way leaves what went
	 in ahead of it (the same shape as KCMPourParagraphAttributes), on the stack under the caller's name.
	@return how many marks went on (0 = taken off, the Source has none there); -1 when a write failed (outWhy). */
int32 KCMApplyRestoreAttr(const UIDRef& targetStory, const KCMAttrRestoreJob& job, PMString& outWhy);

#endif // __KCMRestoreAttr_h__

// End, KCMRestoreAttr.h.
