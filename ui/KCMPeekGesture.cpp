//========================================================================================
//
//  KCMPeekGesture.cpp
//
//  What a mouse gesture under the tool means and what it starts: classifying the modifiers,
//  beginning and ending the reveal and the peeks, working out which document window the mouse is
//  over, and the UI clean-up that follows a batch document close.
//
//  ★The armed state (sPeekArmed / sPeekTargetDB / sPeekSourceDB) stays on the model side in
//    KCMPeek.cpp, so it is read from here **through IKCMCompareFacade** (IsArmed /
//    GetArmedTargetDB / GetArmedSourceDB).
//    ⚠**Free functions of the same name still exist on the model side** (KCMCore.h declares
//    KCMIsArmed, KCMPeek.cpp defines it), so never write that this file calls them: that would
//    read as the UI reaching into the model, the one direction the split forbids.
//    The held state of the CMYK gesture (Alt + left) belongs to KCMCmykCursor.cpp, so the press
//    begins and ends through that file's entry points.
//
//  UI side: it reads whether the tool is being held, which the model cannot see.
//
//  NOTE: the close handling here is the UI half only. Its model twin is KCMHandleDocsClosed() in
//  KCMPeek.cpp, which drops the tracking state of documents that are gone. Both listen to the same
//  close notification for different purposes -- do not merge them.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// The object model:
#include "PersistUtils.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IEventUtils.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISession.h"
#include "IWindow.h"
#include "IWindowUtils.h"
#include "IDocumentPresentation.h"

// The view and its magnification ＝ what the peek observes, on the side that has a window:
#include "IControlView.h"
#include "IPanorama.h"
#include "PMMatrix.h"				// what GetContentToWindowMatrix answers with -- the zoom is read off it

// For folding a batch close (several documents closed in a row) into one:
#include "CObserver.h"				// the base of the completion observer
#include "ISubject.h"				// AttachObserver / IsAttached
#include "IActiveContext.h"			// where the observer lodges (kActiveContextBoss)
#include "IBoolData.h"				// IID_IKFILESCLOSING on the session -- "is a document closing right now"
#include "LinksUIID.h"				// ★a public header: IID_IKFILESCLOSING / kPendingDocumentsClosedMsg, both provided by the application's Links UI

#include "PMReal.h"
#include "PMString.h"

// The plug-in's own headers:
#include "KCMUIID.h"
#include "IKCMMarkData.h"          // reading the marks and the overset state (the held display state is
                                     // read AND written through IKCMCompareFacade instead)
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMScrollMap.h"          // removing / repainting the map strip after a batch close
#include "KCMThumbIdleTask.h"      // defer the post-close rebuild to the next idle (past the front-window change)
#include "Utils.h"                   // Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"     // the peek display, the armed state, the base opacity
#include "KCMCmykCursor.h"         // KCMCmykBeginPress / KCMCmykEndPress -- the held CMYK state belongs there
#include "KCMBoundaryID.h"         // kKCMModeStory (the compare-mode enumerator)
#include "KCMStoryPressMarks.h"    // the coloured ground put under changed text while held, in the Story mode
#include "KCMViewLookup.h"         // KCMQueryViewUnderMouse / KCMQueryMouseContentPoint /
                                     // KCMQueryPanorama / KCMQuerySpreadUIDForView (all UI side)
#include "KCMPeekGesture.h"

// Shift + left lays the other version over the pressed window at 100%, Shift+Alt + left at 50%.
// It shows only while the button is held and goes when it is released (the modifiers themselves may
// be let go of first): they are read **once**, at press time.
static const PMReal kKCMPeekSemiOpacity = 0.5;	// the overlay's opacity (0..1) under Shift+Alt + left
static bool16 sPeekActive        = kFalse;	// is Shift / Shift+Alt + left being held ＝ is an overlay up
static bool16 sSingleShowing     = kFalse;	// is the bare left button being held ＝ are the marks temporarily up at the panel's 25%/75%. The release hides them again and puts the base opacity back
// ★★**Which window is being peeked from.** What has to be repainted on release is **the window
//   that was pressed**; while the peek only ever ran over the Target that was a constant, and the
//   moment it could run from the Source window too, releasing there left the picture on screen
//   (sShowOriginal drops, but no repaint reaches that document).
//   ⚠**This is UI state**: "which window was the button pressed in" is a question about a window,
//     and the model has none (its sOrigDB answers a different question -- which document was
//     rasterised -- and the two agreeing is a consequence, not the same fact).
//   ⚠Its lifetime is sPeekActive's. It is put back to nil on release so a closed document is never
//     pointed at.
static IDataBase* sPeekUnderDB   = nil;

