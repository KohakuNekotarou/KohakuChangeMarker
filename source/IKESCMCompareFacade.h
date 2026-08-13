//========================================================================================
//
//  IKESCMCompareFacade.h
//
//  What the UI may ask of the comparison engine.
//
//  Created 2026-08-13 for the model/UI split (Stage 1). Modelled on the SDK's own pair
//  sample: customconditionaltext (model) publishes ICusCondTxtFacade.h and AddIns it to
//  kUtilsBoss; customconditionaltextui (UI) calls Utils<ICusCondTxtFacade>() and nothing else.
//
//  ★WHY AN INTERFACE AND NOT A HEADER OF FUNCTIONS. Once the model and the UI are separate
//  plug-ins, a free function declared in a shared header cannot be linked -- its body lives in
//  the other .pln. Every call that crosses the boundary has to go through an interface on a
//  boss. That is the whole reason this file exists.
//
//========================================================================================

#ifndef __IKESCMCompareFacade_h__
#define __IKESCMCompareFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"
#include "PMReal.h"		// GetBaseScreenOpacity

// Project includes:
#include "KESCMID.h"

class IDataBase;
class IDocument;

class IKESCMCompareFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKESCMCOMPAREFACADE };

	// ---- starting and stopping ----------------------------------------------------------

	/** The Start/Stop toggle. When armed, clears; when not armed, resolves the active
		document as Target and another open document as Source, and starts. */
	virtual void		ToggleStartStop() = 0;

	/** Stop: remove the marks and disarm the peek. */
	virtual void		StopComparison() = 0;

	/** Start with these two documents. No resolution happens here -- the caller decides which
		is the Target. nil does nothing.
		@warning overwrites an existing armed state without asking; call StopComparison first
		if you want "stop, then start". */
	virtual void		StartComparisonFor(IDocument* target, IDocument* source) = 0;

	/** Whether a comparison can be started: an active document plus at least one other open
		document. Goes through the same resolver as the start branch, so what the menu shows
		and what pressing it does cannot disagree. */
	virtual bool16		CanStartComparison() = 0;

	// ---- state -------------------------------------------------------------------------

	/** kTrue between Start and Stop. */
	virtual bool16		IsArmed() = 0;
	virtual IDataBase*	GetArmedTargetDB() = 0;
	virtual IDataBase*	GetArmedSourceDB() = 0;

	// ---- running a comparison ----------------------------------------------------------

	/** Compare every page of targetDB against the same-numbered page of sourceDB and rebuild
		the mark overlay. outReport receives the same status string the script API returns.

		allowIncremental=kTrue attempts a differential re-comparison: pages whose pairing is
		unchanged since the last run reuse their previous result instead of being rasterised
		again. That is safe ONLY for the Register toggle, where the document content does not
		change and only the pairing moves. Pass kFalse (the default) for Start and for the
		Ignore Page Number Marker toggle, where content or exclusions may differ. */
	virtual ErrorCode	MarkChanges(IDataBase* targetDB, IDataBase* sourceDB,
								PMString& outReport, bool16 allowIncremental = kFalse) = 0;

	/** Re-compare only the pages selected in the Pages panel. Returns kFalse when there was
		nothing to do. Cancelling here KEEPS what was already refreshed -- this is a partial
		update, so stopping half way leaves a narrower version of the same operation. */
	virtual bool16		RefreshSelectedPages(int32* outPages, int32* outChanged,
								bool16* outCancelled = nil, int32* outFailed = nil) = 0;

	/** Whether "Refresh Page Comparison" may be offered right now. */
	virtual bool16		RefreshComparisonAvailable() = 0;

	/** Throw away the whole overlay (and the cached old-version images) and redraw db. */
	virtual void		ClearMarks(IDataBase* db) = 0;

	// ---- mark display settings ---------------------------------------------------------

	/** Whether marks are printed (and therefore also shown on screen at all times), and the
		frame opacity: kTrue=25%, kFalse=75%. */
	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db) = 0;
	virtual void		TogglePrintMarks() = 0;
	virtual void		SetMarkOpacity25(bool16 op25) = 0;
	virtual bool16		GetPrintMarks() = 0;
	virtual bool16		GetMarkOpacity25() = 0;

	// ---- the status line ---------------------------------------------------------------

	/** The last string the model published. The UI reads this when it receives
		kKESCMStatusTextMessage and again from AutoAttach when the panel re-appears.
		Kept on the model side so app.kcmStatus can answer while the panel is closed. */
	virtual void		GetSessionStatus(PMString& out) = 0;

	// ---- the peek overlay --------------------------------------------------------------

	/** Whether both armed documents are still open. Returns kFalse after running the full
		Stop-equivalent clean-up, so a caller that gets kFalse must not touch either database.
		★The gesture code asks this before every peek and on every drag update: a closed
		IDataBase* is a dangling pointer whose address gets reused. */
	virtual bool16		ArmedDocsAlive() = 0;

	/** Show the old version of the spread under the pointer, over the front layout view.
		Rasterises that one spread on first use and keeps exactly one cached.
		⚠ Still resolves the view under the pointer internally, which is the last piece of
		reverse flow left in the model (see the ledger §2-2). Moving that resolution out to the
		caller is a behaviour-affecting change, so it is deliberately NOT part of this pass. */
	virtual void		ShowPeekUnderMouse(IDataBase* targetDB, IDataBase* sourceDB) = 0;

	/** The on-screen opacity marks are drawn at when they are shown permanently (printing ON
		gives the 25%/75% choice, printing OFF gives fully opaque). The UI needs it when the
		"Hold to Hide Marks" toggle flips, to put the permanent value back straight away. */
	virtual PMReal		GetBaseScreenOpacity() = 0;

	// ---- overset -----------------------------------------------------------------------

	/** Scan db for overset locations and store them in the engine state. Emits
		kKESCMOversetRescannedMessage. Does not write the status line. */
	virtual void		ApplyOversetForDoc(IDataBase* db) = 0;

	/** Which document an overset scan should look at: the comparison Target while a comparison
		is running, the active document otherwise. nil when there is nothing to scan.
		★Added over the plan's draft interface (2026-08-13): the Find Overset / Refresh Overset
		menu handlers and UpdateActionStates all ask this before calling ApplyOversetForDoc, so
		leaving it out would have left three UI callers reaching into the model directly. */
	virtual IDataBase*	GetOversetScanTargetDB() = 0;

	// ---- Hide Unchanged Spreads --------------------------------------------------------

	/** Reset the toggle on both sides. restoreSpreads=kTrue shows the spreads we hid again
		before dropping the state; document liveness is checked internally, so kTrue is safe
		even when one side has been closed. */
	virtual void		ResetHideUnchanged(bool16 restoreSpreads) = 0;
	virtual IDataBase*	GetHideUnchangedDB() = 0;
	virtual IDataBase*	GetHideUnchangedSrcDB() = 0;

	/** The toggle itself, and its state for the menu's check mark.
		★Added over the plan's draft interface (2026-08-13): the flyout item that flips it stays
		on the UI side, so both of these cross the boundary. */
	virtual void		HideUnchangedToggle() = 0;
	virtual bool16		GetHideUnchangedOn() = 0;
};

#endif // __IKESCMCompareFacade_h__
