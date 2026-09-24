//========================================================================================
//
//  KCMFacades.cpp
//
//  The boundary interfaces the UI is allowed to use, implemented as thin forwarders to the
//  model's internal functions.
//
//  These bodies deliberately contain no logic. Every one of them forwards to a function that
//  already existed and already worked; the point of this file is to give those functions an
//  address the other plug-in can reach. Putting logic here would mean the same decision lived
//  in two places.
//
//  ONE BODY IS NOT A PURE FORWARD: IKCMStoryEditsFacade::GetRow copies the row's fields out one
//  by one. That is a change of ownership rather than a decision -- the model hands out a pointer
//  into a list it can rebuild, which is safe only while caller and list are in the same plug-in
//  (see the interface header).
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
#include "KCMPairChoice.h"		// the chosen pair: set it, read it, clear it
#include "KCMTaskStartSave.h"	// Task Start: save a copy, choose the file as the Source
#include "KCMCore.h"				// MarkChanges / ClearMarks / DoSetPrintMarks / getters
#include "KCMPeek.h"				// armed docs alive / peek / RefreshSelectedPages / base opacity
#include "KCMColorSampler.h"		// the Alt+left CMYK sample and its drag-time pairing cache
#include "KCMModelNotify.h"		// GetSessionStatus
#include "KCMHideUnchanged.h"		// the Hide Unchanged toggle and its state
#include "KCMDrawEventHandler.h"	// the engine's shared state, which these two publish
#include "KCMPageMap.h"			// registered pages, the page pairing, and the Register toggle
#include "KCMPageCheck.h"
#include "KCMPageMarksDoc.h"	// the marks the document itself carries (the two below)			// the Check toggle and the Save/Load of both flags
#include "KCMPawStamp.h"			// the cat-paw stamps (place / lift / count / the one size)
#include "KCMStoryList.h"			// the Story Edits rows, and where a story begins in a document
#include "KCMStoryDiffRun.h"		// RunOne - re-comparing one row's story ("Refresh Story Comparison")
#include "KCMOversetPoint.h"		// KCMFindOversetOutport - where the "+" of an overflow is
#include "ITextModel.h"			// the story the two above are asked about
#include "KCMStoryTextExport.h"	// KCMExportStoryText - "Export Story Text..." on the flyout
#include "KCMStoryTextImport.h"	// KCMImportStoryText - "Import Story Text..." on the flyout
#include "KCMRejectImport.h"		// "Reject This Import Change" on a change row (2026-09-24, stage 2 A)
#include "KCMRestoreAttr.h"		// "Restore from Source" on an attribute change row (2026-09-24, stage 2 B); KCMAttrMarksSame
#include "KCMTextWords.h"		// WordsAt - the take-back's "all the way back, or not at all" reads the words (2026-09-24 night)
#include "ErrorUtils.h"			// the error state a plain sequence is rolled back by (EndSequenceOrRollBack)
#include "IDataBase.h"			// SaveRestoreModifiedState - reading the two stories must not dirty them
#include "WideString.h"
#include "KCMRedoFromWord.h"		// "Redo from Word" on a change the reader took back (2026-09-24, stage 2 C)
#include "KCMTableMatch.h"		// "Match the Source" on a Table row (2026-09-25, design section 16)
#include "CmdUtils.h"				// ...wrapped in one command sequence
#include "ICommandSequence.h"		// ICommandSequence - a plain sequence (RejectImportChange says why not an abortable one)
#include "KCMBookPair.h"			// which two books, and their display paths
#include "KCMBookCompare.h"		// the book comparison itself
#include "KCMPageNumberMarker.h"	// the folio exclusion toggle
#include "KCMReport.h"			// the Before/After report
#include "KCMRingAdornment.h"	// the story ID labels' toggle (Get/SetShowStoryIds)
#include "KCMExternalSource.h"	// KCMExternalSourceLabel -- the lent Source's words for the panel
#include "KCMStoryMarkBuild.h"	// what the Story mode should be lighting up (Refresh / SetPress)
#include "KCMStoryMarker.h"		// the adornment that draws it - the flash and the shutdown
#include "IKCMResourcesFacade.h"	// the Resources mode's boundary
#include "KCMResourceStore.h"		// ...and the model side it forwards to

//========================================================================================
// KCMCompareFacade -- IKCMCompareFacade
//
// WHAT BELONGS ON THIS INTERFACE WAS DECIDED BY GREPPING FOR CALLERS, not by a design list.
// HideUnchangedToggle / GetHideUnchangedOn / GetOversetScanTargetDB are called from
// KCMActionComponent.cpp; ArmedDocsAlive / ShowPeekAt / GetBaseScreenOpacity from
// KCMPeekGesture.cpp, KCMCmykCursor.cpp and KCMActionComponent.cpp; SampleColorAt /
// BeginColorDrag / EndColorDrag from KCMCmykCursor.cpp, which was including KCMColorSampler.h
// (model) and calling its free functions directly. Every one of those files is UI-side, and every
// one of those calls would fail to link without a method here.
// Two greps are needed to find them all: one for facade callers, one for cross-side INCLUDES.
// The second kind hides from the first.
//========================================================================================
class KCMCompareFacade : public CPMUnknown<IKCMCompareFacade>
{
public:
	KCMCompareFacade(IPMUnknown* boss) : CPMUnknown<IKCMCompareFacade>(boss) {}

	virtual void		ToggleStartStop()		{ KCMToggleStartStop(); }
	virtual void		StopComparison()		{ KCMStopComparison(); }
	virtual void		StartComparisonFor(IDocument* target, IDocument* source)
													{ KCMStartComparisonFor(target, source); }
	virtual void		RefreshComparison()		{ KCMRefreshComparison(); }
	virtual bool16		CanStartComparison()	{ return KCMCanStartComparison(); }

