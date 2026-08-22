//========================================================================================
//
//  KESCMFacades.cpp
//
//  The boundary interfaces the UI is allowed to use, implemented as thin forwarders to the
//  model's internal functions.
//
//  Created 2026-08-13 for the model/UI split (Stage 1). Stage 1 Task 11 puts the first one
//  here (IKESCMCompareFacade); Tasks 12-15 add the other four to this same file.
//
//  These bodies deliberately contain no logic. Every one of them forwards to a function that
//  already existed and already worked; the point of this file is to give those functions an
//  address the other plug-in can reach. Putting logic here would mean the same decision lived
//  in two places.
//
//  ★ONE BODY IS NOT A PURE FORWARD: IKESCMStoryEditsFacade::GetRow copies five fields out of
//  the model's row. That is a change of ownership rather than a decision -- the model hands out
//  a pointer into a list it can rebuild, which is safe only while caller and list are in the
//  same plug-in (see the interface header).
//
//  All of them are AddIn'd to kUtilsBoss (see KESCM.fr), so the UI reaches them with
//  Utils<IKESCMxxx>(). The implementations are our own -- adding somebody else's stock
//  implementation to an existing boss is how you collide with another vendor's plug-in and
//  fail to load, and the unit of collision is the ImplementationID, not the IID.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CPMUnknown.h"

// Project includes:
#include "KESCMID.h"
#include "IKESCMCompareFacade.h"
#include "IKESCMMarkData.h"
#include "IKESCMPageFlagsFacade.h"
#include "IKESCMStoryEditsFacade.h"
#include "IKESCMBookFacade.h"
#include "KESCMComparisonRun.h"		// ToggleStartStop / Stop / StartFor / CanStart / print marks
#include "KESCMCore.h"				// MarkChanges / ClearMarks / DoSetPrintMarks / getters
#include "KESCMPeek.h"				// armed docs alive / peek / RefreshSelectedPages / base opacity
#include "KESCMColorSampler.h"		// the Alt+left CMYK sample and its drag-time pairing cache
#include "KESCMModelNotify.h"		// GetSessionStatus
#include "KESCMOversetApply.h"		// ApplyOversetForDoc / OversetScanTargetDB
#include "KESCMHideUnchanged.h"		// the Hide Unchanged toggle and its state
#include "KESCMDrawEventHandler.h"	// the engine's shared state, which these two publish
#include "KESCMPageMap.h"			// registered pages, the page pairing, and the Register toggle
#include "KESCMPageCheck.h"			// the Check toggle and the Save/Load of both flags
#include "KESCMStoryList.h"			// the Story Edits rows, and where a story begins in a document
#include "KESCMStoryDiffRun.h"		// RunOne - re-comparing one row's story ("Refresh Story Comparison")
#include "KESCMBookPair.h"			// which two books, and their display paths
#include "KESCMBookCompare.h"		// the book comparison itself
#include "KESCMPageNumberMarker.h"	// the folio exclusion toggle
#include "KESCMChangedPagesTSV.h"	// the TSV export

//========================================================================================
// KESCMCompareFacade -- IKESCMCompareFacade
//
// ★The interface carries six methods the plan's draft did not have. They were found by
// grepping for the actual callers before writing this file (Global Constraints: "check the
// move table against the real code"): HideUnchangedToggle / GetHideUnchangedOn /
// GetOversetScanTargetDB are all called from KESCMActionComponent.cpp, and ArmedDocsAlive /
// ShowPeekAt / GetBaseScreenOpacity from KESCMPeekGesture.cpp, KESCMCmykCursor.cpp
// and KESCMActionComponent.cpp -- every one of them a UI-side file. Left out, six calls would
// have kept crossing the boundary as free functions.
// ★★2026-08-15 (stage 2, task 4B) made it nine: SampleColorAt / BeginColorDrag / EndColorDrag
// were found the same way -- KESCMCmykCursor.cpp (UI) was including KESCMColorSampler.h (model)
// and calling its three free functions. Same reason as the six above, found one pass later
// because the grep that produced them looked for facade callers, not for cross-side includes.
//========================================================================================
class KESCMCompareFacade : public CPMUnknown<IKESCMCompareFacade>
{
public:
	KESCMCompareFacade(IPMUnknown* boss) : CPMUnknown<IKESCMCompareFacade>(boss) {}

