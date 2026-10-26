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
#include "KCMTaskStartClone.h"	// Task Start kept in memory (2026-10-13)
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
#include "KCMTrackList.h"			// the Track Changes mode's rows (2026-10-05) - read in that mode instead
#include "KCMTrackRead.h"			// ...read again by a refresh, and asked whether a change is still recorded
#include "KCMTrackOriginal.h"		// "Compare with Tracked Changes..." - the copy with every change rejected
#include "KCMTrackResolve.h"		// Accept / Reject in the Track mode (2026-10-25) - the records found again and processed
#include "KCMStoryDiffRun.h"		// RunOne - re-comparing one row's story ("Refresh Story Comparison")
#include "KCMOversetPoint.h"		// KCMFindOversetOutport - where the "+" of an overflow is
#include "ITextModel.h"			// the story the two above are asked about
#include "KCMStoryTextExport.h"	// KCMExportStoryText - "Export Story Text..." on the flyout
#include "KCMPageExport.h"		// KCMExportPagesAsWord - "Export Pages to Word..." (2026-10-25)
#include "KCMStoryTextImport.h"	// KCMImportStoryTextAsSource - "Import Story Text..."; KCMPourStoryTextWith - Apply All
#include <algorithm>
#include "KCMRestoreAttr.h"		// "Restore from Source" on an attribute change row (2026-09-24, stage 2 B); KCMAttrMarksSame
#include "KCMTextWords.h"		// WordsAt - a write's "all the way, or not at all" reads the words (2026-09-24 night)
#include "ErrorUtils.h"			// the error state a step leaves cleared (EndOrAbort) and a write's is read by
#include "IDataBase.h"			// SaveRestoreModifiedState - reading the two stories must not dirty them
#include "WideString.h"
#include "KCMApplyFromImport.h"		// Apply's paragraph road and table road (2026-10-24)
#include "KCMStoryFollowObserver.h"	// KCMStoryFollowEnsureObservers - the Undo follower attached again after an Apply
#include "KCMTableMatch.h"		// "Match the Source" on a Table row (2026-09-25, design section 16)
#include "CmdUtils.h"				// ...wrapped in one command sequence
#include "ICommandSequence.h"		// IAbortableCmdSeq - every write here is one abortable step (BeginStep / EndStep)
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
#include "KCMPdfDoc.h"				// the PDF comparison (2026-10-14)
// ★Apply change by change, and Restore from Copy (2026-10-24 evening - design section 11 of the import-as-Source design):
#include "KCMCopyStore.h"			// the copies Apply takes and Restore from Copy pastes back
#include "KCMStoryRestore.h"		// KCMCreateWordsWriteCmd - the import's one write command, for one change's words
#include "KCMParaText.h"			// IsObjectCharacter - the changes a words write may not carry
#include "TextChar.h"				// kTextChar_CR
#include "IHyperlinkUtils.h"		// a hyperlink source a copy would carry twice - source/open/interfaces/layout
#include "IAttributeStrand.h"		// a paragraph's style and own overrides, kept and put back
#include "AttributeBossList.h"
#include "IComposeScanner.h"		// FindSurroundingParagraph - the paragraph they are put back over
#include "ITextModelCmds.h"			// ApplyStyleCmd / ApplyCmd
#include "IStyleInfo.h"				// whether a kept paragraph style is still one of the document's
// ★The list like KFC's (2026-10-24 night - docs/superpowers/specs/2026-10-24-kcm-list-like-kfc-design.md):
#include "KCMPlaceReach.h"			// GetChangePlaceReach - locks and hidden, KFC's landing rules
#include "KCMDiag.h"				// KCM_DIAG_FAULT("batch-fail") - a batch's roll-back, measured (test builds only)
#include <set>						// the rows a batch touched
#include <utility>					// std::pair - a batch's (row, change)

namespace
{

/** The same change of the diff's: what, kind, and both ranges (2026-09-25 - a group written together has to say
	whether the row pressed is among the rows it found). */
bool16 SameStoryChange(const KCMStoryChange& a, const KCMStoryChange& b)
{
	return (a.fWhat == b.fWhat && a.fKind == b.fKind && a.fTargetStart == b.fTargetStart && a.fTargetEnd == b.fTargetEnd
			&& a.fSourceStart == b.fSourceStart && a.fSourceEnd == b.fSourceEnd) ? kTrue : kFalse;
}

// ---- ★APPLY CHANGE BY CHANGE, AND RESTORE FROM COPY (2026-10-24 evening - design section 11) ---------------------------

/** kTrue when `words` hold a character InDesign hangs an object on (KCMParaText::IsObjectCharacter - a note's marker, a
	table's anchor, a page number, an anchored object, an index marker, an XML tag's mark): words written as text would
	break it (the 2026-09-21 rule), and a copy put back would make it anew with a new UID (the author, KFC's rule). */
bool16 HoldsObject(const WideString& words)
{
	for (int32 k = 0; k < words.CharCount(); ++k)
		if (KCMParaText::IsObjectCharacter(static_cast<int32>(words.GetChar(k).GetValue())))
			return kTrue;
	return kFalse;
}

/** kTrue when `words` hold a paragraph's return - a change that joins or splits paragraphs goes its paragraph's road. */
bool16 HoldsReturn(const WideString& words)
{
	for (int32 k = 0; k < words.CharCount(); ++k)
		if (words.GetChar(k).GetValue() == kTextChar_CR)
			return kTrue;
	return kFalse;
}

/** Whether the character at `at` of `model` is a paragraph's return (2026-10-24 night - a whole paragraph written with its
	own return, KCMRecordCopy::fShift). Reads only; the caller holds the document's SaveRestoreModifiedState. */
bool16 ReturnAt(ITextModel* model, TextIndex at)
{
	WideString one;
	return (model != nil && at >= 0 && KCMTextWords::WordsAt(model, at, 1, one) && one.CharCount() == 1
			&& one.GetChar(0).GetValue() == kTextChar_CR) ? kTrue : kFalse;
}

/** The same, for `storyUID` of `db` - read under the document's SaveRestoreModifiedState. */
bool16 ReturnAt(IDataBase* db, UID storyUID, TextIndex at)
{
	if (db == nil)
		return kFalse;
	IDataBase::SaveRestoreModifiedState guard(db);
	InterfacePtr<ITextModel> model(UIDRef(db, storyUID), UseDefaultIID());
	return ReturnAt(model, at);
}

/** kTrue when [from, to) of `model` touches a hyperlink's or a cross-reference's source - inside it, around it, across
	its edge (IHyperlinkUtils' two range questions, KFC's doors). A copy of such a range pasted back makes a second link
	where there was one (KFC measured it, R7) - so none is kept. ⚠A question that cannot be asked answers kTrue: no copy
	is the safe side. */
bool16 TouchesLink(ITextModel* model, TextIndex from, TextIndex to)
{
	Utils<IHyperlinkUtils> utils;
	if (!utils || model == nil)
		return kTrue;
	if (!utils->GetRangeContainedHyperlinks(model, from, to).empty())
		return kTrue;
	return (utils->IsRangeContainedByAHyperlink(model, from, to).GetLogicalID() != kInvalidSBOSID) ? kTrue : kFalse;
}

/** The reason a "=" record holds no copy, as the row's message and the Apply sentence say it. */
const char* CopyWhyNotWords(int32 why)
{
	switch (why)
	{
		case kKCMCopyHoldsObject:	return "its words hold an object - put back from a copy, it would be made anew with a new UID";
		case kKCMCopyTouchesLink:	return "it touches a hyperlink or a cross-reference - a copy put back would make a second one";
		case kKCMCopyParagraphRoad:	return "it went in with the rest of its paragraph";
		case kKCMCopyGroup:			return "it went in with other paragraphs put in at the same place";
		case kKCMCopyFailed:		return "the copy could not be made";
		case kKCMCopyMark:			return "it is a mark (ruby, kenten, warichu or tate-chu-yoko) - this version keeps no copy of one";
		case kKCMCopyWholeStory:	return "its story went in whole (Apply All Changes) - Ctrl+Z takes it back";
		default:					return "it was not put in by Apply This Change";
	}
}

/** The paragraph that holds `at`, with its return - the last paragraph's with the story's own (KFC's ParagraphOf:
	IComposeScanner::FindSurroundingParagraph, the end of the story counted). False when it cannot be read. */
bool16 ParagraphOf(ITextModel* model, TextIndex at, TextIndex& outStart, int32& outLength)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	outLength = 0;
	outStart = (scanner != nil) ? scanner->FindSurroundingParagraph(at, &outLength, kFalse) : kInvalidTextIndex;
	return (outStart >= 0 && outLength > 0) ? kTrue : kFalse;
}

/** The style and own overrides of the paragraph that holds `at`, as they read now - kept on `copy` for
	PutParagraphBack (KFC's TakeRowCopy, its paragraph-formatting half). False when the strand cannot be read. */
bool16 KeepParagraph(ITextModel* model, TextIndex at, KCMRecordCopy& copy)
{
	InterfacePtr<IAttributeStrand> paraStrand(static_cast<IAttributeStrand*>(
		(model != nil) ? model->QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND) : nil));
	if (paraStrand == nil)
		return kFalse;
	int32 count = 0;
	copy.fParaStyle = paraStrand->GetStyleUID(at, &count);
	const DataWrapper<AttributeBossList> overrides = paraStrand->GetLocalOverrides(at, &count);
	const AttributeBossList* const own = overrides.get();		// (DataWrapper has no ->: its get())
	if (own != nil && own->CountBosses() > 0)
		copy.fParaOverrides.reset(own->Duplicate());
	return (copy.fParaStyle != kInvalidUID) ? kTrue : kFalse;
}

/** Whether a kept paragraph style is still one of the document's - a style deleted since would be handed to InDesign
	by a UID nothing holds (KFC's ParagraphStyleStands). */
bool16 ParagraphStyleStands(IDataBase* db, UID style)
{
	if (db == nil || style == kInvalidUID || !db->IsValidUID(style))
		return kFalse;
	InterfacePtr<IStyleInfo> info(db, style, UseDefaultIID());
	return (info != nil && info->GetStyleType() == IStyleInfo::kParagraphStyle) ? kTrue : kFalse;
}

// (CaretReadsAsSource - a caret's place read by what stands on either side of it - moved to KCMStoryList on 2026-10-26,
//  the import tidy S10: the records' FollowRecords asks it too.)

/** The kept formatting put back over the paragraph that holds `at`: its style with what the write left on it cleared,
	then its own overrides (KFC's PutParagraphBack - the copy-restore spec's C16). Inside the caller's sequence. */
bool16 PutParagraphBack(ITextModel* model, TextIndex at, const KCMRecordCopy& copy)
{
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	TextIndex paraStart = kInvalidTextIndex;
	int32 length = 0;
	if (cmds == nil || at < 0 || !ParagraphOf(model, at, paraStart, length))
		return kFalse;
	InterfacePtr<ICommand> style(cmds->ApplyStyleCmd(paraStart, length, copy.fParaStyle, kParaAttrStrandBoss,
		kTrue /*replaceOverrides*/, kFalse /*autoNextStyle*/, kFalse /*keepOldStyleAsOverrides*/));
	if (style == nil || CmdUtils::ProcessCommand(style) != kSuccess)
		return kFalse;
	if (copy.fParaOverrides != nil && copy.fParaOverrides->CountBosses() > 0)
	{
		InterfacePtr<ICommand> overrides(cmds->ApplyCmd(paraStart, length, copy.fParaOverrides, kParaAttrStrandBoss));
		if (overrides == nil || CmdUtils::ProcessCommand(overrides) != kSuccess)
			return kFalse;
	}
	return kTrue;
}

}	// anonymous namespace

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
	virtual bool16		RefreshSelectedPages(const UIDList& pages, int32* outPages, int32* outChanged,
								bool16* outCancelled, int32* outFailed)
								{ return KCMRefreshComparisonForSelectedPages(pages, outPages, outChanged, outCancelled, outFailed); }
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
	{
		// (The Track Changes mode was refused here from 2026-10-05 - design 2-3: the report's Story section rebuilt
		//  the rows underneath the Track list. Since 2026-10-11 the report is made only while nothing is started, when
		//  there is no Track list to take apart, so the mode is no longer asked: KCMExportBeforeAfterReport refuses a
		//  running comparison in every mode.)
		return KCMExportBeforeAfterReport(outMessage);
	}

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

	// ★"Compare with Tracked Changes..." (2026-10-05) - the whole road is the model's (KCMTrackOriginal.h).
	virtual bool16	CanTakeTrackOriginalCopy()					{ return KCMCanTakeTrackOriginalCopy(); }
	virtual bool16	TakeTrackOriginalCopy(PMString& outMessage)	{ return KCMTakeTrackOriginalCopy(nil, outMessage); }

	// ★Task Start kept in memory (2026-10-13) - the whole road is the model's (KCMTaskStartClone.h).
	virtual bool16		CanTakeTaskStartClone()					{ return KCMCanTakeTaskStartClone(); }
	virtual bool16		TakeTaskStartClone(PMString& outWhyNot)	{ return KCMTakeTaskStartClone(outWhyNot); }

	// ★The PDF comparison (2026-10-14) - the whole road is the model's (KCMPdfDoc.h).
	virtual bool16		ChoosePdfEnd(bool16 asTarget, const IDFile& pdf, PMString& outMessage)	{ return KCMChoosePdfEnd(asTarget, pdf, outMessage); }
	virtual bool16		IsPdfDoc(IDataBase* db)		{ return KCMIsPdfDoc(db); }

	// ★(2026-10-25 - the author: "while tracking, show on the Source line that the Track is the Source") the panel's
	//   Source line asks. Both asked for life first: the panel asks from its Update, which runs during teardown as well
	//   (KCMPanelObserver.cpp, KCMDocPathFromDB), and KCMIsTrackOriginalOf reads each database's file.
	virtual bool16		IsTrackOriginalOf(IDataBase* sourceDB, IDataBase* targetDB)
	{
		if (sourceDB == nil || targetDB == nil || !KCMIsDocDBOpen(sourceDB) || !KCMIsDocDBOpen(targetDB))
			return kFalse;
		return KCMIsTrackOriginalOf(sourceDB, targetDB);
	}
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

	// (2026-10-26, the import tidy S7) The pages are the Pages panel's selection as the UI read it.
	virtual void	ToggleRegisterForSelection(const UIDList& pages)	{ KCMPageMapToggleSelectedPages(pages); }
	virtual void	ToggleCheckForSelection(const UIDList& pages)		{ KCMPageCheckToggleSelectedPages(pages); }

	virtual KCMPageToggleState	GetRegisterToggleState(const UIDList& pages)	{ return KCMPageMapGetToggleState(pages); }
	virtual KCMPageToggleState	GetCheckToggleState(const UIDList& pages)		{ return KCMPageCheckGetToggleState(pages); }

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
namespace
{

/* The Track mode reads the Track list (2026-10-05): the ONE place the facade asks the mode, so that every
   reader of a row or a change - the tree, the marks, the jump, Prev/Next - follows it without asking. */
bool16 KCMFacadeTrackMode()
{
	return (KCMGetCompareMode() == kKCMModeTrack) ? kTrue : kFalse;
}

const KCMTrackChange* KCMTrackChangeAt(int32 nth, int32 which)
{
	const KCMTrackRow* row = KCMTrackList::GetRow(nth);
	if (row == nil || which < 0 || which >= static_cast<int32>(row->fChanges.size()))
		return nil;
	return &row->fChanges[static_cast<size_t>(which)];
}

/* The fields every change hands out, Story and Track alike (moved out of GetChange, 2026-10-05). */
void KCMFillChangeOut(const KCMStoryChange& change, IKCMStoryEditsFacade::Change& out)
{
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
	out.fOtherLayers	= change.fOtherLayers;
	// (⛔THE TWO FACES OF A ROW WENT ON 2026-09-21 - a change the reader had taken in carried the words as they stood
	//  now AND before, and the model chose which belonged in fText*. Nothing is taken in now: a row has one face.)
	out.fOverset		= change.fOverset;		// decided by the diff (or the Track reader); see KCMStoryList.h
	out.fWholeParagraph	= change.fWholeParagraph;	// a paragraph added or removed whole
	out.fPlace			= change.fPlace;		// the body, a cell or a note - the ID column's word
	out.fWholeCell		= kFalse;				// retired the night it was made (2026-09-19): a table's cells fold into a Table row now; the field keeps the layout
	out.fMarkSpanCount	= static_cast<int32>(change.fMarkSpans.size());	// the cells a Table change marks (GetChangeMarkSpan)
	out.fWriteBlock		= 0;					// ⛔nothing writes from a row any more (2026-09-21)
	out.fAfterNewParagraph	= kFalse;			// ⛔the same
}

}	// anonymous namespace

