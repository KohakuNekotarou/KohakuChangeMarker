//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The Word content an import read, kept per (document, story) for "Redo from Word" (2026-09-24, stage 2 C -
//  design 15-1-6 of docs/superpowers/specs/2026-09-23-kcm-import-sync-design.md; the user's choice of 11-4-3:
//  in memory, no re-reading and no copy on disk).
//
//  ★ONE LIFE: put by the pour once a story's words are in (the next import of that story replaces it), dropped
//   with its document by the close sweep (KCMHandleDocsClosed, the same sweep the page state uses) and by the
//   model's shutdown. A redo asked after that says so out loud (design 11-4-3) - it never finds a stale story
//   under a database address a later document reused ([[uidref-reuse-after-close]]), because the sweep drops
//   the entry before the address can be reused.
//
//========================================================================================

#ifndef __KCMWordKeep_h__
#define __KCMWordKeep_h__

#include "BaseType.h"
#include "KCMStoryShape.h"

class IDataBase;

/** Keep the story as Word left it, for story `story` of `db` (replacing what was kept for it). */
void KCMWordKeepPut(IDataBase* db, UID story, const KCMStoryShape::Story& word);

/** What was kept for that story, or kFalse when nothing is (never imported, imported into a document since
	closed, or InDesign restarted). */
bool16 KCMWordKeepGet(IDataBase* db, UID story, KCMStoryShape::Story& out);

/** Drop the entries of documents that are no longer open - from KCMHandleDocsClosed, the liveness sweep
	(KCMIsDbAlive: a pointer comparison against the document list, never a dereference). */
void KCMWordKeepSweepClosed();

/** Forget everything - the model's shutdown (KCMPeek.cpp, beside KCMSourceCacheClear: a static of strings
	is emptied here rather than left to static destruction, the rule KCMStoryList.h states). */
void KCMWordKeepClear();

#endif // __KCMWordKeep_h__

// End, KCMWordKeep.h.
