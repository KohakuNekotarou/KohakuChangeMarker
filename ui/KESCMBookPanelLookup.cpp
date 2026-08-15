//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarkerUI (KCMUI)
//
//  The front-tab walk. See KESCMBookPanelLookup.h for why it lives on this side.
//
//  Moved here from the model half's KESCMBookPair.cpp on 2026-08-15 (Stage 2, Task 9B).
//  ***** THE CODE IS UNCHANGED. ***** Only the file it sits in and the include list moved: the
//  walk, the container test, the name backstop and every early return are byte-for-byte what the
//  model half ran, so a comparison started today resolves the same pair it resolved yesterday.
//
//  The walk itself is ported from KBS (KBSBookScope.cpp:1007), measured on this machine
//  2026-07-28 and in use since. What is NOT ported is KBS's fall back to the active book when no
//  front tab is found - the header says why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager - the panel walk starts here
#include "IBook.h"				// GetBookTitleName - the name backstop for the ClassID check
#include "IBookManager.h"		// GetBookCount / GetNthBook - the open books to match names against
#include "IBookUIUtils.h"		// GetBookFileFromBookPanel - THIS panel's book, not the active one
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IDataBase.h"			// the panel manager's database, which the panel UIDs live in
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "ISession.h"

// General includes:
#include "PMString.h"
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the CONTAINER
#include "PersistUtils.h"		// ::GetDataBase
#include "SDKFileHelper.h"		// GetPath - so a blank file is refused rather than passed on

// Project includes:
#include "KESCMBookPanelLookup.h"

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

// End, KESCMBookPanelLookup.cpp.
