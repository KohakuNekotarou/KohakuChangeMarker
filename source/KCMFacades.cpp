//========================================================================================
//
//  KCMFacades.cpp
//
//  The boundary interfaces the UI is allowed to use, implemented as thin forwarders to the
//  model's internal functions.
//
//  Created 2026-08-13 for the model/UI split (Stage 1). Stage 1 Task 11 puts the first one
//  here (IKCMCompareFacade); Tasks 12-15 add the other four to this same file.
//
//  These bodies deliberately contain no logic. Every one of them forwards to a function that
//  already existed and already worked; the point of this file is to give those functions an
//  address the other plug-in can reach. Putting logic here would mean the same decision lived
//  in two places.
//
//  ★ONE BODY IS NOT A PURE FORWARD: IKCMStoryEditsFacade::GetRow copies five fields out of
//  the model's row. That is a change of ownership rather than a decision -- the model hands out
//  a pointer into a list it can rebuild, which is safe only while caller and list are in the
//  same plug-in (see the interface header).
//
//  All of them are AddIn'd to kUtilsBoss (see KCM.fr), so the UI reaches them with
//  Utils<IKCMxxx>(). The implementations are our own -- adding somebody else's stock
//  implementation to an existing boss is how you collide with another vendor's plug-in and
//  fail to load, and the unit of collision is the ImplementationID, not the IID.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CPMUnknown.h"

// Project includes:
#include "KCMID.h"
#include "IKCMCompareFacade.h"
#include "IKCMMarkData.h"
#include "IKCMPageFlagsFacade.h"
#include "IKCMStoryEditsFacade.h"
#include "IKCMBookFacade.h"
#include "IKCMStoryMarkFacade.h"
#include "KCMComparisonRun.h"		// ToggleStartStop / Stop / StartFor / CanStart / print marks
#include "KCMCore.h"				// MarkChanges / ClearMarks / DoSetPrintMarks / getters
#include "KCMPeek.h"				// armed docs alive / peek / RefreshSelectedPages / base opacity
#include "KCMColorSampler.h"		// the Alt+left CMYK sample and its drag-time pairing cache
#include "KCMModelNotify.h"		// GetSessionStatus
#include "KCMOversetApply.h"		// ApplyOversetForDoc / OversetScanTargetDB
#include "KCMHideUnchanged.h"		// the Hide Unchanged toggle and its state
#include "KCMDrawEventHandler.h"	// the engine's shared state, which these two publish
#include "KCMPageMap.h"			// registered pages, the page pairing, and the Register toggle
#include "KCMPageCheck.h"			// the Check toggle and the Save/Load of both flags
#include "KCMStoryList.h"			// the Story Edits rows, and where a story begins in a document
#include "KCMStoryDiffRun.h"		// RunOne - re-comparing one row's story ("Refresh Story Comparison")
#include "KCMBookPair.h"			// which two books, and their display paths
#include "KCMBookCompare.h"		// the book comparison itself
#include "KCMPageNumberMarker.h"	// the folio exclusion toggle
#include "KCMChangedPagesTSV.h"	// the TSV export
#include "KCMStoryMarkBuild.h"	// what the Story mode should be lighting up (Refresh / SetPress)
#include "KCMStoryMarker.h"		// the adornment that draws it - the flash and the shutdown

