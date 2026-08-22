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
// ★2026-08-17 (bug recheck B2): KESCMID.h だったのを境界のヘッダーへ絞った。ここが要るのは
//   自分の IID(IID_IKESCMCOMPAREFACADE)だけで、それは KESCMBoundaryID.h にある——そして
//   あちらの :25 が「ここに置いてよいのは境界の ID だけ。**model 専用は KESCMID.h**」と自分で
//   線を引いている。KESCMID.h を引くと、UI が include するこのヘッダーが model 専用の
//   ClassID / ImplID / ScriptInfoID 一式まで連れてくる(値が違うので衝突はしないが、分離の意図が届かない)。
#include "KESCMBoundaryID.h"

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

	// ★★NOT HERE ANY MORE (2026-08-17, bug recheck B2). ClearMarks(IDataBase*) stood at this spot
	// from Task 11 and was never called from anywhere: the UI ends a comparison with
	// ToggleStartStop() or StopComparison(), and the model's own KESCMDoClearMarks is reached
	// directly by KESCMComparisonRun.cpp:105. It came in with the plan's DRAFT interface and no
	// grep for callers ever ran over it -- unlike every method below marked "Added over the plan's
	// draft interface", each of which was found by grepping and each of which is called.
	// ⇒ "a method on a boundary that nobody calls is a promise nobody keeps"
	//   (IKESCMStoryEditsFacade.h:19, which wrote that while dropping its own draft's Rebuild()).

	// ---- mark display settings ---------------------------------------------------------

	/** Whether marks are printed (and therefore also shown on screen at all times), and the
		frame opacity: kTrue=25%, kFalse=75%. */
	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db) = 0;
	virtual void		TogglePrintMarks() = 0;
	virtual void		SetMarkOpacity25(bool16 op25) = 0;
	virtual bool16		GetPrintMarks() = 0;
	virtual bool16		GetMarkOpacity25() = 0;

	/** Which comparison the Start runs: pixels (the original) or story text (2026-08-20).

		★A SETTING OF THE COMPARISON, which is why it sits here next to GetPrintMarks() rather
		than anywhere on the UI side. The model reads it and acts on it; the flyout only chooses.

		⚠SetCompareMode CHANGES THE SETTING AND NOTHING ELSE. It does not re-run a comparison
		that is already armed - the caller decides that, because the same setter is used when the
		panel restores its saved state at start-up, where re-comparing would be wrong. The flyout
		re-compares; start-up does not. */
	virtual KESCMCompareMode	GetCompareMode() = 0;
	virtual void				SetCompareMode(KESCMCompareMode mode) = 0;

	// ---- display toggles ---------------------------------------------------------------
	//
	// Read and written by the flyout toggles, by Save/Load Panel Settings and by the press
	// gesture. These are settings OF the comparison, which is why they sit here beside
	// GetPrintMarks() rather than in IKESCMMarkData -- that interface is read-only by design.
	//
	// ★The plan named a different three (source marks / old page numbers / overset). Grepping
	// the real callers before writing this showed the UI never writes the overset flag -- it
	// only reads it, and clears the whole feature through ClearOverset() below -- while it does
	// write "Hold to Hide Marks". The three here are the three the UI actually writes.

	/** "Show Marks on Source": the Source document carries the same rings at all times. */
	virtual bool16		GetShowSourceMarks() = 0;
	virtual void		SetShowSourceMarks(bool16 on) = 0;

	/** "Show Marks on Target": the Target document carries its marks at all times, rather than
		only while the tool's button is held (2026-08-22).

		★IT MEANS THE SAME THING IN BOTH COMPARE MODES - the Pixel mode's rings and the Story
		mode's inverted characters (user's request: "ピクセルの方もストーリーの方にも"). The two
		are drawn by completely different machinery, so each reads this for itself: the rings in
		KESCMDrawEventHandler, the characters in ui/KESCMStoryPressMarks.
		⚠ON SCREEN ONLY, where the Source one also prints. What comes out of the Target document
		is decided by "Print comparison marks" alone, and this must not quietly override it. */
	virtual bool16		GetShowTargetMarks() = 0;
	virtual void		SetShowTargetMarks(bool16 on) = 0;

	/** "Show Original Page Numbers": the badge showing what a page was numbered before spreads
		were hidden. */
	virtual bool16		GetShowOldPageNumbers() = 0;
	virtual void		SetShowOldPageNumbers(bool16 on) = 0;

	/* ★"Hold to Hide Marks" WAS HERE AND IS GONE (2026-08-22, user's call). It stood for "show the
	   marks permanently, and hide them while the button is held" - and once "Show Marks on Target"
	   existed, the first half of that was the same switch twice over ([[one-question-one-place]]:
	   the drawing side literally read `sAlwaysShowMarks || sTgtMarksOn`).
	   ⇒ The second half became the rule for BOTH toggles instead: **while the button is held,
	     everything is the other way round** - off shows while held, on hides while held. Nothing
	     was lost, and there is one switch fewer to explain. */

	// ---- press-time display state ------------------------------------------------------
	//
	// What the tool's left button does to the marks while it is held. The state lives with the
	// drawing engine because the engine reads it on every draw; the decision of WHEN to change
	// it is the UI's, because it depends on which document window the mouse is over -- and the
	// model has no windows.
	//
	// ⚠ Setters only change the state. Redrawing is the caller's job, exactly as it was before
	// the split: which document to invalidate differs per gesture (Target only, Source only, or
	// both), and folding it in here would repaint documents the old code left alone.

	/** The master "are the rings on screen right now" flag. */
	virtual void		SetMarksVisible(bool16 on) = 0;

	/** The opacity the rings are actually drawn at, and the 25%/75% value the panel radio
		selects. The press puts the selected value in; releasing puts GetBaseScreenOpacity()
		back. */
	virtual void		SetMarkScreenOpacity(const PMReal& opacity) = 0;
	virtual PMReal		GetSelectedMarkOpacity() = 0;

	/** While the tool's button is down, the marks in the window under it are the other way round.
		Target and Source are separate because only that one window turns round.

		★TARGET: "parked" - the standing rings are put away while the button is down. What puts them
		UP while it is down is a different flag (SetMarksVisible), because that one is also raised by
		the peek gestures.

		★★SOURCE: "pressed" - and that is a DIFFERENT QUESTION from the target's (2026-08-22, user's
		call). This one says only that the button is down over the source window; the drawing side
		XORs it with "Show Marks on Source" and so covers both halves of the rule with one flag:
		toggle off + pressed = shown, toggle on + pressed = hidden.
		⚠It used to be a "temp hidden" flag raised only while the toggle was ON, which meant
		  **pressing over a source window whose toggle was off did nothing at all** - while three
		  places in this plug-in stated the rule as holding for "Pixel/Story, Target/Source alike".
		  The user's decision was to make the implementation match the rule, not the other way. */
	virtual bool16		GetMarksTempHidden() = 0;
	virtual void		SetMarksTempHidden(bool16 on) = 0;
	virtual bool16		GetSrcMarksPressed() = 0;
	virtual void		SetSrcMarksPressed(bool16 on) = 0;

	/** The old-version overlay shown by Shift+left (1.0) and Shift+Alt+left (0.5). Set the
		opacity before asking for the overlay; clear ShowOriginal when the button comes up. */
	virtual void		SetPeekOpacity(const PMReal& opacity) = 0;
	virtual bool16		GetShowOriginal() = 0;
	virtual void		SetShowOriginal(bool16 on) = 0;

	// ---- the status line ---------------------------------------------------------------

	/** The last string the model published. The UI reads this when it receives
		kKESCMStatusTextMessage, and app.kcmStatus answers from it.
		Kept on the model side so app.kcmStatus can answer while the panel is closed.
		★Since 2026-08-20 it is ASSEMBLED from the four pieces below (heading, break, body). A
		message stored as one string has three of them empty, so the answer is that string itself. */
	virtual void		GetSessionStatus(PMString& out) = 0;

	/** Remember a string the UI raised itself, WITHOUT emitting a notification.

		★This is what the UI's own KESCMSetStatus calls. A message raised by a UI action -- a menu
		item, a button, a row click -- is painted by the UI directly and does not need to travel
		through the notification, but it still has to be REMEMBERED on the model side, because
		app.kcmStatus answers from that string and because the panel's widgets are rebuilt on
		every re-show.
		⚠ It must not notify: KESCMSetStatus is also what the observer calls when a notification
		arrives, so notifying from here would loop. */
	virtual void		StoreSessionStatus(const PMString& s) = 0;

	/** The same store, told where the message's COLOUR changes (2026-08-20).

		★The panel's message area is drawn by hand and can show two colours, so that the other side
		of a clicked edit has its differing characters at full strength and the words around them
		faded. label is a heading on its own line, mid is what differs, pre/post is the context.

		★WHY THE SPLIT CROSSES THE BOUNDARY INSTEAD OF BEING MADE IN THE PANEL. It cannot be made
		there: the boundary between context and change is a code point index into text that has
		already been cut at both ends, and PMString counts UTF-16. The model made the split
		(KESCMStoryDiffRun's Slice) and this is the same journey the change ROW's three pieces
		already make on IKESCMStoryEditsFacade::Change.

		⚠Like StoreSessionStatus, it must not notify. */
	virtual void		StoreSessionStatusSegments(const PMString& label, const PMString& pre,
												   const PMString& mid, const PMString& post) = 0;

	/** The stored message in its four pieces. The UI reads this back when the panel re-appears, so
		that a coloured message comes back coloured rather than flattening into one colour.
		★A message stored as one string answers with that string in outMid and three empty pieces. */
	virtual void		GetSessionStatusSegments(PMString& outLabel, PMString& outPre,
												 PMString& outMid, PMString& outPost) = 0;

	/** Shutdown only: empty the stored string, so the model's static PMString has no live heap
		buffer to free when the plug-ins unload (Mac unload order differs from Windows).

		★Called from the UI's shutdown, not the model's, and that is deliberate: a model
		plug-in's startup/shutdown service is run again on every background-thread teardown
		(guide vol1-07 L245-253), so clearing it there would empty the status line every time a
		PDF is exported. */
	virtual void		ClearSessionStatus() = 0;

	// ---- what the notification being handled is about ------------------------------------
	//
	// ★★NOT HERE ANY MORE (2026-08-15, API audit B2). Five methods used to sit at this spot --
	// StatusWantsForceRedraw and GetNotifiedDocA/B/C/NavReset -- because the model kept the
	// notification's payload in statics and the UI had to come back and ask for it.
	//
	// It travels WITH the notification now: ISubject::Change takes a third argument, void*
	// changedBy (ISubject.h:150), which reaches the listener as IObserver::Update's fourth. The
	// struct is KESCMNotifyPayload (KESCMModelNotify.h), and the listener casts changedBy back to
	// it -- the shape Adobe's own linksui uses (EditOriginalResumeObserver.cpp:127).
	//
	// ⇒ Five methods off the boundary and four statics out of a model plug-in. See the struct's
	//   comment for why the statics were safe and still wrong.

	// ---- the peek overlay --------------------------------------------------------------

	/** Whether both armed documents are still open. Returns kFalse after running the full
		Stop-equivalent clean-up, so a caller that gets kFalse must not touch either database.
		★The gesture code asks this before every peek and on every drag update: a closed
		IDataBase* is a dangling pointer whose address gets reused. */
	virtual bool16		ArmedDocsAlive() = 0;

	/** Show the old version of the spread at (mx, my), over the front layout view.
		Rasterises that one spread on first use and keeps exactly one cached.
		  mx, my    -- the point to peek at, in targetDB's pasteboard (content) coordinates
		  viewScale -- that window's content-to-window scale (zoom x device scale). The
		               rasterisation dpi is derived from it.
		  uiZoom    -- that window's UI zoom (what the user sees; no device scale). Pass 0 when
		               the panorama could not be queried -- that reproduces exactly what the
		               model used to do for itself when IPanorama came back nil.
		★2026-08-15 (stage 2, task 4B): renamed from ShowPeekUnderMouse and the view resolution
		moved out to the caller. Deciding *which window the pointer is over* is a question only
		the UI can answer; the model now only answers *what is at this point, at this dpi*.
		The dpi arithmetic itself did not move -- see KESCMPeek.h.
		★★★2026-08-16: viewSpreadUID added -- the spread that view is CURRENTLY SHOWING
		(ILayoutControlData::GetSpreadRef). ⚠ Not optional: a master spread and the ordinary
		spreads OVERLAP in pasteboard coordinates, so without it the point lands on an ordinary
		page while the view is showing a master, and the peek image is built for the wrong
		spread -- nothing appears at all. Measured 2026-08-16; full reasoning in KESCMCore.h. */
	virtual void		ShowPeekAt(IDataBase* targetDB, IDataBase* sourceDB,
								   const PMReal& mx, const PMReal& my,
								   const PMReal& viewScale, const PMReal& uiZoom,
								   UID viewSpreadUID) = 0;

	/** The on-screen opacity marks are drawn at when they are shown permanently (printing ON
		gives the 25%/75% choice, printing OFF gives fully opaque). The UI needs it when the
		"Hold to Hide Marks" toggle flips, to put the permanent value back straight away. */
	virtual PMReal		GetBaseScreenOpacity() = 0;

	// ---- the CMYK sampler (Alt+left) ---------------------------------------------------

	/** Read the raw CMYK at (mx, my) on hoverDB and, when otherDB is not nil, at the matching
		point on the paired page of otherDB. Returns kFalse when there is no page under the
		point or the sample could not be taken; the two strings are only valid on kTrue.
		  hoverDB       -- the document the pointer is over. This is the side reported first.
		  otherDB       -- the comparison partner, or nil for the single-document mode.
		  hoverIsTarget -- kTrue when hoverDB is the comparison Target (new) side. Selects the
		                   page-mapping direction and the t/s labels.
		  mx, my        -- the sample point, in hoverDB's pasteboard (content) coordinates.
		  outPanel      -- compact form for the panel status line, which is narrow: measured
		                   2026-08-17, ui/KCMUI.fr's kKESCMStatusTextWidgetID, Frame(8,76,216,150)
		                   = 208x74px, 4 lines. (Named rather than numbered 2026-08-19: the line
		                   number it carried was 14 short, in all four files that quoted it.)
		                   (An older "152px" travelled through three files here; the width is in
		                   the .fr and nowhere else - read it there rather than copying it again.)
		  outCursor     -- the numbers alone, for the cursor bitmap to draw.
		⚠ The caller must have checked that the pointer is still over hoverDB's own window
		before calling. That guard used to live inside the sampler; it moved out with the view
		resolution in 2026-08-15 (stage 2, task 4B). Dropping it makes another window's
		coordinates get read as if they were hoverDB's.
		★★★2026-08-16: viewSpreadUID added, same reason as ShowPeekAt above -- and here the
		failure was SILENT: with a master spread on screen the sampler read an ORDINARY page's
		colour and presented it as the master's. A wrong number looks exactly like a right one. */
	virtual bool16		SampleColorAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
									  const PMReal& mx, const PMReal& my,
									  UID viewSpreadUID,
									  PMString& outPanel, PMString& outCursor) = 0;

	/** Cache the hover-to-other page pairing for the duration of an Alt+left drag. Begin on
		button down, End on button up; between them SampleColorAt skips rebuilding the pairing
		on every sample (up to 20/s). Not used in the single-document mode. */
	virtual void		BeginColorDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget) = 0;
	virtual void		EndColorDrag() = 0;

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

	/** Switch Find Overset off and drop what the scan found. The caller repaints -- it knows
		which document was being scanned, because it asked before calling this.
		★Added over the plan's draft interface (2026-08-13): the flyout toggle called
		KESCMDrawEventHandler::DropOverset() directly, which is a static member of a model class
		and therefore not reachable once the UI is its own plug-in. */
	virtual void		ClearOverset() = 0;

	// ---- Hide Unchanged Spreads --------------------------------------------------------

	// ★★NOT HERE ANY MORE (2026-08-17, bug recheck B2). ResetHideUnchanged(bool16),
	// GetHideUnchangedDB() and GetHideUnchangedSrcDB() stood here from Task 11 and not one of the
	// three was ever called across the boundary.
	//
	// ★The three model-side functions are alive and busy -- KESCMResetHideUnchanged from
	// KESCMDoMarkChangesDoc (re-compare) and KESCMDoClearMarks (Stop) in KESCMCore.cpp, and from
	// the model's Shutdown and the close sweep KESCMHandleDocsClosed in KESCMPeek.cpp; the two
	// getters from that same close sweep -- but every one of those callers is MODEL-side.
	// ⚠2026-08-18 (bug recheck B10, second pass): this sentence used to cite line numbers
	// (KESCMCore.cpp:485,778 / KESCMPeek.cpp:618,878 / :867-868) and ALL FIVE had drifted, two of
	// them by more than fifty lines. Named callers instead: a function name survives the next
	// insertion, a line number does not.
	// The reset happens below this boundary, which is why the UI never had to ask for it: the
	// flyout only needs to flip the toggle and read its state, and those two are right here.
	// Same origin as ClearMarks above: the plan's draft interface, never grepped for callers.

	/** The toggle itself, and its state for the menu's check mark.
		★Added over the plan's draft interface (2026-08-13): the flyout item that flips it stays
		on the UI side, so both of these cross the boundary. */
	virtual void		HideUnchangedToggle() = 0;
	virtual bool16		GetHideUnchangedOn() = 0;

	// ---- documents, redraw, and the application (2026-08-14, Task 16) --------------------
	//
	// These are not about the comparison, and that is exactly why they were easy to miss: they
	// were plain free functions in KESCMCore.h, which works only while everything shares one
	// .pln. A free function's body lives in the plug-in that defines it, so the UI half could
	// not link to any of them once the two are separated. Counted before adding: 23 calls
	// across 10 UI-side files.

	/** kTrue when db still belongs to an open document.

		★NEVER DEREFERENCE A DATABASE TO FIND OUT. A closed one is a dangling pointer whose
		address gets reused, so the test is a pointer comparison against IDocumentList and
		nothing else -- KESCM's rule everywhere it holds a database. */
	virtual bool16		IsDocDBOpen(IDataBase* db) = 0;

	/** Redraw every view of this document. nil is ignored, so callers that may or may not have
		a second document to repaint can call it twice without testing. */
	virtual void		InvalidateDB(IDataBase* db) = 0;

	/** The front document's database, or nil when there is none. Resolved through
		IActiveContext, in one place, so that "which document is the user looking at" has a
		single answer. */
	virtual IDataBase*	GetActiveDocDB() = 0;

	/** kTrue while the application is shutting down (kQuitting / kShuttingDown).

		★While it is, UI work -- touching widgets, forcing redraws, booking idle tasks -- has to
		be skipped and the code reduced to discarding state: the teardown order of windows and
		panels is platform-dependent, and on the Mac it is not the Windows order. The close-all
		phase of a quit, where the user can still cancel at a save prompt, is NOT this: that one
		is still kRunning. */
	virtual bool16		IsAppQuitting() = 0;

	// ---- the page number marker exclusion -----------------------------------------------
	//
	// Whether the folio (page number) area is left out of the pixel comparison. A flyout item
	// flips it and the saved panel state reads and writes it, both UI-side.

	virtual bool16		GetIgnorePageNumberMarker() = 0;
	virtual void		SetIgnorePageNumberMarker(bool16 on) = 0;

	// ---- exporting -----------------------------------------------------------------------

	/** Write the changed pages out as TSV, and describe what happened in outMessage -- the
		path written, or why nothing was. ★The message comes back rather than being shown from
		inside: the status line belongs to the UI, and the flyout item that asked is the one
		that reports (the same shape the plan gives for KESCMChangedPagesTSV in §3.3). */
	virtual void		ExportChangedPagesTSV(PMString& outMessage) = 0;
};

#endif // __IKESCMCompareFacade_h__