	// The chosen Target/Source. The setters resolve "the active document" on this side; see the
	// reason on the interface.
	virtual bool16		SetChosenTargetToActive()	{ return KCMSetChosenTargetToActive(); }
	virtual bool16		SetChosenSourceToActive()	{ return KCMSetChosenSourceToActive(); }
	virtual IDataBase*	GetChosenTargetDB()		{ return KCMChosenTargetDB(); }
	virtual IDataBase*	GetChosenSourceDB()		{ return KCMChosenSourceDB(); }
	// ★Declared at the END of the interface (its vtable is an ABI shared with Kohaku InDesign
	//   MCP), but kept here beside the other chosen-pair members, where it reads. The order of
	//   the overrides in this class has no bearing on the vtable.
	virtual void		ClearChosenDocs()		{ KCMClearChosenDocs(); }

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
												 PMString& outRuby, int32& outAttrKind)
							{ KCMGetSessionStatusSegments(outLabel, outPre, outMid, outPost, outRuby, outAttrKind); }

	// ---- the status line ------------------------------------------------------------------
	// Free functions from KCMModelNotify.h. The panel's status writer and the UI shutdown reach
	// them -- a legal direction (UI -> model) but not one that links across two .pln. Same shape
	// as the CMYK three below.
	// The notification's payload does NOT come through here: it rides on Change()'s changedBy
	// (KCMNotifyPayload), so nothing has to come back and ask what a notification was about.
	virtual void		StoreSessionStatus(const PMString& s)	{ KCMStoreSessionStatus(s); }
	virtual void		StoreSessionStatusSegments(const PMString& label, const PMString& pre,
												   const PMString& mid, const PMString& post,
												   const PMString& ruby, int32 attrKind)
							{ KCMStoreSessionStatusSegments(label, pre, mid, post, ruby, attrKind); }
	virtual void		ClearSessionStatus()	{ KCMClearSessionStatus(); }

	virtual bool16		ArmedDocsAlive()		{ return KCMArmedDocsAlive(); }
	virtual void		ShowPeekAt(IDataBase* targetDB, IDataBase* sourceDB,
								   const PMReal& mx, const PMReal& my,
								   const PMReal& viewScale, const PMReal& uiZoom,
								   UID viewSpreadUID)
													{ KCMPeekShowAt(targetDB, sourceDB, mx, my, viewScale, uiZoom, viewSpreadUID); }
	virtual PMReal		GetBaseScreenOpacity()	{ return KCMBaseScreenOpacity(); }

	// ---- the CMYK sampler -------------------------------------------------------------------
	// KCMCmykCursor.cpp (UI) used to call the free functions in KCMColorSampler.h (model)
	// directly -- legal while both sit in one .pln, but a free function cannot be linked across
	// two, so the three come through the boundary here.
	virtual bool16		SampleColorAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
									  const PMReal& mx, const PMReal& my,
									  UID viewSpreadUID,
									  PMString& outPanel, PMString& outCursor)
													{ return KCMSampleCmykAt(hoverDB, otherDB, hoverIsTarget, mx, my, viewSpreadUID, outPanel, outCursor); }
	virtual void		BeginColorDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget)
													{ KCMSampleCmykBeginDrag(hoverDB, otherDB, hoverIsTarget); }
	virtual void		EndColorDrag()			{ KCMSampleCmykEndDrag(); }

	// (ApplyOversetForDoc / GetOversetScanTargetDB / ClearOverset went with Find Overset,
	//  2026-09-08.)

	// ---- display toggles and press-time display state ---------------------------------------
	// These reach the engine's static members rather than a free function, because there is no
	// function: the UI used to assign to the statics itself, and the bodies here are those very
	// assignments, moved behind the boundary and nothing else.
	// @warning none of them redraws. Invalidation stays with the callers, where the choice of
	// which document to repaint is made -- Target only, Source only, or both, depending on the
	// gesture.

	virtual bool16		GetShowSourceMarks()	{ return KCMDrawEventHandler::sSrcMarksOn; }
	virtual void		SetShowSourceMarks(bool16 on)		{ KCMDrawEventHandler::sSrcMarksOn = on; }
	virtual bool16		GetShowTargetMarks()	{ return KCMDrawEventHandler::sTgtMarksOn; }
	virtual void		SetShowTargetMarks(bool16 on)		{ KCMDrawEventHandler::sTgtMarksOn = on; }
	virtual bool16		GetShowOldPageNumbers()	{ return KCMDrawEventHandler::sShowOldNumbers; }
	virtual void		SetShowOldPageNumbers(bool16 on)	{ KCMDrawEventHandler::sShowOldNumbers = on; }

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

	// Only the toggle and its state cross the boundary. Resetting Hide Unchanged is model-side
	// work with model-side callers, so it has no method here (see the interface).
	virtual void		HideUnchangedToggle()	{ KCMHideUnchangedToggle(); }
	virtual bool16		GetHideUnchangedOn()	{ return KCMGetHideUnchangedOn(); }

	virtual bool16		IsDocDBOpen(IDataBase* db)	{ return KCMIsDocDBOpen(db); }
	virtual void		InvalidateDB(IDataBase* db)	{ KCMInvalidateDB(db); }
	virtual IDataBase*	GetActiveDocDB()		{ return KCMActiveDocDB(); }
	virtual bool16		IsAppQuitting()			{ return KCMAppIsQuitting(); }

	virtual bool16		GetIgnorePageNumberMarker()	{ return KCMGetIgnorePageNumberMarker(); }
	virtual void		SetIgnorePageNumberMarker(bool16 on)
													{ KCMSetIgnorePageNumberMarker(on); }

	virtual bool16		GetPairPagesByUid()			{ return KCMGetPairPagesByUid(); }
	virtual void		SetPairPagesByUid(bool16 on)	{ KCMSetPairPagesByUid(on); }

	virtual bool16		ExportBeforeAfterReport(PMString& outMessage)
													{ return KCMExportBeforeAfterReport(outMessage); }

	virtual bool16		GetShowStoryIds()			{ return KCMGetShowStoryIds(); }
	virtual void		SetShowStoryIds(bool16 on)	{ KCMSetShowStoryIds(on); }

	// ★Appended 2026-09-20 (the ABI stamp went with it). Making the copy is the model's work - the
	//   UI half could not reach ImportINX at all - so this is the usual one-liner.
	// ⛔**HOLLOW SINCE 2026-09-21**: the origin went, and with it the only thing this could open.
	//   The SLOT stays - KIDMCP calls this facade through its vtable, so removing a virtual moves
	//   every one below it onto the wrong method ([[facade-vtable-slot-append-only]]).
	virtual bool16		OpenOriginAsIdml(PMString& outMessage)	{ outMessage.Clear(); return kFalse; }


	// The lent Source (see the interface). Three one-line transfers; the rules are model-side.
	virtual void		StartComparisonWithSourceDB(IDocument* target, IDataBase* sourceDB, const PMString& sourceLabel)
													{ KCMStartComparisonWithSourceDB(target, sourceDB, sourceLabel); }
	virtual void		ReleaseExternalSourceDB(IDataBase* sourceDB)	{ KCMReleaseExternalSource(sourceDB); }
	virtual bool16		GetExternalSourceLabel(IDataBase* db, PMString& outLabel)
													{ return KCMExternalSourceLabel(db, outLabel); }

	// ⛔**THE OLD TASK START'S FIVE SLOTS, HOLLOW SINCE 2026-09-21.** Task Start saves a copy of
	//   the document to a FILE and chooses that file as the Source (KCMTaskStartSave.h); nothing
	//   holds an "origin" any more, so all five answer as though none were ever taken - which is
	//   exactly what every caller then does with them.
	//   ⚠**THE SLOTS STAY.** KIDMCP calls this facade through its vtable, so deleting a virtual
	//   moves every one below it onto the wrong method ([[facade-vtable-slot-append-only]]).
	//   ★The live pair is CanTakeTaskStartCopy / TakeTaskStartCopy, appended at the end.
	virtual bool16		CanTakeTaskStart()					{ return kFalse; }
	virtual bool16		TakeTaskStart(PMString& outWhyNot)	{ outWhyNot.Clear(); return kFalse; }
	virtual bool16		HasOrigin()							{ return kFalse; }
	virtual void		GetOriginLabel(PMString& outLabel)	{ outLabel.Clear(); }
	virtual bool16		IsOriginArmed()						{ return kFalse; }

	// The warning bit that rides with a status notification (2026-09-21). Asking clears it.
	virtual bool16		TakeStatusWarning()					{ return KCMTakeSessionStatusWarning(); }

	// A file choice's path, for the panel's Target:/Source: lines (2026-09-21). Transfers.
	virtual void		GetChosenSourceFileLabel(PMString& outLabel)	{ KCMChosenSourceFileLabel(outLabel); }
	virtual void		GetChosenTargetFileLabel(PMString& outLabel)	{ KCMChosenTargetFileLabel(outLabel); }

	// Task Start, the file way (2026-09-21). Transfers; the rules are model-side.
	virtual bool16		CanTakeTaskStartCopy()					{ return KCMCanTakeTaskStartCopy(); }
	virtual bool16		TakeTaskStartCopy(PMString& outWhyNot)	{ return KCMTakeTaskStartCopy(outWhyNot); }
};

CREATE_PMINTERFACE(KCMCompareFacade, kKCMCompareFacadeImpl)

/** The facade's ABI stamp, exported from the .pln by name so that another plug-in can check it
	WITHOUT going through the facade's vtable (IKCMCompareFacade.h, kKCMCompareFacadeAbi says why
	and who reads it). extern "C": no decoration, so GetProcAddress finds it under this very name. */
extern "C" __declspec(dllexport) int32 KCMCompareFacadeAbi()
{
	return kKCMCompareFacadeAbi;
}


