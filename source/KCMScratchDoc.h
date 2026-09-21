//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - a windowless document that lives for ONE call, and the
//  re-checks that say it is gone again.
//
//  ★WHY (2026-09-20, the user): "一時的にフレームが1つ出来る、これやっぱり怖い。それなら非表示の
//  ドキュメントをつくってそこでフレームを作る方法で、すこし時間がかかってもいい" - and, the same
//  message: "必ず消えているか再確認する仕組みをいれておいてほしい". So a table snippet is imported
//  HERE, never into the Target; the table is copied across with kCopyStoryRangeCmdBoss (measured
//  across documents on 2026-09-19); and when the call ends this document is closed and CHECKED to
//  be gone, while KCMTargetItemCountGuard checks the Target gained no page item.
//
//  THE DOCUMENT is made the way the Task Start copy used to be made (IDocumentCommands'
//  CreateNewCommand with kSuppressUI, one page, the defaults) and closed the same way
//  (KCMCloseRehydrated, after being marked clean). ⛔**The making half it was copied from went on
//  2026-09-21** with the rehydration (KCMRehydrate.cpp, NewDocumentLike) - what that measured is in
//  docs/ai-notes/kcm-rehydration-retired-2026-09-21.md. ⚠THE CLOSE RULE IS KCMRehydrate.h's, and
//  that half is still standing: a close under
//  an outstanding InterfacePtr is a protective shutdown, so every InterfacePtr a caller takes on this
//  document lives in an inner block that ends before this object does.
//
//========================================================================================

#pragma once
#ifndef __KCMScratchDoc_h__
#define __KCMScratchDoc_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

#include <string>
#include <vector>

class IDataBase;

class KCMScratchDoc
{
public:
	KCMScratchDoc();
	/** Closes the document (when Open made one) and records whether it is really gone -
	    LastOneWasClosed. */
	~KCMScratchDoc();

	/** Make the document. kFalse with a reason when the new-document command failed. */
	bool16		Open(PMString& whyNot);

	/** Its database, or nil before Open / after a failed one. */
	IDataBase*	DB() const;

	/** ISnippetImport::ImportFromStream of a page-item snippet (KCMBuildTableSnippet's text) into the
	    first spread. EVERY story the import brought in, in the order the new items were found, in
	    outStories - ⚠not one: a table whose cells hold anchored objects brings in more than one text
	    frame, and which of them is the table's is not for this class to guess (the caller picks the
	    one that holds a table). kFalse with a reason when nothing was imported or no frame holds a
	    story. */
	bool16		ImportSnippet(const std::string& snippet, std::vector<UIDRef>& outStories, PMString& whyNot);

	/** ★THE RE-CHECK the user asked for: kTrue when the most recently destroyed KCMScratchDoc found its
	    document gone from the document list after the close (or never made one). Read after the
	    object's block has ended. */
	static bool16	LastOneWasClosed();

private:
	UIDRef	fDoc;
	KCMScratchDoc(const KCMScratchDoc&);
	KCMScratchDoc& operator=(const KCMScratchDoc&);
};

/** Counts the Target's page items (every spread, every descendant) when made; Unchanged() compares
    the count now with that one - the second re-check: nothing of ours was left in the Target. */
class KCMTargetItemCountGuard
{
public:
	explicit KCMTargetItemCountGuard(IDataBase* target);
	bool16	Unchanged() const;
	int32	Delta() const;		///< items now minus items then (0 when unchanged)

private:
	IDataBase*	fTarget;
	int32		fThen;
	static int32 Count(IDataBase* db);
};

#endif // __KCMScratchDoc_h__

// End, KCMScratchDoc.h.