//========================================================================================
// KCMCompareFacade -- IKCMCompareFacade
//
// ★The interface carries six methods the plan's draft did not have. They were found by
// grepping for the actual callers before writing this file (Global Constraints: "check the
// move table against the real code"): HideUnchangedToggle / GetHideUnchangedOn /
// GetOversetScanTargetDB are all called from KCMActionComponent.cpp, and ArmedDocsAlive /
// ShowPeekAt / GetBaseScreenOpacity from KCMPeekGesture.cpp, KCMCmykCursor.cpp
// and KCMActionComponent.cpp -- every one of them a UI-side file. Left out, six calls would
// have kept crossing the boundary as free functions.
// ★★2026-08-15 (stage 2, task 4B) made it nine: SampleColorAt / BeginColorDrag / EndColorDrag
// were found the same way -- KCMCmykCursor.cpp (UI) was including KCMColorSampler.h (model)
// and calling its three free functions. Same reason as the six above, found one pass later
// because the grep that produced them looked for facade callers, not for cross-side includes.
//========================================================================================
class KCMCompareFacade : public CPMUnknown<IKCMCompareFacade>
{
public:
	KCMCompareFacade(IPMUnknown* boss) : CPMUnknown<IKCMCompareFacade>(boss) {}

	virtual void		ToggleStartStop()		{ KCMToggleStartStop(); }
	virtual void		StopComparison()		{ KCMStopComparison(); }
	virtual void		StartComparisonFor(IDocument* target, IDocument* source)
													{ KCMStartComparisonFor(target, source); }
	virtual bool16		CanStartComparison()	{ return KCMCanStartComparison(); }

	virtual bool16		IsArmed()				{ return KCMIsArmed(); }
	virtual IDataBase*	GetArmedTargetDB()		{ return KCMArmedTargetDB(); }
	virtual IDataBase*	GetArmedSourceDB()		{ return KCMArmedSourceDB(); }

	virtual ErrorCode	MarkChanges(IDataBase* targetDB, IDataBase* sourceDB,
								PMString& outReport, bool16 allowIncremental)
								{ return KCMDoMarkChangesDoc(targetDB, sourceDB, outReport, allowIncremental); }
	virtual bool16		RefreshSelectedPages(int32* outPages, int32* outChanged,
								bool16* outCancelled, int32* outFailed)
								{ return KCMRefreshComparisonForSelectedPages(outPages, outChanged, outCancelled, outFailed); }
	virtual bool16		RefreshComparisonAvailable()	{ return KCMRefreshComparisonAvailable(); }
	// (ClearMarks was here until 2026-08-17 -- removed with its declaration; see the interface.)

	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
													{ KCMDoSetPrintMarks(printFlag, opacity25Flag, db); }
	virtual void		TogglePrintMarks()		{ KCMTogglePrintMarks(); }
	virtual void		SetMarkOpacity25(bool16 op25)	{ KCMSetMarkOpacity25(op25); }
	virtual bool16		GetPrintMarks()			{ return KCMGetPrintMarks(); }
	virtual bool16		GetMarkOpacity25()		{ return KCMGetMarkOpacity25(); }
	virtual void		SetMarkColor(bool16 cyan)	{ KCMSetMarkColor(cyan); }
	virtual bool16		GetMarkColorCyan()		{ return KCMGetMarkColorCyan(); }

	virtual KCMCompareMode	GetCompareMode()					{ return KCMGetCompareMode(); }
	virtual void				SetCompareMode(KCMCompareMode m)	{ KCMSetCompareMode(m); }

	virtual void		GetSessionStatus(PMString& out)	{ KCMGetSessionStatus(out); }
	virtual void		GetSessionStatusSegments(PMString& outLabel, PMString& outPre,
												 PMString& outMid, PMString& outPost,
												 PMString& outRuby)
							{ KCMGetSessionStatusSegments(outLabel, outPre, outMid, outPost, outRuby); }

	// ---- the status line (stage 2) --------------------------------------------------------
	// ★Free functions from KCMModelNotify.h. The panel's status writer and the UI shutdown
	// were calling them directly -- a legal direction (UI -> model) but not one that links
	// across two .pln. Same shape as the CMYK three below.
	// ⚠2026-08-15 (API audit B2): the five that used to stand here with them -- the notification
	//   payload -- are GONE. They travel on Change()'s changedBy now; see IKCMCompareFacade.h
	//   at the spot they were removed from.
	virtual void		StoreSessionStatus(const PMString& s)	{ KCMStoreSessionStatus(s); }
	virtual void		StoreSessionStatusSegments(const PMString& label, const PMString& pre,
												   const PMString& mid, const PMString& post,
												   const PMString& ruby)
							{ KCMStoreSessionStatusSegments(label, pre, mid, post, ruby); }
	virtual void		ClearSessionStatus()	{ KCMClearSessionStatus(); }