//========================================================================================
// KCMMarkData -- IKCMMarkData
//
// The read-only half. Every method answers a question about the state the drawing engine
// holds; not one of them changes it.
//
// These bodies are the very expressions the UI used to write inline, deliberately so: what moved
// is WHERE the question is asked, not WHAT the answer is. The two places that do a little more
// than a lookup (IsOverflowPage and HasAnyMarkableContent) call EnsureOverflowCache first because
// the callers did, in the same position.
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

	// (The six overset readers went with the feature, 2026-09-08.)

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
// The writing half of the per-page flags. Forwarders, almost no logic: which pages are selected,
// what the menu label should say, where the JSON file goes -- all of that already lives in
// KCMPageMap.cpp / KCMPageCheck.cpp / KCMPawStamp.cpp and stays there.
//========================================================================================

// "cleared chk3" / "cleared paw0" for the status line. The two clear items say the same thing
// about different marks, so the lines that build it are written once.
// ⚠**SetTranslatable(kFalse) is what keeps the reader from being shown the key itself** -- the
//   reason every literal put on this line needs the mark is with KCMNotifyStatus in
//   KCMModelNotify.h.
static void KCMSayCleared(const char* what, int32 n)
{
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(what);
	msg.AppendNumber(n);
	KCMNotifyStatus(msg, kTrue /*forceRedrawNow*/);
}

class KCMPageFlagsFacade : public CPMUnknown<IKCMPageFlagsFacade>
{
public:
	KCMPageFlagsFacade(IPMUnknown* boss) : CPMUnknown<IKCMPageFlagsFacade>(boss) {}

	virtual void	ToggleRegisterForSelection()	{ KCMPageMapToggleSelectedPages(); }
	virtual void	ToggleCheckForSelection()		{ KCMPageCheckToggleSelectedPages(); }

	virtual KCMPageToggleState	GetRegisterToggleState()	{ return KCMPageMapGetToggleState(); }
	virtual KCMPageToggleState	GetCheckToggleState()		{ return KCMPageCheckGetToggleState(); }

	// (SaveChecksAndRegister / LoadChecksAndRegister went on 2026-09-07 -- IKCMPageFlagsFacade.h
	//  says why. Their model-side bodies went with them.)
	// ★SaveMarksToDocument was removed on 2026-09-07. Writing is no longer something the reader
	//   asks for separately: a tick or a paw goes into the document the moment it is made, and
	//   comes back out again with Ctrl+Z. There is nothing left for a "save" to do.

	// The cat-paw stamps. The crossing exists because model and UI are two DLLs: the tool lives
	// on the UI side and the store on this one.
	virtual bool16	PawStampPlaceAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                                int32 colour, const PMReal& baseHalf, const PMString& text)
									{ return KCMPawStampPlaceAt(db, pageUID, x, y, colour, baseHalf, text); }
	virtual bool16	PawStampLiftAt(IDataBase* db, UID pageUID, const PMReal& x, const PMReal& y,
	                               const PMReal& baseHalf)
									{ return KCMPawStampLiftAt(db, pageUID, x, y, baseHalf); }
	virtual int32	PawStampCount(IDataBase* db)	{ return KCMPawStampCount(db); }
	virtual int32	PawStampClearPage(IDataBase* db, UID pageUID)
									{ return KCMPawStampClearPage(db, pageUID); }
	virtual PMReal	PawHalfSizeForPage(IDataBase* db, UID pageUID)
									{ return KCMPawHalfSizeForPage(db, pageUID); }

	// Clearing one document's marks (the two flyout items). **The status line is written here**,
	// on the model side, the way Save and Load write theirs -- the UI's action component has no
	// call site of its own for the status line and should not grow one.
	virtual bool16	PageCheckHasAny(IDataBase* db)	{ return KCMPageCheckHasAny(db); }
	virtual int32	ClearChecksInDoc(IDataBase* db)
					{
						const int32 n = KCMPageCheckClearDoc(db);
						KCMSayCleared("cleared chk", n);
						return n;
					}
	virtual int32	ClearPawsInDoc(IDataBase* db)
					{
						// ★The count is read FIRST: KCMPawStampClearDoc answers nothing, and once it
						//   has run there is nothing left to count. (Its tick counterpart returns the
						//   number itself, because it has to read the page set before clearing
						//   anyway -- the write needs that set.)
						const int32 n = KCMPawStampCount(db);
						if (n > 0)
							KCMPawStampClearDoc(db);	// writes the document; the observer redraws
						KCMSayCleared("cleared paw", n);
						return n;
					}
};

CREATE_PMINTERFACE(KCMPageFlagsFacade, kKCMPageFlagsFacadeImpl)


