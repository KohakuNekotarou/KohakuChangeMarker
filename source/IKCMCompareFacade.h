//========================================================================================
//
//  IKCMCompareFacade.h
//
//  What the UI may ask of the comparison engine.
//
//  Modelled on the SDK's own pair sample: customconditionaltext (model) publishes
//  ICusCondTxtFacade.h and AddIns it to kUtilsBoss; customconditionaltextui (UI) calls
//  Utils<ICusCondTxtFacade>() and nothing else.
//
//  WHY AN INTERFACE AND NOT A HEADER OF FUNCTIONS. Once the model and the UI are separate
//  plug-ins, a free function declared in a shared header cannot be linked -- its body lives in
//  the other .pln. Every call that crosses the boundary has to go through an interface on a
//  boss. That is the whole reason this file exists.
//
//  ⚠**THE VTABLE IS SHARED BY THE TWO HALVES, AND THIS IS THE ONE PLACE THE RULE IS WRITTEN** (the
//  other boundary headers point here). The UI calls every method through Utils<IKCMCompareFacade>(),
//  so a method added, removed or moved shifts the slot of every method after it - and a half that
//  was not rebuilt calls the wrong method, silently: it loads, and the panel died while opening, in
//  the method that had moved up into the slot it asked for (measured).
//    - A change to any boundary interface means BUILDING BOTH HALVES (work\kcm-cycle.ps1 builds the
//      two). There is no stamp that checks it - the author decided against one (the two .pln files
//      ship together).
//    - New methods go at the END all the same, so a half left unbuilt by mistake still finds every
//      older slot where it was (memory facade-vtable-slot-append-only).
//  Nothing outside KCM includes the boundary headers.
//
//========================================================================================

#ifndef __IKCMCompareFacade_h__
#define __IKCMCompareFacade_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"
#include "PMReal.h"		// GetBaseScreenOpacity

// Project includes:
// The BOUNDARY header, not KCMID.h. All that is needed here is this interface's own IID
// (IID_IKCMCOMPAREFACADE), and KCMBoundaryID.h draws the line itself: boundary IDs only,
// **model-only IDs belong in KCMID.h**. Including KCMID.h would make this header -- which the UI
// includes -- drag the model's whole set of ClassIDs, ImplIDs and ScriptInfoIDs across with it.
// The values do not collide, but the point of the separation stops being visible.
#include "KCMBoundaryID.h"

class IDataBase;
class IDocument;
class UIDList;		// RefreshSelectedPages - the Pages panel's selection, as the UI read it

// THE WORDS BOTH HALVES SAY - here because both read this header: the model for the press and a script's road, the UI
//  before a dialog it would otherwise open for nothing. English, as every status line. (The PDF comparison's three went
//  with it on 2026-10-27.)
// (the import-as-Source road) The Source is an import's copy: the files' words poured into a Task Start in
//  memory. It is compared in the Story mode alone (design section 2-10).
#define kKCMImportSourceModeRefusal	"The Source is the imported text - it is compared in the Story mode. Switch the Compare mode to Story."