	virtual bool16		ArmedDocsAlive()		{ return KCMArmedDocsAlive(); }
	virtual void		ShowPeekAt(IDataBase* targetDB, IDataBase* sourceDB,
								   const PMReal& mx, const PMReal& my,
								   const PMReal& viewScale, const PMReal& uiZoom,
								   UID viewSpreadUID)
													{ KCMPeekShowAt(targetDB, sourceDB, mx, my, viewScale, uiZoom, viewSpreadUID); }
	virtual PMReal		GetBaseScreenOpacity()	{ return KCMBaseScreenOpacity(); }

	// ---- the CMYK sampler (stage 2, task 4B) --------------------------------------------
	// ★These three are new to the facade. KCMCmykCursor.cpp (UI) was calling the free
	// functions in KCMColorSampler.h (model) directly -- a legal direction while both sit in
	// one .pln, but a free function cannot be linked across two. Task 4B is the pass that
	// touches those very files, so they go through the boundary here rather than in task 9.
	virtual bool16		SampleColorAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
									  const PMReal& mx, const PMReal& my,
									  UID viewSpreadUID,
									  PMString& outPanel, PMString& outCursor)
													{ return KCMSampleCmykAt(hoverDB, otherDB, hoverIsTarget, mx, my, viewSpreadUID, outPanel, outCursor); }
	virtual void		BeginColorDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget)
													{ KCMSampleCmykBeginDrag(hoverDB, otherDB, hoverIsTarget); }
	virtual void		EndColorDrag()			{ KCMSampleCmykEndDrag(); }

	virtual void		ApplyOversetForDoc(IDataBase* db)	{ KCMApplyOversetForDoc(db); }
	virtual IDataBase*	GetOversetScanTargetDB()	{ return KCMOversetScanTargetDB(); }
	virtual void		ClearOverset()			{ KCMDrawEventHandler::DropOverset(); }

	// ---- display toggles and press-time display state (Task 12) --------------------------
	// These reach the engine's static members rather than a free function, because there was
	// never a function: the UI assigned to the statics directly. The bodies are the assignments
	// that used to sit in KCMActionComponent.cpp, KCMPanelState.cpp and
	// KCMPeekGesture.cpp, moved behind the boundary and nothing else. No caller lost or
	// gained a redraw -- invalidation stayed with the callers, where the choice of which
	// document to repaint is made.

	virtual bool16		GetShowSourceMarks()	{ return KCMDrawEventHandler::sSrcMarksOn; }
	virtual void		SetShowSourceMarks(bool16 on)		{ KCMDrawEventHandler::sSrcMarksOn = on; }
	virtual bool16		GetShowTargetMarks()	{ return KCMDrawEventHandler::sTgtMarksOn; }
	virtual void		SetShowTargetMarks(bool16 on)		{ KCMDrawEventHandler::sTgtMarksOn = on; }
	virtual bool16		GetShowOldPageNumbers()	{ return KCMDrawEventHandler::sShowOldNumbers; }
	virtual void		SetShowOldPageNumbers(bool16 on)	{ KCMDrawEventHandler::sShowOldNumbers = on; }
	// (GetHoldToHideMarks / SetHoldToHideMarks は 2026-08-22 に撤去＝IKCMCompareFacade.h の注記を見よ)

	virtual void		SetMarksVisible(bool16 on)			{ KCMDrawEventHandler::sMarksVisible = on; }
	virtual void		SetMarkScreenOpacity(const PMReal& opacity)
														{ KCMDrawEventHandler::sMarkScreenOpacity = opacity; }
	virtual PMReal		GetSelectedMarkOpacity()	{ return KCMDrawEventHandler::SelectedMarkOpacity(); }
	virtual bool16		GetMarksTempHidden()	{ return KCMDrawEventHandler::sMarksTempHidden; }
	virtual void		SetMarksTempHidden(bool16 on)		{ KCMDrawEventHandler::sMarksTempHidden = on; }
	virtual bool16		GetSrcMarksPressed()	{ return KCMDrawEventHandler::sSrcMarksPressed; }
	virtual void		SetSrcMarksPressed(bool16 on)		{ KCMDrawEventHandler::sSrcMarksPressed = on; }
	virtual void		SetPeekOpacity(const PMReal& opacity)
														{ KCMDrawEventHandler::sPeekOpacity = opacity; }
	virtual bool16		GetShowOriginal()		{ return KCMDrawEventHandler::sShowOriginal; }
	virtual void		SetShowOriginal(bool16 on)			{ KCMDrawEventHandler::sShowOriginal = on; }

	// (ResetHideUnchanged / GetHideUnchangedDB / GetHideUnchangedSrcDB were here until 2026-08-17.
	//  The model-side functions they forwarded to are still in use -- from the model. See the interface.)
	virtual void		HideUnchangedToggle()	{ KCMHideUnchangedToggle(); }
	virtual bool16		GetHideUnchangedOn()	{ return KCMGetHideUnchangedOn(); }

	virtual bool16		IsDocDBOpen(IDataBase* db)	{ return KCMIsDocDBOpen(db); }
	virtual void		InvalidateDB(IDataBase* db)	{ KCMInvalidateDB(db); }
	virtual IDataBase*	GetActiveDocDB()		{ return KCMActiveDocDB(); }
	virtual bool16		IsAppQuitting()			{ return KCMAppIsQuitting(); }

	virtual bool16		GetIgnorePageNumberMarker()	{ return KCMGetIgnorePageNumberMarker(); }
	virtual void		SetIgnorePageNumberMarker(bool16 on)
													{ KCMSetIgnorePageNumberMarker(on); }

	virtual void		ExportChangedPagesTSV(PMString& outMessage)
													{ KCMExportChangedPagesTSV(outMessage); }
};

