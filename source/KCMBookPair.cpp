//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM)
//
//  Book comparison: which two books. See KCMBookPair.h for the contract.
//
//  ⚠THE FRONT-TAB WALK IS NOT IN THIS FILE ANY MORE. It moved to ui/KCMBookPanelLookup.cpp on
//  2026-08-15 (Stage 2, Task 9B) because it needs PaletteRefUtils, IBookUIUtils and IPanelMgr, and
//  a model plug-in may reach none of them; the resolver below is handed the answer. The walk itself
//  is ported from KBS (KBSBookScope::GetPanelBookFile), measured on this machine 2026-07-28 and in
//  use since. What is NOT ported is KBS's fall back to the active book when no front tab is found -
//  the header says why. (This preamble described the walk as living here until 2026-08-18, three
//  days after it left: the file the note is attached to is not the file the note is about.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBook.h"				// GetBookTitleName / IsOpen - the display name and the ClassID backstop
#include "IBookContent.h"		// GetIDFile / GetShortName - one chapter
#include "IBookContentMgr.h"	// GetContentCount / GetNthContent - the chapter list, in book order
#include "IBookManager.h"		// GetBookCount / GetNthBook / FindOpenBookByName
#include "IBookUtils.h"			// GetBookContentStatus - what the book knows about a chapter
#include "IDataBase.h"			// the book's own database, which its chapter UIDs live in
#include "ISession.h"

// General includes:
#include "PersistUtils.h"		// ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - so a blank file is refused rather than passed on
#include "WideString.h"			// the UTF-16 route for a chapter name (see CollectChapters)

// Project includes:
#include "KCMBookPair.h"

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
    apart.

    ***** EVERY POSITION IN THE BOOK PRODUCES EXACTLY ONE ENTRY, even one that cannot be read.
    ***** Two things downstream are counting on index i meaning "the book's i-th chapter", and a
    silent `continue` here would break both at once (found 2026-08-18, bug recheck B8):
      1. the pairing is BY POSITION, so dropping one chapter pairs every later chapter with its
         neighbour - a whole book reported as changed, with no hint why;
      2. KCMChapterStatusText is handed this same i and passes it to GetNthContent, so the word
         the book contributes to a failure ("missing", "in use") would describe another chapter.
         That function bounds-checks i against the book in case the book was edited in between -
         which is worth nothing if the list it is checked against was built with gaps in it.
    ⚠ It also contradicts the rule the pairing states forty lines below and keeps: a chapter that
      cannot be compared gets a ROW WITH A REASON, because a missing row reads as "nothing to say
      about this chapter", which is the one meaning it must never carry. */
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
		ChapterEntry entry;
		entry.fName.SetTranslatable(kFalse);

		// An entry is pushed for this position whatever happens below - see the header note. When
		// the book has no readable content here, the entry stays as constructed (fHasFile kFalse),
		// and the pairing turns that into a failed chapter with a reason.
		const UID contentUID = contentMgr->GetNthContent(i);
		if (contentUID != kInvalidUID)
		{
			InterfacePtr<IBookContent> content(bookDB, contentUID, UseDefaultIID());
			if (content != nil)
			{
				// The name is taken FIRST and unconditionally, because a chapter that cannot be
				// opened still has to be named in the report. Via the UTF-16 buffer: the
				// PMString(char*) conversions do not survive a Japanese chapter name (KBS arrived
				// at the same route, KBSBookScope::ListBookChapters - GetShortName straight into
				// GrabUTF16Buffer).
				WideString shortName = content->GetShortName();
				const UTF16TextChar* buf = shortName.GrabUTF16Buffer(nil);
				if (buf != nil)
					entry.fName.AppendW(buf);

				// ***** The answer is checked, not assumed. ***** IBookContent.h:121-125 says
				// GetIDFile returns "kTrue if a file CAN be obtained", so a chapter without one is
				// a real case. It cannot be compared - but it still reaches the list, with a
				// reason (see the pairing). KBS discards this return value; here it is the
				// difference between "no change" and "never looked", which is exactly the
				// distinction that took a day to find in KBS.
				entry.fHasFile = content->GetIDFile(entry.fFile);
			}
		}

		// A row still has to say WHICH chapter it is. An unnamed one is only marginally better than
		// the dropped row this loop refuses to produce, so the position stands in for the name -
		// the one fact that is known even when nothing else about the chapter could be read.
		if (entry.fName.IsEmpty())
		{
			entry.fName = PMString("(chapter ");
			entry.fName.AppendNumber(i + 1);
			entry.fName.Append(")");
			entry.fName.SetTranslatable(kFalse);
		}

		out.push_back(entry);
	}
}