//========================================================================================
// KCMStoryEditsFacade -- IKCMStoryEditsFacade
//
// The read side of the Story Edits list, plus the two "where does this story begin" questions
// the navigation asks of whichever document it is about to scroll.
//
// NO Build/Clear/ShutdownCleanup. Every caller of those is model-side (KCMCore.cpp builds and
// clears, KCMPeek.cpp clears and empties at shutdown), so a Rebuild() here would be a method
// nobody calls.
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

		// Eight of the row's ten fields. fPageIndex is the list's sort key and no caller reads
		// it; fChanges is the child list, handed over one at a time by GetChange.
		out.fStoryUID	= row->fStoryUID;
		out.fText		= row->fText;
		out.fKinds		= row->fKinds;
		out.fFrameUID	= row->fFrameUID;
		out.fPageUID	= row->fPageUID;
		out.fTextCompared = row->fTextCompared;
		out.fAttrKind	= static_cast<int32>(row->fAttrKind);	// 0 = none, 1 = ruby, 2 = kenten, 3 = footnote, 4 = endnote, 5 = warichu, 6 = tate-chu-yoko
		out.fAttrKindCount = row->fAttrKindCount;				// how many DIFFERENT kinds - "Ruby+" when > 1
		out.fHasTextChange = row->fHasTextChange;				// what the DIFF found, not what the counters said
		return kTrue;
	}

	// (⛔**THE WHOLE OF THIS QUESTION WENT ON 2026-09-21** with the restore: whether a change the
	//  reader had taken in was still standing as taken in. It was asked of the DOCUMENT, never of a
	//  flag - the story counter now against the counter recorded at the write - so a row stopped
	//  being drawn as taken in the moment Ctrl+Z was pressed, with no undo-specific code anywhere
	//  ([[command-history-and-undo-stack]]). It moved to KCMStoryDiffRun on 2026-09-16 because the
	//  WRITING side had to ask exactly the same question, and the two had disagreed
	//  ([[one-question-one-place]]).)

	virtual int32	GetChangeCount(int32 nth)
	{
		// The refusals an import left, then the live diff's changes - one index space, defined in
		// KCMStoryList and asked for the same way by every question below.
		// (⛔A third list, the changes the reader had taken in, was in it until 2026-09-21.)
		return KCMStoryList::GetMergedChangeCount(nth);
	}

	virtual bool16	GetChange(int32 nth, int32 which, Change& out)
	{
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return kFalse;

		const KCMStoryChange& change = *found;
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
		out.fRubyGroup		= change.fRubyGroup;	// how it is SET - the readings alone cannot say
		out.fOtherRubyGroup	= change.fOtherRubyGroup;
		out.fAttrKind		= static_cast<int32>(change.fAttrKind);
		out.fLayers			= change.fLayers;		// a warichu / tate-chu-yoko change, line by line
		out.fOtherLayers	= change.fOtherLayers;	// (traded below once a tate-chu-yoko or warichu is taken in)

		// (⛔**THE TWO FACES OF A ROW WENT ON 2026-09-21.** A change the reader had taken in carried the
		//  words as they stood now AND as they stood before, and the model - not the panel - chose
		//  which of them belonged in fText*, by asking the document's own counter. It traded the two
		//  sides' ruby and the two sides' layers over as well, so that a taken-in row described the
		//  story rather than the comparison. Nothing is taken in now: a row has one face.)
		out.fOverset		= change.fOverset;		// decided by the diff; see KCMStoryList.h
		out.fWholeParagraph		= change.fWholeParagraph;		// a paragraph added or removed whole
		out.fPlace				= change.fPlace;				// the body, a cell or a note - the ID column's word
		out.fWholeCell			= kFalse;						// retired the night it was made (2026-09-19): a table's cells fold into a Table row now; the field keeps the layout
		out.fMarkSpanCount		= static_cast<int32>(change.fMarkSpans.size());	// the cells a Table change marks (GetChangeMarkSpan)
		// ★★fReplaced IS BACK IN USE, WITH THE OPPOSITE MEANING (2026-09-24, stage 2 C): kTrue for a change the reader
		//   TOOK BACK and still standing so - the "=" row, the Source's state - which is what its menu, its sign and
		//   the marks ask. (From 2026-09-15 to 2026-09-21 it said "taken IN".) The layout is unchanged.
		{
			const KCMRejectedRecord* const rec = KCMStoryList::RejectedAt(nth, which);
			out.fReplaced = (rec != nil && KCMStoryList::RejectedStateOf(nth, *rec, KCMArmedTargetDB()) == kKCMRejectedStanding)
				? kTrue : kFalse;
		}
		out.fWriteBlock		= 0;							// ⛔the same, for the reason above: nothing writes
		out.fAfterNewParagraph	= kFalse;					// ⛔the same


		// ★★**A WHOLE PARAGRAPH IS HANDED OUT AS ITS WORDS, WITHOUT THE BREAK** (2026-09-19, the user:
		//   "the mark reaches the end of the paragraph above - I want that gone"). The model's ranges
		//   carry the return of the paragraph before (or the paragraph's own) because the WRITE needs
		//   it (KCMStoryList.h, fWholeParagraph); shown as they stand, the standing mark, the jump's
		//   flash and the double click's selection all began on that return - at the END OF THE
		//   PARAGRAPH ABOVE. This is the ONE place the UI and the marks read a change's ranges from, so
		//   the break is cut off here and nothing over there has to know it was ever in the range.
		//   ★All four pairs, because all four are shown: the target's (whichever of the three the
		//     branches above chose - a replaced paragraph's range holds the break it wrote), and the
		//     source's. A caret is left as it is (KCMShownSpan).
		KCMShownSpan(change.fBreakAt, out.fTargetStart, out.fTargetEnd);
		KCMShownSpan(change.fBreakAt, out.fSourceStart, out.fSourceEnd);
		return kTrue;
	}

	virtual int32	GetChangeAttrKind(int32 nth, int32 which)
	{
		// Out of range answers "no attribute" rather than failing: the caller is the tree asking how
		// tall a row is, and a row it cannot identify gets the ordinary height - the same shape the
		// list has had all along. (GetChange returns kFalse for this case because its caller is
		// about to DRAW the change and must not draw a stale one.)
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return static_cast<int32>(kKCMStoryAttrNone);

		return static_cast<int32>(found->fAttrKind);
	}

	virtual bool16	GetChangeHasAttrValue(int32 nth, int32 which)
	{
		// Same out-of-range rule as the kind above, and for the same caller: an unknown row gets
		// the ordinary one-line height rather than an error.
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return kFalse;

		// ⚠THE SIDE THE ROW SHOWS, which is fRuby - not fOtherRuby. The list shows the newer
		//   version, so an attribute that was removed leaves this empty and the row is drawn on one
		//   line; the older side's value is still read, but it belongs to the message area.
		// ★A REPLACED RUBY ROW IS STILL A TWO-LINE ROW: its reading is the one that went in, which
		//   is the source's, and fRuby was copied from the change it was made out of. Asking the
		//   same field of both states keeps the row from changing height when it is taken in.
		return found->fRuby.IsEmpty() ? kFalse : kTrue;
	}

	virtual int32	RefreshRow(int32 nth)
	{
		// The two documents the comparison is holding. ASKED FOR AGAIN RATHER THAN REMEMBERED:
		// the panel can only reach this while a comparison is armed, but "armed" and "still open"
		// are different questions and the second one is the one that matters here.
		IDataBase* const targetDB = KCMArmedTargetDB();
		IDataBase* sourceDB = KCMArmedSourceDB();
		if (targetDB == nil || !KCMIsDocDBOpen(targetDB))
			return -1;
		// ⛔**THE ORIGIN'S BRANCH WENT ON 2026-09-21.** A Task Start used to arm with NO Source
		//   database at all, so refreshing one row meant rehydrating a copy here and closing it on
		//   the way out. A Task Start is a file Start opens now, so both ends are always live
		//   databases and the question is only whether they are still open.
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
			return -1;

		const int32 count = KCMStoryDiffRun::RunOne(targetDB, sourceDB, nth);

		// NOTHING IS SAID WHEN NOTHING CHANGED. The notification makes the panel rebuild the
		// whole tree, which costs the reader their selection - so a refresh that could not be
		// done leaves the list alone rather than shaking it for no result.
		if (count >= 0)
		{
			// (⛔A refresh also cleared what this row was keeping as already taken in - "a refresh is
			//  a fresh start", the user's call of 2026-09-15 - until the restore went on 2026-09-21.)
			KCMNotify(kKCMStoryEditsRebuiltMessage);
		}

		return count;
	}

	// The one wording every retired restore slot answers with. ★It is NOT translatable, like every
	// other sentence this facade puts on the status line.
	bool16	RestoreIsGone(PMString& outMessage)
	{
		outMessage = PMString("The restore was removed on 2026-09-21 - copy the older words from the "
							  "Source document, which Start leaves open.");
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ⛔**THE FOUR RETIRED RESTORE SLOTS** (2026-09-21). They kept their places for the reason
	//   RestoreAllStories below has kept its since 2026-09-20: KIDMCP calls this facade through its
	//   VTABLE, so taking a virtual out of the middle would land its callers on another method
	//   ([[facade-vtable-slot-append-only]]). ★The feature itself went on the user's word - "the
	//   Source document is in front of you, so if you want it back, take it from there" - once a Task
	//   Start had become a saved copy that Start opens in a window.
	// ⚠**ONE WORDING, whichever slot is called**, so a caller built against the old header is told the
	//   same thing wherever it lands.
	virtual bool16	RestoreChange(int32 nth, int32 which, PMString& outMessage)
	{
		return this->RestoreIsGone(outMessage);
	}

	virtual bool16	RestoreAllInStory(int32 nth, PMString& outMessage)
	{
		return this->RestoreIsGone(outMessage);
	}

	virtual bool16	UndoRestoreChange(int32 nth, int32 which, PMString& outMessage)
	{
		return this->RestoreIsGone(outMessage);
	}

	// ★It answered "is there a Source the older words can be read out of". Nothing writes the Target
	//   from a menu now, so there is one answer. (The IMPORT writes, and never asked this.)
	virtual bool16	CanWriteToTarget()
	{
		return kFalse;
	}

	virtual int32	GetChangeLineCount(int32 nth, int32 which)
	{
		// Same out-of-range rule as GetChangeAttrKind, for the same caller (the tree asking a height).
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return 1;

		// ★The layered kinds say how many lines themselves; every other mark is two, whether or not
		//   this side carries it (the side without draws a bar - KCMAttrKindHasMarkLine).
		// (⛔A tate-chu-yoko or warichu STANDING AS TAKEN IN was drawn from the Source's lines, and its
		//  height was taken from the same side, until the restore went on 2026-09-21.)
		const KCMStoryLayers* const layers = &found->fLayers;
		if (KCMAttrKindIsLayered(found->fAttrKind) && layers->fCount >= 2)
			return layers->fCount;
		return KCMAttrKindHasMarkLine(found->fAttrKind) ? 2 : 1;
	}

	virtual void	StoreStatusLayers(const KCMStoryLayers& layers)	{ KCMStoreSessionStatusLayers(layers); }
	virtual void	GetStatusLayers(KCMStoryLayers& out)			{ KCMGetSessionStatusLayers(out); }

	virtual bool16	GetChangeMarkSpan(int32 nth, int32 which, int32 i, TextIndex& outFrom, TextIndex& outTo)
	{
		// The same index space as GetChange, so the panel names the same change here as there.
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil || found->fWhat != KCMStoryChange::kTable
			|| i < 0 || static_cast<size_t>(i) >= found->fMarkSpans.size())
			return kFalse;
		outFrom = found->fMarkSpans[static_cast<size_t>(i)].fFrom;
		outTo   = found->fMarkSpans[static_cast<size_t>(i)].fTo;
		return kTrue;
	}

	// ---- "Reject This Import Change" (2026-09-24, stage 2 A - design section 13) ----------------------

	virtual int32	HasImportChange(int32 nth, int32 which)
	{
		UIDRef story;
		TextIndex from = 0;
		TextIndex to = 0;
		if (!this->ChangeRangeInTarget(nth, which, story, from, to))
			return 0;
		return KCMCountImportChanges(story, from, to);
	}

	virtual int32	RejectImportChange(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		UIDRef story;
		TextIndex from = 0;
		TextIndex to = 0;
		if (!this->ChangeRangeInTarget(nth, which, story, from, to))
		{
			outMessage = "this change is not in a document that is open and compared";
			return -1;
		}

		// ★ONE UNDO STEP for the whole row: a replace row is two changes (its insertion and its deletion's mark),
		//   and one Ctrl+Z has to bring both back.
		// ⚠★★A PLAIN SEQUENCE, NOT AN ABORTABLE ONE (measured 2026-09-24 on the application): wrapped in
		//   BeginAbortableCmdSeq, the reject was one step - but undoing it left the undo stack EMPTY: the import's
		//   own step below it ("Import Story Text") was gone. Unwrapped, the step below survived (two steps);
		//   BeginCommandSequence keeps both - one step, and the import still undoable under it. ⇒ Asked first
		//   whether there is anything to reject, so an empty sequence never lands on the stack.
		// ★★THE ROW IS COMPARED AGAIN FIRST, AND HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		Change before;
		if (!this->GetChange(nth, which, before))
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the change once more";
			return -1;
		}
		// ★THE CHANGE AS IT STANDS, FOR THE RECORD (stage 2 C): the model's own, break and all - the row it will
		//   become once the reject has put the Source's words back.
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		if (KCMCountImportChanges(story, from, to) <= 0)
		{
			outMessage = "no import change on this row";
			return 0;
		}
		ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
		if (sequence != nil)
		{
			PMString name("Reject This Import Change");
			name.SetTranslatable(kFalse);
			sequence->SetName(name);
		}
		int32 done = KCMRejectImportChanges(story, from, to);
		// ★★★**ALL THE WAY BACK, OR NOT AT ALL** (2026-09-24 night, the user's rule: "if it is not exactly the
		//   same, cancel the take-back"). The Source's words have to stand where the change stood, or the
		//   whole sequence is rolled back before it ends - a rejected tracked change InDesign did not bring all
		//   the way back is then not left half done in the document.
		const bool16 same = (done > 0)
			? this->PlaceReadsAsSource(story, live.fTargetStart, live.fTargetStart + (live.fSourceEnd - live.fSourceStart),
									   live, kKCMStoryAttrNone)
			: kTrue;
		this->EndSequenceOrRollBack(sequence, same);
		if (done < 0)
		{
			outMessage = "the story keeps no change history";
			return -1;
		}
		if (!same)
		{
			outMessage = "the Source's words did not come all the way back (InDesign left something behind), "
						 "so the reject was cancelled - the document is as it was";
			this->RefreshRow(nth);
			return -1;
		}
		// ★★THE CHANGE STAYS ON THE ROW AS A RECORD (2026-09-24, stage 2 C - design 15-1-1): the "=" row, where the
		//   Source's words now stand (the live start, the Source's length), with the counter the reject left the
		//   story at - what tells "still taken back" from "undone" from now on.
		if (done > 0)
			KCMStoryList::AddRejected(nth, live, live.fTargetStart,
									  live.fTargetStart + (live.fSourceEnd - live.fSourceStart),
									  KCMStoryDiffRun::CountForKind(story, kKCMStoryAttrNone), kKCMStoryAttrNone);
		// ★THE ROW IS COMPARED AGAIN: a reject does not move the list by itself (measured 2026-09-24 - rows and
		//   status line stayed as they were until a refresh), and a row still showing a change that is gone
		//   would be offered again. The record above outlives this refresh (PruneRejected keeps a Standing one).
		if (done > 0)
		{
			this->RefreshRow(nth);
			// ★AND THE "=" IS CHECKED ONCE MORE, AFTER THE REFRESH (the record stands only while its place reads
			//   as the Source's - KCMRejectedRecord). The rollback above makes this a belt-and-braces line: it
			//   can only speak when the refresh itself moved something.
			if (!KCMStoryList::RejectedStanding(nth, live, story.GetDataBase()))
				outMessage = "the place does not read as the Source's after the reject - the row shows what differs";
		}
		return done;
	}