CREATE_PMINTERFACE(KCMCompareFacade, kKCMCompareFacadeImpl)


//========================================================================================
// KCMMarkData -- IKCMMarkData
//
// The read-only half. Every method answers a question about the state the drawing engine
// holds; not one of them changes it.
//
// ★These bodies are the very expressions the UI used to write inline. Deliberately so: the
// point of Task 12 is to move WHERE the question is asked, not WHAT the answer is. The two
// places that do a little more than a lookup (IsOverflowPage and HasAnyMarkableContent) call
// EnsureOverflowCache first because the callers did, in the same position.
//========================================================================================
class KCMMarkData : public CPMUnknown<IKCMMarkData>
{
public:
	KCMMarkData(IPMUnknown* boss) : CPMUnknown<IKCMMarkData>(boss) {}

	virtual IDataBase*	GetMarkedTargetDB()		{ return KCMDrawEventHandler::sDB; }
	virtual IDataBase*	GetMarkedSourceDB()		{ return KCMDrawEventHandler::sSrcDB; }

	virtual bool16		HasEntryForPage(UID pageUID)
	{
		return (KCMDrawEventHandler::sEntries.find(pageUID) !=
				KCMDrawEventHandler::sEntries.end()) ? kTrue : kFalse;
	}

	virtual bool16		IsSourcePageMarked(UID sourcePageUID)
	{
		return (KCMDrawEventHandler::sSrcPageToTarget.find(sourcePageUID) !=
				KCMDrawEventHandler::sSrcPageToTarget.end()) ? kTrue : kFalse;
	}