/** The book's display name: IBook::GetBookTitleName().
    That INCLUDES the .indb extension - measured 2026-08-11, an open book called new.indb reports
    "new.indb", not "new".

    ***** File-local since 2026-08-18. ***** Its one caller is the fallback in
    KCMBookDisplayPath, right below; nothing outside this file has asked for a book's bare name
    since the panel and the dialog went over to full paths (2026-08-12). */
PMString BookDisplayName(IBook* book)
{
	if (book == nil)
		return PMString();

	PMString name = book->GetBookTitleName();	// includes the .indb extension (measured 2026-08-11)
	name.SetTranslatable(kFalse);
	return name;
}

/** The word for one BookContentStatus::State.

    ***** Empty for kDocNormal. ***** "The book has nothing to add" is not a reason, and appending
    "(normal)" to a failure would say less than saying nothing. The caller only appends a non-empty
    word, so a chapter the book considers fine fails with the plain sentence it always had. */
const char* StatusWord(BookContentStatus::State state)
{
	switch (state)
	{
		case BookContentStatus::kDocMising:		return "missing";		// (sic - the SDK's spelling)
		case BookContentStatus::kDocOutofDate:	return "out of date";
		case BookContentStatus::kDocInUse:		return "in use";
		case BookContentStatus::kDocOpen:		return "open";
		default:								return "";				// kDocNormal - see above
	}
}

}	// anonymous namespace

bool16 KCMResolveBookPair(const IDFile& panelBookFile, IBook*& outTarget, IBook*& outSource)
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
	// ui/KCMBookPanelLookup.cpp because it needs WidgetBin, IBookUIUtils and IPanelMgr, none of
	// which a model plug-in may reach). The rule below did not change: that file's book is the
	// Target, the first OTHER open book is the Source.
	// ⚠A caller that could not identify a front tab must not call this at all - it must NOT hand in
	//   a blank file and it must NOT fall back to the active book (see KCMBookPanelLookup.h).
	// ***** "IS IT LISTED" IS NOT THE SAME QUESTION AS "IS IT OPEN". ***** A book that is closing is
	// still on IBookManager's list, still reports IsOpen and still has a live database at the moment
	// its close is broadcast - measured on the release build 2026-07-27 as
	// "books=1, ours listed, IsOpen=1, db=1". The lookup alone therefore hands back books that
	// nothing may be run against; IBook::IsOpen (IBook.h:78) is the flag the close clears, so it is
	// asked as well. KBS keeps the same two tests together and says not to separate them when
	// tidying (KBSBookScope::FindOpenBookByPath, :283-289).
	outTarget = bookMgr->FindOpenBookByName(panelBookFile);	// non-owning; nil means "not open"
	if (outTarget == nil || !outTarget->IsOpen())
	{
		outTarget = nil;		// the contract is "whichever could not be resolved is left nil"
		return kFalse;
	}

	// The first OTHER open book is the source (the older version). Same rule as the document
	// comparison's KCMFirstOtherDoc (KCMComparisonRun.cpp - model side, moved there with the
	// rest of the resolver in Stage 1 Task 9; it read "KCMPanelObserver.cpp" until 2026-08-18,
	// which is a UI file and has not held that function since), including its known limitation:
	// with three or more books open this picks one of them arbitrarily. That is survivable only
	// because both names are always shown on screen - see the caller.
	const int32 bookCount = bookMgr->GetBookCount();
	for (int32 i = 0; i < bookCount; ++i)
	{
		IBook* book = bookMgr->GetNthBook(i);				// non-owning pointer - no release
		// IsOpen() for the reason given at the target above, and it weighs MORE here: this takes the
		// first book that is not the target, so a book in the middle of closing is precisely what it
		// would otherwise pick up - and the user would be shown its name as the Source.
		if (book != nil && book != outTarget && book->IsOpen())
		{
			outSource = book;
			break;
		}
	}

	return (outSource != nil);
}

PMString KCMBookDisplayPath(IBook* book)
{
	if (book == nil)
		return PMString();

	// IBook names its own file (IBook.h:102) - no walk of the Book panel needed, and no second
	// source of truth: this is the same book object the comparison is about to be handed.
	const IDFile file = book->GetBookFileSpec();
	SDKFileHelper helper(file);

	PMString path = helper.GetPath();
	if (path.IsEmpty())
		return BookDisplayName(book);		// unsaved or unnamed - the title is all there is

	path.SetTranslatable(kFalse);
	return path;
}

