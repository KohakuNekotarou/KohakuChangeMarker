//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KESCM)
//
//  Book comparison: which two books. See KESCMBookPair.h for the contract.
//
//  The front-tab walk is ported from KBS (KBSBookScope.cpp:1007), measured on this machine
//  2026-07-28 and in use since. What is NOT ported is KBS's fall back to the active book when no
//  front tab is found - the header says why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBook.h"				// GetBookTitleName - the display name and the ClassID backstop
#include "IBookContent.h"		// GetIDFile / GetShortName - one chapter
#include "IBookContentMgr.h"	// GetContentCount / GetNthContent - the chapter list, in book order
#include "IBookManager.h"		// GetBookCount / GetNthBook / FindOpenBookByName
#include "IDataBase.h"			// the book's own database, which its chapter UIDs live in
#include "ISession.h"

// General includes:
#include "PersistUtils.h"		// ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - so a blank file is refused rather than passed on
#include "WideString.h"			// the UTF-16 route for a chapter name (see CollectChapters)

// Project includes:
#include "KESCMBookPair.h"

namespace
{

/** A chapter as the book lists it. */
struct ChapterEntry
{
	IDFile		fFile;
	PMString	fName;
	bool16		fHasFile;	// kFalse when the book cannot name a file for this chapter

	ChapterEntry() : fHasFile(kFalse) {}
};

/** Collect a book's chapters IN BOOK ORDER - the order the pairing pairs them in.

    The name is taken here rather than derived from the path later, because the book already has
    one (GetShortName) and building a second answer to the same question is how the two drift
    apart. */
void CollectChapters(IBook* book, std::vector<ChapterEntry>& out)
{
	if (book == nil)
		return;

	InterfacePtr<IBookContentMgr> contentMgr(book, UseDefaultIID());
	if (contentMgr == nil)
		return;

	IDataBase* bookDB = ::GetDataBase(book);
	if (bookDB == nil)
		return;

	const int32 contentCount = contentMgr->GetContentCount();
	for (int32 i = 0; i < contentCount; ++i)
	{
		const UID contentUID = contentMgr->GetNthContent(i);
		if (contentUID == kInvalidUID)
			continue;

		InterfacePtr<IBookContent> content(bookDB, contentUID, UseDefaultIID());
		if (content == nil)
			continue;

		ChapterEntry entry;
		entry.fName.SetTranslatable(kFalse);

		// The name is taken FIRST and unconditionally, because a chapter that cannot be opened
		// still has to be named in the report. Via the UTF-16 buffer: the PMString(char*)
		// conversions do not survive a Japanese chapter name (KBS arrived at the same route,
		// KBSBookScope.cpp:1207-1216).
		WideString shortName = content->GetShortName();
		const UTF16TextChar* buf = shortName.GrabUTF16Buffer(nil);
		if (buf != nil)
			entry.fName.AppendW(buf);

		// ***** The answer is checked, not assumed. ***** IBookContent.h:121-125 says GetIDFile
		// returns "kTrue if a file CAN be obtained", so a chapter without one is a real case. It
		// cannot be compared - but it still reaches the list, with a reason (see the pairing).
		// KBS discards this return value; here it is the difference between "no change" and
		// "never looked", which is exactly the distinction that took a day to find in KBS.
		entry.fHasFile = content->GetIDFile(entry.fFile);

		out.push_back(entry);
	}
}

}	// anonymous namespace

