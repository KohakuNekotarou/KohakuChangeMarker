//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  KCM'S OWN COPY STORES (2026-10-24, the import-as-Source road - docs/superpowers/specs/2026-10-24-kcm-import-as-
//  source-design.md section 11-3; the author: "when a row is put back by a right click, put it back from a Copy - the
//  way KFC does"). Before Apply This Change writes over the document's words, the words are copied - their formatting
//  with them - into a store of KCM's own, so that the row's Restore from Copy can paste them back later.
//  KFC's KFCCopyStore, copied: a store is the scrap database of a data exchange controller of KCM's own
//  (kKCMCopyStoreBoss) - not a document, not the user's clipboard, not in app.documents - ONE PER DOCUMENT (a store
//  shared by several documents once made a copy of a style no document had: docs/ai-notes/private-scrap-store-
//  2026-10-06.md), never purged, never emptied one copy at a time (an Undo or a Redo can bring back a record that
//  names any of them).
//  !WHERE KCM PARTS FROM KFC: a store goes when its document closes, saved or not. The comparison and its records go
//   with the document (KCMTaskStartCloneDocClosing), so nothing can name its copies again; KFC keeps a saved
//   document's store for a book chapter's rows, which KCM does not have.
//  !THE RANGE LIES IN ONE STORY THREAD - the primary one, a table cell's or a footnote's. Measured 2026-10-24 in KT
//   (a0fbd5d, work/note-scripts/2026-10-24-kcm-copy-threads/threads.ps1): a copy and its paste inside one cell's or one
//   footnote's thread neither crashed nor lost any character's formatting. What crashed in KT on 2026-10-06 was a
//   range RUNNING ACROSS threads. The commands check nothing, so this file does (InOneThread).
//  The main thread only: Apply This Change and Restore from Copy run there.
//
//========================================================================================

#ifndef __KCMCopyStore_h__
#define __KCMCopyStore_h__

#include "UIDRef.h"

class IDataBase;
class ITextModel;

namespace KCMCopyStore
{
	/** One copy: the store it is in (its number in this session - 0 = none) and its story in the store's database. */
	struct CopyRef
	{
		uint32	store;
		UID		story;
		CopyRef() : store(0), story(kInvalidUID) {}
		bool16 IsSet() const { return (store != 0 && story != kInvalidUID) ? kTrue : kFalse; }
	};

	/** Whether [start, end) of `model` lies in ONE story thread and short of that thread's own closing character - the
	    check the copy and the paste both make. A caret (start == end) on the closing character is inside. */
	bool16 InOneThread(ITextModel* model, TextIndex start, TextIndex end);

	/** [start, end) of `story` (a document's text model) copied into a new story of its document's store - made now when
	    the document has none. An empty range gives an empty copy (put back, it takes the range away). False = refused
	    or failed, outCopy left empty: an error already standing (no command is processed over one), a range not in one
	    thread, no store could be made, a copy that did not come out as long as the range (a test build's fault switch
	    copy-fail too - KCMDiag.h). Writes the store only, never the document; the error state is left as it was found.
	    Outside any command sequence of the document's. */
	bool16 Take(const UIDRef& story, TextIndex start, TextIndex end, CopyRef& outCopy);

	/** The copy's length - its story's primary thread without the closing return; -1 = not held. */
	int32 Length(const CopyRef& copy);

	/** The copy put over [start, end) of `story` - one command, inside the caller's command sequence: ITextUtils'
	    QueryCopyStoryCommand, or, for an empty copy, the range deleted (ITextModelCmds::DeleteCmd). False = not held, a
	    range not in one thread, or the command did not go through (the error state is left for the caller's sequence to
	    be decided by). */
	bool16 PasteOver(const CopyRef& copy, const UIDRef& story, TextIndex start, TextIndex end);

	/** ANOTHER DOCUMENT'S WORDS PUT OVER [start, end) of `story` (2026-10-24 night, Story mode's Restore from Source by
	    copy - design 1-8): [fromStart, fromEnd) of `from` - the Source document's story, or a Task Start kept in memory -
	    copied straight onto the range, formatting and all - one command, inside the caller's command sequence:
	    ITextUtils' QueryCopyStoryCommand (PasteOver's own, with the Source story where the store's would be - the way
	    KIDMCP copies from its clone into a document), or, for an empty `from` range, the range deleted. No store is
	    made for `from`: a store bound to a Task Start in memory would never be let go by kBeforeCloseDoc (it is no
	    document), and one database's address reused by another would share it. False = a range not in one thread on
	    either side, an error already standing, or the command did not go through (the error state is left for the
	    caller's sequence, as PasteOver's). The `from` document's modified state is kept as it was. */
	bool16 CopyOver(const UIDRef& from, TextIndex fromStart, TextIndex fromEnd, const UIDRef& story, TextIndex start,
					TextIndex end);

	/** A DOCUMENT IS CLOSING (kBeforeCloseDoc, KCMDocResponder.cpp): its store is let go. The address is compared,
	    never followed. */
	void DocumentClosing(IDataBase* db);

	/** The controlled shutdown (the shutdown service in KCMPeek.cpp): every store let go - Cleanup, then Release - while
	    the session is whole. */
	void ShutdownCleanup();
}

#endif // __KCMCopyStore_h__

// End, KCMCopyStore.h.
