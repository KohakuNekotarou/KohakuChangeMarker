//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - what the reader has selected, read as a list of stories
//
//  WHAT THIS IS FOR. "Export Story Text..." writes every story of the document unless something is
//  selected, and then it writes the stories that selection touches (the user's decision,
//  2026-09-15; narrowed to the LAYOUT selection only on 2026-09-16). This file answers the one
//  question that takes: which stories.
//
//  ★**IT LIVES ON THE UI SIDE BECAUSE A SELECTION DOES.** The model half is handed a list of UIDs
//  and is never told what a selection is; it checks that each UID really is a story of the document
//  it is exporting, and gets on with the writing. [[model-plugin-must-not-drive-ui]] is this same
//  boundary seen from the other end.
//
//========================================================================================
#ifndef __KCMStorySelection_h__
#define __KCMStorySelection_h__

#include "BaseType.h"

class IDataBase;
class UIDList;

/** Append to `outStories` every story the reader's selection touches, each one once.

	★**A SELECTED FRAME MEANS ITS WHOLE STORY**, not the part that one frame shows. An exported
	  file is a story; a story can run through twenty frames; half of one could not be read back.
	★★**A CARET DOES NOT COUNT** (the user's decision, 2026-09-16). Only what is selected ON THE
	  LAYOUT is read: a frame, or a path carrying text on it. A typing position selects nothing
	  here, so a reader who is editing text and exports gets the WHOLE document, the same as one
	  who has selected nothing at all. The reason it is not read at all - rather than read
	  carefully - is in the .cpp: the answer outlives the caret.
	★**A GROUP IS LOOKED INSIDE OF**, because what the reader sees is the frames, not the group.

	@param db the document being exported. A selection living in ANY OTHER document is passed over
	          rather than exported by mistake - the two really can differ, which is what
	          KCMStoryJump.cpp measured when a story editor was in front.
	@param outStories appended to, never cleared, and never given the same story twice.
	@return kTrue when something was selected AT ALL.
	⚠**THE CALLER NEEDS BOTH ANSWERS, WHICH IS WHY THERE ARE TWO.** Nothing selected means "write
	 the whole document"; a selection holding no text at all means "write nothing, and say so". An
	 empty list on its own cannot tell those two apart, and guessing picks the wrong one half the
	 time - the half where the reader watches all forty stories come out. */
bool16 KCMCollectSelectedStories(IDataBase* db, UIDList& outStories);

#endif // __KCMStorySelection_h__

// End, KCMStorySelection.h.