bool16 KESCMResolveBookPair(const IDFile& panelBookFile, IBook*& outTarget, IBook*& outSource)
{
	outTarget = nil;
	outSource = nil;

	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;

	InterfacePtr<IBookManager> bookMgr(session, UseDefaultIID());
	if (bookMgr == nil)
		return kFalse;

	// The front tab is now OBSERVED BY THE UI and handed in (Stage 2, Task 9B - the walk moved to
	// ui/KESCMBookPanelLookup.cpp because it needs WidgetBin, IBookUIUtils and IPanelMgr, none of
	// which a model plug-in may reach). The rule below did not change: that file's book is the
	// Target, the first OTHER open book is the Source.
	// ⚠A caller that could not identify a front tab must not call this at all - it must NOT hand in
	//   a blank file and it must NOT fall back to the active book (see KESCMBookPanelLookup.h).
	outTarget = bookMgr->FindOpenBookByName(panelBookFile);	// non-owning; nil means "not open"
	if (outTarget == nil)
		return kFalse;

	// The first OTHER open book is the source (the older version). Same rule as the document
	// comparison's KESCMFirstOtherDoc (KESCMPanelObserver.cpp), including its known limitation:
	// with three or more books open this picks one of them arbitrarily. That is survivable only
	// because both names are always shown on screen - see the caller.
	const int32 bookCount = bookMgr->GetBookCount();
	for (int32 i = 0; i < bookCount; ++i)
	{
		IBook* book = bookMgr->GetNthBook(i);				// non-owning pointer - no release
		if (book != nil && book != outTarget)
		{
			outSource = book;
			break;
		}
	}

	return (outSource != nil);
}

PMString KESCMBookDisplayName(IBook* book)
{
	if (book == nil)
		return PMString();

	PMString name = book->GetBookTitleName();	// includes the .indb extension (measured 2026-08-11)
	name.SetTranslatable(kFalse);
	return name;
}

PMString KESCMBookDisplayPath(IBook* book)
{
	if (book == nil)
		return PMString();

	// IBook names its own file (IBook.h:102) - no walk of the Book panel needed, and no second
	// source of truth: this is the same book object the comparison is about to be handed.
	const IDFile file = book->GetBookFileSpec();
	SDKFileHelper helper(file);

	PMString path = helper.GetPath();
	if (path.IsEmpty())
		return KESCMBookDisplayName(book);	// unsaved or unnamed - the title is all there is

	path.SetTranslatable(kFalse);
	return path;
}

void KESCMBuildChapterPairing(IBook* target, IBook* source, std::vector<KESCMChapterResult>& out)
{
	out.clear();

	std::vector<ChapterEntry> targetChapters;
	std::vector<ChapterEntry> sourceChapters;
	CollectChapters(target, targetChapters);
	CollectChapters(source, sourceChapters);

	// Pair BY POSITION. Whichever book has more chapters leaves a tail unpaired, and unpaired IS
	// the answer for those: added on the target side, deleted on the source side. Nothing about
	// them needs to be opened.
	const size_t targetCount = targetChapters.size();
	const size_t sourceCount = sourceChapters.size();
	const size_t pairCount   = (targetCount > sourceCount) ? targetCount : sourceCount;

	for (size_t i = 0; i < pairCount; ++i)
	{
		const bool16 hasTarget = (i < targetCount);
		const bool16 hasSource = (i < sourceCount);

		KESCMChapterResult result;
		if (hasTarget)
			result.fTargetFile = targetChapters[i].fFile;
		if (hasSource)
			result.fSourceFile = sourceChapters[i].fFile;

		// The name comes from the target side; only a chapter that exists in the source alone is
		// named from there. Same rule the TSV export uses for pages.
		result.fName  = hasTarget ? targetChapters[i].fName : sourceChapters[i].fName;
		result.fState = hasTarget ? (hasSource ? kKESCMChapterUnknown : kKESCMChapterAdded)
		                          : kKESCMChapterDeleted;

		// A chapter the book cannot name a file for can never be opened, so it is answered here
		// rather than handed on to the comparison. It is NOT dropped: a missing row would read as
		// "nothing to say about this chapter", which is the one meaning it must never carry.
		if ((hasTarget && !targetChapters[i].fHasFile) || (hasSource && !sourceChapters[i].fHasFile))
		{
			result.fState = kKESCMChapterFailed;
			result.fWhy   = PMString("the book gives no file for this chapter");
			result.fWhy.SetTranslatable(kFalse);
		}

		out.push_back(result);
	}
}

// End, KESCMBookPair.cpp.