	virtual void		ToggleStartStop()		{ KESCMToggleStartStop(); }
	virtual void		StopComparison()		{ KESCMStopComparison(); }
	virtual void		StartComparisonFor(IDocument* target, IDocument* source)
													{ KESCMStartComparisonFor(target, source); }
	virtual bool16		CanStartComparison()	{ return KESCMCanStartComparison(); }

	virtual bool16		IsArmed()				{ return KESCMIsArmed(); }
	virtual IDataBase*	GetArmedTargetDB()		{ return KESCMArmedTargetDB(); }
	virtual IDataBase*	GetArmedSourceDB()		{ return KESCMArmedSourceDB(); }

	virtual ErrorCode	MarkChanges(IDataBase* targetDB, IDataBase* sourceDB,
								PMString& outReport, bool16 allowIncremental)
								{ return KESCMDoMarkChangesDoc(targetDB, sourceDB, outReport, allowIncremental); }
	virtual bool16		RefreshSelectedPages(int32* outPages, int32* outChanged,
								bool16* outCancelled, int32* outFailed)
								{ return KESCMRefreshComparisonForSelectedPages(outPages, outChanged, outCancelled, outFailed); }
	virtual bool16		RefreshComparisonAvailable()	{ return KESCMRefreshComparisonAvailable(); }
	// (ClearMarks was here until 2026-08-17 -- removed with its declaration; see the interface.)

	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
													{ KESCMDoSetPrintMarks(printFlag, opacity25Flag, db); }
	virtual void		TogglePrintMarks()		{ KESCMTogglePrintMarks(); }
	virtual void		SetMarkOpacity25(bool16 op25)	{ KESCMSetMarkOpacity25(op25); }
	virtual bool16		GetPrintMarks()			{ return KESCMGetPrintMarks(); }
	virtual bool16		GetMarkOpacity25()		{ return KESCMGetMarkOpacity25(); }

	virtual KESCMCompareMode	GetCompareMode()					{ return KESCMGetCompareMode(); }
	virtual void				SetCompareMode(KESCMCompareMode m)	{ KESCMSetCompareMode(m); }

	virtual void		GetSessionStatus(PMString& out)	{ KESCMGetSessionStatus(out); }
	virtual void		GetSessionStatusSegments(PMString& outLabel, PMString& outPre,
												 PMString& outMid, PMString& outPost)
							{ KESCMGetSessionStatusSegments(outLabel, outPre, outMid, outPost); }

	// ---- the status line (stage 2) --------------------------------------------------------
	// ★Free functions from KESCMModelNotify.h. The panel's status writer and the UI shutdown
	// were calling them directly -- a legal direction (UI -> model) but not one that links
	// across two .pln. Same shape as the CMYK three below.
	// ⚠2026-08-15 (API audit B2): the five that used to stand here with them -- the notification
	//   payload -- are GONE. They travel on Change()'s changedBy now; see IKESCMCompareFacade.h
	//   at the spot they were removed from.
	virtual void		StoreSessionStatus(const PMString& s)	{ KESCMStoreSessionStatus(s); }
	virtual void		StoreSessionStatusSegments(const PMString& label, const PMString& pre,
												   const PMString& mid, const PMString& post)
							{ KESCMStoreSessionStatusSegments(label, pre, mid, post); }
	virtual void		ClearSessionStatus()	{ KESCMClearSessionStatus(); }

	virtual bool16		ArmedDocsAlive()		{ return KESCMArmedDocsAlive(); }
	virtual void		ShowPeekAt(IDataBase* targetDB, IDataBase* sourceDB,
								   const PMReal& mx, const PMReal& my,
								   const PMReal& viewScale, const PMReal& uiZoom,
								   UID viewSpreadUID)
													{ KESCMPeekShowAt(targetDB, sourceDB, mx, my, viewScale, uiZoom, viewSpreadUID); }
	virtual PMReal		GetBaseScreenOpacity()	{ return KESCMBaseScreenOpacity(); }