	virtual bool16		GetChangeCells(UID pageUID, int32& outChanged, int32& outTotal)
	{
		outChanged = 0;
		outTotal   = 0;
		std::map<UID, KCMOverlayEntry*>::const_iterator it = KCMDrawEventHandler::sEntries.find(pageUID);
		if (it == KCMDrawEventHandler::sEntries.end() || it->second == nil)
			return kFalse;
		outChanged = it->second->changedCells;
		outTotal   = it->second->w * it->second->h;	// the entry's image is the denominator
		return kTrue;
	}

	virtual bool16		IsOverflowPage(IDataBase* db, UID pageUID, bool16 isTargetSide)
	{
		KCMDrawEventHandler::EnsureOverflowCache();	// no-op when the cache already matches
		const bool16 cacheMatch = isTargetSide ? (KCMDrawEventHandler::sOverflowCacheDB == db)
											   : (KCMDrawEventHandler::sOverflowCacheSrcDB == db);
		if (!cacheMatch)
			return kFalse;
		const std::set<UID>& overflowSet = isTargetSide ? KCMDrawEventHandler::sOverflowT
													   : KCMDrawEventHandler::sOverflowS;
		return (overflowSet.find(pageUID) != overflowSet.end()) ? kTrue : kFalse;
	}

	virtual bool16		IsPageOnHiddenSpread(IDataBase* db, UID pageUID)
									{ return KCMIsPageOnHiddenSpread(db, pageUID); }

	virtual bool16		HasAnyMarkableContent()
	{
		KCMDrawEventHandler::EnsureOverflowCache();
		return (!KCMDrawEventHandler::sEntries.empty() ||
				!KCMDrawEventHandler::sOverflowT.empty() ||
				!KCMDrawEventHandler::sOverflowS.empty() ||
				(KCMDrawEventHandler::sDB    != nil && KCMPageMapHasAnyRegistered(KCMDrawEventHandler::sDB)) ||
				(KCMDrawEventHandler::sSrcDB != nil && KCMPageMapHasAnyRegistered(KCMDrawEventHandler::sSrcDB)))
			? kTrue : kFalse;
	}

	virtual bool16		GetOversetOn()			{ return KCMDrawEventHandler::sOversetOn; }
	virtual IDataBase*	GetOversetDB()			{ return KCMDrawEventHandler::sOversetDB; }

	virtual bool16		IsOversetPage(UID pageUID)
	{
		return (KCMDrawEventHandler::sOversetPages.find(pageUID) !=
				KCMDrawEventHandler::sOversetPages.end()) ? kTrue : kFalse;
	}

	virtual int32		GetOversetPageCount()	{ return (int32)KCMDrawEventHandler::sOversetPages.size(); }

	virtual void		GetOversetPageUIDs(std::vector<UID>& out)
	{
		out.assign(KCMDrawEventHandler::sOversetPages.begin(),
				   KCMDrawEventHandler::sOversetPages.end());
	}

	virtual void		GetOversetLocations(std::vector<KCMOversetLoc>& out)
	{
		out = KCMDrawEventHandler::sOversetLocs;
	}

	virtual void		GetRegisteredPages(IDataBase* db, std::set<UID>& out)
	{
		KCMPageMapCollectRegistered(db, out);
	}

	virtual void		GetPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
							std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
	{
		KCMBuildPairing(targetDB, sourceDB, outTargetPages, outSourcePages);
	}

	virtual void		GetMasterPagePairing(IDataBase* targetDB, IDataBase* sourceDB,
							std::vector<UID>& outTargetPages, std::vector<UID>& outSourcePages)
	{
		KCMBuildMasterPairing(targetDB, sourceDB, outTargetPages, outSourcePages);
	}