private:
	/** Whether [tFrom, tTo) of the Target story reads as the Source's [fSourceStart, fSourceEnd) of `live` - the
		same characters, and for an attribute (attrKind not kKCMStoryAttrNone) the same marks of that kind
		(KCMAttrMarksSame). ★Asked INSIDE the reject's and the restore's sequence, before it ends, so that a
		take-back that did not bring the Source's back can be rolled back whole (EndSequenceOrRollBack).
		⚠A Source that is not open cannot be read; the answer is then kTrue - nothing can be said against it. */
	bool16 PlaceReadsAsSource(const UIDRef& target, TextIndex tFrom, TextIndex tTo, const KCMStoryChange& live,
							  int32 attrKind)
	{
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
			return kTrue;
		const UIDRef source(sourceDB, target.GetUID());
		const int32 len = tTo - tFrom;
		if (len != live.fSourceEnd - live.fSourceStart || len < 0)
			return kFalse;
		{
			IDataBase::SaveRestoreModifiedState targetGuard(target.GetDataBase());
			IDataBase::SaveRestoreModifiedState sourceGuard(sourceDB);
			InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
			InterfacePtr<ITextModel> sModel(source, UseDefaultIID());
			WideString tWords, sWords;
			if (!KCMTextWords::WordsAt(tModel, tFrom, len, tWords)
				|| !KCMTextWords::WordsAt(sModel, live.fSourceStart, len, sWords) || tWords != sWords)
				return kFalse;
		}
		if (attrKind == kKCMStoryAttrNone)
			return kTrue;
		return KCMAttrMarksSame(target, source, attrKind, tFrom, tTo, live.fSourceStart, live.fSourceEnd);
	}

	/** Ends a plain command sequence - committed when `keep`, ROLLED BACK when not.
		★THE OFFICIAL ROLLBACK OF A PLAIN SEQUENCE (CmdUtils.h, SequenceContext: the changes "are committed if the
		  global error code is kSuccess when this helper class goes out of scope, otherwise the database is rolled
		  back to its state before the sequence started"): the error state is raised, the sequence ended, the
		  error state cleared. ⚠Not an abortable sequence: one of those, ended, took the import's own undo step
		  below it away (measured 2026-09-24, RejectImportChange says so). ⚠KBS measured (2026-07-31) that this
		  route does not roll back across SEVERAL documents; here it is one story of one document, which is what
		  the header promises. */
	void EndSequenceOrRollBack(ICommandSequence* sequence, bool16 keep)
	{
		if (sequence == nil)
			return;
		if (!keep)
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
		CmdUtils::EndCommandSequence(sequence);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	/** The Target and Source stories and the change, when change `which` of row `nth` is an attribute change of a
		kind that is written back, on a paired story, with both documents open (2026-09-24, stage 2 B).
		★THE SAME UID ON BOTH SIDES: a Task Start copy is a saved file and a pair saved under a new name keeps its
		  uids (KCMStoryStamp.h) - the door RunOne walks through. */
	bool16 AttrChangeInBoth(int32 nth, int32 which, UIDRef& outTarget, UIDRef& outSource, Change& outChange)
	{
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
			return kFalse;
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
			return kFalse;
		if (!this->GetChange(nth, which, outChange) || outChange.fWhat != Change::kWhatAttr)
			return kFalse;
		if (outChange.fReplaced)
			return kFalse;		// already taken back (the "=" row): its item is "Redo from Word" (stage 2 C)
		const int32 k = outChange.fAttrKind;
		if (k != kKCMStoryAttrRuby && k != kKCMStoryAttrKenten && k != kKCMStoryAttrWarichu && k != kKCMStoryAttrTcy)
			return kFalse;
		outTarget = UIDRef(targetDB, row.fStoryUID);
		outSource = UIDRef(sourceDB, row.fStoryUID);
		return kTrue;
	}

	/** The Target and Source stories and the change, when change `which` of row `nth` is a TABLE row whose table stands
		on both sides (Table ≠: kWhatTable, fKind 0 = replace), on a paired story, with both documents open (2026-09-25,
		"Match the Source"). ⚠Table + and Table − (fKind 1 / 2) are the change history's: the import put them in or
		took them out as tracked characters, and "Reject This Import Change" brings them back. */
	bool16 TableChangeInBoth(int32 nth, int32 which, UIDRef& outTarget, UIDRef& outSource, Change& outChange)
	{
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
			return kFalse;
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
			return kFalse;
		if (!this->GetChange(nth, which, outChange) || outChange.fWhat != Change::kWhatTable || outChange.fKind != 0)
			return kFalse;
		if (outChange.fReplaced)
			return kFalse;
		outTarget = UIDRef(targetDB, row.fStoryUID);
		outSource = UIDRef(sourceDB, row.fStoryUID);
		return kTrue;
	}

	/** Row `nth` as a PAIRED story of the Target that is open and armed - the first test both change row items
		make (the reject and the restore; 2026-09-24). An unpaired row (added or removed) has no counterpart, and
		a story uid of kInvalidUID stands for a file (an import's "!" row about a file with no story). */
	bool16 PairedTargetRow(int32 nth, IDataBase*& outTargetDB, Row& outRow)
	{
		outTargetDB = KCMArmedTargetDB();
		if (outTargetDB == nil || !KCMIsDocDBOpen(outTargetDB))
			return kFalse;
		return (this->GetRow(nth, outRow) && (outRow.fKinds & kKCMStoryKindUnpaired) == 0
				&& outRow.fStoryUID != kInvalidUID) ? kTrue : kFalse;
	}

	/** Compares row `nth` again and says whether change `which` is still `before`: the same kind, the same
		attribute, and the same place on BOTH sides. Both change row items ask this before they act (2026-09-24).
		★★THE LIST DOES NOT FOLLOW AN EDIT (the reject's live re-check): after Ctrl+Z of a reject it still showed
		  the rows as they were after it, two characters off - so a stale row could name the range of a DIFFERENT
		  change (measured: the second "・" row then stood exactly on the first "・"). Refreshed here, and the
		  change at this index must still be the same on both sides - the source side is what tells the two "・"
		  apart. Otherwise nothing is done and the reader is asked to right-click again. */
	bool16 SameChangeAfterRefresh(int32 nth, int32 which, const Change& before)
	{
		this->RefreshRow(nth);
		Change now;
		return (this->GetChange(nth, which, now) && now.fKind == before.fKind && now.fWhat == before.fWhat
				&& now.fAttrKind == before.fAttrKind
				&& now.fTargetStart == before.fTargetStart && now.fTargetEnd == before.fTargetEnd
				&& now.fSourceStart == before.fSourceStart && now.fSourceEnd == before.fSourceEnd) ? kTrue : kFalse;
	}

	/** The story and the range of change `which` of row `nth`, in the Target that is open and armed. */
	bool16 ChangeRangeInTarget(int32 nth, int32 which, UIDRef& outStory, TextIndex& outFrom, TextIndex& outTo)
	{
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
			return kFalse;
		Change change;
		if (!this->GetChange(nth, which, change))
			return kFalse;
		// ★ONLY A ROW ABOUT WORDS OR A TABLE (the same day's final review): a "!" row (kWhatRefused) has no place
		//   at all - its range reads 0..0, and it would have offered to reject whatever of the import's stood at
		//   the story's first character. A ruby/kenten row (kWhatAttr) is stage 2 B's: those are not tracked.
		if (change.fWhat != Change::kWhatText && change.fWhat != Change::kWhatTable)
			return kFalse;
		if (change.fReplaced)
			return kFalse;		// already taken back (the "=" row): its item is "Redo from Word" (stage 2 C)
		outStory = UIDRef(targetDB, row.fStoryUID);
		outFrom = change.fTargetStart;
		outTo = change.fTargetEnd;
		return kTrue;
	}

public:

	// ---- "Restore from Source" (2026-09-24, stage 2 B - design section 14) ----------------------------

	virtual bool16	CanRestoreAttr(int32 nth, int32 which)
	{
		UIDRef target, source;
		Change change;
		return this->AttrChangeInBoth(nth, which, target, source, change);
	}

	virtual int32	RestoreAttr(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		UIDRef target, source;
		Change before;
		if (!this->AttrChangeInBoth(nth, which, target, source, before))
		{
			outMessage = "this change is not an attribute change of two documents that are open and compared";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the change once more";
			return -1;
		}
		// ★THE CHANGE AS IT STANDS, FOR THE RECORD (stage 2 C) - the model's own; the refresh above found it the same.
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		// ★PLANNED BEFORE THE SEQUENCE BEGINS: a refusal (the words differ, a kenten this build cannot write) writes
		//   nothing, and an empty sequence never lands on the undo stack - the reject's count-first, in this shape.
		KCMAttrRestoreJob job;
		if (!KCMPlanRestoreAttrFromSource(target, source, before.fAttrKind,
										  before.fTargetStart, before.fTargetEnd, before.fSourceStart, before.fSourceEnd,
										  job, outMessage))
			return -1;
		ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
		if (sequence != nil)
		{
			PMString name("Restore from Source");
			name.SetTranslatable(kFalse);
			sequence->SetName(name);
		}
		int32 done = KCMApplyRestoreAttr(target, job, outMessage);
		// ★★★**ALL THE WAY BACK, OR NOT AT ALL** (2026-09-24 night, the user's rule) - the marks of this kind over
		//   the change's characters have to read as the Source's, or the sequence is rolled back whole. ★Over the
		//   ROW'S range: the plan's window may be wider (KCMAttrRestorePlan grows it), and what the row promised is
		//   what is checked. ⚠A half write (done < 0) is rolled back the same way: it changed the document too.
		const bool16 same = (done >= 0)
			? this->PlaceReadsAsSource(target, live.fTargetStart, live.fTargetEnd, live, live.fAttrKind)
			: kFalse;
		this->EndSequenceOrRollBack(sequence, same);
		if (done >= 0 && !same)
		{
			outMessage = "the Source's marks did not come all the way back, so the restore was cancelled - the document is as it was";
			done = -1;
		}
		// ★★THE CHANGE STAYS ON THE ROW AS A RECORD (2026-09-24, stage 2 C): the "=" row over the same characters (a
		//   mark changes no length), with the ATTRIBUTE counter the restore left the story at (the text counter cannot
		//   see a mark - KCMStoryDiffRun::CountForKind says why).
		if (done >= 0)
			KCMStoryList::AddRejected(nth, live, live.fTargetStart, live.fTargetEnd,
									  KCMStoryDiffRun::CountForKind(target, live.fAttrKind), live.fAttrKind);
		// ★THE ROW IS COMPARED AGAIN either way: a rolled-back write leaves the document as it was, but the list is
		//   compared against what stands there, whatever that is.
		this->RefreshRow(nth);
		// ★AND THE "=" IS CHECKED ONCE MORE, AFTER THE REFRESH (KCMRejectedRecord) - belt and braces, as the reject's.
		if (done >= 0 && !KCMStoryList::RejectedStanding(nth, live, target.GetDataBase()))
			outMessage = "the marks do not read as the Source's after the restore - the row shows what differs";
		return done;
	}

	// ---- "Match the Source" (2026-09-25 - design section 16) ---------------------------------------------

	virtual bool16	CanMatchTable(int32 nth, int32 which)
	{
		UIDRef target, source;
		Change change;
		return this->TableChangeInBoth(nth, which, target, source, change);
	}

	virtual int32	MatchTable(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		UIDRef target, source;
		Change before;
		if (!this->TableChangeInBoth(nth, which, target, source, before))
		{
			outMessage = "this row is not a table that differs between two documents that are open and compared";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the table row once more";
			return -1;
		}
		// ★THE TWO TABLES BY THEIR OWN IDS, as the comparison paired them (KCMStoryChange::fTargetTableUID).
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		if (live.fTargetTableUID == kInvalidUID || live.fSourceTableUID == kInvalidUID)
		{
			outMessage = "the two tables could not be named on both sides (is the Source document open?)";
			return -1;
		}
		ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
		if (sequence != nil)
		{
			PMString name("Match the Source");
			name.SetTranslatable(kFalse);
			sequence->SetName(name);
		}
		std::vector<KCMTableMatchKept> kept;
		int32 done = KCMMatchTableToSource(target, source, live.fTargetTableUID, live.fSourceTableUID, kept, outMessage);
		// ★★★**ALL THE WAY, OR NOT AT ALL** (the user's rule): the table has to read as the match promised - the
		//   Source's shape, the Source's content in every cell the shape changed, and untouched words in every cell
		//   left alone - or the whole sequence is rolled back. ⚠A half-done match (done < 0) is rolled back the same way.
		std::string why;
		const bool16 same = (done >= 0)
			? KCMTableReadsAsSource(target, source, live.fTargetTableUID, live.fSourceTableUID, kept, why)
			: kFalse;
		this->EndSequenceOrRollBack(sequence, same);
		if (done >= 0 && !same)
		{
			PMString msg("the table did not come all the way to the Source's (");
			PMString reason;
			reason.SetUTF8String(why);
			msg.Append(reason);
			msg.Append("), so the match was cancelled - the document is as it was");
			msg.SetTranslatable(kFalse);
			outMessage = msg;
			done = -1;
		}
		// ★★THE TABLE STAYS ON THE ROW AS A RECORD (2026-09-25, the user: "make it redoable, the same way as the other
		//   redos"): the "=" row over the table's anchor characters - as many as the Source's now - with the text
		//   counter the match left the story at. Its right-click offers "Redo from Word" (a table record is redone
		//   at the table's size - RedoFromWord), and an undo of the match shows the live Table row again through it.
		if (done >= 0)
			KCMStoryList::AddRejected(nth, live, live.fTargetStart,
									  live.fTargetStart + (live.fSourceEnd - live.fSourceStart),
									  KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone);
		// ★THE ROW IS COMPARED AGAIN either way: matched, the live Table row goes (the same table on both sides, by
		//   id) and the record stands "="; rolled back, the list is compared against what stands there.
		this->RefreshRow(nth);
		return done;
	}

	// ---- "Redo from Word" (2026-09-24, stage 2 C - design section 15) ------------------------------------

	virtual bool16	CanRedoFromWord(int32 nth, int32 which)
	{
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
			return kFalse;
		const KCMRejectedRecord* const rec = KCMStoryList::RejectedAt(nth, which);
		return (rec != nil && KCMStoryList::RejectedStateOf(nth, *rec, targetDB) == kKCMRejectedStanding) ? kTrue : kFalse;
	}

	virtual int32	RedoFromWord(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
		{
			outMessage = "this change is not in a document that is open and compared";
			return -1;
		}
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
		{
			outMessage = "the Source document is not open";
			return -1;
		}
		const KCMRejectedRecord* const rec = KCMStoryList::RejectedAt(nth, which);
		if (rec == nil || KCMStoryList::RejectedStateOf(nth, *rec, targetDB) != kKCMRejectedStanding)
		{
			outMessage = "this change is not taken back - nothing to redo";
			return -1;
		}
		const KCMRejectedRecord record = *rec;			// a copy: the list is rebuilt below
		const UIDRef target(targetDB, row.fStoryUID);
		const UIDRef source(sourceDB, row.fStoryUID);

		// ★★A TABLE RECORD - the "=" a "Match the Source" left (2026-09-25, design 16-1 item 7) - is redone by the
		//   same mechanism at the table's size: Word's shape and cells back, under the import's signature. It plans
		//   and writes in one call (a shape round is read back before the next is planned), so it runs inside the
		//   sequence and the sequence is rolled back when it could not be done whole.
		if (record.fLive.fWhat == KCMStoryChange::kTable)
		{
			if (record.fLive.fTargetTableUID == kInvalidUID)
			{
				outMessage = "the table is not named on the record - compare again";
				return -1;
			}
			ICommandSequence* tableSequence = CmdUtils::BeginCommandSequence();
			if (tableSequence != nil)
			{
				PMString name("Redo from Word");
				name.SetTranslatable(kFalse);
				tableSequence->SetName(name);
			}
			const int32 redone = KCMRedoTableFromWord(target, record.fLive.fTargetTableUID, outMessage);
			this->EndSequenceOrRollBack(tableSequence, (redone > 0) ? kTrue : kFalse);
			if (redone > 0)
				KCMStoryList::MarkRedone(nth, record, KCMStoryDiffRun::CountForKind(target, record.fCounterKind));
			this->RefreshRow(nth);
			return redone;
		}

		// ★PLANNED FIRST (design 15-1-8): nothing kept, the words edited since, nothing to redo - each a refusal
		//   that writes nothing and lands nothing on the undo stack.
		KCMStoryShape::Story now;
		KCMStorySync::Plan plan;
		if (!KCMPlanRedoFromWord(target, source, record, now, plan, outMessage))
			return -1;
		ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
		if (sequence != nil)
		{
			PMString name("Redo from Word");
			name.SetTranslatable(kFalse);
			sequence->SetName(name);
		}
		const int32 done = KCMApplyRedoFromWord(target, now, plan, outMessage);
		if (sequence != nil)
			CmdUtils::EndCommandSequence(sequence);
		// ★THE RECORD STAYS, MARKED REDONE (design 15-1-3): its counter says "live" from here - and says "=" again
		//   the moment the reader undoes the redo, which is what keeps the place for a second redo.
		if (done > 0)
			KCMStoryList::MarkRedone(nth, record, KCMStoryDiffRun::CountForKind(target, record.fCounterKind));
		this->RefreshRow(nth);
		return done;
	}

	// ⛔**RETIRED SINCE 2026-09-20**, and since 2026-09-21 no restore of any size is left to come back
	//   to. The slot stays for the vtable reason given above.
	virtual bool16	RestoreAllStories(PMString& outMessage)
	{
		return this->RestoreIsGone(outMessage);
	}

	virtual bool16	GetOversetPoint(IDataBase* db, UID storyUID, TextIndex at,
									UID& outFrame, PBPMPoint& outPb)
	{
		// The model side of the jump's overset branch. The walk itself is KCMOversetPoint.cpp -
		// restored from the Find Overset feature retired on 2026-09-08, the two computations only.
		if (db == nil || storyUID == kInvalidUID)
			return kFalse;
		InterfacePtr<ITextModel> model(UIDRef(db, storyUID), UseDefaultIID());
		if (model == nil)
			return kFalse;
		return KCMFindOversetOutport(model, db, at, outFrame, outPb);
	}
	virtual UID		GetFirstFrameUID(IDataBase* db, UID storyUID)
					{ return KCMStoryFirstFrameUID(db, storyUID); }

	virtual bool16	GetStoryStartPoint(IDataBase* db, UID storyUID, UID& outFrame, PBPMPoint& outPb)
					{ return KCMStoryStartPoint(db, storyUID, outFrame, outPb); }
	virtual bool16	GetStoryPointAt(IDataBase* db, UID storyUID, TextIndex index, PBPMPoint& outPb)
					{ return KCMStoryPointAt(db, storyUID, index, outPb); }
	virtual UID		GetStoryFrameAt(IDataBase* db, UID storyUID, TextIndex index)
					{ return KCMStoryFrameAt(db, storyUID, index); }
	// (2026-09-24) The caret readings: the same two functions told that `at` is a GAP, not a character
	// (in front of a table: the far edge of the character before it). ⚠Declared at the END of
	// IKCMStoryEditsFacade ([[facade-vtable-slot-append-only]]) and written here beside the two they
	// vary - the vtable's order is the interface's, not this class's.
	virtual bool16	GetCaretPointAt(IDataBase* db, UID storyUID, TextIndex at, PBPMPoint& outPb)
					{ return KCMStoryPointAt(db, storyUID, at, outPb, kTrue); }
	virtual UID		GetCaretFrameAt(IDataBase* db, UID storyUID, TextIndex at)
					{ return KCMStoryFrameAt(db, storyUID, at, kTrue); }

	virtual bool16	ExportStoryText(const IDFile& parent, const UIDList& onlyThese,
									PMString& outMessage)
	{
		// ★THE ACTIVE DOCUMENT IS THE ONE EXPORTED, decided here rather than in the UI: which
		//   document a menu item acts on is a model question, and the UI half already asks this
		//   facade every other such question.
		// ⚠WHICH STORIES is the other half of that, and it is NOT a model question: a selection is
		//  the UI's own state. It arrives already resolved, and empty means all.
		return KCMExportStoryText(KCMActiveDocDB(), parent, onlyThese, outMessage);
	}

	virtual bool16	InImportMode()		// ⛔retired with the fourth mode (2026-09-20) - the slot stays
	{
		return kFalse;
	}

	virtual bool16	ImportStoryText(const SysFileList& files, PMString& outMessage)
	{
		// ★The whole sequence is the model's (read, take the origin, hold, set the mode, start) -
		//   the order of those five matters and is stated where they live, not here.
		return KCMImportStoryText(files, outMessage);
	}
};

