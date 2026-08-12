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
#include "IApplication.h"		// QueryPanelManager - the panel walk starts here
#include "IBook.h"				// GetBookTitleName - the display name and the ClassID backstop
#include "IBookContent.h"		// GetIDFile / GetShortName - one chapter
#include "IBookContentMgr.h"	// GetContentCount / GetNthContent - the chapter list, in book order
#include "IBookManager.h"		// GetBookCount / GetNthBook / FindOpenBookByName
#include "IBookUIUtils.h"		// GetBookFileFromBookPanel - THIS panel's book, not the active one
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IDataBase.h"			// the book's own database, which its chapter UIDs live in
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "ISession.h"

// General includes:
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the CONTAINER
#include "PersistUtils.h"		// ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - so a blank file is refused rather than passed on
#include "WideString.h"			// the UTF-16 route for a chapter name (see CollectChapters)

// Project includes:
#include "KESCMBookPair.h"

namespace
{

// kBookPanelBoss lives in BOOK PANEL.APLN and is declared in no public header, so the number has
// to be spelled out. Taken from a live object-model dump and cross-checked against the running
// panel list, where every open book's panel came back as kBookPanelBoss (measured 2026-07-28,
// docs/ai-notes/book-panel-active-tab.md). A future build could renumber it - that is what the
// name check below is for, and why a total miss reports "no front tab" instead of guessing.
const uint32 kBookPanelBossRawClassID = 0x10101;

/** Does this panel name belong to a book that is open right now?

    Backstop for the hard-coded ClassID above. A book panel is titled with the book's title name,
    and when two open books share a name InDesign appends " 2" to the later one - so a leading
    match counts too. Cheap enough: there are rarely more than a handful of books. */
bool16 PanelNameMatchesOpenBook(const PMString& panelName)
{
	if (panelName.IsEmpty())
		return kFalse;

	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;

	InterfacePtr<IBookManager> bookMgr(session, UseDefaultIID());
	if (bookMgr == nil)
		return kFalse;

	const int32 bookCount = bookMgr->GetBookCount();
	for (int32 i = 0; i < bookCount; ++i)
	{
		IBook* book = bookMgr->GetNthBook(i);	// non-owning pointer - no release
		if (book == nil)
			continue;

		const PMString bookTitle = book->GetBookTitleName();
		if (bookTitle.IsEmpty())
			continue;
		if (panelName.Compare(kFalse, bookTitle) == 0)
			return kTrue;
		if (panelName.IndexOfString(bookTitle) == 0)	// "Book 1" -> tab "Book 1 2"
			return kTrue;
	}
	return kFalse;
}

/** Is this registered panel one of InDesign's book panels? */
bool16 IsBookPanelView(IControlView* panelView, const PMString& panelName)
{
	if (panelView == nil)
		return kFalse;
	return (::GetClass(panelView).Get() == kBookPanelBossRawClassID)
	       || PanelNameMatchesOpenBook(panelName);
}

/** The book file THIS panel is showing; kFalse when the panel could not be resolved.

    Handing the panel itself in is the whole point: given a real widget IBookUIUtils resolves that
    panel's book, where a nil widget falls through to QueryActiveBookPanel - the active book, which
    is precisely the value this file exists to avoid.

    An empty result is refused here rather than passed on, because further up it would read as
    "no book at all" instead of "this panel could not be asked". */
bool16 GetBookFileFromPanelView(IControlView* panelView, IDFile& outFile)
{
	IDFile panelBookFile;
	Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);

	SDKFileHelper panelFileHelper(panelBookFile);
	if (panelFileHelper.GetPath().empty())
		return kFalse;

	outFile = panelBookFile;
	return kTrue;
}

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

bool16 KESCMGetPanelBookFile(IDFile& outFile)
{
	if (!Utils<IBookUIUtils>().Exists())
		return kFalse;

	// Walk every registered panel rather than asking for "the" book panel. Two other routes were
	// tried and failed, and are not worth repeating (2026-07-27/28):
	//   - GetBookPanelWidget() returns nil for us. It is filled in by the book panel's OWN actions
	//     (SetBookPanelWidget), so a command coming from another panel's flyout finds nothing.
	//   - GetBookFileFromBookPanel(file, nil) falls through to QueryActiveBookPanel - the ACTIVE
	//     book, which is the value we are trying not to use.
	// The walk works because InDesign creates one book panel per open book (tab count == panel
	// count) and registers each of them with IPanelMgr. Their WidgetIDs are numbered dynamically
	// (kBookPanelWidgetID, +101, +102 ...), so naming one is impossible by construction.
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return kFalse;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return kFalse;

	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return kFalse;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		PMString panelName;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, nil, &panelName))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!IsBookPanelView(panelView, panelName))
			continue;

		// The front tab is decided on the CONTAINER, never on the panel. A book panel sitting
		// behind another tab still reports itself visible - all three panels came back "Visible
		// state 1" in the measurement - while only the front tab's kTabPanelContainerType is
		// visible. Asking panelView->IsVisible() here would match every book panel and pick
		// whichever came first.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid())
			continue;
		if (!PaletteRefUtils::IsPaletteVisible(container))
			continue;

		// Keep looking when this panel could not be asked, rather than handing back a blank file.
		if (!GetBookFileFromPanelView(panelView, outFile))
			continue;

		return kTrue;
	}

	// No visible book panel: it is iconised, its palette is closed, or no book is open at all.
	// The caller must NOT fall back to the active book - see the header.
	return kFalse;
}

bool16 KESCMResolveBookPair(IBook*& outTarget, IBook*& outSource)
{
	outTarget = nil;
	outSource = nil;

	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;

	InterfacePtr<IBookManager> bookMgr(session, UseDefaultIID());
	if (bookMgr == nil)
		return kFalse;

	IDFile panelFile;
	if (!KESCMGetPanelBookFile(panelFile))
		return kFalse;

	outTarget = bookMgr->FindOpenBookByName(panelFile);		// non-owning; nil means "not open"
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