// After the marks (the frames and the change counts) are switched on or off, repaint the document
// they belong to so it takes effect at once. ★It uses **the document the marks are on** rather than
// the armed Target, so that it works whether or not a comparison is armed.
static void KCMInvalidateMarksDoc()
{
	Utils<IKCMCompareFacade>()->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedTargetDB());
}

// The db of the document window directly under the mouse, or nil. The three tests below differ only
// in which db they compare it against, so the window resolution is written once, here.
//
// ★Why the mouse and not the front document: the CMYK sampling and the partial spread refresh hit
//   test against the **Target's** page coordinates, so with the mouse over the Source, or over an
//   unrelated third document, that window's local coordinates would be read as the Target's page
//   coordinates. Only a press over the right window may act.
//   ⚠It used to ask Utils<ILayoutUIUtils>()->GetFrontDocument(). Operating the **new pane** of a
//   split window (kLayoutSecondaryPanelWidgetID) leaves OWL's own idea of which view is active on
//   the original pane, and the test failed (measured by the user). ⇒ QueryWindowUnderPoint settles
//   the document from the mouse position alone, and **no active state is consulted at all** ---- so
//   do not put a "front view" back into the name or the body of this test.
// ★★Its first three steps (GetGlobalMouseLocation -> QueryWindowUnderPoint -> IDocumentPresentation)
//   are KCMQueryViewUnderMouse's first three as well, and the two are deliberately NOT folded:
//     - here  ... "is the **document** under the mouse the Target or the Source" ＝ a question about
//                 a document, and three steps answer it
//     - there ... "which **view** is under the mouse" ＝ it has to go on and pick the pane of a split
//                 window (FindWidget plus a hit test)
//   Borrowing that one would add the pane work for nothing **and change the answer for a window with
//   no layout widget (the story editor, say), where it returns nil**. ⇒ when the window resolution
//   on one side is touched, read the other.
static IDataBase* KCMQueryDocDbUnderMouse()
{
	GSysPoint globalPt = Utils<IEventUtils>()->GetGlobalMouseLocation();

	InterfacePtr<IWindow> hitWindow(Utils<IWindowUtils>()->QueryWindowUnderPoint(globalPt, kFalse));
	if (hitWindow == nil)
		return nil;

	InterfacePtr<IDocumentPresentation> hitPres(hitWindow, UseDefaultIID());
	if (hitPres == nil)
		return nil;

	return hitPres->GetDocumentUIDRef().GetDataBase();
}

static bool16 KCMMouseIsOverTarget()
{
	IDataBase* const armedTarget = Utils<IKCMCompareFacade>()->GetArmedTargetDB();
	return (armedTarget != nil && KCMQueryDocDbUnderMouse() == armedTarget) ? kTrue : kFalse;
}

// Is the window under the mouse the Source ＝ the db the always-on Source frames are drawn on
// (sSrcDB)? It is what lets a press in the Source window put those frames away for as long as it is
// held (the mirror of KCMMouseIsOverTarget above).
// ★The Source marks are tied to **sSrcDB, not to the armed sPeekSourceDB**: the two are the same
//   document, but this test belongs to the db the marks are actually on.
static bool16 KCMMouseIsOverSource()
{
	IDataBase* const markedSrcDB = Utils<IKCMMarkData>()->GetMarkedSourceDB();
	return (markedSrcDB != nil &&
	        KCMQueryDocDbUnderMouse() == markedSrcDB) ? kTrue : kFalse;
}

// Is the window under the mouse the older version's document? ★It is a **different question** from
// KCMMouseIsOverSource above, which is why both exist:
//   - above ... "is this the window of the db the Source frames are drawn on" (asked to put those
//               frames away)
//   - here  ... "is the older version being looked at" (asked to decide which version's changes to
//               show)
// ⚠**In the Story mode the one above is always kFalse**: that mode draws no frames, so
//   GetMarkedSourceDB() stays nil (drawRings is (mode != Story) in KCMDrawEventHandler).
//   ⇒ borrowing it would mean a press in the Source window did nothing there. These are not the same
//     test written twice; they ask about **two different facts** -- the db the marks went on, and the
//     db that was armed.
static bool16 KCMMouseIsOverArmedSource()
{
	IDataBase* const armedSource = Utils<IKCMCompareFacade>()->GetArmedSourceDB();
	return (armedSource != nil &&
	        KCMQueryDocDbUnderMouse() == armedSource) ? kTrue : kFalse;
}