	// ---- the CMYK sampler (stage 2, task 4B) --------------------------------------------
	// ★These three are new to the facade. KESCMCmykCursor.cpp (UI) was calling the free
	// functions in KESCMColorSampler.h (model) directly -- a legal direction while both sit in
	// one .pln, but a free function cannot be linked across two. Task 4B is the pass that
	// touches those very files, so they go through the boundary here rather than in task 9.
	virtual bool16		SampleColorAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
									  const PMReal& mx, const PMReal& my,
									  UID viewSpreadUID,
									  PMString& outPanel, PMString& outCursor)
													{ return KESCMSampleCmykAt(hoverDB, otherDB, hoverIsTarget, mx, my, viewSpreadUID, outPanel, outCursor); }
	virtual void		BeginColorDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget)
													{ KESCMSampleCmykBeginDrag(hoverDB, otherDB, hoverIsTarget); }
	virtual void		EndColorDrag()			{ KESCMSampleCmykEndDrag(); }

	virtual void		ApplyOversetForDoc(IDataBase* db)	{ KESCMApplyOversetForDoc(db); }
	virtual IDataBase*	GetOversetScanTargetDB()	{ return KESCMOversetScanTargetDB(); }
	virtual void		ClearOverset()			{ KESCMDrawEventHandler::DropOverset(); }

	// ---- display toggles and press-time display state (Task 12) --------------------------
	// These reach the engine's static members rather than a free function, because there was
	// never a function: the UI assigned to the statics directly. The bodies are the assignments
	// that used to sit in KESCMActionComponent.cpp, KESCMPanelState.cpp and
	// KESCMPeekGesture.cpp, moved behind the boundary and nothing else. No caller lost or
	// gained a redraw -- invalidation stayed with the callers, where the choice of which
	// document to repaint is made.

	virtual bool16		GetShowSourceMarks()	{ return KESCMDrawEventHandler::sSrcMarksOn; }
	virtual void		SetShowSourceMarks(bool16 on)		{ KESCMDrawEventHandler::sSrcMarksOn = on; }
	virtual bool16		GetShowTargetMarks()	{ return KESCMDrawEventHandler::sTgtMarksOn; }
	virtual void		SetShowTargetMarks(bool16 on)		{ KESCMDrawEventHandler::sTgtMarksOn = on; }
	virtual bool16		GetShowOldPageNumbers()	{ return KESCMDrawEventHandler::sShowOldNumbers; }
	virtual void		SetShowOldPageNumbers(bool16 on)	{ KESCMDrawEventHandler::sShowOldNumbers = on; }
	// (GetHoldToHideMarks / SetHoldToHideMarks は 2026-08-22 に撤去＝IKESCMCompareFacade.h の注記を見よ)

	virtual void		SetMarksVisible(bool16 on)			{ KESCMDrawEventHandler::sMarksVisible = on; }
	virtual void		SetMarkScreenOpacity(const PMReal& opacity)
														{ KESCMDrawEventHandler::sMarkScreenOpacity = opacity; }
	virtual PMReal		GetSelectedMarkOpacity()	{ return KESCMDrawEventHandler::SelectedMarkOpacity(); }
	virtual bool16		GetMarksTempHidden()	{ return KESCMDrawEventHandler::sMarksTempHidden; }
	virtual void		SetMarksTempHidden(bool16 on)		{ KESCMDrawEventHandler::sMarksTempHidden = on; }
	virtual bool16		GetSrcMarksPressed()	{ return KESCMDrawEventHandler::sSrcMarksPressed; }
	virtual void		SetSrcMarksPressed(bool16 on)		{ KESCMDrawEventHandler::sSrcMarksPressed = on; }
	virtual void		SetPeekOpacity(const PMReal& opacity)
														{ KESCMDrawEventHandler::sPeekOpacity = opacity; }
	virtual bool16		GetShowOriginal()		{ return KESCMDrawEventHandler::sShowOriginal; }
	virtual void		SetShowOriginal(bool16 on)			{ KESCMDrawEventHandler::sShowOriginal = on; }

	// (ResetHideUnchanged / GetHideUnchangedDB / GetHideUnchangedSrcDB were here until 2026-08-17.
	//  The model-side functions they forwarded to are still in use -- from the model. See the interface.)
	virtual void		HideUnchangedToggle()	{ KESCMHideUnchangedToggle(); }
	virtual bool16		GetHideUnchangedOn()	{ return KESCMGetHideUnchangedOn(); }

	virtual bool16		IsDocDBOpen(IDataBase* db)	{ return KESCMIsDocDBOpen(db); }
	virtual void		InvalidateDB(IDataBase* db)	{ KESCMInvalidateDB(db); }
	virtual IDataBase*	GetActiveDocDB()		{ return KESCMActiveDocDB(); }
	virtual bool16		IsAppQuitting()			{ return KESCMAppIsQuitting(); }

	virtual bool16		GetIgnorePageNumberMarker()	{ return KESCMGetIgnorePageNumberMarker(); }
	virtual void		SetIgnorePageNumberMarker(bool16 on)
													{ KESCMSetIgnorePageNumberMarker(on); }

	virtual void		ExportChangedPagesTSV(PMString& outMessage)
													{ KESCMExportChangedPagesTSV(outMessage); }
};

