//========================================================================================
//
//  KCMDocUidSet.h
//
//  A container, in memory only, holding "document database -> set of page UIDs".
//
//  KCM keeps two states of exactly this shape:
//    - registered pages (Added/Removed = pages with no counterpart) ... sRegistered, KCMPageMap.cpp
//    - checked pages (the tick)                                     ... sChecked, KCMPageCheck.cpp
//  They mean different things, so the two sets stay separate; but the container itself and the
//  routine operations on it (liveness sweep / clear all / membership / count / bulk replace)
//  are the same, so only that shared part lives here.
//
//  Rules:
//    - This container writes nothing to the document file (it never dirties the document).
//      What goes when is up to the file that owns each set: Stop forgets the registrations and
//      never the ticks (sChecked is a cache of labels the document itself holds - KCMPageCheck.cpp).
//    - **A closed database is never dereferenced.** The liveness sweep decides purely by pointer
//      comparison against IDocumentList::FindDocByDataBase (memory uidref-reuse-after-close).
//    - An entry whose set became empty is dropped at once, which keeps both the sweep and the
//      "does this document have any" test cheap. Insert and Replace(non-empty) create an entry,
//      Erase and Replace(empty) drop one, ClearAllDocs drops them all.
//
//========================================================================================
#ifndef __KCMDocUidSet_h__
#define __KCMDocUidSet_h__

#include "BaseType.h"		// int32, bool16
#include "OMTypes.h"		// UID
#include <map>
#include <set>
#include <vector>

class IDataBase;

class KCMDocUidSet
{
public:
	typedef std::map<IDataBase*, std::set<UID> > Map;

	// Is uid in db's set? kFalse when db is nil or there is no entry for that document.
	bool16 Contains(IDataBase* db, UID uid) const;

	// Does db's set hold anything at all (existence only; the drawing side's early out uses it).
	bool16 HasAny(IDataBase* db) const;

	// How many are in db's set (0 when there is no entry).
	int32 CountIn(IDataBase* db) const;

	// The questions asked about a selection (Register and Check count with the first; ToggleAll
	//   decides with the second).
	//   Unlike calling Contains once per page, **the lock and the FindDoc happen once**: taking
	//   the lock again for every page lets the set change midway, and the All/Some answer would
	//   then be settled from two different states of it.
	//   @warning db nil / no entry counts as "none of them are in", which is what Contains says.

	// How many of uids are in db's set.
	int32 CountIn(IDataBase* db, const std::vector<UID>& uids) const;

	// Is any of uids missing from db's set (kFalse when uids is empty).
	bool16 AnyNotIn(IDataBase* db, const std::vector<UID>& uids) const;

	// Add uid to db's set (nothing happens when db is nil). Creates the entry if there is none.
	void Insert(IDataBase* db, UID uid);

	// Remove uid from db's set. Drops the whole entry once it is empty.
	void Erase(IDataBase* db, UID uid);

	/** Put all of `uids` in, or take all of them out -- whichever the set is not already full of.
		Returns kTrue when they went IN, which is what the caller words its message from.

		**The rule is "any missing means add them all"**, so a partly registered selection registers
		the rest rather than clearing what is there. (Register's toggle. Check keeps the same rule
		but writes the document, so it works out the same answer itself - KCMPageCheck.cpp.)
		@warning the decision and the change are under ONE lock: the answer is what decides the
		  direction, so they belong in one. */
	bool16 ToggleAll(IDataBase* db, const std::vector<UID>& uids);

	// Forget every document. Only empties the map; no pointer is touched (no dereference).
	void ClearAllDocs();

	// Replace db's set wholesale with uids (what the sync from the document writes -
	// KCMPageCheckReplaceAll). An empty uids drops the entry.
	void Replace(IDataBase* db, const std::set<UID>& uids);

	// Add db's set into out. **out is not cleared** -- this merges into an existing set.
	void CollectInto(IDataBase* db, std::set<UID>& out) const;

	// The liveness sweep run after documents close: drops the entries of closed documents, state
	// only. A closed database is never dereferenced (pointer comparison against
	// FindDocByDataBase, nothing more). The close-up can arrive during the shutdown sequence as
	// well, so session / app / docList are all nil-guarded.
	void SweepClosedDocs();

private:
	// The one way an entry is looked up. fMap is keyed by IDataBase*, so **a background thread
	//   (the asynchronous PDF export) always misses**: it is handed a clone with a different
	//   pointer, and the registered pages' green "/" and the ticks would be absent from the
	//   export. When the pointer misses, the lookup falls back on **file identity
	//   (KCMIsSameDoc)** - **for the clone this thread exports and nothing else**
	//   (KCMIsExportCloneOnThisThread): a document placed into another one names the same file too,
	//   and with any database let through, its ticks printed in the other document's PDF (measured).
	//   @warning only the readers (Contains / HasAny / CountIn / CollectInto) go through this.
	//     **The writers must not**: writing always happens on the main thread and means "add to
	//     this very db", so growing a clone's entry would be the wrong document, not a rescue.
	//   Cost: fMap holds one element per open document (a handful), and the linear scan only
	//   runs when the pointer lookup missed.
	Map::const_iterator FindDoc(IDataBase* db) const;

	Map fMap;
};

#endif // __KCMDocUidSet_h__