//========================================================================================
// The shared entry points for the tracker (the left button), called from the press and from the
// release while the KCM tool is active (KCMTracker.cpp). A bare press inverts the marks; the
// modifiers switch that for a peek or for CMYK.
// The press state (sPeekActive / sSingleShowing / sPeekUnderDB) is this file's; what is actually
// drawn is raised and lowered through IKCMCompareFacade.
//
// ★Where it came from: all of these were middle-button-plus-modifier gestures before they were
//   moved onto the tool's left button. The middle-button route is gone, and so are the Ctrl
//   gestures for the panel and for re-comparing -- re-comparing lives on the page context menu as
//   "Refresh Page Comparison".
//========================================================================================

// Start a peek from the tracker (the left button). While a comparison is armed and the mouse is
// over **either** of the two compared windows, the OTHER version of the spread under the mouse is
// laid over it at opacity (1.0 = opaque / 0.5 = half). There is no temporary switch to the hand
// tool: the tracker already has the mouse captured and a drag goes to ContinueTracking.
//
// ★★**Resolving the view is done here**, on the UI side: "which window, at what magnification,
//   with the mouse where" are questions with no answer where there is no window, so they are
//   observed here and the values are handed down to the model.
//   ⚠**The two early returns leave sPeekActive up and the marks switched off.** That is not a leak:
//   every way the press can end goes through KCMTrackerRevealEnd, which puts both back.
static void KCMTrackerBeginPeek(PMReal opacity)
{
	// ★★★**The peek works from either window** (asked for by the user: put the Shift behaviour on the
	//   Source side as well, "peeking at the Target from the Source"). It used to answer over the
	//   Target window only, and what was laid over was always the older version ＝ one way.
	//   ★**The window that was pressed stays underneath; the partner document is what is laid over
	//   it** ＝ from either window, what you see is the other version.
	// ⚠Target is tested first and **the order carries meaning**: where a document is being compared
	//   against itself (sSrcDB == sDB) both tests are true. Falling to the Target side is what it has
	//   always done.
	const bool16 overTarget = KCMMouseIsOverTarget();
	const bool16 overSource = overTarget ? kFalse : KCMMouseIsOverArmedSource();
	if (!Utils<IKCMCompareFacade>()->ArmedDocsAlive() || (!overTarget && !overSource))
		return;	// not started / a compared document has closed / not over either compared window
	sPeekActive = kTrue;
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	compare->SetPeekOpacity(opacity);	// the overlay's opacity, read at draw time
	sSingleShowing = kFalse;
	compare->SetMarksVisible(kFalse);	// no frames while peeking ＝ the other version alone

	// The layout view the mouse is over (split windows included -- see KCMQueryViewUnderMouse).
	InterfacePtr<IControlView> view(KCMQueryViewUnderMouse());
	if (view == nil)
		return;

	PMReal mx = 0.0, my = 0.0;
	if (!KCMQueryMouseContentPoint(view, mx, my))
		return;

	// Two observations: the content -> window scale (zoom x device scale), and the UI zoom (the
	// magnification the reader sees).
	// ★What dpi they turn into -- the floor at 50%, the clamp to 16..300dpi -- is the model's
	//   decision and is not touched here. Where the panorama cannot be read, uiZoom = 0 is handed
	//   down and the model reads that as "no floor".
	const PMReal viewScale = view->GetContentToWindowMatrix().GetXScale();
	PMReal uiZoom = 0.0;
	InterfacePtr<IPanorama> peekPano(KCMQueryPanorama(view));
	if (peekPano != nil)
		uiZoom = peekPano->GetXScaleFactor(kFalse);

	// ★★★**Which spread that view is showing** is handed down too. Without it, a point over a master
	//   spread lands on an ordinary page instead -- the two OVERLAP in pasteboard coordinates -- and
	//   **not one page of the other version appears**. "Which spread is on screen" is a question about
	//   a window ＝ the UI observes it and tells the model.
	//
	// ★★★**The direction is settled by the argument order alone.** What the model does
	//   (KCMPeekShowAt) is "**find the page under the mouse in the FIRST argument's document, and
	//   rasterise the SECOND argument's matching page over it**" ---- it never asks which of the two
	//   is the newer version. The page pairing (KCMBuildPairing / KCMBuildMasterPairing) is built in
	//   argument order as well, and the drawing side only reads sOrigDB, never either document's
	//   role. ⇒ **not one line of reverse-direction code was needed**; handing the pressed window in
	//   as the first argument was all of it.
	// ⚠One asymmetry is left. The model's "do not lay anything over an unchanged spread" optimisation
	//   is written as `sDB ==` **the first argument**, so peeking from the Source it never holds and
	//   even an unchanged spread is rasterised (＝ **slower, but not wrong**). Making it symmetric
	//   would mean looking the page up through sSrcPageToTarget; see first whether it is noticeable.
	IDataBase* const under = overTarget ? compare->GetArmedTargetDB() : compare->GetArmedSourceDB();
	IDataBase* const over  = overTarget ? compare->GetArmedSourceDB() : compare->GetArmedTargetDB();
	sPeekUnderDB = under;		// the window the release repaints (see the declaration)
	compare->ShowPeekAt(under, over,
	                    mx, my, viewScale, uiZoom,
	                    KCMQuerySpreadUIDForView(view));
}