CREATE_PMINTERFACE(KESCMCompareFacade, kKESCMCompareFacadeImpl)


//========================================================================================
// KESCMMarkData -- IKESCMMarkData
//
// The read-only half. Every method answers a question about the state the drawing engine
// holds; not one of them changes it.
//
// ★These bodies are the very expressions the UI used to write inline. Deliberately so: the
// point of Task 12 is to move WHERE the question is asked, not WHAT the answer is. The two
// places that do a little more than a lookup (IsOverflowPage and HasAnyMarkableContent) call
// EnsureOverflowCache first because the callers did, in the same position.
//========================================================================================
class KESCMMarkData : public CPMUnknown<IKESCMMarkData>
{
public:
	KESCMMarkData(IPMUnknown* boss) : CPMUnknown<IKESCMMarkData>(boss) {}

	virtual IDataBase*	GetMarkedTargetDB()		{ return KESCMDrawEventHandler::sDB; }
	virtual IDataBase*	GetMarkedSourceDB()		{ return KESCMDrawEventHandler::sSrcDB; }

	virtual bool16		HasEntryForPage(UID pageUID)
	{
		return (KESCMDrawEventHandler::sEntries.find(pageUID) !=
				KESCMDrawEventHandler::sEntries.end()) ? kTrue : kFalse;
	}

	virtual bool16		IsSourcePageMarked(UID sourcePageUID)
	{
		return (KESCMDrawEventHandler::sSrcPageToTarget.find(sourcePageUID) !=
				KESCMDrawEventHandler::sSrcPageToTarget.end()) ? kTrue : kFalse;
	}

	virtual bool16		GetChangeCells(UID pageUID, int32& outChanged, int32& outTotal)
	{
		outChanged = 0;
		outTotal   = 0;
		std::map<UID, KESCMOverlayEntry*>::const_iterator it = KESCMDrawEventHandler::sEntries.find(pageUID);
		if (it == KESCMDrawEventHandler::sEntries.end() || it->second == nil)
			return kFalse;
		outChanged = it->second->changedCells;
		outTotal   = it->second->w * it->second->h;	// the entry's image is the denominator
		return kTrue;
	}

	virtual bool16		IsOverflowPage(IDataBase* db, UID pageUID, bool16 isTargetSide)
	{
		KESCMDrawEventHandler::EnsureOverflowCache();	// no-op when the cache already matches
		const bool16 cacheMatch = isTargetSide ? (KESCMDrawEventHandler::sOverflowCacheDB == db)
											   : (KESCMDrawEventHandler::sOverflowCacheSrcDB == db);
		if (!cacheMatch)
			return kFalse;
		const std::set<UID>& overflowSet = isTargetSide ? KESCMDrawEventHandler::sOverflowT
													   : KESCMDrawEventHandler::sOverflowS;
		return (overflowSet.find(pageUID) != overflowSet.end()) ? kTrue : kFalse;
	}

	virtual bool16		IsPageOnHiddenSpread(IDataBase* db, UID pageUID)
									{ return KESCMIsPageOnHiddenSpread(db, pageUID); }

	virtual bool16		HasAnyMarkableContent()
	{
		KESCMDrawEventHandler::EnsureOverflowCache();
		return (!KESCMDrawEventHandler::sEntries.empty() ||
				!KESCMDrawEventHandler::sOverflowT.empty() ||
				!KESCMDrawEventHandler::sOverflowS.empty() ||
				(KESCMDrawEventHandler::sDB    != nil && KESCMPageMapHasAnyRegistered(KESCMDrawEventHandler::sDB)) ||
				(KESCMDrawEventHandler::sSrcDB != nil && KESCMPageMapHasAnyRegistered(KESCMDrawEventHandler::sSrcDB)))
			? kTrue : kFalse;
	}

	virtual bool16		GetOversetOn()			{ return KESCMDrawEventHandler::sOversetOn; }
	virtual IDataBase*	GetOversetDB()			{ return KESCMDrawEventHandler::sOversetDB; }

	virtual bool16		IsOversetPage(UID pageUID)
	{
		return (KESCMDrawEventHandler::sOversetPages.find(pageUID) !=
				KESCMDrawEventHandler::sOversetPages.end()) ? kTrue : kFalse;
	}

	virtual int32		GetOversetPageCount()	{ return (int32)KESCMDrawEventHandler::sOversetPages.size(); }