CREATE_PMINTERFACE(KCMStoryEditsFacade, kKCMStoryEditsFacadeImpl)


//========================================================================================
// KCMResourcesFacade -- IKCMResourcesFacade
//
// Which DEFINITIONS differ between the two documents. Forwarders to KCMResourceStore, one per
// method of the interface, which holds the answer - see IKCMResourcesFacade.h for why it is held
// rather than recomputed.
//
// ★NOTHING IS DECIDED HERE. Every method is one line, because the questions this interface asks
// ("how many rows", "what is row n") are the store's questions and the store answers them for
// the model side too. A facade that computed anything would be a second place to fix.
//========================================================================================

class KCMResourcesFacade : public CPMUnknown<IKCMResourcesFacade>
{
public:
	KCMResourcesFacade(IPMUnknown* boss) : CPMUnknown<IKCMResourcesFacade>(boss) {}

	virtual int32	GetChangeCount()
					{ return KCMResourceStore::GetChangeCount(); }

	virtual bool16	GetNthChange(int32 n, PMString& outKind, PMString& outKey,
								 KCMResourceChangeKind& outWhat)
					{ return KCMResourceStore::GetNthChange(n, outKind, outKey, outWhat); }

	virtual bool16	GetNthValues(int32 n, PMString& outSourceBody, PMString& outTargetBody)
					{ return KCMResourceStore::GetNthValues(n, outSourceBody, outTargetBody); }