// Classify the modifiers (see KCMPeekGesture.h). ★**The assignment is defined here and nowhere
// else**: the tracker's press branch (KCMTracker.cpp) and KCMTrackerRevealBegin below are the only
// two that ask, and everything after them branches on the value those calls returned.
KCMGesture KCMClassifyGesture(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	// A left button with Ctrl (cmd) is unassigned: re-comparing moved to the page context menu and
	// the panel operations to the flyout, and the tracker handles neither.
	// ★Mac's Control is unassigned as well: on macOS Control-click is the standard secondary-button
	//   (context menu) gesture, so even arriving as a left press it must not have the reveal taken
	//   from it. MacCtrlDown() is always kFalse on Windows, so nothing changes there.
	if (cmdDown || macCtrlDown)
		return kKCMGestureNone;
	if (altDown && !shiftDown)
		return kKCMGestureCmyk;		// Alt alone: sample the CMYK colour
	if (shiftDown && altDown)
		return kKCMGesturePeek50;		// Shift+Alt: the other version laid over at 50%
	if (shiftDown)
		return kKCMGesturePeek100;	// Shift: the other version laid over at 100%
	return kKCMGestureReveal;			// no modifier: invert the pressed window's marks
}

void KCMTrackerRevealBegin(bool16 shiftDown, bool16 altDown, bool16 cmdDown, bool16 macCtrlDown)
{
	KCMCmykClearPending();	// whether this press shows a CMYK cursor is settled in the Cmyk branch below (default: it does not)

	const KCMGesture gesture = KCMClassifyGesture(shiftDown, altDown, cmdDown, macCtrlDown);
	if (gesture == kKCMGestureNone)
		return;	// unassigned (Ctrl / Command, Mac's Control). The tracker has the mouse, but nothing drawn changes.

	// ---- Put the pressed window's always-on marks away for as long as the button is held ----
	// The gestures that hide are the reveal and the peeks (no modifier / Shift / Shift+Alt).
	// ★CMYK (Alt alone) does not hide: it samples with the frames left standing. Only the pressed
	// window's frames go (Target and Source are decided separately).
	// ★★★The rule is a single one -- **while the button is held, that window's marks are the opposite
	//   of what they were**: shown ones go away, hidden ones come up (the coming-up side is the reveal
	//   and the Story branch further down). The toggle that used to own this ("Hold to Hide Marks")
	//   was retired: its "keep them up" half duplicated "Show Marks on ..." exactly, and this
	//   temp-hide was the only thing it had of its own, so that is now simply what a press does while
	//   a "Show Marks on ..." toggle is on.
	const bool16 tempHideGesture = (gesture != kKCMGestureCmyk);
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (tempHideGesture)
	{
		if (compare->GetShowTargetMarks() && !compare->GetMarksTempHidden() && KCMMouseIsOverTarget())
		{
			compare->SetMarksTempHidden(kTrue);
			KCMInvalidateMarksDoc();	// repaint the Target
		}
		// ★★★The Source side **says only "pressed" and does not look at the toggle**. Whether that means
		//   show or hide is the drawing side's decision -- it **XORs** this with sSrcMarksOn -- so
		//   reading GetShowSourceMarks() here would put **the same decision in two places**
		//   ([[one-question-one-place]]).
		//   ⇒ this is what makes **a press in a Source window whose toggle is OFF bring the frames up**,
		//     which is what the rule had been claiming in three places: the same in Pixel and Story,
		//     the same for Target and Source.
		//   ⚠The Target side above still reads GetShowTargetMarks(), which looks asymmetric. It cannot
		//     be folded the same way: over there "show" is owned by a second flag (sMarksVisible, the
		//     reveal below) which the peeks raise as well. **Only the Source side folds into one.**
		if (!compare->GetSrcMarksPressed() && KCMMouseIsOverSource())
		{
			compare->SetSrcMarksPressed(kTrue);
			compare->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedSourceDB());	// repaint the Source (compare was queried above)
		}
	}

	// ---- The gesture branches ----
	if (gesture == kKCMGestureCmyk)
	{
		// ★The held CMYK state belongs to KCMCmykCursor.cpp, sCmykCursorPending included -- whether this
		//   press shows a CMYK cursor at all (by default it does not) is initialised over there.
		KCMCmykBeginPress();
		return;
	}
	if (gesture == kKCMGesturePeek50)
	{
		// Shift+Alt + left: lay the other version over at 50%.
		KCMTrackerBeginPeek(kKCMPeekSemiOpacity);
		return;
	}
	if (gesture == kKCMGesturePeek100)
	{
		// Shift + left: lay the other version over, fully opaque.
		KCMTrackerBeginPeek(PMReal(1.0));
		return;
	}

	// ---- No modifier, Story mode: while held, put a coloured ground under the changed text ----
	// ★★A different road from the Pixel reveal below, because the picture is made differently. Pixel
	//   knows only **where the page looks different**, so it draws frames; Story knows **which
	//   characters changed**, so it lays a coloured ground under exactly those (the user's choice; it
	//   inverted them until it turned out that could not be printed). ⇒ it does not change size with
	//   the magnification, and it needs no frame around the page.
	// ⚠The temp-hide above deals with **frames only**, and the Story mode has none (drawRings is
	//   (mode != Story) in KCMDrawEventHandler). So "the opposite while held" for the Story ground is
	//   NOT decided here but in KCMStoryMarksRefresh, which XORs the pressed window's toggle with the
	//   press (KCMStoryPressMarks.cpp).
	//   ⇒ **press a window whose toggle is OFF and they appear; press one that is ON and they go.**
	//     This branch only reports that the button went down.
	// ★Only the pressed window's side is marked (the user's choice): deleted characters exist in the
	//   older version alone and inserted ones in the newer, so which document is being looked at
	//   decides what there is to mark.
	if (compare->GetCompareMode() == kKCMModeStory)
	{
		if (KCMMouseIsOverTarget())
			KCMStoryPressMarksBegin(kFalse /*target*/);
		else if (KCMMouseIsOverArmedSource())
			KCMStoryPressMarksBegin(kTrue /*source*/);
		return;		// the frame reveal below does not exist in the Story mode
	}

	// ---- No modifier, Pixel mode: bring the marks up for as long as the button is held ----
	// ★This is the **coming-up side** of "the opposite while held". Where the Target's marks are
	//   already up, the temp-hide above has just put them away, so there is nothing to do here.
	if (compare->GetShowTargetMarks())
		return;

	// "Is there anything to mark at all": five things OR-ed together (the changes, the overflow on
	// both sides, the registered pages on both sides). ★Matching the overflow set to the current pair
	// of documents is part of that answer and is done on the other side of IKCMMarkData, not here.
	if (!Utils<IKCMMarkData>()->HasAnyMarkableContent())
		return;

	// Bring them up over the Target window only. Over the Source window the same press is carried by
	// SetSrcMarksPressed above (the drawing side XORs it), and over an unrelated window nothing
	// happens at all.
	if (!KCMMouseIsOverTarget())
		return;

	sSingleShowing = kTrue;
	compare->SetMarkScreenOpacity(compare->GetSelectedMarkOpacity());	// the panel's 25% / 75%
	compare->SetMarksVisible(kTrue);	// up for as long as the button is held
	KCMInvalidateMarksDoc();
}