	virtual void		GetOversetPageUIDs(std::vector<UID>& out)
	{
		out.assign(KESCMDrawEventHandler::sOversetPages.begin(),
				   KESCMDrawEventHandler::sOversetPages.end());
	}

	virtual void		GetOversetLocations(std::vector<KESCMOversetLoc>& out)
	{
		out = KESCMDrawEventHandler::sOversetLocs;
	}

	virtual void		GetRegisteredPages(IDataBase* db, std::set<UID>& out)
	{
		KESCMPageMapCollectRegistered(db, out);
	}

	virtual void		GetPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
							std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
	{
		KESCMBuildPairing(targetDB, sourceDB, outTargetPages, outSourcePages);
	}

	virtual void		GetMasterPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
							std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
	{
		KESCMBuildMasterPairing(targetDB, sourceDB, outTargetPages, outSourcePages);
	}

	virtual void		GetAllPageUIDs(IDataBase* db, std::vector<UID>& out)
													{ KESCMCollectPageUIDs(db, out); }
	virtual void		GetMasterPageUIDs(IDataBase* db, std::vector<UID>& out)
													{ KESCMCollectMasterPageUIDs(db, out); }
	virtual bool16		GetMarkablePageUIDs(IDataBase* db, std::set<UID>& outPages)
													{ return KESCMCollectChangedPageUIDs(db, outPages); }
	virtual UID			GetFramePageUID(IDataBase* db, UID frameUID)
													{ return KESCMFramePageUID(db, frameUID); }
};

CREATE_PMINTERFACE(KESCMMarkData, kKESCMMarkDataImpl)


//========================================================================================
// KESCMPageFlagsFacade -- IKESCMPageFlagsFacade
//
// The writing half of the two per-page flags. Six forwarders, no logic: which pages are
// selected, what the menu label should say, where the JSON file goes -- all of that already
// lives in KESCMPageMap.cpp / KESCMPageCheck.cpp and stays there.
//========================================================================================
class KESCMPageFlagsFacade : public CPMUnknown<IKESCMPageFlagsFacade>
{
public:
	KESCMPageFlagsFacade(IPMUnknown* boss) : CPMUnknown<IKESCMPageFlagsFacade>(boss) {}

	virtual void	ToggleRegisterForSelection()	{ KESCMPageMapToggleSelectedPages(); }
	virtual void	ToggleCheckForSelection()		{ KESCMPageCheckToggleSelectedPages(); }

	virtual KESCMPageToggleState	GetRegisterToggleState()	{ return KESCMPageMapGetToggleState(); }
	virtual KESCMPageToggleState	GetCheckToggleState()		{ return KESCMPageCheckGetToggleState(); }

	virtual void	SaveChecksAndRegister()			{ KESCMPageCheckSaveToFile(); }
	virtual void	LoadChecksAndRegister()			{ KESCMPageCheckLoadFromFile(); }
};

CREATE_PMINTERFACE(KESCMPageFlagsFacade, kKESCMPageFlagsFacadeImpl)


//========================================================================================
// KESCMStoryEditsFacade -- IKESCMStoryEditsFacade
//
// The read side of the Story Edits list, plus the two "where does this story begin" questions
// the navigation asks of whichever document it is about to scroll.
//
// ★NO Build/Clear/ShutdownCleanup. The callers were grepped before this class was written and
// all of them are model-side (KESCMCore.cpp builds and clears, KESCMPeek.cpp clears and empties
// at shutdown), so the plan's draft Rebuild() would have been a method nobody calls.
//========================================================================================
class KESCMStoryEditsFacade : public CPMUnknown<IKESCMStoryEditsFacade>
{
public:
	KESCMStoryEditsFacade(IPMUnknown* boss) : CPMUnknown<IKESCMStoryEditsFacade>(boss) {}

	virtual int32	GetRowCount()	{ return KESCMStoryList::GetRowCount(); }

	virtual bool16	GetRow(int32 nth, Row& out)
	{
		const KESCMStoryRow* row = KESCMStoryList::GetRow(nth);
		if (row == nil)
			return kFalse;	// out of range, or the placeholder row -- out is left as the caller had it

		// Six of the row's seven fields. fPageIndex is the list's sort key and no caller reads it.
		out.fStoryUID	= row->fStoryUID;
		out.fText		= row->fText;
		out.fKinds		= row->fKinds;
		out.fFrameUID	= row->fFrameUID;
		out.fPageUID	= row->fPageUID;
		out.fTextCompared = row->fTextCompared;
		return kTrue;
	}