class KCMStoryEditsFacade : public CPMUnknown<IKCMStoryEditsFacade>
{
public:
	KCMStoryEditsFacade(IPMUnknown* boss) : CPMUnknown<IKCMStoryEditsFacade>(boss),
		fBatch(kFalse), fBatchFailed(kFalse), fStale(kFalse), fStepWasModified(kFalse) {}

	virtual int32	GetRowCount()
	{
		if (KCMFacadeTrackMode())
			return KCMTrackList::GetRowCount();
		return KCMStoryList::GetRowCount();
	}

	virtual bool16	GetRow(int32 nth, Row& out)
	{
		if (KCMFacadeTrackMode())
		{
			// A Track row is drawn as a Story row: one story's first words, frame and page (KCMStoryList::ReadRowForStory).
			const KCMTrackRow* trackRow = KCMTrackList::GetRow(nth);
			if (trackRow == nil)
				return kFalse;
			out.fStoryUID	= trackRow->fRow.fStoryUID;
			out.fText		= trackRow->fRow.fText;
			out.fKinds		= trackRow->fRow.fKinds;
			out.fFrameUID	= trackRow->fRow.fFrameUID;
			out.fPageUID	= trackRow->fRow.fPageUID;
			out.fTextCompared = kTrue;
			out.fAttrKind	= 0;
			out.fAttrKindCount = 0;
			out.fHasTextChange = kTrue;
			out.fSourceStoryUID = trackRow->fRow.fStoryUID;	// the Track original is a copy: the same uid (2026-10-26)
			return kTrue;
		}
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
		out.fSourceStoryUID = row->fSourceStoryUID;				// the Source story it compares against (2026-10-26)
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
		if (KCMFacadeTrackMode())
		{
			const KCMTrackRow* r = KCMTrackList::GetRow(nth);
			return (r != nil) ? static_cast<int32>(r->fChanges.size()) : 0;
		}
		return KCMStoryList::GetMergedChangeCount(nth);
	}

	virtual bool16	GetChange(int32 nth, int32 which, Change& out)
	{
		if (KCMFacadeTrackMode())
		{
			// No "=" record is made in the Track mode, and its ranges carry no paragraph break to cut (KCMTrackRead).
			const KCMTrackChange* c = KCMTrackChangeAt(nth, which);
			if (c == nil)
				return kFalse;
			KCMFillChangeOut(c->fChange, out);
			out.fReplaced = kFalse;
			return kTrue;
		}
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return kFalse;

		const KCMStoryChange& change = *found;
		KCMFillChangeOut(change, out);		// every field a change hands out (Story and Track alike)
		// ★★fReplaced IS BACK IN USE, WITH THE OPPOSITE MEANING (2026-09-24, stage 2 C): kTrue for a change KCM WROTE as
		//   the Source's and still standing so - the "=" row, the Source's state - which is what its menu, its sign and
		//   the marks ask. (From 2026-09-15 to 2026-09-21 it said "taken IN".) The layout is unchanged.
		{
			const KCMEqualRecord* const rec = KCMStoryList::EqualAt(nth, which);
			out.fReplaced = (rec != nil && KCMStoryList::EqualStateOf(nth, *rec, KCMArmedTargetDB()) == kKCMEqualStanding)
				? kTrue : kFalse;
		}


		// ★★**A WHOLE PARAGRAPH IS HANDED OUT AS ITS WORDS, WITHOUT THE BREAK** (2026-09-19, the user:
		//   "the mark reaches the end of the paragraph above - I want that gone"). The model's ranges
		//   carry the return of the paragraph before (or the paragraph's own) because the WRITE needs
		//   it (KCMStoryList.h, fWholeParagraph); shown as they stand, the standing mark, the jump's
		//   flash and the selection (the double click's then) all began on that return - at the END OF THE
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
		if (KCMFacadeTrackMode())
			return static_cast<int32>(kKCMStoryAttrNone);	// a Track change is words, never an attribute
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil)
			return static_cast<int32>(kKCMStoryAttrNone);

		return static_cast<int32>(found->fAttrKind);
	}

	virtual bool16	GetChangeHasAttrValue(int32 nth, int32 which)
	{
		// Same out-of-range rule as the kind above, and for the same caller: an unknown row gets
		// the ordinary one-line height rather than an error.
		if (KCMFacadeTrackMode())
			return kFalse;
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
		if (KCMFacadeTrackMode())
		{
			IDataBase* const trackTargetDB = KCMArmedTargetDB();
			if (trackTargetDB == nil || !KCMIsDocDBOpen(trackTargetDB))
				return -1;
			// ★The whole list is read again, not one row: rows are (story, author) pairs, and one story's records
			//   can make or lose rows for several authors at once. Reading every record is a walk of the stories
			//   and no composition (KCMTrackRead.h).
#ifdef KCM_DIAG
			KCMStoryFollowDiagReport(trackTargetDB, "track row refresh");	// test builds: is the Undo follower still there
#endif
			const int32 trackCount = KCMTrackRead::Build(trackTargetDB, KCMArmedSourceDB(), nil);
			KCMNotify(kKCMStoryEditsRebuiltMessage);
			return trackCount;
		}
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
		// ★NOR INSIDE A BATCH (2026-10-24 night): RunBatch says it once, at its end.
		if (count >= 0 && !fBatch)
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
		if (KCMFacadeTrackMode())
			return 1;
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
		if (KCMFacadeTrackMode())
			return kFalse;		// a Track change is never a table's
		const KCMStoryChange* const found = KCMStoryList::GetMergedChange(nth, which);
		if (found == nil || found->fWhat != KCMStoryChange::kTable
			|| i < 0 || static_cast<size_t>(i) >= found->fMarkSpans.size())
			return kFalse;
		outFrom = found->fMarkSpans[static_cast<size_t>(i)].fFrom;
		outTo   = found->fMarkSpans[static_cast<size_t>(i)].fTo;
		return kTrue;
	}