void KCMTrackerRevealEnd()
{
	// ★Releasing what the CMYK gesture (Alt + left) held -- the press-time font, the hover/other
	//   caches, the status line cleared to a single space -- belongs to KCMCmykCursor.cpp.
	KCMCmykEndPress();

	// Take away the coloured ground the Story mode put up while held. ⚠It does nothing where the
	//   press did not put any up (it remembers that itself), so pressing and releasing in the Pixel
	//   mode does **not** clear the temporary marker a jump left behind.
	KCMStoryPressMarksEnd();

	// Put back the always-on frames that were hidden while the button was held ＝ the returning side
	// of "the opposite while held". Depending on which window was pressed, the Target flag or the
	// Source one (or both) is up; where that window's toggle is off, neither was raised in the first
	// place and this does nothing. What raises them is each "Show Marks on ..." toggle, in
	// KCMTrackerRevealBegin.
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare->GetMarksTempHidden())
	{
		compare->SetMarksTempHidden(kFalse);
		KCMInvalidateMarksDoc();	// repaint the Target
	}
	if (compare->GetSrcMarksPressed())
	{
		compare->SetSrcMarksPressed(kFalse);
		compare->InvalidateDB(Utils<IKCMMarkData>()->GetMarkedSourceDB());	// repaint the Source (compare was queried above)
	}

	if (sPeekActive)
	{
		// Shift / Shift+Alt + left was released -> take the overlay away, leaving the marks alone. The
		// raster cache is kept, so peeking again is immediate.
		sPeekActive = kFalse;
		if (compare->GetShowOriginal())
		{
			compare->SetShowOriginal(kFalse);
			// ★Repaint **the window that was being peeked from**. This was GetArmedTargetDB() outright,
			//   so the moment the peek could run from the Source window as well, releasing there left
			//   the picture on screen ---- sShowOriginal drops, but no repaint reaches that document.
			//   ⚠Where it could not be recorded it falls back to the Target: there is no way to reach
			//     this without a press, but falling back to what it used to do reads better than
			//     handing nil down and having nothing happen.
			compare->InvalidateDB((sPeekUnderDB != nil) ? sPeekUnderDB : compare->GetArmedTargetDB());
		}
		sPeekUnderDB = nil;		// never left pointing at a document that may close
	}
	else if (sSingleShowing)
	{
		// The Pixel reveal was released -> switch the frames off again and put the base opacity back.
		sSingleShowing = kFalse;
		compare->SetMarksVisible(kFalse);
		compare->SetMarkScreenOpacity(compare->GetBaseScreenOpacity());
		KCMInvalidateMarksDoc();
	}
}