	virtual int32	GetChangeCount(int32 nth)
	{
		const KESCMStoryRow* row = KESCMStoryList::GetRow(nth);
		return (row != nil) ? static_cast<int32>(row->fChanges.size()) : 0;
	}

	virtual bool16	GetChange(int32 nth, int32 which, Change& out)
	{
		const KESCMStoryRow* row = KESCMStoryList::GetRow(nth);
		if (row == nil || which < 0 || which >= static_cast<int32>(row->fChanges.size()))
			return kFalse;

		const KESCMStoryChange& change = row->fChanges[which];
		out.fKind		= static_cast<int32>(change.fKind);
		out.fWhat		= static_cast<int32>(change.fWhat);
		out.fTargetStart = change.fTargetStart;
		out.fTargetEnd	= change.fTargetEnd;
		out.fSourceStart = change.fSourceStart;
		out.fSourceEnd	= change.fSourceEnd;
		out.fHasSource	= change.fHasSource;
		out.fTextPre	= change.fTextPre;
		out.fText		= change.fText;
		out.fTextPost	= change.fTextPost;
		out.fOtherTextPre	= change.fOtherTextPre;
		out.fOtherText		= change.fOtherText;
		out.fOtherTextPost	= change.fOtherTextPost;
		out.fRuby			= change.fRuby;			// only meaningful when fWhat is kAttr
		out.fOtherRuby		= change.fOtherRuby;
		return kTrue;
	}

	virtual int32	RefreshRow(int32 nth)
	{
		// The two documents the comparison is holding. ★ASKED FOR AGAIN RATHER THAN REMEMBERED:
		// the panel can only reach this while a comparison is armed, but "armed" and "still open"
		// are different questions and the second one is the one that matters here.
		IDataBase* const targetDB = KESCMArmedTargetDB();
		IDataBase* const sourceDB = KESCMArmedSourceDB();
		if (targetDB == nil || sourceDB == nil)
			return -1;
		if (!KESCMIsDocDBOpen(targetDB) || !KESCMIsDocDBOpen(sourceDB))
			return -1;

		const int32 count = KESCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

		// ★NOTHING IS SAID WHEN NOTHING CHANGED. The notification makes the panel rebuild the
		//   whole tree, which costs the reader their selection - so a refresh that could not be
		//   done leaves the list alone rather than shaking it for no result.
		if (count >= 0)
			KESCMNotify(kKESCMStoryEditsRebuiltMessage);

		return count;
	}

	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID)
					{ return KESCMStoryFirstFrameUID(db, storyUID); }

	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
					{ return KESCMStoryStartPoint(db, storyUID, outFrame, outPb); }
	virtual bool16	GetStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb)
					{ return KESCMStoryPointAt(db, storyUID, index, outPb); }
	virtual UID		GetStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index)
					{ return KESCMStoryFrameAt(db, storyUID, index); }
};

CREATE_PMINTERFACE(KESCMStoryEditsFacade, kKESCMStoryEditsFacadeImpl)


//========================================================================================
// KESCMBookFacade -- IKESCMBookFacade
//
// Book comparison: the fifth and last boundary of Stage 1. Three forwarders.
//
// ★What is NOT here was decided by grepping callers, not by the plan: KESCMGetBookResultText
// is read only by KESCMScriptProvider (model-side), KESCMBuildChapterPairing only by
// KESCMCompareBooks, and KESCMElidePathFront moved to the UI in this same task.
//========================================================================================
class KESCMBookFacade : public CPMUnknown<IKESCMBookFacade>
{
public:
	KESCMBookFacade(IPMUnknown* boss) : CPMUnknown<IKESCMBookFacade>(boss) {}

	virtual bool16		ResolveBookPair(const IDFile& panelBookFile,
								IBook*& outTarget, IBook*& outSource)
						{ return KESCMResolveBookPair(panelBookFile, outTarget, outSource); }

	virtual PMString	GetBookDisplayPath(IBook* book)
						{ return KESCMBookDisplayPath(book); }

	virtual ErrorCode	CompareBooks(IBook* target, IBook* source,
							std::vector<KESCMChapterResult>& outChapters, PMString& outReport)
						{ return KESCMCompareBooks(target, source, outChapters, outReport); }
};

CREATE_PMINTERFACE(KESCMBookFacade, kKESCMBookFacadeImpl)

// End of KESCMFacades.cpp.