	virtual void		GetAllPageUIDs(IDataBase* db, std::vector<UID>& out)
													{ KCMCollectPageUIDs(db, out); }
	virtual void		GetMasterPageUIDs(IDataBase* db, std::vector<UID>& out)
													{ KCMCollectMasterPageUIDs(db, out); }
	virtual bool16		GetMarkablePageUIDs(IDataBase* db, std::set<UID>& outPages)
													{ return KCMCollectChangedPageUIDs(db, outPages); }
	virtual UID			GetFramePageUID(IDataBase* db, UID frameUID)
													{ return KCMFramePageUID(db, frameUID); }
};

CREATE_PMINTERFACE(KCMMarkData, kKCMMarkDataImpl)


//========================================================================================
// KCMPageFlagsFacade -- IKCMPageFlagsFacade
//
// The writing half of the two per-page flags. Six forwarders, no logic: which pages are
// selected, what the menu label should say, where the JSON file goes -- all of that already
// lives in KCMPageMap.cpp / KCMPageCheck.cpp and stays there.
//========================================================================================
class KCMPageFlagsFacade : public CPMUnknown<IKCMPageFlagsFacade>
{
public:
	KCMPageFlagsFacade(IPMUnknown* boss) : CPMUnknown<IKCMPageFlagsFacade>(boss) {}

	virtual void	ToggleRegisterForSelection()	{ KCMPageMapToggleSelectedPages(); }
	virtual void	ToggleCheckForSelection()		{ KCMPageCheckToggleSelectedPages(); }

	virtual KCMPageToggleState	GetRegisterToggleState()	{ return KCMPageMapGetToggleState(); }
	virtual KCMPageToggleState	GetCheckToggleState()		{ return KCMPageCheckGetToggleState(); }

	virtual void	SaveChecksAndRegister()			{ KCMPageCheckSaveToFile(); }
	virtual void	LoadChecksAndRegister()			{ KCMPageCheckLoadFromFile(); }
};

CREATE_PMINTERFACE(KCMPageFlagsFacade, kKCMPageFlagsFacadeImpl)


//========================================================================================
// KCMStoryEditsFacade -- IKCMStoryEditsFacade
//
// The read side of the Story Edits list, plus the two "where does this story begin" questions
// the navigation asks of whichever document it is about to scroll.
//
// ★NO Build/Clear/ShutdownCleanup. The callers were grepped before this class was written and
// all of them are model-side (KCMCore.cpp builds and clears, KCMPeek.cpp clears and empties
// at shutdown), so the plan's draft Rebuild() would have been a method nobody calls.
//========================================================================================
class KCMStoryEditsFacade : public CPMUnknown<IKCMStoryEditsFacade>
{
public:
	KCMStoryEditsFacade(IPMUnknown* boss) : CPMUnknown<IKCMStoryEditsFacade>(boss) {}

	virtual int32	GetRowCount()	{ return KCMStoryList::GetRowCount(); }

	virtual bool16	GetRow(int32 nth, Row& out)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(nth);
		if (row == nil)
			return kFalse;	// out of range, or the placeholder row -- out is left as the caller had it