// KCMResetPeekGestureState (declared in KCMPeekGesture.h) -- forget that a press is showing
// anything.
//
// ⚠**There is exactly one caller**, and it is the UI's close handling: the
//   kKCMComparisonDocsClosedMessage branch of KCMModelChangeObserver, and only where the comparison
//   itself ended. **Arming (Start) and disarming (Stop) do not call it.**
// ★★**That is not an oversight** (every route was walked): these flags are only up while the button
//   is held, a press can end in exactly three ways, and **all three go through KCMTrackerRevealEnd**
//   ---- KCMTracker.cpp's EndTracking (the ordinary release), AbortTracking (interrupted by a menu,
//   say), and the path in BeginTracking where the base refused to track. ∴ they cannot still be up
//   when Start or Stop runs, and if one somehow were, the next release would drop it.
//   ⇒ **what was missing was this explanation, not a call.** Reviving the "just in case" call would
//     add one more line nobody can say why they need.
void KCMResetPeekGestureState()
{
	sPeekActive    = kFalse;
	sSingleShowing = kFalse;
	// ⚠**The road here is "a document closed"** (KCMModelChangeObserver calls it on that
	//   notification), so the peeked-from window pointer has to go as well: carry a closed
	//   IDataBase* forward and the next release would ask for a repaint of it. ★The name is the
	//   contract -- a function that says it forgets the press state must forget all of it.
	sPeekUnderDB   = nil;
}