void KCMBuildChapterPairing(IBook* target, IBook* source, std::vector<KCMChapterResult>& out)
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

		KCMChapterResult result;
		if (hasTarget)
			result.fTargetFile = targetChapters[i].fFile;
		if (hasSource)
			result.fSourceFile = sourceChapters[i].fFile;

		// The name comes from the target side; only a chapter that exists in the source alone is
		// named from there. Same rule the TSV export uses for pages.
		result.fName  = hasTarget ? targetChapters[i].fName : sourceChapters[i].fName;
		result.fState = hasTarget ? (hasSource ? kKCMChapterUnknown : kKCMChapterAdded)
		                          : kKCMChapterDeleted;

		// A chapter the book cannot name a file for can never be opened, so it is answered here
		// rather than handed on to the comparison. It is NOT dropped: a missing row would read as
		// "nothing to say about this chapter", which is the one meaning it must never carry.
		//
		// ⚠ONLY WHERE A FILE WOULD ACTUALLY HAVE BEEN OPENED - that is, for a PAIR (2026-08-18, bug
		//  recheck B8). Added and Deleted are finished answers reached without reading anything, so
		//  overwriting one with "failed" would throw away a correct answer to say we could not do
		//  something we were never going to do. The header states the principle two paragraphs up:
		//  "having no counterpart IS the answer; nothing about it needs opening or comparing."
		if (result.fState == kKCMChapterUnknown &&
		    (!targetChapters[i].fHasFile || !sourceChapters[i].fHasFile))
		{
			result.fState = kKCMChapterFailed;
			// ★"in the book" is what separates this from the other no-file reason: OpenChapter's
			//   says the CHAPTER named no file, this one says the BOOK does not. The verdict itself
			//   is not repeated here - see KCMBookResult.h's fWhy.
			result.fWhy   = PMString("no file in the book");
			result.fWhy.SetTranslatable(kFalse);
		}

		out.push_back(result);
	}
}

PMString KCMChapterStatusText(IBook* book, int32 chapterIndex)
{
	PMString out;
	out.SetTranslatable(kFalse);

	if (book == nil || chapterIndex < 0)
		return out;

	InterfacePtr<IBookContentMgr> contentMgr(book, UseDefaultIID());
	IDataBase* bookDB = ::GetDataBase(book);
	if (contentMgr == nil || bookDB == nil)
		return out;

	// The index is the pairing's, and the pairing was built from this same list - but it was built
	// BEFORE the chapters were opened, and this runs after. A book edited in between would make the
	// index name a different chapter, so it is bounds-checked and no word is given rather than a
	// wrong one. (Nothing in this plug-in edits a book; the caller is a failure path, which is
	// exactly where an assumption is least worth making.)
	if (chapterIndex >= contentMgr->GetContentCount())
		return out;

	const UID contentUID = contentMgr->GetNthContent(chapterIndex);
	if (contentUID == kInvalidUID)
		return out;

	InterfacePtr<IBookContent> content(bookDB, contentUID, UseDefaultIID());
	if (content == nil)
		return out;

	// Utils<IBookUtils> is kUtilsBoss - the same boss this plug-in already reaches for
	// Utils<IDocumentCommands> and Utils<IDocumentUtils> in KCMBookCompare.cpp. All three are
	// IID_I*UTILS entries on kUtilsBoss served by APPFRAMEWORK.RPLN, so a model plug-in reaches this
	// one on exactly the terms it already reaches those.
	//
	// Exists() is asked even so. Utils.h:68-72 gives the bare one-line call as the ordinary form
	// (what KBS uses at KBSBookScope.cpp:1413) and offers Exists() at :103-104 for "the plug-in that
	// supplies the interface has been removed" - which APPFRAMEWORK is not. The difference is which
	// path this sits on: the function only runs once a chapter has ALREADY failed to open, the one
	// path a working test run never takes. A nil here would turn a reportable failure into a crash,
	// found by a user rather than by us, and the test that prevents it is one comparison.
	Utils<IBookUtils> bookUtils;
	if (!bookUtils.Exists())
		return out;

	out.Append(StatusWord(bookUtils->GetBookContentStatus(content)));
	return out;
}

// End, KCMBookPair.cpp.