private:
	/** Whether [tFrom, tTo) of the Target story reads as the Source's [fSourceStart, fSourceEnd) of `live` - the
		same characters, and for an attribute (attrKind not kKCMStoryAttrNone) the same marks of that kind
		(KCMAttrMarksSame). ★Asked INSIDE a write's step, before it ends, so that a write that did not bring the
		Source's back can be aborted whole (EndStep).
		⚠A Source that is not open cannot be read; the answer is then kTrue - nothing can be said against it. */
	bool16 PlaceReadsAsSource(const UIDRef& target, UID sourceStory, TextIndex tFrom, TextIndex tTo,
							  const KCMStoryChange& live, int32 attrKind)
	{
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB))
			return kTrue;
		// ★THE ROW'S SOURCE STORY, NOT THE TARGET'S UID (2026-10-26): the two are one story for a UID pair and two for a
		//   row paired by place (KCMStoryRow::fSourceStoryUID) - read under the Target's uid, the Source would answer with
		//   a different story or none, and a write that did go in would be rolled back as "not the Source's".
		const UIDRef source(sourceDB, sourceStory);
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

	/** ★ONE WRITE'S UNDO STEP - OR A PART OF A BATCH'S (2026-10-24 night - design 1-4/1-5/1-8, KFC's "rows selected
		together go in as one step"): an ABORTABLE sequence named `name` (EndOrAbort says why abortable), or nil inside a
		batch (RunBatch), whose ONE sequence holds every write - a sequence nested in another breaks the outer one's
		roll-back (KBS measured 2026-07-31). Whether the Target was modified before is noted for the abort. */
	IAbortableCmdSeq* BeginStep(const char* name)
	{
		if (fBatch)
			return nil;
		IDataBase* const db = KCMArmedTargetDB();
		fStepWasModified = (db != nil && KCMIsDocDBOpen(db) && db->IsModified()) ? kTrue : kFalse;
		IAbortableCmdSeq* sequence = CmdUtils::BeginAbortableCmdSeq("KCMStoryEditsStep");
		if (sequence != nil)
		{
			PMString stepName(name);
			stepName.SetTranslatable(kFalse);
			sequence->SetName(stepName);
		}
		return sequence;
	}

	/** Ends BeginStep's sequence - committed when `keep`, aborted when not (EndOrAbort). Inside a batch there is none to
		end: a write that did not go in marks the batch failed, and RunBatch aborts ALL of it. @return whether the step
		was KEPT - `keep`, unless the test build's step-fail switch turned it round, so that a caller that reads it says
		what really happened (Match the Source does, since 2026-10-25 - tests-m T-M3). */
	bool16 EndStep(IAbortableCmdSeq* sequence, bool16 keep)
	{
		// (Fault switch step-fail, a test build's only - KCMDiag.h: a write that went in is taken as not gone in, so that the
		//  abort is measured - the single step's here, a batch's in RunBatch.)
		if (keep && KCM_DIAG_FAULT("step-fail"))
			keep = kFalse;
		if (fBatch)
		{
			if (!keep)
				fBatchFailed = kTrue;
			return keep;
		}
		this->EndOrAbort(sequence, keep, fStepWasModified);
		return keep;
	}

	/** ★ALL OF IT OR NONE OF IT, MEASURED (2026-10-24 night - work/note-scripts/2026-10-24-kcm-list/probe-step.ps1): the
		step committed when `keep`, otherwise ABORTED - its writes taken back by the abort itself, wherever it runs. ⚠A plain
		sequence ended with the error raised (the EndSequenceOrRollBack that stood here until 2026-10-25) was measured to
		KEEP its write and its step when the call came from a script (P1: KIDMCP's SCRIPT_REQUEST runs each request in a
		step of its own; the guide checks the error "when control returns to the application", and that helper cleared it
		before then). The abort was measured to leave nothing (P2, and tests-batch T-B3), and a committed abortable step to
		leave the step below it as it was (T-B2). The error state is cleared either way (KFC's EndWriteStep), and a document
		clean before the step is clean again after an abort, which does not put the flag back (memory
		command-sequence-rollback-on-error). ★EVERY WRITE OF THIS FACADE ENDS HERE since 2026-10-25 (Match the Source was
		the last plain one). */
	void EndOrAbort(IAbortableCmdSeq* sequence, bool16 keep, bool16 wasModified)
	{
		if (sequence == nil)
			return;
		if (keep)
			CmdUtils::EndCommandSequence(sequence);
		else
		{
			CmdUtils::AbortCommandSequence(sequence);
			IDataBase* const db = KCMArmedTargetDB();
			if (!wasModified && db != nil && KCMIsDocDBOpen(db))
				db->SetModified(kFalse);
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	/** The Target and Source stories and the change, when change `which` of row `nth` is an attribute change of a
		kind that is written back, on a paired story, with both documents open (2026-09-24, stage 2 B).
		★THE ROW'S SOURCE STORY ON THE SOURCE SIDE (2026-10-26 - KCMStoryRow::fSourceStoryUID): the same uid for every
		  row paired by UID (a Task Start copy is a saved file and a pair saved under a new name keeps its uids -
		  KCMStoryStamp.h), the Source's own story for a row paired by the place of its first frame - the door RunOne
		  walks through. */
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
			return kFalse;		// already the Source's (the "=" row): Ctrl+Z is its way back - a mark keeps no copy
		const int32 k = outChange.fAttrKind;
		if (k != kKCMStoryAttrRuby && k != kKCMStoryAttrKenten && k != kKCMStoryAttrWarichu && k != kKCMStoryAttrTcy)
			return kFalse;
		outTarget = UIDRef(targetDB, row.fStoryUID);
		outSource = UIDRef(sourceDB, row.fSourceStoryUID);	// the row's Source story (2026-10-26)
		return kTrue;
	}

	/** The Target and Source stories and the change, when change `which` of row `nth` is a TABLE row whose table stands
		on both sides (Table ≠: kWhatTable, fKind 0 = replace), on a paired story, with both documents open (2026-09-25,
		"Match the Source"). ⚠Not Table + / Table − (fKind 1 / 2): a table only one side has is not matched cell for cell -
		the story's own Apply puts it in or takes it out against an import's copy, and between two documents it is the
		reader's to copy. */
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
		outSource = UIDRef(sourceDB, row.fSourceStoryUID);	// the row's Source story (2026-10-26)
		return kTrue;
	}

	/** Row `nth` as a PAIRED story of the Target that is open and armed - the first test every change row item makes
		(2026-09-24). An unpaired row (added or removed) has no counterpart, and a story uid of kInvalidUID stands for a
		file (an import's "!" row about a file with no story). */
	bool16 PairedTargetRow(int32 nth, IDataBase*& outTargetDB, Row& outRow)
	{
		outTargetDB = KCMArmedTargetDB();
		if (outTargetDB == nil || !KCMIsDocDBOpen(outTargetDB))
			return kFalse;
		return (this->GetRow(nth, outRow) && (outRow.fKinds & kKCMStoryKindUnpaired) == 0
				&& outRow.fStoryUID != kInvalidUID) ? kTrue : kFalse;
	}

	/** Whether row `nth`'s story has moved since the row was last compared - the aggregate counter (any attribute kind asks
		it - KCMStoryDiffRun::CountForKind) against the one the row was compared at (fAllCount): an edit KCM was not told of,
		plain typing above all (2026-10-26, the import tidy S10 - RestoreChangeFromCopy asks it before it refuses). kFalse for
		a row never compared, a Removed one, a row standing for a file, or no armed Target. */
	bool16 ChangedSinceCompared(int32 nth)
	{
		IDataBase* const targetDB = KCMArmedTargetDB();
		const KCMStoryRow* const row = KCMStoryList::GetRow(nth);
		if (targetDB == nil || !KCMIsDocDBOpen(targetDB) || row == nil || row->fStoryUID == kInvalidUID
			|| (row->fKinds & kKCMStoryKindRemoved) != 0 || row->fComparedAt.empty())
			return kFalse;
		return (KCMStoryDiffRun::CountForKind(UIDRef(targetDB, row->fStoryUID), kKCMStoryAttrRuby) != row->fAllCount)
			   ? kTrue : kFalse;
	}

	/** Compares row `nth` again and says whether change `which` is still `before`: the same kind, the same
		attribute, and the same place on BOTH sides. Every change row item asks this before it acts (2026-09-24).
		★★THE LIST DOES NOT FOLLOW AN EDIT (the retired reject's live re-check): after Ctrl+Z of a reject it still showed
		  the rows as they were after it, two characters off - so a stale row could name the range of a DIFFERENT
		  change (measured: the second "・" row then stood exactly on the first "・"). Refreshed here, and the
		  change at this index must still be the same on both sides - the source side is what tells the two "・"
		  apart. Otherwise nothing is done and the reader is asked to right-click again. */
	bool16 SameChangeAfterRefresh(int32 nth, int32 which, const Change& before)
	{
		this->RefreshRow(nth);
		Change now;
		const bool16 same = (this->GetChange(nth, which, now) && now.fKind == before.fKind && now.fWhat == before.fWhat
							 && now.fAttrKind == before.fAttrKind
							 && now.fTargetStart == before.fTargetStart && now.fTargetEnd == before.fTargetEnd
							 && now.fSourceStart == before.fSourceStart && now.fSourceEnd == before.fSourceEnd)
							? kTrue : kFalse;
		// ★A BATCH ASKS (2026-10-24 night): the row's other indices it was given came from the list before this refresh.
		if (!same)
			fStale = kTrue;
		return same;
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
		return this->RestoreAttrAs(nth, which, "Restore from Source", outMessage);
	}

	/** RestoreAttr under an undo step of `stepName`'s - "Apply This Change" makes a mark the files' by the same road
		(2026-10-24, the import-as-Source road: the Source IS the files then). */
	int32	RestoreAttrAs(int32 nth, int32 which, const char* stepName, PMString& outMessage)
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
		//   nothing, and an empty sequence never lands on the undo stack.
		KCMAttrRestoreJob job;
		if (!KCMPlanRestoreAttrFromSource(target, source, before.fAttrKind,
										  before.fTargetStart, before.fTargetEnd, before.fSourceStart, before.fSourceEnd,
										  job, outMessage))
			return -1;
		IAbortableCmdSeq* sequence = this->BeginStep(stepName);	// (a part of the batch's step inside one)
		int32 done = KCMApplyRestoreAttr(target, job, outMessage);
		// ★★★**ALL THE WAY BACK, OR NOT AT ALL** (2026-09-24 night, the user's rule) - the marks of this kind over
		//   the change's characters have to read as the Source's, or the sequence is rolled back whole. ★Over the
		//   ROW'S range: the plan's window may be wider (KCMAttrRestorePlan grows it), and what the row promised is
		//   what is checked. ⚠A half write (done < 0) is rolled back the same way: it changed the document too.
		const bool16 same = (done >= 0)
			? this->PlaceReadsAsSource(target, source.GetUID(), live.fTargetStart, live.fTargetEnd, live, live.fAttrKind)
			: kFalse;
		this->EndStep(sequence, same);
		if (done >= 0 && !same)
		{
			outMessage = "the Source's marks did not come all the way back, so the restore was cancelled - the document is as it was";
			done = -1;
		}
		// ★★THE CHANGE STAYS ON THE ROW AS A RECORD (2026-09-24, stage 2 C): the "=" row over the same characters (a
		//   mark changes no length), with the ATTRIBUTE counter the restore left the story at (the text counter cannot
		//   see a mark - KCMStoryDiffRun::CountForKind says why).
		if (done >= 0)
			KCMStoryList::AddEqual(nth, live, live.fTargetStart, live.fTargetEnd,
									  KCMStoryDiffRun::CountForKind(target, live.fAttrKind), live.fAttrKind);
		// ★THE ROW IS COMPARED AGAIN either way: a rolled-back write leaves the document as it was, but the list is
		//   compared against what stands there, whatever that is.
		this->RefreshRow(nth);
		// ★AND THE "=" IS CHECKED ONCE MORE, AFTER THE REFRESH (KCMEqualRecord) - belt and braces.
		if (done >= 0 && !KCMStoryList::EqualStanding(nth, live, target.GetDataBase()))
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
		// ★A TASK START KEPT IN MEMORY CANNOT BE MATCHED FROM (2026-10-13): the match is checked afterwards against the
		//   Source's table as INX writes it, and a clone cannot be exported (KCMResourceSnapshot.cpp) - the table would
		//   be changed and then the whole match rolled back. Said before anything is touched.
		if (KCMIsExternalSource(source.GetDataBase()))
		{
			outMessage = "the Source is a Task Start kept in memory, whose tables cannot be read back - take a Task Start (Save)... to match a table";
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
		// ★ONE ABORTABLE STEP (2026-10-25, the tidy's S11 - it was a plain sequence until then): a plain sequence ended
		//   with the error raised kept its write when the call came from a script (EndOrAbort says what was measured);
		//   the abort takes the match back wherever it was called from.
		IAbortableCmdSeq* sequence = this->BeginStep("Match the Source");
		KCMTableMatchBefore tableBefore;
		int32 done = KCMMatchTableToSource(target, source, live.fTargetTableUID, live.fSourceTableUID, tableBefore, outMessage);
		// ★★★**ALL THE WAY, OR NOT AT ALL** (the user's rule, and 2026-09-25: "put it back from the Source, check it with
		//   INX afterwards, and if anything is wrong nothing comes back"): the table has to read as the match promised -
		//   by the text, by the table model's geometry, and by INX (KCMTableReadsAsSource says which part against what) -
		//   or the whole step is aborted. ⚠A half-done match (done < 0) is aborted the same way.
		std::string why;
		const bool16 same = (done >= 0)
			? KCMTableReadsAsSource(target, source, live.fTargetTableUID, live.fSourceTableUID, tableBefore, why)
			: kFalse;
		const bool16 kept = this->EndStep(sequence, same);
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
		else if (done >= 0 && !kept)
		{
			outMessage = "the match did not go in, so it was cancelled - the document is as it was";
			outMessage.SetTranslatable(kFalse);
			done = -1;
		}
		// ★★THE TABLE STAYS ON THE ROW AS A RECORD (2026-09-25): the "=" row over the table's anchor characters - as
		//   many as the Source's now - with the text counter the match left the story at; an undo of the match shows the
		//   live Table row again through it. (Its "Redo from Word" went on 2026-10-25; Ctrl+Z is the way back.)
		if (done >= 0)
			KCMStoryList::AddEqual(nth, live, live.fTargetStart,
									  live.fTargetStart + (live.fSourceEnd - live.fSourceStart),
									  KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone);
		// ★THE ROW IS COMPARED AGAIN either way: matched, the live Table row goes (the same table on both sides, by
		//   id) and the record stands "="; rolled back, the list is compared against what stands there.
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
		// ★THE DOCUMENT EXPORTED IS DECIDED HERE rather than in the UI: which document a menu item acts on is a
		//   model question, and the UI half already asks this facade every other such question.
		// ★★AND IT IS THE TASK DOCUMENT - the chosen Target, else the active one (2026-09-25, the user's decision): the
		//   one the import writes into (KCMTaskDocumentDB says what went wrong while the two were asked apart).
		// ⚠WHICH STORIES is the other half of that, and it is NOT a model question: a selection is
		//  the UI's own state. It arrives already resolved, and empty means all.
		// ⚠★A SELECTION MADE IN ANOTHER DOCUMENT IS REFUSED, NOT MATCHED: the export takes the listed stories by their
		//  uid alone, and another document's uids name whatever this document holds under the same numbers.
		IDataBase* const db = KCMTaskDocumentDB();
		if (!onlyThese.IsEmpty() && onlyThese.GetDataBase() != db)
		{
			outMessage = "the selection is in another document than the one exported";
			outMessage.SetTranslatable(kFalse);
			return kFalse;
		}
		return KCMExportStoryText(db, parent, onlyThese, outMessage);
	}

	virtual IDataBase*	GetTaskDocumentDB()	{ return KCMTaskDocumentDB(); }

	// (2026-10-25) Export Pages to Word. ⚠Declared at the END of IKCMStoryEditsFacade, written here beside the story export
	//   it borrows from - the vtable's order is the interface's.
	virtual bool16	ExportPagesAsWord(const IDFile& parent, const UIDList& pages, PMString& outMessage)
	{
		// ★THE PAGES ARE NAMED (2026-10-26, the import tidy S7): the menu's are the Pages panel's selection as the UI read it,
		//   a script's the ones it named. An empty list meant "the Pages panel's selection" read here until then - the model
		//   does not read the panel any more. ★Decided on in the one place the page items share (KCMPageMapPagesOf, masters
		//   left out - a master's items are every page's): pages of the list's document only, each once.
		IDataBase* db = nil;
		std::vector<UID> chosen;
		if (!KCMPageMapPagesOf(pages, kFalse /*no masters*/, db, chosen))
		{
			outMessage = "Export Pages to Word: no page is named - select pages in the Pages panel, or name them (\"1,3\")";
			outMessage.SetTranslatable(kFalse);
			return kFalse;
		}
		UIDList list(db);
		for (size_t i = 0; i < chosen.size(); ++i)
			list.Append(chosen[i]);
		return KCMExportPagesAsWord(db, list, parent, outMessage);
	}

	// (⛔GetPagesPanelSelection - the Pages panel's selection read by the model for the menu - went on 2026-10-26, the
	//  import tidy S7: the UI reads it, KCMReadSelectedPages. IKCMStoryEditsFacade.h keeps its tombstone.)

	// ---- ★THE TRACK CHANGES MODE (2026-10-05) - IKCMStoryEditsFacade.h says what each one answers ----
	// ⚠The change-row menu's actions (Restore from Source, Match the Source, Apply, Restore from Copy) are not offered in
	//   this mode (KCMStoryRefresh's ChangeRowMenuLive asks KCMModeDiffsStoryText); called anyway, each stops before
	//   writing - a Track change is never an attribute or a table, and the others find the Story list empty.
	virtual int32	GetTrackAuthorCount()	{ return KCMFacadeTrackMode() ? KCMTrackList::GetAuthorCount() : 0; }

	virtual bool16	GetTrackAuthor(int32 a, TrackAuthor& out)
	{
		const KCMTrackAuthor* au = KCMFacadeTrackMode() ? KCMTrackList::GetAuthor(a) : nil;
		if (au == nil)
			return kFalse;
		out.fName = au->fName;
		out.fName.SetTranslatable(kFalse);
		out.fFirstRow = au->fFirstRow;
		out.fRowCount = au->fRowCount;
		out.fChangeCount = au->fChangeCount;
		out.fHasColour = au->fHasColour;
		out.fR = au->fR; out.fG = au->fG; out.fB = au->fB;
		return kTrue;
	}

	virtual int32	GetTrackAuthorOfRow(int32 nth)
	{
		const KCMTrackRow* r = KCMFacadeTrackMode() ? KCMTrackList::GetRow(nth) : nil;
		return (r != nil) ? r->fAuthor : -1;
	}

	virtual bool16	GetTrackChange(int32 nth, int32 which, TrackChange& out)
	{
		const KCMTrackChange* c = KCMFacadeTrackMode() ? KCMTrackChangeAt(nth, which) : nil;
		if (c == nil)
			return kFalse;
		out.fAuthor = c->fAuthor;
		out.fKind = c->fKind;
		out.fTime = c->fTime;
		out.fHasColour = c->fHasColour;
		out.fR = c->fR; out.fG = c->fG; out.fB = c->fB;
		out.fHidden = c->fHidden;
		out.fSourceExact = c->fSourceExact;
		return kTrue;
	}

	virtual int32	GetTrackChangeTotal()	{ return KCMFacadeTrackMode() ? KCMTrackList::GetTotalChangeCount() : 0; }

	virtual bool16	TrackChangeStillRecorded(int32 nth, int32 which)
	{
		if (!KCMFacadeTrackMode())
			return kFalse;
		const KCMTrackRow* r = KCMTrackList::GetRow(nth);
		const KCMTrackChange* c = KCMTrackChangeAt(nth, which);
		IDataBase* const targetDB = KCMArmedTargetDB();
		if (r == nil || c == nil || targetDB == nil || !KCMIsDocDBOpen(targetDB))
			return kFalse;
		return KCMTrackRead::StillRecorded(targetDB, r->fRow.fStoryUID, *c);
	}

	virtual bool16	InImportMode()		// ⛔retired with the fourth mode (2026-09-20) - the slot stays
	{
		return kFalse;
	}

	// ---- ★THE IMPORT THE OTHER WAY ROUND (2026-10-24, adopted 2026-10-25) - IKCMStoryEditsFacade.h says what
	//      each one answers; the design is docs/superpowers/specs/2026-10-24-kcm-import-as-source-design.md ----------
	// ★APPLY IS THE IMPORT'S OWN ROAD (design 6): the words of ONE change by the import's one write command
	//   (ApplyWordsOne - since 2026-10-24 evening, design 11-1), or, when a change cannot be written on its own, by
	//   KCMPlanApplyParagraph + KCMApplyParagraphPlan (the import's comparison narrowed to a paragraph); a mark by Restore
	//   from Source (the Source IS the files), a table by KCMApplyTableFromImport (Match the Source reads the Source's table
	//   back through INX, which a Task Start in memory cannot give - MatchTable says so), a whole story or all of them by
	//   the import's pour. ★ALL OF THEM UNSIGNED AND UNTRACKED since the same evening (design 11-2 - they were signed and
	//   tracked until then). What went in is a "=" record - the record Restore from Source and Match the Source leave
	//   too, since "=" means the same thing for all three: the Target reads as the Source - holding the copy Restore
	//   from Copy pastes back (11-3) where one was kept.

	virtual bool16	ImportStoryTextAsSource(const SysFileList& files, PMString& outMessage)
	{
		return KCMImportStoryTextAsSource(files, outMessage);
	}

	virtual bool16	IsImportSource()
	{
		return (KCMIsArmed() && KCMIsImportClone(KCMArmedSourceDB())) ? kTrue : kFalse;
	}

	virtual bool16	CanApplyChange(int32 nth, int32 which)
	{
		IDataBase* targetDB = nil;
		Row row;
		Change change;
		return this->ApplicableChange(nth, which, targetDB, row, change);
	}

	virtual int32	ApplyChange(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		// ★THE COPY READ ONCE FOR THE WHOLE PRESS (2026-10-26 - KCMImportSourceReadScope): the table check below and the
		//   paragraph's plan read the same story - and a batch's scope (ApplyChanges) covers every row of it.
		KCMImportSourceReadScope oneReading;
		IDataBase* targetDB = nil;
		Row row;
		Change change;
		if (!this->ApplicableChange(nth, which, targetDB, row, change))
		{
			outMessage = "this row is not a change of the imported text that can be put in";
			return -1;
		}
		// ★A STORY WHOSE TABLES THE FILES ADD OR TAKE AWAY (measured 2026-10-24, matrix A55) is put in whole: the import
		//   pairs its paragraphs and tables only once its tables agree, so no single row of it can go in first.
		if (KCMImportStoryNeedsTableRound(UIDRef(targetDB, row.fStoryUID), row.fSourceStoryUID))
		{
			outMessage = "the imported text adds or takes away a table in this story - put the story in with Apply All "
						 "Changes in This Story (the tables go first)";
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		if (change.fWhat == Change::kWhatTable)
			return this->ApplyTable(nth, which, targetDB, row, outMessage);
		if (change.fWhat == Change::kWhatAttr && change.fAttrKind != kKCMStoryAttrFootnote)
		{
			const int32 done = this->RestoreAttrAs(nth, which, "Apply This Change", outMessage);
			KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
			// The restore says nothing when it succeeds (its menu item writes the sentence) - Apply says it here.
			if (done >= 0 && outMessage.IsEmpty())
			{
				outMessage = "applied the mark";
				if (done > 0)
				{
					outMessage.Append(" (");
					outMessage.AppendNumber(done);
					outMessage.Append(done == 1 ? " mark put on)" : " marks put on)");
				}
				else
					outMessage.Append(" (taken off - the imported text has none there)");
				outMessage.Append(" - Ctrl+Z takes it back");
				outMessage.SetTranslatable(kFalse);
			}
			return done;
		}
		// ★CHANGE BY CHANGE since 2026-10-24 evening (design section 11-1): the words of this change alone, or its
		//   paragraph's road when it cannot be written on its own (ApplyWordsOne says when).
		return this->ApplyWordsOne(nth, which, targetDB, row, outMessage);
	}

	virtual bool16	CanApplyStory(int32 nth)
	{
		if (KCMFacadeTrackMode() || !this->IsImportSource())
			return kFalse;
		IDataBase* targetDB = nil;
		Row row;
		if (!this->PairedTargetRow(nth, targetDB, row))
			return kFalse;
		std::vector<KCMStoryChange> lives;
		KCMStoryList::LiveChanges(nth, lives);
		return lives.empty() ? kFalse : kTrue;
	}

	virtual int32	ApplyStory(int32 nth, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		KCMImportSourceReadScope oneReading;		// (ApplyChange says why)
		IDataBase* targetDB = nil;
		Row row;
		if (!this->CanApplyStory(nth) || !this->PairedTargetRow(nth, targetDB, row))
		{
			outMessage = "this row is not a story of the imported text with anything to put in";
			return -1;
		}
		KCMStoryTextSet set;
		KCMStoryShape::Story word;
		// ★THE FILES' WORDS AS THE IMPORT'S COPY HOLDS THEM (2026-10-26, the import tidy S5 - KCMImportSourceStory: what the
		//   list compared the document with), by the story they named - the row's Source story, the copy's: another uid than
		//   the Target's for a row paired by place, whose Target story the pour below writes into (set.fUids).
		PMString whyNot;
		if (!KCMImportSourceStory(row.fSourceStoryUID, word, whyNot))
		{
			outMessage = whyNot;
			return -1;
		}
		set.fUids.push_back(row.fStoryUID);
		set.fStories.push_back(word);
		set.fFileNames.push_back(PMString());
		return this->PourIntoTarget(targetDB, set, std::vector<UID>(1, row.fStoryUID), "Apply All Changes in This Story", nth,
									outMessage);
	}

	virtual int32	ApplyAll(PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		KCMImportSourceReadScope oneReading;		// (ApplyChange says why)
		IDataBase* const targetDB = KCMArmedTargetDB();
		if (KCMFacadeTrackMode() || !this->IsImportSource() || targetDB == nil || !KCMIsDocDBOpen(targetDB))
		{
			outMessage = "no imported text is being compared";
			return -1;
		}
		// Every row of the list with something left to put in - the stories the import filled (the list holds no other).
		KCMStoryTextSet set;
		for (int32 r = 0; r < KCMStoryList::GetRowCount(); ++r)
		{
			IDataBase* db = nil;
			Row row;
			if (!this->PairedTargetRow(r, db, row))
				continue;
			std::vector<KCMStoryChange> lives;
			KCMStoryList::LiveChanges(r, lives);
			KCMStoryShape::Story word;
			PMString whyNot;
			if (lives.empty() || !KCMImportSourceStory(row.fSourceStoryUID, word, whyNot))	// the import's copy, by the story the files named (ApplyStory says why)
				continue;
			set.fUids.push_back(row.fStoryUID);
			set.fStories.push_back(word);
			set.fFileNames.push_back(PMString());
		}
		if (set.fUids.empty())
		{
			outMessage = "nothing left to put in";
			return 0;
		}
		return this->PourIntoTarget(targetDB, set, std::vector<UID>(), "Apply All Changes", -1, outMessage);
	}

	// ---- ★RESTORE FROM COPY (2026-10-24 evening - design section 11-3; the author: "when a row is put back by a right
	//   click, put it back from a Copy - the way KFC does; KFC greys Copy Undo when the copy would change a UID") -------

	virtual bool16	CanRestoreChangeFromCopy(int32 nth, int32 which)
	{
		IDataBase* targetDB = nil;
		Row row;
		if (!this->HasRestorableCopy(nth, which, targetDB, row))
			return kFalse;
		// ★Greyed, too, where a caret's place no longer reads as the Source's by its surroundings (CaretReadsAsSource).
		const KCMEqualRecord* const rec = KCMStoryList::EqualAt(nth, which);
		return (rec != nil && (rec->fNowEnd != rec->fNowStart
							   || KCMStoryList::CaretReadsAsSource(targetDB, row.fStoryUID, row.fSourceStoryUID, rec->fNowStart, rec->fLive.fSourceStart)))
			   ? kTrue : kFalse;
	}

	virtual int32	RestoreChangeFromCopy(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		IDataBase* targetDB = nil;
		Row row;
		Change before;
		if (!this->HasRestorableCopy(nth, which, targetDB, row) || !this->GetChange(nth, which, before))
		{
			// ★★THE LIST MAY BE OUT OF DATE FIRST (2026-10-26, the import tidy S10): a word typed before a "=" leaves its record
			//   reading stale at the old place - the record sits the list out, and this index no longer names it. Compared
			//   again, the record is placed anew (KCMStoryList::FollowRecords) and the reader is asked once more, as when the
			//   change at an index is no longer the same (SameChangeAfterRefresh). ⚠The panel's list does not follow plain
			//   typing (the Story mode's rule - KCMStoryFollowObserver); this is the command's own look.
			if (this->ChangedSinceCompared(nth))
			{
				this->RefreshRow(nth);
				fStale = kTrue;		// a batch leaves the rest of this row's indices (RunBatch)
				outMessage = "the list was out of date - it has been compared again; right-click the row once more";
				return -1;
			}
			outMessage = "this row is not a change Apply This Change put in with a copy kept";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME ROW (SameChangeAfterRefresh says why - the list does not
		//   follow every edit by itself).
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the row once more";
			return -1;
		}
		const KCMEqualRecord* const rec = KCMStoryList::EqualAt(nth, which);
		if (rec == nil || rec->fCopy.fWhyNot != kKCMCopyKept)
		{
			outMessage = "this row no longer holds its copy - compare again";
			return -1;
		}
		const KCMEqualRecord record = *rec;		// a copy: the list is rebuilt below
		const UIDRef target(targetDB, row.fStoryUID);
		// ★THE "=" HAS TO BE STANDING: Apply's words still where it put them. Words edited since are not overwritten - the
		//   reader's edit is not this command's to take away (KFC's "edited since").
		if (KCMStoryList::EqualStateOf(nth, record, targetDB) != kKCMEqualStanding)
		{
			outMessage = "the words Apply put in have been edited since - left as they are";
			return -1;
		}
		// ★A CARET'S PLACE HAS TO READ AS THE SOURCE'S BY ITS SURROUNDINGS (CaretReadsAsSource says why): otherwise the words
		//   would go back where an edit has moved them from.
		if (record.fNowEnd == record.fNowStart
			&& !KCMStoryList::CaretReadsAsSource(targetDB, row.fStoryUID, row.fSourceStoryUID, record.fNowStart, record.fLive.fSourceStart))
		{
			outMessage = "the place it went in has moved since (an edit before it) - left as it is; Ctrl+Z takes it back";
			return -1;
		}
		if (record.fCopy.fParaStyle != kInvalidUID && !ParagraphStyleStands(targetDB, record.fCopy.fParaStyle))
		{
			outMessage = "the paragraph style of the paragraph before it has been deleted since - left as it is";
			return -1;
		}
		// ★A WHOLE PARAGRAPH RESTORED FROM THE SOURCE WITH ITS OWN RETURN (KCMRecordCopy::fShift, 2026-10-24 night) is put
		//   back one character on as well - past the return that stands at the record's end, which has to be there still.
		const int32 shift = record.fCopy.fShift;
		if (shift != 0 && !ReturnAt(targetDB, row.fStoryUID, record.fNowEnd))
		{
			outMessage = "the paragraph break it was put in beside has gone since - left as it is";
			return -1;
		}
		IAbortableCmdSeq* sequence = this->BeginStep("Restore from Copy");	// (a part of the batch's step inside one)
		bool16 ok = KCMCopyStore::PasteOver(record.fCopy.fCopy, target, record.fNowStart + shift, record.fNowEnd + shift);
		InterfacePtr<ITextModel> model(target, UseDefaultIID());
		if (ok && record.fCopy.fParaStyle != kInvalidUID)
			ok = PutParagraphBack(model, record.fNowStart, record.fCopy);
		// ★ALL OF IT OR NONE OF IT: the place has to read as the words Apply wrote over, or the step is rolled back.
		if (ok && record.fHasLiveWords)
		{
			IDataBase::SaveRestoreModifiedState guard(targetDB);
			WideString now;
			ok = (KCMTextWords::WordsAt(model, record.fNowStart, record.fLiveWords.CharCount(), now)
				  && now == record.fLiveWords) ? kTrue : kFalse;
		}
		ok = (ok && ErrorUtils::PMGetGlobalErrorCode() == kSuccess) ? kTrue : kFalse;
		this->EndStep(sequence, ok);
		KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		if (!ok)
		{
			this->RefreshRow(nth);
			outMessage = "the copy could not be put back (a locked story or layer?) - nothing was changed";
			return -1;
		}
		// ★THE RECORD STAYS, MARKED PUT BACK (the counters' way - KCMEqualRecord): it reads as the live change from
		//   here, and as "=" again the moment the reader undoes this.
		KCMStoryList::MarkPutBack(nth, record, KCMStoryDiffRun::CountForKind(target, record.fCounterKind));
		this->RefreshRow(nth);
		outMessage = "restored from the copy - Ctrl+Z takes it back";
		return 1;
	}

	virtual bool16	GetChangeCopyState(int32 nth, int32 which, PMString& outWhyNot)
	{
		outWhyNot.Clear();
		outWhyNot.SetTranslatable(kFalse);
		const KCMEqualRecord* const rec = KCMStoryList::EqualAt(nth, which);
		if (rec == nil)
			return kFalse;
		if (rec->fCopy.fWhyNot == kKCMCopyKept && KCMCopyStore::Length(rec->fCopy.fCopy) >= 0)
			return kTrue;
		int32 why = (rec->fCopy.fWhyNot == kKCMCopyKept) ? kKCMCopyFailed : rec->fCopy.fWhyNot;
		// The records the other roads make (a table's, a mark's) were never asked for one - said for what they are.
		if (why == kKCMCopyNotTaken)
			why = (rec->fLive.fWhat == KCMStoryChange::kTable) ? kKCMCopyHoldsObject
				: (rec->fCounterKind != kKCMStoryAttrNone) ? kKCMCopyMark : kKCMCopyNotTaken;
		outWhyNot = CopyWhyNotWords(why);
		outWhyNot.SetTranslatable(kFalse);
		return kFalse;
	}

	virtual int32	GetProposedCount()
	{
		if (!this->IsImportSource())
			return -1;
		int32 proposed = 0;
		const int32 rows = this->GetRowCount();
		for (int32 r = 0; r < rows; ++r)
		{
			const int32 changes = this->GetChangeCount(r);
			for (int32 c = 0; c < changes; ++c)
			{
				Change change;
				if (this->GetChange(r, c, change) && !change.fReplaced && change.fWhat != Change::kWhatRefused)
					++proposed;
			}
		}
		return proposed;
	}

	// ---- ★THE LIST LIKE KFC'S (2026-10-24 night) - IKCMStoryEditsFacade.h says what each answers -------------------------

	virtual int32	GetChangePlaceReach(int32 nth, int32 which)
	{
		IDataBase* targetDB = nil;
		Row row;
		Change change;
		if (!this->PairedTargetRow(nth, targetDB, row) || !this->GetChange(nth, which, change))
			return 0;
		return KCMPlaceReach(UIDRef(targetDB, row.fStoryUID), change.fTargetStart, change.fTargetEnd);
	}

	// ---- ★RESTORE FROM SOURCE BY COPY, IN THE STORY MODE (design 1-8 - the author: "in Story mode, a function that puts
	//   the Source's text back using a copy of the Source - in the right-click menu", "on the Story row too") -----------

	virtual bool16	CanRestoreTextFromSource(int32 nth, int32 which)
	{
		IDataBase* targetDB = nil;
		Row row;
		Change change;
		return this->SourceTextChange(nth, which, targetDB, row, change);
	}

	virtual int32	RestoreTextFromSource(int32 nth, int32 which, PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		IDataBase* targetDB = nil;
		Row row;
		Change before;
		if (!this->SourceTextChange(nth, which, targetDB, row, before))
		{
			outMessage = "this row is not a text change of two documents that are open and compared";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the change once more";
			return -1;
		}
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;		// the model's own ranges - a whole paragraph's break and all
		IDataBase* const sourceDB = KCMArmedSourceDB();
		const UIDRef target(targetDB, row.fStoryUID);
		const UIDRef source(sourceDB, row.fSourceStoryUID);	// the row's Source story (AttrChangeInBoth says why)
		const TextIndex tFrom = live.fTargetStart;
		const TextIndex tTo = live.fTargetEnd;
		const TextIndex sFrom = live.fSourceStart;
		const TextIndex sTo = live.fSourceEnd;

		// ★BOTH SIDES READ, AND NO SAID BEFORE ANYTHING IS WRITTEN - the copy's rules (KFC's, and Apply's TakeApplyCopy).
		WideString targetWords;
		WideString sourceWords;
		int32 shift = 0;
		const char* whyNot = nil;
		{
			IDataBase::SaveRestoreModifiedState targetGuard(targetDB);
			IDataBase::SaveRestoreModifiedState sourceGuard(sourceDB);
			InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
			InterfacePtr<ITextModel> sModel(source, UseDefaultIID());
			if (!KCMCopyStore::InOneThread(tModel, tFrom, tTo) || !KCMCopyStore::InOneThread(sModel, sFrom, sTo)
				|| !KCMTextWords::WordsAt(tModel, tFrom, tTo - tFrom, targetWords)
				|| !KCMTextWords::WordsAt(sModel, sFrom, sTo - sFrom, sourceWords))
				whyNot = "its place could not be read as one stretch of one story thread";
			else if (HoldsObject(targetWords) || HoldsObject(sourceWords))
				whyNot = "its words hold an object (a table, a note, an anchored frame...) - copied, it would be made anew with a new UID";
			else if (TouchesLink(tModel, tFrom, tTo) || TouchesLink(sModel, sFrom, sTo))
				whyNot = "it touches a hyperlink or a cross-reference - a copy would make a second one";
			else if (HoldsReturn(targetWords) || HoldsReturn(sourceWords))
			{
				// ★A RETURN GOES ONLY AS A WHOLE PARAGRAPH'S OWN. A copied return brings the formatting of the paragraph it
				//   ends, so a range that joins or splits paragraphs would hand one paragraph's formatting to another.
				//   ★"\rNEW" (the break before it - KCMStoryChange::fBreakAt, the diff's usual form) GOES ONE CHARACTER ON,
				//   AS "NEW\r": the same characters, but the paragraph comes (or goes) with its own return and the one before
				//   keeps its own - "\rNEW" copied in would end the paragraph before with the Source's break and leave NEW
				//   on that paragraph's return. Possible while the paragraph's own return follows it, on the side that holds
				//   it - not for the last paragraph of a story or a cell, whose return is the thread's own.
				const bool16 deleting = (sourceWords.CharCount() == 0) ? kTrue : kFalse;
				const bool16 inserting = (targetWords.CharCount() == 0) ? kTrue : kFalse;
				if (!live.fWholeParagraph || (!deleting && !inserting))
					whyNot = "it joins or splits paragraphs - a copy cannot keep each paragraph's own formatting";
				else if (live.fBreakAt == kKCMBreakLeads)
				{
					const bool16 shifts = deleting
						? (ReturnAt(tModel, tFrom) && ReturnAt(tModel, tTo) && KCMCopyStore::InOneThread(tModel, tFrom + 1, tTo + 1))
						: (ReturnAt(sModel, sFrom) && ReturnAt(sModel, sTo) && KCMCopyStore::InOneThread(sModel, sFrom + 1, sTo + 1)
						   && ReturnAt(tModel, tFrom) && KCMCopyStore::InOneThread(tModel, tFrom + 1, tFrom + 1));
					if (shifts)
						shift = 1;
					else
						whyNot = "it is the last paragraph of its story (or cell) - a copy cannot bring its own paragraph formatting";
				}
				// (kKCMBreakTrails - "NEW\r", the first paragraph of its place - goes as it is: the return is its own.)
			}
		}
		if (whyNot != nil)
		{
			outMessage = whyNot;
			outMessage.Append(" - left as it is");
			outMessage.SetTranslatable(kFalse);
			return -1;
		}

		// THE TARGET'S OWN WORDS COPIED FIRST, for Restore from Copy (before the sequence - it writes the store only) - over
		// the range the write will cover, which the record then puts back over as well (KCMRecordCopy::fShift).
		std::vector<KCMRecordCopy> copies(1, this->TakeApplyCopy(target, tFrom + shift, tTo + shift, targetWords));
		copies[0].fShift = shift;

		IAbortableCmdSeq* sequence = this->BeginStep("Restore from Source");	// (a part of the batch's step inside one)
		// An empty Source range is a deletion: its place on the Source side does not matter (CopyOver deletes).
		const TextIndex fromStart = (sTo > sFrom) ? sFrom + shift : sFrom;
		const TextIndex fromEnd = (sTo > sFrom) ? sTo + shift : sFrom;
		const bool16 wrote = KCMCopyStore::CopyOver(source, fromStart, fromEnd, target, tFrom + shift, tTo + shift);
		// ★ALL OF IT OR NONE OF IT: the place has to read as the Source's before the step ends, or it is rolled back.
		const bool16 same = (wrote && ErrorUtils::PMGetGlobalErrorCode() == kSuccess
							 && this->PlaceReadsAsSource(target, source.GetUID(), tFrom, tFrom + (sTo - sFrom), live, kKCMStoryAttrNone))
							? kTrue : kFalse;
		this->EndStep(sequence, same);
		KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		if (!same)
		{
			this->RefreshRow(nth);
			outMessage = wrote ? "the place did not read as the Source after the copy - nothing was changed"
							   : "the copy could not be written (a locked story or layer?) - nothing was changed";
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		// THE "=" RECORD - this change alone, with the Target's copy: Restore from Copy puts the Target's own words back.
		std::vector<KCMStoryChange> group(1, live);
		std::vector<TextIndex> nowStarts(1, tFrom);
		std::vector<WideString> wordsBefore(1, targetWords);
		KCMStoryList::AddEqualGroup(nth, group, nowStarts, wordsBefore,
									   KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone, &copies);
		this->RefreshRow(nth);
		outMessage = "restored from the Source";
		if (copies[0].fWhyNot == kKCMCopyKept)
			outMessage.Append(" - right-click it: Restore from Copy, or Ctrl+Z");
		else
		{
			outMessage.Append(" - Ctrl+Z takes it back (no copy was kept: ");
			outMessage.Append(CopyWhyNotWords(copies[0].fWhyNot));
			outMessage.Append(")");
		}
		outMessage.SetTranslatable(kFalse);
		return 1;
	}

	virtual bool16	CanRestoreStoryFromSource(int32 nth)
	{
		const int32 changes = this->GetChangeCount(nth);
		for (int32 c = 0; c < changes; ++c)
			if (this->CanRestoreTextFromSource(nth, c))
				return kTrue;
		return kFalse;
	}

	virtual int32	RestoreStoryFromSource(int32 nth, PMString& outMessage)
	{
		std::vector<int32> rows;
		std::vector<int32> changes;
		const int32 count = this->GetChangeCount(nth);
		for (int32 c = 0; c < count; ++c)
		{
			if (!this->CanRestoreTextFromSource(nth, c))
				continue;
			rows.push_back(nth);
			changes.push_back(c);
		}
		if (changes.empty())
		{
			outMessage = "this story has no text change to restore from the Source";
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		return this->RunBatch(&rows[0], &changes[0], static_cast<int32>(changes.size()), "Restore All Changes in This Story",
							  &KCMStoryEditsFacade::RestoreTextFromSource, kTrue, outMessage);
	}

	// ---- ★SEVERAL ROWS AT ONCE, ONE UNDO STEP (design 1-4 / 1-5 - KFC's "rows selected together go in as one step") ----

	virtual int32	ApplyChanges(const int32* rows, const int32* changes, int32 count, PMString& outMessage)
	{
		KCMImportSourceReadScope oneReading;		// every row of the batch reads its story once (ApplyChange says why)
		return this->RunBatch(rows, changes, count, "Apply These Changes", &KCMStoryEditsFacade::ApplyChange, kFalse,
							  outMessage);
	}

	virtual int32	RestoreChangesFromCopy(const int32* rows, const int32* changes, int32 count, PMString& outMessage)
	{
		return this->RunBatch(rows, changes, count, "Restore These from Copy", &KCMStoryEditsFacade::RestoreChangeFromCopy,
							  kFalse, outMessage);
	}

	virtual int32	RestoreTextsFromSource(const int32* rows, const int32* changes, int32 count, PMString& outMessage)
	{
		return this->RunBatch(rows, changes, count, "Restore These from Source", &KCMStoryEditsFacade::RestoreTextFromSource,
							  kFalse, outMessage);
	}

private:
	/** Change `which` of row `nth` as Restore from Source (text) takes it: the Story mode - not the Track mode, not an
		import's copy as the Source, not a PDF at either end - both documents open, the story paired, a TEXT change that is
		not a "=" record (a record shown live - undone, or put back from its copy - is restored again, its record updated in
		place: AddEqualGroup's twin). Reads no document: whether the words can be copied is said on the press. */
	bool16 SourceTextChange(int32 nth, int32 which, IDataBase*& outTargetDB, Row& outRow, Change& outChange)
	{
		if (KCMFacadeTrackMode() || this->IsImportSource() || !this->PairedTargetRow(nth, outTargetDB, outRow))
			return kFalse;
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (sourceDB == nil || !KCMIsDocDBOpen(sourceDB) || KCMIsPdfDoc(sourceDB) || KCMIsPdfDoc(outTargetDB))
			return kFalse;
		return (this->GetChange(nth, which, outChange) && !outChange.fReplaced && outChange.fWhat == Change::kWhatText)
			   ? kTrue : kFalse;
	}

	/** One change's command, as RunBatch drives it - ApplyChange, RestoreChangeFromCopy, RestoreTextFromSource. */
	typedef int32 (KCMStoryEditsFacade::*OneChange)(int32 nth, int32 which, PMString& outMessage);

	/** ★SEVERAL CHANGES AS ONE UNDO STEP (2026-10-24 night - design 1-4/1-5/1-8, KFC's ReplaceRows): `one` over every
		(rows[i], changes[i]) inside ONE command sequence named `name` - BeginStep opens none of its own meanwhile, so
		nothing nests. By row, and bottom-up within a row: the list is in the story's order, so a higher index stands
		lower, and writing it first leaves the places above where they were. A change `one` refuses before it writes is
		left and said; once a row has been compared again because it was out of date, the rest of its indices (taken from
		the list before) are not trusted and are left too. The FIRST WRITE THAT DOES NOT GO IN (EndStep in the batch) stops
		it, and the whole step is rolled back. The panel is told once, at the end. One pair alone is `one` itself, with its
		own step, unless `oneAsBatch`. @return the changes done; -1 when nothing was written (outMessage). */
	int32 RunBatch(const int32* rows, const int32* changes, int32 count, const char* name, OneChange one, bool16 oneAsBatch,
				   PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		if (rows == nil || changes == nil || count <= 0)
		{
			outMessage = "no change was named";
			return -1;
		}
		if (count == 1 && !oneAsBatch)
			return (this->*one)(rows[0], changes[0], outMessage);
		if (fBatch)
		{
			outMessage = "another batch is still running";
			return -1;
		}
		std::vector<std::pair<int32, int32> > pairs;
		for (int32 i = 0; i < count; ++i)
			pairs.push_back(std::make_pair(rows[i], changes[i]));
		std::sort(pairs.begin(), pairs.end(), [](const std::pair<int32, int32>& a, const std::pair<int32, int32>& b)
				  { return (a.first != b.first) ? (a.first < b.first) : (a.second > b.second); });
		pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());

		// ★AN ABORTABLE SEQUENCE, ABORTED ON THE FIRST WRITE THAT DOES NOT GO IN - EndOrAbort says why (measured: a plain one
		//   ended with the error raised kept both writes and the step, tests-batch T-B3's first run). ICommandSequence.h:
		//   "regular command sequences cannot do this kind of error handling". The modified flag is read now for the abort.
		IDataBase* const batchDB = KCMArmedTargetDB();
		const bool16 wasModified = (batchDB != nil && KCMIsDocDBOpen(batchDB) && batchDB->IsModified()) ? kTrue : kFalse;
		IAbortableCmdSeq* sequence = CmdUtils::BeginAbortableCmdSeq("KCMStoryEditsBatch");
		if (sequence == nil)
		{
			outMessage = "InDesign would not start an undoable step - nothing was changed";
			return -1;
		}
		{
			PMString stepName(name);
			stepName.SetTranslatable(kFalse);
			sequence->SetName(stepName);
		}
		fBatch = kTrue;
		fBatchFailed = kFalse;
		int32 done = 0;
		int32 left = 0;
		int32 staleRow = -1;
		PMString firstWhy;
		PMString failure;
		std::set<int32> touched;
		for (size_t i = 0; i < pairs.size(); ++i)
		{
			const int32 r = pairs[i].first;
			touched.insert(r);
			if (r == staleRow)
			{
				++left;
				continue;
			}
			fStale = kFalse;
			PMString why;
			const int32 result = (this->*one)(r, pairs[i].second, why);
			// (Fault switch batch-fail, a test build's only - KCMDiag.h: a write after the first taken as not gone in.)
			if (!fBatchFailed && result >= 0 && done > 0 && KCM_DIAG_FAULT("batch-fail"))
			{
				fBatchFailed = kTrue;
				why = "the batch-fail switch is on";
			}
			if (fBatchFailed)
			{
				failure = why;
				break;
			}
			if (result < 0)
			{
				++left;
				if (firstWhy.IsEmpty())
					firstWhy = why;
				if (fStale)
					staleRow = r;
				continue;
			}
			++done;
		}
		const bool16 failed = fBatchFailed;
		// ★ALL OF IT OR NONE OF IT: one write that did not go in takes every write of the step with it (EndOrAbort).
		this->EndOrAbort(sequence, failed ? kFalse : kTrue, wasModified);
		IDataBase* const targetDB = KCMArmedTargetDB();
		if (targetDB != nil && KCMIsDocDBOpen(targetDB))
			KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		// Rolled back: the rows touched are compared again, against what stands (still quiet - fBatch is up).
		if (failed)
			for (std::set<int32>::const_iterator it = touched.begin(); it != touched.end(); ++it)
				this->RefreshRow(*it);
		fBatch = kFalse;
		fBatchFailed = kFalse;
		fStale = kFalse;
		KCMNotify(kKCMStoryEditsRebuiltMessage);

		if (failed)
		{
			outMessage = "a change could not be written";
			if (!failure.IsEmpty())
			{
				outMessage.Append(" (");
				outMessage.Append(failure);
				outMessage.Append(")");
			}
			outMessage.Append(" - the whole step was rolled back; nothing was changed");
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		if (done == 0)
		{
			outMessage = "nothing was changed - ";
			outMessage.Append(firstWhy.IsEmpty() ? PMString("no change could be done") : firstWhy);
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		outMessage = name;
		outMessage.Append(": ");
		outMessage.AppendNumber(done);
		outMessage.Append(done == 1 ? " change" : " changes");
		outMessage.Append(" in one step - Ctrl+Z takes it back");
		if (left > 0)
		{
			outMessage.Append(" - ");
			outMessage.AppendNumber(left);
			outMessage.Append(left == 1 ? " left as it is: " : " left as they are; the first: ");
			outMessage.Append(firstWhy);
		}
		outMessage.SetTranslatable(kFalse);
		return done;
	}

	/** A "=" row that holds its copy - Apply This Change's (an import's copy as the Source) or, since 2026-10-24 night,
		Restore from Source's (the Story mode, design 1-8): the story is paired, the row is a Standing record, and its copy was
		kept and is still in the store. The records of the other roads (a mark's restore, a match) hold none.
		(CanRestoreChangeFromCopy adds the caret's test; RestoreChangeFromCopy asks it after comparing again, to say why.) */
	bool16 HasRestorableCopy(int32 nth, int32 which, IDataBase*& outTargetDB, Row& outRow)
	{
		if (KCMFacadeTrackMode() || !this->PairedTargetRow(nth, outTargetDB, outRow))
			return kFalse;
		Change change;
		if (!this->GetChange(nth, which, change) || !change.fReplaced)
			return kFalse;
		const KCMEqualRecord* const rec = KCMStoryList::EqualAt(nth, which);
		return (rec != nil && rec->fCopy.fWhyNot == kKCMCopyKept && KCMCopyStore::Length(rec->fCopy.fCopy) >= 0)
			   ? kTrue : kFalse;
	}

	/** Change `which` of row `nth` as one Apply carries: the Source is an import's copy, the story is paired, and the
		change is LIVE - not a "=" record (taken in already), not a "!" row - of a kind Apply puts in: words (a footnote's
		marker with them), a ruby / kenten / warichu / tate-chu-yoko mark, or a table that stands on both sides. ⚠A table
		only one side has (Table + / −) is the story's to apply whole - its paragraph's own road cannot place it. */
	bool16 ApplicableChange(int32 nth, int32 which, IDataBase*& outTargetDB, Row& outRow, Change& outChange)
	{
		if (KCMFacadeTrackMode() || !this->IsImportSource())
			return kFalse;
		if (!this->PairedTargetRow(nth, outTargetDB, outRow))
			return kFalse;
		if (!this->GetChange(nth, which, outChange) || outChange.fReplaced)
			return kFalse;		// a "=" record: put in already (Restore from Copy or Ctrl+Z is its way back)
		// ★A RECORD THAT IS NOT "=" IS A CHANGE TO PUT IN AGAIN (corrected 2026-10-24 evening, the second try-out's tests):
		//   one whose Apply was undone, or that Restore from Copy put back (marked PutBack), reads as the live change it
		//   was - and the list shows the RECORD in its live twin's place (KCMMergeEqual hides the twin, "the record keeps
		//   the place a later write or put-back acts from"). This refused any record until then, on the belief that the twin
		//   stood beside it to be pressed - so a row put back from its copy could not be applied again (measured: "this row
		//   is not a change of the imported text that can be put in"). The write's record then updates that one in place
		//   (AddEqualGroup's twin).
		switch (outChange.fWhat)
		{
			case Change::kWhatText:
				return kTrue;
			case Change::kWhatAttr:
			{
				const int32 k = outChange.fAttrKind;
				return (k == kKCMStoryAttrRuby || k == kKCMStoryAttrKenten || k == kKCMStoryAttrWarichu
						|| k == kKCMStoryAttrTcy || k == kKCMStoryAttrFootnote) ? kTrue : kFalse;
			}
			case Change::kWhatTable:
				return (outChange.fKind == 0) ? kTrue : kFalse;
			default:
				return kFalse;
		}
	}

	/** The copy Apply This Change keeps of [from, to) of the document before it writes over it (design section 11-3) - or
		why it keeps none: an object's character in the words (a copy put back would make it anew, with a new UID - the
		author's rule, KFC's too), a hyperlink's or cross-reference's source touched (KFC's C4-2), the copy not made.
		Outside the document's sequence: it writes the store only (KCMCopyStore::Take). */
	KCMRecordCopy TakeApplyCopy(const UIDRef& target, TextIndex from, TextIndex to, const WideString& words)
	{
		KCMRecordCopy out;
		if (HoldsObject(words))
		{
			out.fWhyNot = kKCMCopyHoldsObject;
			return out;
		}
		{
			IDataBase::SaveRestoreModifiedState guard(target.GetDataBase());
			InterfacePtr<ITextModel> model(target, UseDefaultIID());
			if (TouchesLink(model, from, to))
			{
				out.fWhyNot = kKCMCopyTouchesLink;
				return out;
			}
		}
		out.fWhyNot = KCMCopyStore::Take(target, from, to, out.fCopy) ? kKCMCopyKept : kKCMCopyFailed;
		return out;
	}

	/** ★ONE CHANGE, ONE WRITE (2026-10-24 evening, design section 11-1 - the author: "not paragraph by paragraph -
		somehow, row by row, change by change"). The Target's [fTargetStart, fTargetEnd) is made the Source's
		[fSourceStart, fSourceEnd) by the import's one write command (KCMCreateWordsWriteCmd - a ReplaceCmd, an InsertCmd
		at a caret, a DeleteCmd for nothing), unsigned and untracked (11-2), the document's words copied first for
		Restore from Copy (11-3); the paragraph's other rows stay proposals. A change that cannot be written on its own
		goes its paragraph's road (ApplyWords), which says why: a whole paragraph (one row is its paragraph already), an
		object's character on either side (words written as text break objects - the 2026-09-21 rule), a return on
		either side (paragraphs joined or split), a place that cannot be read as one stretch of one story thread. */
	int32 ApplyWordsOne(int32 nth, int32 which, IDataBase* targetDB, const Row& row, PMString& outMessage)
	{
		Change before;
		if (!this->GetChange(nth, which, before))
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the change once more";
			return -1;
		}
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		if (live.fWholeParagraph)
			return this->ApplyWords(nth, which, targetDB, row, outMessage, nil);
		const UIDRef target(targetDB, row.fStoryUID);
		WideString liveWords;
		WideString sourceWords;
		bool16 readable = kFalse;
		{
			IDataBase::SaveRestoreModifiedState guard(targetDB);
			InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
			readable = (KCMCopyStore::InOneThread(tModel, live.fTargetStart, live.fTargetEnd)
						&& KCMTextWords::WordsAt(tModel, live.fTargetStart, live.fTargetEnd - live.fTargetStart, liveWords))
					   ? kTrue : kFalse;
		}
		IDataBase* const sourceDB = KCMArmedSourceDB();
		if (readable && sourceDB != nil && KCMIsDocDBOpen(sourceDB))
		{
			IDataBase::SaveRestoreModifiedState guard(sourceDB);
			InterfacePtr<ITextModel> sModel(UIDRef(sourceDB, row.fSourceStoryUID), UseDefaultIID());	// the copy's story the row compares against
			readable = KCMTextWords::WordsAt(sModel, live.fSourceStart, live.fSourceEnd - live.fSourceStart, sourceWords);
		}
		else
			readable = kFalse;
		const char* road = nil;
		if (!readable)
			road = "its place could not be read as one stretch of text";
		else if (HoldsObject(liveWords) || HoldsObject(sourceWords))
			road = "it moves or deletes a character InDesign hangs an object on";
		else if (HoldsReturn(liveWords) || HoldsReturn(sourceWords))
			road = "it joins or splits paragraphs";
		if (road != nil)
			return this->ApplyWords(nth, which, targetDB, row, outMessage, road);

		// THE COPY, before the sequence (it writes the store only).
		std::vector<KCMRecordCopy> copies(1, this->TakeApplyCopy(target, live.fTargetStart, live.fTargetEnd, liveWords));

		IAbortableCmdSeq* sequence = this->BeginStep("Apply This Change");	// (a part of the batch's step inside one)
		ErrorCode wrote = kFailure;
		{
			InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
			InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(tModel, live.fTargetStart,
																live.fTargetEnd - live.fTargetStart, sourceWords));
			wrote = (write != nil) ? CmdUtils::ProcessCommand(write) : kFailure;
		}
		// ★ALL OF IT OR NONE OF IT: the place has to read as the files' before the sequence ends, or it is rolled back.
		const bool16 same = (wrote == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess
							 && this->PlaceReadsAsSource(target, row.fSourceStoryUID, live.fTargetStart, live.fTargetStart + sourceWords.CharCount(),
														 live, kKCMStoryAttrNone)) ? kTrue : kFalse;
		this->EndStep(sequence, same);
		KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		if (!same)
		{
			this->RefreshRow(nth);
			outMessage = (wrote == kSuccess)
						 ? "the place did not read as the imported text after the write - nothing was put in"
						 : "the write failed (a locked story or layer?) - nothing was put in";
			return -1;
		}
		// THE "=" RECORD - this change alone, with its copy.
		std::vector<KCMStoryChange> group(1, live);
		std::vector<TextIndex> nowStarts(1, live.fTargetStart);
		std::vector<WideString> wordsBefore(1, liveWords);
		KCMStoryList::AddEqualGroup(nth, group, nowStarts, wordsBefore,
									   KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone, &copies);
		this->RefreshRow(nth);
		outMessage = "applied (1 write)";
		if (copies[0].fWhyNot == kKCMCopyKept)
			outMessage.Append(" - right-click it: Restore from Copy, or Ctrl+Z");
		else
		{
			outMessage.Append(" - Ctrl+Z takes it back (no copy was kept: ");
			outMessage.Append(CopyWhyNotWords(copies[0].fWhyNot));
			outMessage.Append(")");
		}
		outMessage.SetTranslatable(kFalse);
		return 1;
	}

	/** The words of one change - and of every change standing in the same paragraph, which the import's road writes
		together - put into the document as ONE undo step ("Apply This Change"), then "=" records for them and the row
		compared again. The geometry of the records (first worked out for the retired reject): the group's first Target
		position holds the Source's range from the group's first Source character to its last, each row its own share.
		★SINCE 2026-10-24 EVENING ONLY FOR WHAT CANNOT BE WRITTEN ON ITS OWN (ApplyWordsOne): `road` is why (nil for a
		whole paragraph), said in the message. A whole paragraph that goes in alone keeps a copy for Restore from Copy -
		what it took out (empty when it put one in) - and, put in with the break first or taken out from the return
		before it, the formatting of the paragraph before it (design 11-3); the rows of a paragraph's road keep none. */
	int32 ApplyWords(int32 nth, int32 which, IDataBase* targetDB, const Row& row, PMString& outMessage, const char* road)
	{
		Change before;
		if (!this->GetChange(nth, which, before))
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		// ★★COMPARED AGAIN FIRST, AND IT HAS TO BE THE SAME CHANGE - SameChangeAfterRefresh says why.
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the change once more";
			return -1;
		}
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		const UIDRef target(targetDB, row.fStoryUID);

		// ★PLANNED BEFORE THE SEQUENCE BEGINS: a refusal writes nothing and lands nothing on the undo stack.
		KCMStoryShape::Story now;
		KCMStorySync::Plan plan;
		int32 paraStart = 0;
		int32 paraEnd = 0;
		if (!KCMPlanApplyParagraph(target, row.fSourceStoryUID, live, now, plan, paraStart, paraEnd, outMessage))
			return -1;

		// ★THE ROWS THAT GO IN TOGETHER: the import's road writes the whole paragraph - every change standing in it - a
		//   paragraph taken out alone, and every paragraph the files put in at one place at once (KCMStorySync::Narrow).
		std::vector<KCMStoryChange> group;
		{
			std::vector<KCMStoryChange> lives;
			KCMStoryList::LiveChanges(nth, lives);
			const bool16 removing = (live.fWholeParagraph && live.fKind == KCMStoryChange::kInsert) ? kTrue : kFalse;
			const bool16 inserting = (live.fWholeParagraph && live.fKind == KCMStoryChange::kDelete) ? kTrue : kFalse;
			bool16 pressedIn = kFalse;
			for (size_t i = 0; i < lives.size(); ++i)
			{
				const KCMStoryChange& c = lives[i];
				if (c.fWhat == KCMStoryChange::kTable || c.fWhat == KCMStoryChange::kRefused)
					continue;
				bool16 inIt = kFalse;
				if (removing)
					inIt = SameStoryChange(c, live);
				else if (inserting)
					inIt = (c.fWholeParagraph && c.fKind == KCMStoryChange::kDelete && c.fTargetStart == live.fTargetStart)
						   ? kTrue : kFalse;
				else
				{
					TextIndex f = c.fTargetStart;
					TextIndex t = c.fTargetEnd;
					KCMShownSpan(c.fBreakAt, f, t);
					inIt = (!c.fWholeParagraph && f >= paraStart && t <= paraEnd) ? kTrue : kFalse;
				}
				if (!inIt)
					continue;
				group.push_back(c);
				if (SameStoryChange(c, live))
					pressedIn = kTrue;
			}
			if (!pressedIn)
				group.push_back(live);
			std::sort(group.begin(), group.end(), [](const KCMStoryChange& a, const KCMStoryChange& b)
					  { return (a.fSourceStart != b.fSourceStart) ? (a.fSourceStart < b.fSourceStart)
																  : (a.fTargetStart < b.fTargetStart); });
		}
		TextIndex tLo = group[0].fTargetStart;
		TextIndex sLo = group[0].fSourceStart;
		TextIndex sHi = group[0].fSourceEnd;
		for (size_t i = 1; i < group.size(); ++i)
		{
			tLo = std::min(tLo, group[i].fTargetStart);
			sLo = std::min(sLo, group[i].fSourceStart);
			sHi = std::max(sHi, group[i].fSourceEnd);
		}
		std::vector<WideString> liveWords;
		{
			IDataBase::SaveRestoreModifiedState guard(targetDB);
			InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
			for (size_t i = 0; i < group.size(); ++i)
			{
				WideString words;
				KCMTextWords::WordsAt(tModel, group[i].fTargetStart, group[i].fTargetEnd - group[i].fTargetStart, words);
				liveWords.push_back(words);
			}
		}
		// ★THE COPIES RESTORE FROM COPY PASTES BACK (design 11-3), taken before the sequence (they write the store only):
		//   a whole paragraph that goes in alone keeps what it takes out - nothing, when it puts one in - and, when it
		//   goes in with the break first or comes out from the return before it, the formatting of the paragraph before
		//   it, whose ending return the write changes. The rows of a paragraph's road keep none (several changes went in
		//   as one write), nor do paragraphs put in at one place together.
		std::vector<KCMRecordCopy> copies(group.size());
		for (size_t i = 0; i < copies.size(); ++i)
			copies[i].fWhyNot = (road != nil || !live.fWholeParagraph) ? kKCMCopyParagraphRoad : kKCMCopyGroup;
		if (road == nil && live.fWholeParagraph && group.size() == 1)
		{
			copies[0] = this->TakeApplyCopy(target, group[0].fTargetStart, group[0].fTargetEnd, liveWords[0]);
			if (copies[0].fWhyNot == kKCMCopyKept && group[0].fBreakAt == kKCMBreakLeads)
			{
				IDataBase::SaveRestoreModifiedState guard(targetDB);
				InterfacePtr<ITextModel> tModel(target, UseDefaultIID());
				if (!KeepParagraph(tModel, group[0].fTargetStart, copies[0]))
				{
					copies[0] = KCMRecordCopy();
					copies[0].fWhyNot = kKCMCopyFailed;
				}
			}
		}

		IAbortableCmdSeq* sequence = this->BeginStep("Apply This Change");	// (a part of the batch's step inside one)
		// ★UNSIGNED AND UNTRACKED (design 11-2, the author 2026-10-24: "if it is tracked, change it without tracking").
		const int32 done = KCMApplyParagraphPlan(target, now, plan, outMessage);
		// ★ALL OF IT OR NONE OF IT (every write's rule here): a write refused halfway is aborted whole.
		this->EndStep(sequence, (done > 0) ? kTrue : kFalse);
		KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		if (done <= 0)
		{
			this->RefreshRow(nth);
			return -1;
		}
		// ★THE "=" RECORDS, WHEN THE PLACE READS AS THE SOURCE'S. A record whose place does not would be Stale and shown as
		//   nothing (KCMEqualRecord), so it is not made: the row then shows what still differs, which is the truth.
		KCMStoryChange whole = live;
		whole.fSourceStart = sLo;
		whole.fSourceEnd = sHi;
		const bool16 same = this->PlaceReadsAsSource(target, row.fSourceStoryUID, tLo, tLo + (sHi - sLo), whole, kKCMStoryAttrNone);
		if (same)
		{
			std::vector<TextIndex> nowStarts;
			for (size_t i = 0; i < group.size(); ++i)
				nowStarts.push_back(tLo + (group[i].fSourceStart - sLo));
			KCMStoryList::AddEqualGroup(nth, group, nowStarts, liveWords,
										   KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone,
										   &copies);
		}
		this->RefreshRow(nth);

		outMessage = "applied (";
		outMessage.AppendNumber(done);
		outMessage.Append(done == 1 ? " write)" : " writes)");
		if (group.size() > 1)
		{
			outMessage.Append(live.fWholeParagraph ? " - the " : " - this paragraph's ");
			outMessage.AppendNumber(static_cast<int32>(group.size()));
			outMessage.Append(live.fWholeParagraph ? " paragraphs put in at this place went in together"
												   : " changes went in together");
		}
		// ★WHY THE PARAGRAPH'S ROAD (design 11-1): said, so that a change going in with its neighbours is not a surprise.
		if (road != nil)
		{
			outMessage.Append(group.size() > 1 ? ", because this one " : " - with its paragraph's road, because ");
			outMessage.Append(road);
		}
		if (!same)
			outMessage.Append(" - the place does not read exactly as the imported text; the row shows what still differs");
		outMessage.Append((same && copies.size() == 1 && copies[0].fWhyNot == kKCMCopyKept)
						  ? " - right-click it: Restore from Copy, or Ctrl+Z" : " - Ctrl+Z takes it back");
		outMessage.SetTranslatable(kFalse);
		return done;
	}

	/** A table that stands on both sides (Table ≠) made the files' - its shape and its cells' words - by
		KCMApplyTableFromImport, as ONE undo step, then a "=" record over its anchor characters (MatchTable's geometry). */
	int32 ApplyTable(int32 nth, int32 which, IDataBase* targetDB, const Row& row, PMString& outMessage)
	{
		Change before;
		if (!this->GetChange(nth, which, before))
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		if (!this->SameChangeAfterRefresh(nth, which, before))
		{
			outMessage = "the list was out of date - it has been compared again; right-click the table row once more";
			return -1;
		}
		const KCMStoryChange* const modelChange = KCMStoryList::GetMergedChange(nth, which);
		if (modelChange == nil)
		{
			outMessage = "this change is not in the list any more";
			return -1;
		}
		const KCMStoryChange live = *modelChange;
		if (live.fTargetTableUID == kInvalidUID)
		{
			outMessage = "the table is not named on the row - compare again";
			return -1;
		}
		const UIDRef target(targetDB, row.fStoryUID);
		IAbortableCmdSeq* sequence = this->BeginStep("Apply This Change");	// (a part of the batch's step inside one)
		const int32 done = KCMApplyTableFromImport(target, row.fSourceStoryUID, live.fTargetTableUID, outMessage);	// unsigned, untracked (design 11-2)
		this->EndStep(sequence, (done > 0) ? kTrue : kFalse);
		KCMStoryFollowEnsureObservers(targetDB);	// the Undo follower, attached again (PourIntoTarget says why)
		if (done > 0)
			KCMStoryList::AddEqual(nth, live, live.fTargetStart,
									  live.fTargetStart + (live.fSourceEnd - live.fSourceStart),
									  KCMStoryDiffRun::CountForKind(target, kKCMStoryAttrNone), kKCMStoryAttrNone);
		this->RefreshRow(nth);
		if (done <= 0)
			return -1;
		outMessage = "applied the table (";
		outMessage.AppendNumber(done);
		outMessage.Append(done == 1 ? " move or write)" : " moves and writes)");
		outMessage.Append(" - Ctrl+Z takes it back");
		outMessage.SetTranslatable(kFalse);
		return done;
	}

	/** The import's pour into the TARGET - unsigned and untracked since 2026-10-24 (design 11-2: the author asked for
		Apply's writes to go in without a change history; they were signed and tracked until then), the "!" rows left as
		the import noted them - as ONE undo step named `stepName`, then the row (oneRow >= 0) or every row compared
		again. */
	int32 PourIntoTarget(IDataBase* targetDB, const KCMStoryTextSet& set, const std::vector<UID>& only,
						 const char* stepName, int32 oneRow, PMString& outMessage)
	{
		// ★★THE ROWS STAY, AS "=" (2026-10-26, the author: "Apply on the Story row makes the rows go - keep them"; "the same on
		//   Proposed Edits"): each poured row's live changes - not its "!" rows - and the document's words at each, taken
		//   BEFORE the pour, become its "=" records after it (KCMStoryList::RecordPour). A row whose story is not poured keeps
		//   its records as they are: nothing wrote over them.
		std::vector<int32> pouredRows;
		std::vector< std::vector<KCMStoryChange> > pouredLives;
		std::vector< std::vector<WideString> > pouredWords;
		for (int32 r = 0; r < KCMStoryList::GetRowCount(); ++r)
		{
			if (oneRow >= 0 && r != oneRow)
				continue;
			IDataBase* rowDB = nil;
			Row row;
			if (!this->PairedTargetRow(r, rowDB, row)
				|| std::find(set.fUids.begin(), set.fUids.end(), row.fStoryUID) == set.fUids.end()
				|| (!only.empty() && std::find(only.begin(), only.end(), row.fStoryUID) == only.end()))
				continue;
			std::vector<KCMStoryChange> lives;
			KCMStoryList::LiveChanges(r, lives);
			std::vector<KCMStoryChange> going;
			std::vector<WideString> words;
			{
				IDataBase::SaveRestoreModifiedState guard(targetDB);
				InterfacePtr<ITextModel> tModel(UIDRef(targetDB, row.fStoryUID), UseDefaultIID());
				for (size_t i = 0; i < lives.size(); ++i)
				{
					if (lives[i].fWhat == KCMStoryChange::kRefused)
						continue;
					WideString w;
					if (tModel != nil && lives[i].fTargetEnd > lives[i].fTargetStart)
						KCMTextWords::WordsAt(tModel, lives[i].fTargetStart, lives[i].fTargetEnd - lives[i].fTargetStart, w);
					going.push_back(lives[i]);
					words.push_back(w);
				}
			}
			pouredRows.push_back(r);
			pouredLives.push_back(going);
			pouredWords.push_back(words);
		}

		KCMPourOptions options;
		options.fNoteRefusals = kFalse;
		options.fOnly = only;
		options.fStepName = stepName;
		options.fStepName.SetTranslatable(kFalse);
		PMString poured;
		bool16 cancelled = kFalse;
		const bool16 any = KCMPourStoryTextWith(targetDB, set, options, poured, cancelled);
		// ★THE POURED ROWS' RECORDS, KEPT AND ADDED TO (2026-10-26 - until then they went: DropRecords, the 10-24 night
		//   re-check; the compare below now places them from their Source ranges, KCMStoryList::RecordPour says why). Only
		//   when something went in - a cancelled pour put nothing in.
		if (any && !cancelled)
		{
			for (size_t i = 0; i < pouredRows.size(); ++i)
			{
				IDataBase* rowDB = nil;
				Row row;
				if (!this->PairedTargetRow(pouredRows[i], rowDB, row))
					continue;
				const UIDRef target(targetDB, row.fStoryUID);
				std::vector<uint32> counters;
				std::vector<int32> kinds;
				for (size_t k = 0; k < pouredLives[i].size(); ++k)
				{
					const KCMStoryChange& c = pouredLives[i][k];
					const int32 kind = (c.fWhat == KCMStoryChange::kAttr) ? c.fAttrKind : kKCMStoryAttrNone;
					kinds.push_back(kind);
					counters.push_back(KCMStoryDiffRun::CountForKind(target, kind));
				}
				KCMStoryList::RecordPour(pouredRows[i], pouredLives[i], pouredWords[i], counters, kinds);
			}
		}
		// ★THE UNDO FOLLOWER ATTACHED AGAIN (measured 2026-10-24 on the application: after "Apply All Changes in This
		//   Story" an Undo put the words back and the list did not follow - its observer had gone from the story, the
		//   loss memory observer-lost-when-object-purged describes). Attaching is idempotent (IsAttached first).
		//   ★Since 2026-10-25 the follower is on the DOCUMENT (KCMStoryFollowObserver.h), which is not built again while
		//   open: this call - and the other "attached again" ones - only make sure it is there.
		KCMStoryFollowEnsureObservers(targetDB);
		if (oneRow >= 0)
			this->RefreshRow(oneRow);
		else
		{
			IDataBase* const sourceDB = KCMArmedSourceDB();
			for (int32 r = 0; r < KCMStoryList::GetRowCount(); ++r)
				KCMStoryDiffRun::RunOne(targetDB, sourceDB, r);
			KCMNotify(kKCMStoryEditsRebuiltMessage);
		}
		if (cancelled)
		{
			outMessage = "cancelled - nothing was put in";
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		outMessage = "applied: ";
		outMessage.Append(poured);
		outMessage.Append(" - Ctrl+Z takes it back");
		outMessage.SetTranslatable(kFalse);
		return any ? 1 : 0;
	}

	// ---- ★A BATCH'S STATE (2026-10-24 night - RunBatch). The facade is one object for the session; a batch lives inside
	// one call on the main thread, so these are up only while it runs.
	bool16	fBatch;			// the batch's one sequence is open: BeginStep opens none of its own, RefreshRow tells nobody
	bool16	fBatchFailed;	// ... and a write inside it did not go in (EndStep) - the whole step is rolled back
	bool16	fStale;			// SameChangeAfterRefresh found the list out of date since this was last cleared
	bool16	fStepWasModified;	// BeginStep's reading of the Target's modified flag - what an abort puts back (EndOrAbort)

public:
	// ---- ★ACCEPT AND REJECT IN THE TRACK MODE (2026-10-25) - IKCMStoryEditsFacade.h says what each answers; the design
	//      is docs/superpowers/specs/2026-10-25-kcm-track-accept-reject-design.md. Each scope gathers the records its rows
	//      were made of (KCMTrackChange::fRecords) and hands them to ResolveTrack - one step, the list read again. ----------

	virtual bool16	CanResolveTrackChange(int32 nth, int32 which)
	{
		IDataBase* const targetDB = KCMArmedTargetDB();
		return (KCMFacadeTrackMode() && KCMIsArmed() && targetDB != nil && KCMIsDocDBOpen(targetDB)
				&& KCMTrackChangeAt(nth, which) != nil) ? kTrue : kFalse;
	}

	virtual int32	ResolveTrackChanges(const int32* rows, const int32* changes, int32 count, bool16 accept,
										PMString& outMessage)
	{
		std::vector<KCMTrackResolveItem> items;
		int32 named = 0;
		for (int32 i = 0; rows != nil && changes != nil && i < count; ++i)
			if (this->AddTrackChangeItems(rows[i], changes[i], items))
				++named;
		const char* step = (named > 1) ? (accept ? "Accept Changes" : "Reject Changes")
									   : (accept ? "Accept Change" : "Reject Change");
		return this->ResolveTrack(items, named, accept, step, outMessage);
	}

	virtual int32	ResolveTrackRow(int32 nth, bool16 accept, PMString& outMessage)
	{
		std::vector<KCMTrackResolveItem> items;
		const int32 named = this->AddTrackRowItems(nth, items);
		return this->ResolveTrack(items, named, accept,
								  accept ? "Accept Author's Changes in Story" : "Reject Author's Changes in Story", outMessage);
	}

	virtual int32	ResolveTrackAuthor(int32 a, bool16 accept, PMString& outMessage)
	{
		std::vector<KCMTrackResolveItem> items;
		int32 named = 0;
		const KCMTrackAuthor* const author = KCMFacadeTrackMode() ? KCMTrackList::GetAuthor(a) : nil;
		if (author != nil)
			for (int32 r = author->fFirstRow; r < author->fFirstRow + author->fRowCount; ++r)
				named += this->AddTrackRowItems(r, items);
		return this->ResolveTrack(items, named, accept,
								  accept ? "Accept All Changes by Author" : "Reject All Changes by Author", outMessage);
	}

	virtual int32	ResolveTrackAll(bool16 accept, PMString& outMessage)
	{
		std::vector<KCMTrackResolveItem> items;
		int32 named = 0;
		if (KCMFacadeTrackMode())
			for (int32 r = 0; r < KCMTrackList::GetRowCount(); ++r)
				named += this->AddTrackRowItems(r, items);
		return this->ResolveTrack(items, named, accept, accept ? "Accept All Changes" : "Reject All Changes", outMessage);
	}

private:
	/** The records change `which` of row `nth` was made of, onto `out`, with its story and its author's name.
		@return kFalse when it is no Track change. */
	bool16 AddTrackChangeItems(int32 nth, int32 which, std::vector<KCMTrackResolveItem>& out)
	{
		const KCMTrackRow* const row = KCMFacadeTrackMode() ? KCMTrackList::GetRow(nth) : nil;
		const KCMTrackChange* const change = (row != nil) ? KCMTrackChangeAt(nth, which) : nil;
		const KCMTrackAuthor* const author = (change != nil) ? KCMTrackList::GetAuthor(change->fAuthor) : nil;
		if (author == nil)
			return kFalse;
		for (size_t k = 0; k < change->fRecords.size(); ++k)
		{
			KCMTrackResolveItem item;
			item.fStory = row->fRow.fStoryUID;
			item.fAuthor = author->fName;
			item.fRecord = change->fRecords[k];
			out.push_back(item);
		}
		return kTrue;
	}

	/** Every change of row `nth` onto `out`. @return how many changes. */
	int32 AddTrackRowItems(int32 nth, std::vector<KCMTrackResolveItem>& out)
	{
		const KCMTrackRow* const row = KCMFacadeTrackMode() ? KCMTrackList::GetRow(nth) : nil;
		int32 named = 0;
		for (int32 c = 0; row != nil && c < static_cast<int32>(row->fChanges.size()); ++c)
			if (this->AddTrackChangeItems(nth, c, out))
				++named;
		return named;
	}

	/** ONE abortable undo step named `stepName` (BeginStep / EndStep - EndOrAbort says why abortable) around
		KCMTrackResolve::Run, then the Track list read again, kept or rolled back, and the Undo follower attached again
		(the 2026-10-24 measurement PourIntoTarget records). `changes` is how many changes the records came from - the
		number the sentence says. */
	int32 ResolveTrack(const std::vector<KCMTrackResolveItem>& items, int32 changes, bool16 accept, const char* stepName,
					   PMString& outMessage)
	{
		outMessage.Clear();
		outMessage.SetTranslatable(kFalse);
		IDataBase* const targetDB = KCMArmedTargetDB();
		if (!KCMFacadeTrackMode() || !KCMIsArmed() || targetDB == nil || !KCMIsDocDBOpen(targetDB))
		{
			outMessage = "the Track Changes comparison is not running";
			return -1;
		}
		if (items.empty())
		{
			outMessage = "no tracked change was named";
			return -1;
		}
		IAbortableCmdSeq* sequence = this->BeginStep(stepName);
		if (sequence == nil)
		{
			outMessage = "InDesign would not start an undoable step - nothing was changed";
			return -1;
		}
		int32 gone = 0;
		PMString why;
		const int32 done = KCMTrackResolve::Run(targetDB, items, accept, gone, why);
		const bool16 kept = this->EndStep(sequence, (done > 0) ? kTrue : kFalse);
		KCMTrackRead::Build(targetDB, KCMArmedSourceDB(), nil);
		KCMStoryFollowEnsureObservers(targetDB);
		KCMNotify(kKCMStoryEditsRebuiltMessage);
		if (done < 0)
		{
			outMessage = why;
			outMessage.Append(" - the step was rolled back; nothing was changed");
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		if (done == 0)
		{
			outMessage = "Nothing to do - these changes are no longer recorded (accepted, rejected or undone since the list "
						 "was read).";
			outMessage.SetTranslatable(kFalse);
			return 0;
		}
		if (!kept)
		{
			outMessage = "The step was rolled back - nothing was changed.";
			outMessage.SetTranslatable(kFalse);
			return -1;
		}
		outMessage = accept ? "Accepted: " : "Rejected: ";
		outMessage.AppendNumber(changes);
		outMessage.Append((changes == 1) ? " change" : " changes");
		if (gone > 0)
		{
			outMessage.Append(" (");
			outMessage.AppendNumber(gone);
			outMessage.Append((gone == 1) ? " record was" : " records were");
			outMessage.Append(" no longer recorded)");
		}
		outMessage.Append(" - Ctrl+Z takes it back.");
		outMessage.SetTranslatable(kFalse);
		return done;
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
								  UID sourceStoryUID, TextIndex sourceFrom, TextIndex sourceTo);

	virtual void	ShowJumpFlashColoured(IDataBase* db, UID storyUID,
										  TextIndex from, TextIndex to,
										  TextIndex sourceFrom, TextIndex sourceTo,
										  bool16 hasColour, uint8 r, uint8 g, uint8 b);
	// No ShutdownMarks: teardown is model-side only and KCMPeek.cpp calls the marker directly
	// (IKCMStoryMarkFacade.h says why a boundary method with no caller is worse than none).
};

CREATE_PMINTERFACE(KCMStoryMarkFacade, kKCMStoryMarkFacadeImpl)

void KCMStoryMarkFacade::ShowJumpFlash(IDataBase* db, UID storyUID,
										 TextIndex from, TextIndex to,
										 UID sourceStoryUID, TextIndex sourceFrom, TextIndex sourceTo)
{
	KCMStoryMarkDocs flash;
	KCMStoryMarker::AddFlashRange(flash, db, storyUID, from, to);

	// THE ROW'S SOURCE STORY, IN THE OTHER DOCUMENT (2026-10-26 - it was storyUID itself until then): the same uid for
	// every row paired by UID, which is what the whole Story mode is built on - the diff pairs stories by uid, the click
	// selects by uid, and the standing marks light both by uid (KCMStoryMarkBuild) - and the Source's own story for a row
	// paired by the place of its first frame (KCMStoryKinds.h, kKCMStoryKindByPosition). kInvalidUID lights nothing there.
	// @warning the general rule that a uid names a DIFFERENT object in another document is true, and for a row paired by
	// place it is exactly why the Source's story is handed in on its own. Believing it of every row is what once left the
	// older window bare.
	// @warning `db` is NOT always the target: a Removed story is read out of the older document,
	// and then this test is what stops the same document being marked twice (which would invert
	// twice and leave a hole - KCMStoryMarkRanges.h).
	// The test lives here rather than at the caller because "is the older window open" is a fact
	// about the comparison, and the comparison is this side's ([[one-question-one-place]]).
	IDataBase* const flashSourceDB = KCMArmedSourceDB();
	if (sourceStoryUID != kInvalidUID && flashSourceDB != nil && flashSourceDB != db && KCMIsDocDBOpen(flashSourceDB))
	{
		KCM_DIAG_LOG("flash source story=%u [%d,%d)", static_cast<unsigned>(sourceStoryUID.Get()),
					 static_cast<int>(sourceFrom), static_cast<int>(sourceTo));	// test builds: which story is lit over there
		KCMStoryMarker::AddFlashRange(flash, flashSourceDB, sourceStoryUID, sourceFrom, sourceTo);
	}

	KCMStoryMarker::ShowFlash(flash);
}

/* ShowJumpFlash in the Track mode's author colour (2026-10-05). The Source window is lit only when the change's
   place in the copy is known (sourceFrom >= 0) - otherwise its range is the story's start, and a flash there
   would point at nothing. */
void KCMStoryMarkFacade::ShowJumpFlashColoured(IDataBase* db, UID storyUID,
												TextIndex from, TextIndex to,
												TextIndex sourceFrom, TextIndex sourceTo,
												bool16 hasColour, uint8 r, uint8 g, uint8 b)
{
	KCMStoryMarkDocs flash;
	KCMStoryMarker::AddFlashRange(flash, db, storyUID, from, to, hasColour, r, g, b);
	IDataBase* const flashSourceDB = KCMArmedSourceDB();
	if (sourceFrom >= 0 && flashSourceDB != nil && flashSourceDB != db && KCMIsDocDBOpen(flashSourceDB))
		KCMStoryMarker::AddFlashRange(flash, flashSourceDB, storyUID, sourceFrom, sourceTo, hasColour, r, g, b);
	KCMStoryMarker::ShowFlash(flash);
}

// End of KCMFacades.cpp.