//========================================================================================
// Fold the clean-up after a batch close (several documents in a row, or the close-all at quit)
// into one
//
//   kAfterCloseDoc arrives **once per closed document**, so the model sweeps once per document and
//   notifies once per document. If the UI clean-up (removing the scroll-map strip, invalidating the
//   views, booking the thumbnail rebuild, refreshing the panel and the status line) ran on each of
//   those, closing N documents would run it N times. Dropping the state is done immediately; only
//   the UI half is held back and flushed once, when they have all closed. It also touches widgets
//   fewer times while things are coming apart, which helps at quit (on the Mac especially).
//
//   ★"Is a document closing right now" and "they have all closed" are both published by the
//     application's own Links UI plug-in, and only have to be read (public header LinksUIID.h):
//       - IID_IKFILESCLOSING        = an IBoolData on the session boss (kTrue from the first close,
//                                     kFalse once they are all done)
//       - kPendingDocumentsClosedMsg = sent to the application's subject the moment the last one closes
//   ★Where the Links UI is absent or disabled the flag cannot be read. Nothing is held back then and
//     each document is cleaned up as it closes ＝ the behaviour from before this existed.
//========================================================================================

static bool16 sDeferredCloseUiPending = kFalse;	// is the UI clean-up being held back (the completion notification flushes it once)

// Is a batch close running right now? It only reads the session flag the application keeps; kFalse
// where that cannot be read.
// ★It is not static because **the UI's close branch (KCMModelChangeObserver) asks it**, and the
//   deferral it feeds (KCMDeferCloseUi) lives here with it -- the question and its answer in one
//   place. ⚠The model does not ask: KCMPeek.cpp records that the model asking for UI state was the
//   flow going the wrong way.
bool16 KCMBatchCloseInProgress()
{
	ISession* session = GetExecutionContextSession();	// can be nil while the application is quitting
	if (session == nil)
		return kFalse;
	InterfacePtr<IBoolData> filesClosing(session, IID_IKFILESCLOSING);
	return (filesClosing != nil && filesClosing->GetBool()) ? kTrue : kFalse;
}

// Flush the UI clean-up that was held back, once, when the batch close completes.
static void KCMFlushDeferredCloseUi()
{
	if (!sDeferredCloseUiPending)
		return;
	sDeferredCloseUiPending = kFalse;

	// ★This function asks three times (the two guards below, and the InvalidateDB in the loop at the
	//   end), so the interface is queried once and kept (Utils.h:74-80).
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());

	if (compare->IsAppQuitting())
		return;		// touch no widget while quitting (Shutdown drops the state)

	// ★★Do not run the clean-up if a new comparison has been started while it was held back. This
	//   function exists to clean up after a document that closed, but what it does -- remove the
	//   strip, put "marks cleared" in the status line -- is not aimed at any particular document.
	//   Should the completion notification never arrive, the deferral stays up; if the reader then
	//   presses Start and a later batch close finally flushes it, it would **remove the running
	//   comparison's strip and overwrite its status line with "marks cleared"**.
	//   ★While armed there is nothing left to clean up anyway: the deferral is only ever raised when
	//     the comparison had ended (the model dropped its state ＝ disarmed), and the Start that
	//     followed has re-injected the strip and refreshed the status line and the panel. The closed
	//     document's window went with the document, so no strip is left behind either. ∴ lowering the
	//     flag is the whole of it.
	if (compare->IsArmed())
		return;

	// If Find Overset is on by itself (and its scanned document is still alive), keep the map and
	// only repaint it; otherwise remove it. The same decision the close branch makes when it cleans
	// up immediately.
	if (Utils<IKCMMarkData>()->GetOversetOn())
		KCMScrollMapInvalidateAll();
	else
		KCMScrollMapDetachAll();

	PMString s("marks cleared");	// the same wording the Stop button (DoClear) uses
	s.SetTranslatable(kFalse);
	KCMSetStatus(s);

	// ★What survives is not necessarily what survived when the deferral was raised -- it is a batch
	//   close, so more have been shut since. No recorded pointer is used: the documents open **now**
	//   are enumerated here and then. The marks are already gone, so repainting an unrelated document
	//   draws no frames.
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList != nil)
	{
		const int32 nDocs = docList->GetDocCount();
		for (int32 i = 0; i < nDocs; ++i)
		{
			IDocument* doc = docList->GetNthDoc(i);
			if (doc == nil)
				continue;
			IDataBase* db = ::GetUIDRef(doc).GetDataBase();
			compare->InvalidateDB(db);
			KCMScheduleThumbRefresh(db);	// deferred thumbnail rebuild (repeats of the same db are folded)
		}
	}

	KCMRefreshPanel();
}