class IKCMCompareFacade : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMCOMPAREFACADE };

	// ---- starting and stopping ----------------------------------------------------------

	/** The Start/Stop toggle. When armed, clears; when not armed, resolves the pair -- the
		chosen Target and Source, and the old automatic rule (active document = Target, another
		open document = Source) for whichever of the two has not been chosen -- and starts.
		@warning it refuses, with a message on the status line, when the two come out the same
		document. That cannot happen by the automatic rule; it is what a choice can ask for. */
	virtual void		ToggleStartStop() = 0;

	/** Stop: remove the marks and disarm the peek. */
	virtual void		StopComparison() = 0;

	/** Start with these two documents. No resolution happens here -- the caller decides which
		is the Target. nil does nothing.
		@warning overwrites an existing armed state without asking; call StopComparison first
		if you want "stop, then start". */
	virtual void		StartComparisonFor(IDocument* target, IDocument* source) = 0;

	/** Whether a comparison can be started: two documents resolve. Goes through the same
		resolver as the start branch, so what the menu shows and what pressing it does cannot
		disagree.
		@warning it does NOT answer "are they the same document". That is asked at the Start,
		which says so rather than greying itself out -- see ToggleStartStop. */
	virtual bool16		CanStartComparison() = 0;

	// ---- the chosen Target and Source --------------------------------------------------
	//
	// "Set as Target" / "Set as Source" on the flyout. The choice is made before starting and
	// **survives a Stop**; what ends it is the document closing, and then only for the document
	// that closed. Whichever has not been chosen still falls to the automatic rule.
	//
	// WHY THE SETTERS TAKE NO DOCUMENT. The menu item's job is to say "the active one", not to
	// name a document: which document is active is answered on the model side, in the one place
	// that already answers it for the comparison. A UI that resolved it for itself would be a
	// second answer to the same question, and the two would drift the first time "active" was
	// found to mean something more exact than it does today.

	/** Make the active (front) document the Target / the Source. kFalse, and nothing set, when
		there is no active document.
		@warning they only set. Refreshing the panel and saying so on the status line are the
		caller's, exactly as with SetCompareMode -- what the UI shows is the UI's decision. */
	virtual bool16		SetChosenTargetToActive() = 0;
	virtual bool16		SetChosenSourceToActive() = 0;

	/** What has been chosen, for the panel's Target:/Source: lines.
		@warning nil unless that document is still open -- and nil also means "not chosen", which
		is deliberate: to the panel and to the resolver the two cases are the same. */
	virtual IDataBase*	GetChosenTargetDB() = 0;
	virtual IDataBase*	GetChosenSourceDB() = 0;

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
		update, so stopping half way leaves a narrower version of the same operation.
		`pages` IS THE SELECTION THE UI READ (KCMReadSelectedPages, masters in): the Pages panel is a
		  UI part, which the model does not reach for. */
	virtual bool16		RefreshSelectedPages(const UIDList& pages, int32* outPages, int32* outChanged,
								bool16* outCancelled = nil, int32* outFailed = nil) = 0;

	/** Whether "Refresh Page Comparison" may be offered right now.

		NEVER IN THE STORY MODE. What it refreshes is the PIXEL comparison, and the story mode
		rasterises no page at all - pressing it would spend time redrawing the selected pages and
		change nothing on screen. The story mode's own refresh is the one on a row's right-click
		menu ("Refresh Story Comparison"), so each mode has exactly one and they do not overlap. */
	virtual bool16		RefreshComparisonAvailable() = 0;

	// A ClearMarks(IDataBase*) belongs nowhere near this boundary. The UI ends a comparison with
	// ToggleStartStop() or StopComparison(), and the model's own clean-up is reached from
	// KCMComparisonRun.cpp -- model-side. **A method on a boundary that nobody calls is a promise
	// nobody keeps** (IKCMStoryEditsFacade.h, which states the same rule about its own missing
	// Build()). Everything below was put here because a grep found a real UI-side caller.

	// ---- mark display settings ---------------------------------------------------------

	/** Whether marks are printed (and therefore also shown on screen at all times), and the
		frame opacity: kTrue=25%, kFalse=75%. */
	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db) = 0;
	virtual void		TogglePrintMarks() = 0;
	virtual void		SetMarkOpacity25(bool16 op25) = 0;
	virtual bool16		GetPrintMarks() = 0;
	virtual bool16		GetMarkOpacity25() = 0;

	/** The mark colour: kFalse = red (the default), kTrue = cyan.

		THIS REPLACED AN AUTOMATIC CHOICE. The rings used to switch to cyan by themselves over
		grounds that looked reddish, decided per pixel off the comparison raster. Two things ended
		that: the reader could not tell why a mark was the colour it was, and the Story mode's wash
		**cannot read the ground at all** (a text adornment is handed no pixels), so the two modes
		would have disagreed about how colour is chosen. The reader picks instead.
		One flag serves both modes - they both draw through SelectedMarkColor(). */
	virtual void		SetMarkColor(bool16 cyan) = 0;
	virtual bool16		GetMarkColorCyan() = 0;

	/** Which comparison the Start runs: pixels (the original) or story text.

		A SETTING OF THE COMPARISON, which is why it sits here next to GetPrintMarks() rather
		than anywhere on the UI side. The model reads it and acts on it; the flyout only chooses.

		@warning SetCompareMode CHANGES THE SETTING AND NOTHING ELSE. It does not re-run a
		comparison that is already armed - the caller decides that, because the same setter is used
		when the panel restores its saved state at start-up, where re-comparing would be wrong. The
		flyout re-compares; start-up does not.
 WHILE A TRACK LISTING IS UP, GetCompareMode answers kKCMModeTrack - it is not a setting
		(StartTrackChanges below raises it) - and SetCompareMode(kKCMModeTrack) changes nothing. The setting itself is
		GetCompareModeSetting (the last slot). */
	virtual KCMCompareMode	GetCompareMode() = 0;
	virtual void				SetCompareMode(KCMCompareMode mode) = 0;

	// ---- display toggles ---------------------------------------------------------------
	//
	// Read and written by the flyout toggles, by Save/Load Panel Settings and by the press
	// gesture. These are settings OF the comparison, which is why they sit here beside
	// GetPrintMarks() rather than in IKCMMarkData -- that interface is read-only by design.
	//
	// The three here are the three the UI actually WRITES. The overset flag looks like a fourth
	// and is not: the UI only reads it, and clears the whole feature through ClearOverset() below.

	/** "Always Show Marks on Source": the Source document carries the same rings at all times.

		**THE TWO DISPLAY TOGGLES ARE ABOUT THE SCREEN, AND ONLY THE SCREEN** (the
		user, settling it in one sentence: "with Print ON they are printable and permanently shown;
		Always Show only ever shows"). What reaches paper, a PDF or the print preview is decided by
		**"Print comparison marks" alone, for BOTH documents** - and because that switch also puts
		the marks up on screen, turning it on means "what you see is what will come out".
		⇒ These two never grant output, and never withhold it.
		⚠**THIS PARAGRAPH IS THE ONE OTHER FILES CITE.** It said the opposite for the Source side
		 until today ("ON SCREEN ONLY, where the Source one also prints"), and that sentence was
		 copied into KCMStoryMarkBuild as the specification - so when the Pixel side changed on
		 2026-09-07, the Story side kept the retracted rule. **A citation does not follow its
		 source.** If this changes again, grep for GetShowSourceMarks before believing anything. */
	virtual bool16		GetShowSourceMarks() = 0;
	virtual void		SetShowSourceMarks(bool16 on) = 0;

	/** "Always Show Marks on Target": the Target document carries its marks at all times, rather
		than only while the tool's button is held.

		IT MEANS THE SAME THING IN BOTH COMPARE MODES - the Pixel mode's rings and the Story mode's
		coloured wash under the changed characters. The two are drawn by completely different
		machinery, so each reads this for itself: the rings in KCMDrawEventHandler, the wash in
		KCMStoryMarkBuild.
		@warning **ON SCREEN ONLY.** What reaches paper or a PDF is decided by "Print comparison
		 marks", for BOTH documents, and neither of these two switches may quietly override it.
		⚠**THIS PARAGRAPH SAID THE OPPOSITE UNTIL 2026-09-08** ("where the Source one also
		 prints"), and it was cited as the specification by KCMStoryMarkBuild - so when the Pixel
		 side made the two symmetrical on 2026-09-07 (spec map MK-14), the Story side went on
		 printing a Source document's marks with the print toggle off. It also called the Story
		 mark "inverted characters", which it stopped being when the inversion was replaced by a
		 wash (spec map SMK-07). **A sentence quoted as a specification has to be edited when the
		 thing it describes changes - nothing warns you.** */
	virtual bool16		GetShowTargetMarks() = 0;
	virtual void		SetShowTargetMarks(bool16 on) = 0;

	/** "Show Original Page Numbers": the badge showing what a page was numbered before spreads
		were hidden. */
	virtual bool16		GetShowOldPageNumbers() = 0;
	virtual void		SetShowOldPageNumbers(bool16 on) = 0;

	// ---- press-time display state ------------------------------------------------------
	//
	// What the tool's left button does to the marks while it is held. The state lives with the
	// drawing engine because the engine reads it on every draw; the decision of WHEN to change
	// it is the UI's, because it depends on which document window the mouse is over -- and the
	// model has no windows.
	//
	// @warning setters only change the state. Redrawing is the caller's job: which document to
	// invalidate differs per gesture (Target only, Source only, or both), and folding it in here
	// would repaint documents the callers leave alone.

	/** The master "are the rings on screen right now" flag. */
	virtual void		SetMarksVisible(bool16 on) = 0;

	/** The opacity the rings are actually drawn at, and the 25%/75% value the panel radio
		selects. The press puts the selected value in; releasing puts GetBaseScreenOpacity()
		back. */
	virtual void		SetMarkScreenOpacity(const PMReal& opacity) = 0;
	virtual PMReal		GetSelectedMarkOpacity() = 0;

	/** While the tool's button is down, the marks in the window under it are THE OTHER WAY ROUND -
		off shows while held, on hides while held. Target and Source are separate because only the
		window the press happened in turns round.

		TARGET: "parked" - the standing rings are put away while the button is down. What puts them
		UP while it is down is a different flag (SetMarksVisible), because that one is also raised
		by the peek gestures.

		SOURCE: "pressed" - and that is a DIFFERENT QUESTION from the target's. This one says only
		that the button is down over the source window; the drawing side XORs it with "Always Show
		Marks on Source" and so covers both halves of the rule with one flag: toggle off + pressed
		= shown, toggle on + pressed = hidden.
		@warning do not turn it back into a "temp hidden" flag that is only raised while the toggle
		is ON. That makes **pressing over a source window whose toggle is off do nothing at all**,
		while three places in this plug-in state the rule as holding for "Pixel/Story,
		Target/Source alike". */
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
		kKCMStatusTextMessage, and app.kcmStatus answers from it.
		Kept on the model side so app.kcmStatus can answer while the panel is closed.
		It is ASSEMBLED from the pieces below (heading, break, body). A message stored as one
		string has the others empty, so the answer is that string itself. */
	virtual void		GetSessionStatus(PMString& out) = 0;

	/** Remember a string the UI raised itself, WITHOUT emitting a notification.

		This is what the UI's own KCMSetStatus calls. A message raised by a UI action -- a menu
		item, a button, a row click -- is painted by the UI directly and does not need to travel
		through the notification, but it still has to be REMEMBERED on the model side, because
		app.kcmStatus answers from that string and because the panel's widgets are rebuilt on
		every re-show.
		@warning it must not notify: KCMSetStatus is also what the observer calls when a
		notification arrives, so notifying from here would loop. */
	virtual void		StoreSessionStatus(const PMString& s) = 0;

	/** The same store, told where the message's COLOUR changes.

		The panel's message area is drawn by hand and can show two colours, so that the other side
		of a clicked edit has its differing characters at full strength and the words around them
		faded. label is a heading on its own line, mid is what differs, pre/post is the context.

		WHY THE SPLIT CROSSES THE BOUNDARY INSTEAD OF BEING MADE IN THE PANEL. It cannot be made
		there: the boundary between context and change is a code point index into text that has
		already been cut at both ends, and the joined text carries no mark of it. The model made the split
		(KCMStoryDiffRun's Slice) and this is the same journey the change ROW's three pieces
		already make on IKCMStoryEditsFacade::Change.

		@warning like StoreSessionStatus, it must not notify. */
	virtual void		StoreSessionStatusSegments(const PMString& label, const PMString& pre,
												   const PMString& mid, const PMString& post,
												   const PMString& ruby, int32 attrKind) = 0;

	/** The stored message in its five pieces. The UI reads this back when the panel re-appears, so
		that a coloured message comes back coloured rather than flattening into one colour.
		A message stored as one string answers with that string in outMid and the rest empty.
		outRuby is the READING drawn above the changed characters, and it comes back here for the
		same reason the colours do: a re-shown panel that lost only the reading would be showing
		the older version WITHOUT the very thing the row could not show.
		outAttrKind says WHICH attribute that upper line belongs to, so a kenten comes back as its
		MARK rather than as the name of one - the same question every other reader of these two
		fields has to ask (IKCMStoryEditsFacade.h). */
	virtual void		GetSessionStatusSegments(PMString& outLabel, PMString& outPre,
												 PMString& outMid, PMString& outPost,
												 PMString& outRuby, int32& outAttrKind) = 0;

	/** Shutdown only: empty the stored string, so the model's static PMString has no live heap
		buffer to free when the plug-ins unload (Mac unload order differs from Windows).

		Called from the UI's shutdown, not the model's, and that is deliberate: a model plug-in's
		startup/shutdown service is run again on every background-thread teardown (guide vol1-07
		L245-253), so clearing it there would empty the status line every time a PDF is exported. */
	virtual void		ClearSessionStatus() = 0;

	// ---- what the notification being handled is about ------------------------------------
	//
	// NOTHING TO ASK FOR. What a notification is about travels WITH it: ISubject::Change takes a
	// third argument, void* changedBy (ISubject.h:150), which reaches the listener as
	// IObserver::Update's fourth. The struct is KCMNotifyPayload (KCMModelNotify.h), and the
	// listener casts changedBy back to it -- the shape Adobe's own linksui uses
	// (LinksUIPanelTreeObserver.cpp:213).
	//
	// @warning do not add "what was that notification about" getters here. They would be statics
	// in a model plug-in read back after the fact; see the payload struct's comment for why that
	// is safe today and still wrong.

	// ---- the peek overlay --------------------------------------------------------------

	/** Whether both armed documents are still open. Returns kFalse after running the full
		Stop-equivalent clean-up, so a caller that gets kFalse must not touch either database.
		The gesture code asks this before every peek and on every drag update: a closed
		IDataBase* is a dangling pointer whose address gets reused. */
	virtual bool16		ArmedDocsAlive() = 0;

	/** Show the old version of the spread at (mx, my), over the front layout view.
		Rasterises that one spread on first use and keeps exactly one cached.
		  mx, my    -- the point to peek at, in targetDB's pasteboard (content) coordinates
		  viewScale -- that window's content-to-window scale (zoom x device scale). The
		               rasterisation dpi is derived from it.
		  uiZoom    -- that window's UI zoom (what the user sees; no device scale). Pass 0 when
		               the panorama could not be queried.
		  viewSpreadUID -- the spread that view is CURRENTLY SHOWING
		               (ILayoutControlData::GetSpreadRef).
		Deciding *which window the pointer is over* is a question only the UI can answer; the model
		answers *what is at this point, at this dpi*, which is why the view resolution is the
		caller's and the dpi arithmetic is not (see KCMPeek.h).
		@warning viewSpreadUID is not optional. A master spread and the ordinary spreads OVERLAP in
		pasteboard coordinates, so without it the point lands on an ordinary page while the view is
		showing a master, the peek image is built for the wrong spread, and nothing appears at all.
		Full reasoning in KCMCore.h. */
	virtual void		ShowPeekAt(IDataBase* targetDB, IDataBase* sourceDB,
								   const PMReal& mx, const PMReal& my,
								   const PMReal& viewScale, const PMReal& uiZoom,
								   UID viewSpreadUID) = 0;

	/** The on-screen opacity marks are drawn at when they are shown permanently (printing ON
		gives the 25%/75% choice, printing OFF gives fully opaque). The UI needs it to put the
		permanent value back straight away: when the press is released (KCMPeekGesture) and when
		the "Print comparison marks" toggle changes what the permanent value IS
		(KCMActionComponent). */
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
		  outPanel      -- compact form for the panel status line, which is narrow: the widget is
		                   ui/KCMUI.fr's kKCMStatusTextWidgetID, and its Frame is the only place
		                   that says how wide -- read it there rather than copying the number into
		                   a fourth file.
		  outCursor     -- the numbers alone, for the cursor bitmap to draw.
		@warning the caller must have checked that the pointer is still over hoverDB's own window
		before calling. Dropping that guard makes another window's coordinates get read as if they
		were hoverDB's.
		@warning viewSpreadUID matters here for the same reason as in ShowPeekAt, and here the
		failure is SILENT: with a master spread on screen the sampler reads an ORDINARY page's
		colour and presents it as the master's. A wrong number looks exactly like a right one. */
	virtual bool16		SampleColorAt(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget,
									  const PMReal& mx, const PMReal& my,
									  UID viewSpreadUID,
									  PMString& outPanel, PMString& outCursor) = 0;

	/** Cache the hover-to-other page pairing for the duration of an Alt+left drag. Begin on
		button down, End on button up; between them SampleColorAt skips rebuilding the pairing
		on every sample (up to 20/s). Not used in the single-document mode. */
	virtual void		BeginColorDrag(IDataBase* hoverDB, IDataBase* otherDB, bool16 hoverIsTarget) = 0;
	virtual void		EndColorDrag() = 0;

	// (No overset here: Find Overset was taken out - the author's call - because InDesign's own Preflight
	//  panel finds overset text, keeps up with edits and JUMPS TO THE PLACE, which this never did.)

	// ---- Hide Unchanged Spreads --------------------------------------------------------
	//
	// Only the toggle and its state cross the boundary, because only they are asked for from the
	// UI: the flyout flips the toggle and reads it back for the check mark.
	//
	// Resetting Hide Unchanged is model-side work with model-side callers -- KCMDoMarkChangesDoc
	// (re-compare) and KCMDoClearMarks (Stop) in KCMCore.cpp, and the model's Shutdown and the
	// close sweep KCMHandleDocsClosed in KCMPeek.cpp. Named rather than cited by line: a function
	// name survives the next insertion, a line number does not.

	/** The toggle itself, and its state for the menu's check mark. */
	virtual void		HideUnchangedToggle() = 0;
	virtual bool16		GetHideUnchangedOn() = 0;

	// ---- documents, redraw, and the application ------------------------------------------
	//
	// These are not about the comparison, and that is exactly why they were easy to miss: they
	// were plain free functions in KCMCore.h, which works only while everything shares one .pln.
	// A free function's body lives in the plug-in that defines it, so the UI half could not link
	// to any of them once the two are separated -- and the UI calls them from most of its files.

	/** kTrue when db still belongs to an open document.

		NEVER DEREFERENCE A DATABASE TO FIND OUT. A closed one is a dangling pointer whose address
		gets reused, so the test is a pointer comparison against IDocumentList and nothing else --
		KCM's rule everywhere it holds a database. */
	virtual bool16		IsDocDBOpen(IDataBase* db) = 0;

	/** Redraw every view of this document. nil is ignored, so callers that may or may not have
		a second document to repaint can call it twice without testing. */
	virtual void		InvalidateDB(IDataBase* db) = 0;

	/** The front document's database, or nil when there is none. Resolved through
		IActiveContext, in one place, so that "which document is the user looking at" has a
		single answer. */
	virtual IDataBase*	GetActiveDocDB() = 0;

	/** kTrue while the application is shutting down (kQuitting / kShuttingDown).

		While it is, UI work -- touching widgets, forcing redraws, booking idle tasks -- has to be
		skipped and the code reduced to discarding state: the teardown order of windows and panels
		is platform-dependent, and on the Mac it is not the Windows order.
		@warning the close-all phase of a quit, where the user can still cancel at a save prompt,
		is NOT this: that one is still kRunning. */
	virtual bool16		IsAppQuitting() = 0;

	// ---- the page number marker exclusion -----------------------------------------------
	//
	// Whether the folio (page number) area is left out of the pixel comparison. A flyout item
	// flips it and the saved panel state reads and writes it, both UI-side.

	virtual bool16		GetIgnorePageNumberMarker() = 0;
	virtual void		SetIgnorePageNumberMarker(bool16 on) = 0;

	// ---- a Source that is not an open document -------------------------------
	//
	// A database that is in no IDocumentList, chosen as the Source: the Task Start kept in memory
	// (KCMTaskStartClone.h - this plug-in lends its own clone; KCMExternalSource.h carries the
	// contract). Everywhere else KCM asks "is it in the list", and a lent database is not. No other
	// plug-in lends one (the two entrances for that were taken out - the author: "remove them").

	/** kTrue, and outLabel filled, when `db` is the lent Source (chosen or armed) -- the panel
		asks this when FindDocByDataBase has no name for it. */
	virtual bool16		GetExternalSourceLabel(IDataBase* db, PMString& outLabel) = 0;

	// ---- clearing the chosen pair --------------------------------------------

	/** Drop both chosen documents, so that the next Start falls back to the automatic rule
		(active document = Target, the earliest-opened other document = Source). The panel's
		Target:/Source: lines go back to bare labels, and a lent Source is forgotten with them.

		@warning **THIS METHOD does not stop a running comparison.** It drops the choices and
		nothing else, so clearing while a comparison is armed leaves that comparison drawing --
		and the panel goes on naming the ARMED pair, so nothing appears to have happened until
		the next Stop.
		**THE FLYOUT ITEM IS NOT THIS BARE METHOD**: it stops first and then
		clears, and it is live WHILE a comparison is armed -- unlike the two "Set as" items it
		undoes, which stay greyed (KCMActionComponent, DoAction and UpdateActionStates).
		⇒ a caller that wants what the menu item does must call StopComparison() itself first. */
	virtual void		ClearChosenDocs() = 0;

	// ---- refreshing a running comparison -------------------------------------

	/** Compare the SAME TWO DOCUMENTS again -- the flyout's "Refresh Comparison", under Start.

		What it is for: the reader has edited one of the two and wants the marks to say what is
		true now. It was Stop-then-Start before this existed, which is two presses and takes the
		marks off the screen in between.

		**IT RUNS THE START PROCEDURE**, with the armed pair, so a refresh and a start cannot
		come to disagree about what starting means. Nothing is stopped first: arming is
		idempotent, and re-comparing an armed pair is what the register toggle already does.
		⚠It does nothing when nothing is armed, or when either document has gone -- ask
		IsArmed() and ArmedDocsAlive() to decide whether to OFFER it (that is what the menu's
		enable state does). This is a full comparison, so it costs what a Start costs.
		⚠**NOT the two partial refreshes.** RefreshSelectedPages above re-does the pages chosen
		in the Pages panel, and IKCMStoryEditsFacade::RefreshRow re-does one row; both leave the
		rest alone. This one re-does everything, in whichever mode is current.
		If the comparison is CANCELLED (the progress bar's Cancel, on longer runs), the
		comparison is STOPPED rather than left armed with no marks -- see KCMComparisonRun.h. */
	virtual void		RefreshComparison() = 0;

	// ---- the page pairing rule ------------------------------------------------------------
	//
	// Whether ordinary pages pair by UID (the default; a page with its namesake, a UID on one side
	// only being an added / removed page) or by position (the older rule, for two documents that
	// share no history). A flyout check toggle flips it and the saved panel state reads and writes
	// it, both UI-side; the rule is KCMPagePairRule.h.

	virtual bool16		GetPairPagesByUid() = 0;
	virtual void		SetPairPagesByUid(bool16 on) = 0;

	// ---- the Before/After report ---------------------------------------------------------
	//
	// One PDF: a first page, then every changed page with the older version (rings printed) on
	// the left and the newer on the right, then the Story and the Resources tables (KCMReport.h).
	// Made only while NOTHING IS STARTED: it compares the pair a Start would, for
	// itself - the pixels in every compare mode - and leaves nothing started; while a comparison
	// runs it refuses and says to Stop (the flyout greys its item then). kTrue when written;
	// outMessage names the folder, or says why not. The flyout item calls it and shows the message.

	virtual bool16		ExportBeforeAfterReport(PMString& outMessage) = 0;

	// ---- the story ID labels ("Show Story IDs") --------------------------------------------
	//
	// Whether every page item that holds a story has its story's UID written above it
	// (KCMRingAdornment.h, the section of the same name). Default OFF. A flyout check toggle
	// flips it and the saved panel state reads and writes it, both UI-side; the drawing is the
	// model's.

	virtual bool16		GetShowStoryIds() = 0;
	virtual void		SetShowStoryIds(bool16 on) = 0;

	/** **THE PATH SHOWN WHEN THAT END OF THE PAIR IS A FILE**. A Task Start saves a
		copy of the document and chooses THAT FILE as the Source without opening it, so the panel
		has a path to show and no document to ask for a name. Empty when that end is a document,
		the lent database, or nothing at all - which is how the caller tells the cases apart.
		⚠**The file is not read.** A path is the whole of it. */
	virtual void		GetChosenSourceFileLabel(PMString& outLabel) = 0;
	virtual void		GetChosenTargetFileLabel(PMString& outLabel) = 0;

	/** **TASK START, THE FILE WAY** (the user's design). Saves a copy of the document
		to a file the reader picks and chooses that file as the Source. **It opens nothing and
		starts nothing** - what comes back is the ordinary "a Source has been chosen" state.
		Which document: the chosen Target, else the active one. The Target is then that same
		document. KCMTaskStartSave.h carries the order of the steps and why each is where it is.
		⚠**kFalse WITH AN EMPTY outWhyNot MEANS THE READER CANCELLED THE SAVE DIALOG**, and the
		 caller says nothing at all in that case. Anything else is a real failure. */
	virtual bool16		CanTakeTaskStartCopy() = 0;
	virtual bool16		TakeTaskStartCopy(PMString& outWhyNot) = 0;

	/** "COMPARE WITH TRACKED CHANGES" (the author: "Compare with Tracked Changes alone", "do not make a Source
		document"): the Target - the chosen one, else the active document - has its tracked changes listed from its own
		records, and nothing else is made: no file, no window, no Source. A running comparison is stopped first; a Stop
		ends the listing and the mode is the one before it (KCMTrackStart.h).
		@return kTrue when the listing started; outMessage is the status line's sentence either way. */
	virtual bool16		CanStartTrackChanges() = 0;
	virtual bool16		StartTrackChanges(PMString& outMessage) = 0;

	/** TASK START, KEPT IN MEMORY (the author: "make a Task Start that is a clone, as a new menu" - the menu
		says "Task Start", and the item that saves a copy became "Task Start (Save)..."). The document's state is cloned
		and chosen as the Source - no dialog, no file, nothing started. KCMTaskStartClone.h carries the steps and when
		the clone goes. kFalse with the reason when it was not taken (nothing was changed then). */
	virtual bool16		CanTakeTaskStartClone() = 0;
	virtual bool16		TakeTaskStartClone(PMString& outWhyNot) = 0;

	/** THE COMPARE MODE THE READER CHOSE - Pixel, Story or Resources: what Start compares in, and what Save
		Panel Settings writes. It differs from GetCompareMode only while a Track listing is up (GetCompareMode answers
		kKCMModeTrack then, and this the mode a Stop goes back to). */
	virtual KCMCompareMode	GetCompareModeSetting() = 0;
};

#endif // __IKCMCompareFacade_h__