	virtual int32	GetNthAttrCount(int32 n)
					{ return KCMResourceStore::GetNthAttrCount(n); }

	virtual bool16	GetNthAttr(int32 n, int32 i, PMString& outName,
							   PMString& outSource, PMString& outTarget)
					{ return KCMResourceStore::GetNthAttr(n, i, outName, outSource, outTarget); }

	virtual void	GetSummary(PMString& out)
					{ KCMResourceStore::GetSummary(out); }
};

CREATE_PMINTERFACE(KCMResourcesFacade, kKCMResourcesFacadeImpl)


//========================================================================================
// KCMBookFacade -- IKCMBookFacade
//
// Book comparison. Three forwarders.
//
// What is NOT here was decided by grepping callers: KCMGetBookResultText is read only by
// KCMScriptProvider (model-side), KCMBuildChapterPairing only by KCMCompareBooks, and
// KCMElidePathFront moved to the UI and was then deleted there.
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
//  THE SIXTH BOUNDARY, AND THE ONLY ONE NOT DRAWN BY THE MODEL/UI SPLIT ITSELF. It appeared when
//  something that had been on the UI side moved over - the global text adornment that draws the
//  Story mode's marks. It had to move because **the UI's File > Export > PDF runs in the
//  background and never hands a kUIPlugIn any drawing**, so marks living there could not reach an
//  exported PDF at all.
//
//  EVERY METHOD IS ONE LINE, WHICH IS THE POINT. The facade is a door, not a place where things
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
	// No ShutdownMarks: teardown is model-side only and KCMPeek.cpp calls the marker directly
	// (IKCMStoryMarkFacade.h says why a boundary method with no caller is worse than none).
};