/** An observer whose only job is to receive the batch-close completion
    (kPendingDocumentsClosedMsg). The .fr AddIn lodges it on kActiveContextBoss under
    IID_IKCMDOCSCLOSEDOBSERVER, for the same reason the layout-sync observer lodges there: it is the
    arrangement that has been shown to work. What it subscribes to is the application's subject. */
class KCMDocsClosedObserver : public CObserver
{
public:
	KCMDocsClosedObserver(IPMUnknown* boss) : CObserver(boss, IID_IKCMDOCSCLOSEDOBSERVER) {}
	~KCMDocsClosedObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KCMDocsClosedObserver, kKCMDocsClosedObserverImpl)

void KCMDocsClosedObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol == IID_IAPPLICATION && theChange == kPendingDocumentsClosedMsg)
		KCMFlushDeferredCloseUi();
}

// Subscribe to the application's subject (once, from Startup).
//
// ★★**This is the one observer that is NOT detached at shutdown.** Its two siblings in the same
//   arrangement -- lodged on kActiveContextBoss, subscribed to the application's subject --
//   KCMModelChangeObserver and KCMPanelVisibilityObserver, both are. **What settles it is what
//   Update does while the application is quitting**:
//     - those two ... most of their branches are unguarded (KCMModelChangeObserver::Update has six
//                     branches and exactly one carries an IsAppQuitting guard), so they could touch
//                     a widget from code that is coming apart ⇒ **they must be detached**
//     - this one  ... Update calls KCMFlushDeferredCloseUi and nothing else, and **its entrance is
//                     guarded twice** ⇒ letting it run does nothing:
//       (1) KCMPeekGestureShutdown() lowers sDeferredCloseUiPending, so KCMFlushDeferredCloseUi
//           returns at **its first guard** (!sDeferredCloseUiPending)
//       (2) and past that, its **IsAppQuitting() guard** returns without touching a widget
//   ⇒ **the asymmetry is deliberate, not something left undone.** ⚠But those two points are the
//     whole of the reason, so if either goes, add the detach here. It would do no harm: the
//     KCMModelChangeObserver side touches the same GetActiveContext() at shutdown and detaches, and
//     that passes the teardown-safety checks.
//
// ⚠**"Detaching is itself what crashes" would be too wide a rule, and it was believed here for
//   three days.** What crashed on the layout-sync side was the KCMSetLayoutSync(kFalse) route ＝
//   **walking every open view with GetAllLayoutViews** while they are being torn down
//   (KCMViewSync.cpp) -- not the detach. The counter-example was inside this same plug-in all along:
//   KCMDetachModelChangeObserver touches GetActiveContext() at shutdown and does not crash.
//   ⇒ ★**listing two dangerous things together turns an operation that uses only one of them into a
//     prohibition as well.**
void KCMAttachDocsClosedObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKCMDOCSCLOSEDOBSERVER));
	if (obs == nil)
		return;
	InterfacePtr<IApplication> app(session->QueryApplication());
	InterfacePtr<ISubject> subject(app, IID_ISUBJECT);
	if (subject == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMDOCSCLOSEDOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKCMDOCSCLOSEDOBSERVER);
}

// KCMDeferCloseUi (declared in KCMPeekGesture.h) -- hold the UI clean-up back.
// ★Its one caller is the UI's own close branch (KCMModelChangeObserver), which has already asked
//   KCMBatchCloseInProgress above. The model never calls it: deciding **when** to touch the UI is
//   the UI's business, and the model only reports that a document closed.
void KCMDeferCloseUi()
{
	sDeferredCloseUiPending = kTrue;
}

// KCMPeekGestureShutdown (declared in KCMPeekGesture.h) -- the teardown clean-up.
void KCMPeekGestureShutdown()
{
	// ★★★**This one line is a guard**, not tidiness. KCMDocsClosedObserver is not detached at
	//   shutdown (the reason is above KCMAttachDocsClosedObserver), so if kPendingDocumentsClosedMsg
	//   arrives after Shutdown, Update still runs -- and this assignment is what makes
	//   KCMFlushDeferredCloseUi return at **its first guard** (!sDeferredCloseUiPending).
	//   ⚠Remove it as "pointless" and the only guard left is that function's IsAppQuitting().
	sDeferredCloseUiPending = kFalse;
}