		// Six of the row's seven fields. fPageIndex is the list's sort key and no caller reads it.
		out.fStoryUID	= row->fStoryUID;
		out.fText		= row->fText;
		out.fKinds		= row->fKinds;
		out.fFrameUID	= row->fFrameUID;
		out.fPageUID	= row->fPageUID;
		out.fTextCompared = row->fTextCompared;
		out.fAttrKind	= static_cast<int32>(row->fAttrKind);	// 0 = none, 1 = ruby
		return kTrue;
	}

	virtual int32	GetChangeCount(int32 nth)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(nth);
		return (row != nil) ? static_cast<int32>(row->fChanges.size()) : 0;
	}

	virtual bool16	GetChange(int32 nth, int32 which, Change& out)
	{
		const KCMStoryRow* row = KCMStoryList::GetRow(nth);
		if (row == nil || which < 0 || which >= static_cast<int32>(row->fChanges.size()))
			return kFalse;

		const KCMStoryChange& change = row->fChanges[which];
		out.fKind		= static_cast<int32>(change.fKind);
		out.fWhat		= static_cast<int32>(change.fWhat);
		out.fTargetStart = change.fTargetStart;
		out.fTargetEnd	= change.fTargetEnd;
		out.fSourceStart = change.fSourceStart;
		out.fSourceEnd	= change.fSourceEnd;
		out.fTextPre	= change.fTextPre;
		out.fText		= change.fText;
		out.fTextPost	= change.fTextPost;
		out.fOtherTextPre	= change.fOtherTextPre;
		out.fOtherText		= change.fOtherText;
		out.fOtherTextPost	= change.fOtherTextPost;
		out.fRuby			= change.fRuby;			// only meaningful when fWhat is kAttr
		out.fOtherRuby		= change.fOtherRuby;
		out.fAttrKind		= static_cast<int32>(change.fAttrKind);
		return kTrue;
	}

	virtual int32	GetChangeAttrKind(int32 nth, int32 which)
	{
		// Out of range answers "no attribute" rather than failing: the caller is the tree asking how
		// tall a row is, and a row it cannot identify gets the ordinary height - the same shape the
		// list has had all along. (GetChange returns kFalse for this case because its caller is
		// about to DRAW the change and must not draw a stale one.)
		const KCMStoryRow* row = KCMStoryList::GetRow(nth);
		if (row == nil || which < 0 || which >= static_cast<int32>(row->fChanges.size()))
			return static_cast<int32>(kKCMStoryAttrNone);

		return static_cast<int32>(row->fChanges[which].fAttrKind);
	}

	virtual int32	RefreshRow(int32 nth)
	{
		// The two documents the comparison is holding. ★ASKED FOR AGAIN RATHER THAN REMEMBERED:
		// the panel can only reach this while a comparison is armed, but "armed" and "still open"
		// are different questions and the second one is the one that matters here.
		IDataBase* const targetDB = KCMArmedTargetDB();
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (targetDB == nil || sourceDB == nil)
			return -1;
		if (!KCMIsDocDBOpen(targetDB) || !KCMIsDocDBOpen(sourceDB))
			return -1;

		const int32 count = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

		// ★NOTHING IS SAID WHEN NOTHING CHANGED. The notification makes the panel rebuild the
		//   whole tree, which costs the reader their selection - so a refresh that could not be
		//   done leaves the list alone rather than shaking it for no result.
		if (count >= 0)
			KCMNotify(kKCMStoryEditsRebuiltMessage);

		return count;
	}

	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID)
					{ return KCMStoryFirstFrameUID(db, storyUID); }

	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
					{ return KCMStoryStartPoint(db, storyUID, outFrame, outPb); }
	virtual bool16	GetStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb)
					{ return KCMStoryPointAt(db, storyUID, index, outPb); }
	virtual UID		GetStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index)
					{ return KCMStoryFrameAt(db, storyUID, index); }
};

CREATE_PMINTERFACE(KCMStoryEditsFacade, kKCMStoryEditsFacadeImpl)


//========================================================================================
// KCMBookFacade -- IKCMBookFacade
//
// Book comparison: the fifth and last boundary of Stage 1. Three forwarders.
//
// ★What is NOT here was decided by grepping callers, not by the plan: KCMGetBookResultText
// is read only by KCMScriptProvider (model-side), KCMBuildChapterPairing only by
// KCMCompareBooks, and KCMElidePathFront moved to the UI in this same task.
//========================================================================================
class KCMBookFacade : public CPMUnknown<IKCMBookFacade>
{
public:
	KCMBookFacade(IPMUnknown* boss) : CPMUnknown<IKCMBookFacade>(boss) {}

	virtual bool16		ResolveBookPair(const IDFile& panelBookFile,
								IBook*& outTarget, IBook*& outSource)
						{ return KCMResolveBookPair(panelBookFile, outTarget, outSource); }

	virtual PMString	GetBookDisplayPath(IBook* book)
						{ return KCMBookDisplayPath(book); }