CREATE_PMINTERFACE(KCMStoryMarkFacade, kKCMStoryMarkFacadeImpl)

void KCMStoryMarkFacade::ShowJumpFlash(IDataBase* db, UID storyUID,
										 TextIndex from, TextIndex to,
										 TextIndex sourceFrom, TextIndex sourceTo)
{
	KCMStoryMarkDocs flash;
	KCMStoryMarker::AddFlashRange(flash, db, storyUID, from, to);

	// THE SAME STORY UID, IN THE OTHER DOCUMENT - which is what the whole Story mode is built on:
	// the diff pairs stories by uid, the double click selects in both by uid, and the standing
	// marks light both by uid (KCMStoryMarkBuild).
	// @warning the general rule that a uid names a DIFFERENT object in another document is true
	// and does not apply here. Believing that it did is what once left the older window bare.
	// @warning `db` is NOT always the target: a Removed story is read out of the older document,
	// and then this test is what stops the same document being marked twice (which would invert
	// twice and leave a hole - KCMStoryMarkRanges.h).
	// The test lives here rather than at the caller because "is the older window open" is a fact
	// about the comparison, and the comparison is this side's ([[one-question-one-place]]).
	IDataBase* const flashSourceDB = KCMArmedSourceDB();
	if (flashSourceDB != nil && flashSourceDB != db && KCMIsDocDBOpen(flashSourceDB))
		KCMStoryMarker::AddFlashRange(flash, flashSourceDB, storyUID, sourceFrom, sourceTo);

	KCMStoryMarker::ShowFlash(flash);
}

// End of KCMFacades.cpp.