	virtual ErrorCode	CompareBooks(IBook* target, IBook* source,
							std::vector<KCMChapterResult>& outChapters, PMString& outReport)
						{ return KCMCompareBooks(target, source, outChapters, outReport); }
};

CREATE_PMINTERFACE(KCMBookFacade, kKCMBookFacadeImpl)

//========================================================================================
//  IKCMStoryMarkFacade -- putting the Story mode's marks up and taking them down.
//
//  ★★THE SIXTH BOUNDARY, AND THE FIRST ADDED AFTER THE SPLIT ITSELF (2026-08-23). The other five
//  were drawn when model and UI were separated; this one appeared when a thing that had been on
//  the UI side moved over - the global text adornment that draws the Story mode's marks. It had to
//  move because **the UI's File > Export > PDF runs in the background and never hands a kUIPlugIn
//  any drawing**, so marks living there could not reach an exported PDF at all.
//
//  ★EVERY METHOD IS ONE LINE, WHICH IS THE POINT. The facade is a door, not a place where things
//  are decided: what should be lit is worked out in KCMStoryMarkBuild and drawn in
//  KCMStoryMarker, both of which the UI has no business knowing about.
//========================================================================================

class KCMStoryMarkFacade : public CPMUnknown<IKCMStoryMarkFacade>
{
public:
	KCMStoryMarkFacade(IPMUnknown* boss) : CPMUnknown<IKCMStoryMarkFacade>(boss) {}

	virtual void	Refresh()					{ KCMStoryMarkRefresh(); }

	virtual void	SetPress(bool16 active, bool16 useSourceDocument)
					{ KCMStoryMarkSetPress(active, useSourceDocument); }

	virtual void	ShowJumpFlash(IDataBase* db, UID storyUID,
								  TextIndex from, TextIndex to,
								  TextIndex sourceFrom, TextIndex sourceTo);

	virtual void	ClearJumpFlash()			{ KCMStoryMarker::ClearFlash(); }
	// ⚠No ShutdownMarks: teardown is model-side only and KCMPeek.cpp calls the marker directly
	//   (IKCMStoryMarkFacade.h says why a boundary method with no caller is worse than none).
};

CREATE_PMINTERFACE(KCMStoryMarkFacade, kKCMStoryMarkFacadeImpl)

void KCMStoryMarkFacade::ShowJumpFlash(IDataBase* db, UID storyUID,
										 TextIndex from, TextIndex to,
										 TextIndex sourceFrom, TextIndex sourceTo)
{
	KCMStoryMarkDocs flash;
	KCMStoryMarker::AddFlashRange(flash, db, storyUID, from, to);

	// ★THE SAME STORY UID, IN THE OTHER DOCUMENT - which is what the whole Story mode is built on:
	//   the diff pairs stories by uid, the double click selects in both by uid, and the standing
	//   marks light both by uid (KCMStoryMarkBuild). ⚠The general rule that a uid names a
	//   different object in another document is TRUE and does not apply here, and believing it did
	//   is what left the older window nearly bare until 2026-08-22 (bug A6).
	// ⚠`db` IS NOT ALWAYS THE TARGET: a Removed story is read out of the older document, and then
	//   this test is what stops the same document being marked twice (which would invert twice and
	//   leave a hole - KCMStoryMarkRanges.h).
	// ★THE TEST LIVES HERE RATHER THAN AT THE CALLER because "is the older window open" is a fact
	//   about the comparison, and the comparison is this side's ([[one-question-one-place]]).
	IDataBase* const flashSourceDB = KCMArmedSourceDB();
	if (flashSourceDB != nil && flashSourceDB != db && KCMIsDocDBOpen(flashSourceDB))
		KCMStoryMarker::AddFlashRange(flash, flashSourceDB, storyUID, sourceFrom, sourceTo);

	KCMStoryMarker::ShowFlash(flash);
}

// End of KCMFacades.cpp.
