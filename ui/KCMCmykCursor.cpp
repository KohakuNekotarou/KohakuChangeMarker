//========================================================================================
//
//  KCMCmykCursor.cpp
//
//  Alt + left, the colour compare. It holds the mode fixed for the length of a press (which document
//  is being looked at), the drawing of the CMYK onto the cursor itself, the live update during a
//  drag, and the building of the "no value" display.
//
//  ★The split out of KCMPeek.cpp changed not one line inside these functions. What changed is which
//    file they sit in and who can see them.
//    ★★The press-time state (sCmyk*) belongs to this file alone. Before the split three files
//      (cursor drawing, the gesture, Shutdown) reached into it directly, and it was the first thing
//      that would not come apart. The three lumps the outside needs were each given one way in:
//      KCMCmykBeginPress / KCMCmykEndPress / KCMCmykShutdown.
//
//  This is the UI side: it builds the cursor bitmap and draws into a gPort.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Object model:
#include "IDataBase.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "ISession.h"

// Geometry / view:
#include "IControlView.h"
#include "PMReal.h"
#include "PMString.h"

// The custom bitmap cursor (Alt + left draws the CMYK on the cursor itself):
// (ICursorUtils.h arrives through KCMCheckGlyph.h ---- no symbol of it is used directly here, so the
//  direct include was dropped.)
#include "IGraphicsPort.h"			// setrgbcolor / rectfill / selectfont / show
#include "IFontMgr.h"				// obtaining the default font
#include "IPMFont.h"

#include <chrono>				// steady_clock, for the throttle on live re-sampling during a drag

// Project includes:
// ★★**The include of KCMColorSampler.h (the model side) was dropped.** The direction was legal in
//   itself - this is the UI side - but the three things being called through it were **free
//   functions**, and a free function stops linking the moment the two sides become separate .plns.
//   ⇒ They go through IKCMCompareFacade's SampleColorAt / BeginColorDrag / EndColorDrag now.
#include "KCMCheckGlyph.h"         // KCMDrawCheckGlyph (the checkmark, shared with the CMYK cursor)
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMViewLookup.h"         // KCMQueryViewUnderMouse / KCMFindDocDbForView / KCMQueryMouseContentPoint
                                     // (★the third one joined when resolving the sampling point moved
                                     //  out of the sampler and over to this side)
#include "Utils.h"                   // Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"     // the armed state / ArmedDocsAlive, and the three CMYK sampling
                                     // calls ---- everything that crosses to the model side
#include "KCMCmykCursor.h"

// The mode Alt + left (the colour pick) fixes for the length of a press. It is decided at press time
// from "the document under the mouse", held, and dropped at RevealEnd ---- outside a press these are
// always nil / the default. ★The user asked for three cases (replacing a single sSoloCmykDB):
//   comparing, over the Target window … hover = Target / other = Source (two lines, "t" first)
//   comparing, over the Source window … hover = Source / other = Target (two lines, "s" first)
//   comparing, over a third document, or stopped … hover = that document / other = nil (one line)
// Dragging into another window mid-press does NOT switch the reference, so the two lines never trade
// places; while the pointer is off the pressed window the sampler's own identity guard rejects it and
// "no value ---" is shown. The pointers are for comparison only and are never dereferenced.
static IDataBase* sCmykHoverDB       = nil;		// the side under the mouse = the document on the first line
static IDataBase* sCmykOtherDB       = nil;		// the one it is compared with (nil = the lone-pick mode)
static bool16     sCmykHoverIsTarget = kFalse;	// whether hover is the Target (the newer): the direction of
												// the page pairing, and the t/s labels

//========================================================================================
// The CMYK of Alt + left is drawn on **the cursor itself**, on top of going to the panel's status
//   line.
//   A cursor is drawn by the OS, so it crosses the document window's edge and follows the mouse. How:
//   a "custom bitmap cursor" ---- a CursorSpec callback that draws into a buffer of our own through
//   AGM. ChangeModalCursor is available to KCM because it has a tracker, which is to say a tool of
//   its own. CreateCursorBitmapProc cannot be handed data through an argument, so the string to draw
//   sits in the file-static sCmykCursorText and the callback reads it from there.
//   ★It began as a spike to see whether anything appeared at all; the coordinate system (the y
//     direction), the alpha and the sizes were then tuned against the running application.
//========================================================================================
static PMString sCmykCursorText;			// "... t\n... s": two lines separated by LF, each labelled t/s at the
											// end, the first being the window under the mouse. Stored where a
											// colour sample succeeded.
static bool16   sCmykCursorPending = kFalse;	// whether the last BeginTracking should put a CMYK cursor up

// The throttle on live re-sampling during a drag (50ms ≒ twenty times a second). ★So that the first
// sample of every press always gets through, sCmykDragThrottleStarted is put back at RevealEnd and at
// Shutdown. It used to be a function-local static inside KCMTrackerUpdateCmykDrag, so once raised it
// stayed raised, and from the second press onwards the first sample of a drag could be caught by a
// throttle counted from the PREVIOUS press. (Nothing went blank - the value at press time comes from
// RevealBegin - but it broke this file's rule that no state is held outside a press.)
static std::chrono::steady_clock::time_point sCmykDragLastSample;
static bool16                                sCmykDragThrottleStarted = kFalse;

// The default font, held only for the length of an Alt + left drag (taken in RevealBegin's Alt
// branch, released at RevealEnd). It is a cache so that redrawing the cursor during a drag (twenty
// times a second at most) does not look the font up by name through IFontMgr every time.
// ★It is deliberately NOT a file-static InterfacePtr: a Release at static-destruction time happens
//   after the object model is gone, which is dangerous. A raw pointer with an explicit release at
//   RevealEnd (always nil outside a press) is used instead.
static IPMFont* sCmykCursorFont = nil;

// Split at LF (0x0A) into at most two lines.
static void KCMSplitTwoLines(const PMString& src, PMString& line1, PMString& line2)
{
	line1.Clear(); line1.SetTranslatable(kFalse);
	line2.Clear(); line2.SetTranslatable(kFalse);
	const int32 n = src.NumUTF16TextChars();
	const UTF16TextChar* buf = src.GrabUTF16Buffer(nil);
	bool16 second = kFalse;
	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] == 0x000A) { second = kTrue; continue; }
		if (!second) line1.AppendW(UTF32TextChar(buf[i]));
		else         line2.AppendW(UTF32TextChar(buf[i]));
	}
}

// Draw a space-separated line as a "table". The first four tokens (the C/M/Y/K heading, or three-digit
// values) go into fixed columns at x0 + col * pitch, and the fifth onwards (the t/s label) to the right
// of the fourth column, at x0 + 4 * pitch. Draw the heading row and the data rows with the same x0 and
// pitch and the CMYK letters stand above their digits **without measuring the font** ---- which is what
// the user asked for. The drawing goes through KCMShowHalo (a white rim with a black body).
static void KCMShowHalo(IGraphicsPort* gPort, const PMReal& x, const PMReal& y, const PMString& s);	// forward

static void KCMDrawColumns(IGraphicsPort* gPort, IPMFont* font, const PMReal& fs,
                             const PMReal& x0, const PMReal& pitch, const PMReal& y, const PMString& row)
{
	if (font == nil)
		return;
	// ★The font is selected here and once only. It used to be selected inside KCMShowHalo, per token,
	//   which came to as many as fifteen times a frame at twenty frames a second. It is the same font
	//   at the same size every time, so once is enough.
	gPort->selectfont(font, fs);

	PMString tok; tok.SetTranslatable(kFalse);
	int32 col = 0;
	const int32 n = row.NumUTF16TextChars();
	const UTF16TextChar* b = row.GrabUTF16Buffer(nil);
	for (int32 i = 0; i <= n; ++i)
	{
		if (i < n && b[i] != 0x0020)	// anything but a space builds up the current token
		{
			tok.AppendW(UTF32TextChar(b[i]));
			continue;
		}
		if (tok.NumUTF16TextChars() > 0)	// a separator (a space, or the end of the line) settles it
		{
			const int32 c = (col < 4) ? col : 4;	// the fifth onwards (the label) goes right of column four
			KCMShowHalo(gPort, x0 + pitch * PMReal(c), y, tok);
			++col;
			tok.Clear();
			tok.SetTranslatable(kFalse);
		}
	}
}

// Draw a string at (x,y); an empty one draws nothing.
// ★It assumes the caller (KCMDrawColumns) has already selectfont'd. It does not choose again.
static void KCMShowHalo(IGraphicsPort* gPort, const PMReal& x, const PMReal& y, const PMString& s)
{
	const int32 n = s.NumUTF16TextChars();
	if (n <= 0)
		return;
	const UTF16TextChar* b = s.GrabUTF16Buffer(nil);

	// The white rim first (drawn white, offset one pixel in each of eight directions), then the black
	// body. Readable over a transparent background whether what is behind it is light or dark.
	static const int kDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const int kDY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	const PMReal o(1.0);
	gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
	for (int i = 0; i < 8; ++i)
		gPort->show(x + PMReal(kDX[i]) * o, y + PMReal(kDY[i]) * o, (uint32)n, b);
	gPort->setrgbcolor(PMReal(0.0), PMReal(0.0), PMReal(0.0));
	gPort->show(x, y, (uint32)n, b);
}

// The checkmark that sits at the top of the CMYK cursor. Its colour is decided by the armed state and
// nothing else - the same rule the always-on tool cursor follows (KCMCursorProvider.cpp), settled by
// the user as "while Start is in force it is black over any document": armed = a black checkmark,
// stopped = an inverted one (a black rim with a white body ---- the same values KCMCheckGlyph.h gives
// the inactive cursor, and the same ones its PNGs are generated with).
// ★Over a third document while Start is in force the display is a lone line, as when stopped, but the
//   checkmark stays black ＝ "the comparison is running".
static void KCMDrawCmykCursorCheck(IGraphicsPort* gPort)
{
	if (Utils<IKCMCompareFacade>()->IsArmed())
		KCMDrawCheckGlyph(gPort);											// black (armed)
	else
		KCMDrawCheckGlyph(gPort, PMReal(1.0), PMReal(0.0), PMReal(5.0));	// inverted (stopped)
}

// The CursorSpec callback, called by the cursor machinery on the UI thread. bitmapBuffer has already
// been allocated by the caller at (the maximum cursor size)² × 4. *width / *height are the maximum
// size on the way in (twice that when hiRes) and the size actually used on the way out.
static void KCMCmykCursorBitmapProc(uchar* bitmapBuffer, uint32* width, uint32* height, bool16* hasAlpha, bool16 hiRes)
{
	// The preamble - clear the whole allocation to transparent, and answer the logical maximum size -
	// is shared with the checkmark cursor (KCMCheckGlyph.h).
	// The background is left transparent ＝ no black box, as the user asked.
	uint32 maxLogW = 0, maxLogH = 0;
	KCMCursorBitmapBegin(bitmapBuffer, *width, *height, hiRes, maxLogW, maxLogH);

	// Break the display string (two rows of numbers, each labelled t/s at the end) apart and settle on
	// "the largest font that still fits the width" from the longer of the two ---- the user asked for the
	// letters and numbers to be as large as the maximum cursor size allows.
	PMString line1, line2;
	KCMSplitTwoLines(sCmykCursorText, line1, line2);
	const int32 chars1 = line1.NumUTF16TextChars();
	const int32 chars2 = line2.NumUTF16TextChars();

	// ★The empty-string guard. Called with an empty string, maxChars below becomes 1 and fs leaps to
	//   (maxLogW - 8) * 100 / 58 = 100 to 200pt, painting a giant "C M Y K" across the whole cursor ----
	//   which looks exactly like rubbish. The ordinary route cannot do it (InstallCmykCursor runs only
	//   where a value was sampled), but the proc can be called again when the cursor cache is rebuilt or
	//   the DPI changes, so the guard is there.
	//   ★It must NOT return without setting *width / *height / *hasAlpha: unset, they are taken as the
	//     maximum size in 24-bit RGB and produce real rubbish. It falls back to the 24x24 checkmark-only
	//     cursor, the same one the tool wears all the time.
	if (chars1 <= 0 && chars2 <= 0)
	{
		InterfacePtr<IGraphicsPort> gPortCheckOnly(KCMCursorBitmapFinish(
			bitmapBuffer, width, height, hasAlpha, hiRes, 24u, 24u, maxLogW, maxLogH));
		if (gPortCheckOnly == nil)
			return;
		gPortCheckOnly->setopacity(PMReal(1.0), kFalse);
		KCMDrawCmykCursorCheck(gPortCheckOnly);
		return;
	}

	int32 maxChars = (chars2 > chars1) ? chars2 : chars1;
	if (maxChars < 1) maxChars = 1;

	// The font is sized generously from the width available (a character ≒ 0.58em; never below 7pt).
	// ★There is deliberately NO upper cap: raising one from 18 to 26 to 48pt changed nothing in the
	// running application, because the value derived from maxLogW (the maximum logical cursor size,
	// which the OS and the cursor manager decide) is already the real ceiling.
	int32 fs = ((int32)maxLogW - 8) * 100 / (maxChars * 58);
	if (fs < 7)  fs = 7;

	// ★The bitmap's width is fitted tightly to the real width of the content. Taking the full maximum
	// width leaves a broad transparent margin on the right, and the first frame of it can be seen to
	// flicker (rubbish again). The content width = 6 on the left + four columns of pitch (2.1em) + the
	// label (t/s, one character ≒ 0.58em) + 4 on the right ≒ 10 + 8.98em. ★Keep it equal to the
	// pitch = 2.1 × fs used in the drawing below. (It went from 10.14em to 8.98em when the labels were
	// shortened from tgt/src to t/s ＝ the transparent margin on the right went with them.)
	const int32 contentW = 10 + (fs * 898) / 100;	// always positive: fs >= 7 is guaranteed above
	uint32 logW = (uint32)contentW;					// KCMCursorBitmapFinish does the clamping

	// Under the checkmark (which reaches to about y = 18) go the "C M Y K" heading and two rows of data.
	// Their positions and the height all come from fs.
	const int32 gap    = (fs * 130) / 100;	// the line spacing ≒ 1.3em
	const int32 yHdr   = 22 + fs;			// the heading's baseline, below the checkmark (the whole block sits
											// a little lower than it first did, at the user's request)
	const int32 yData1 = yHdr + gap;		// the Target row
	const int32 yData2 = yData1 + gap;		// the Source row
	// The bottom row has no descenders, so the bitmap ends just below its baseline. Leave a transparent
	// margin down there and the first frame flickers in it (reported as a flash about three pixels below
	// the text). +2 covers the halo (y+1) and the antialiasing.
	// ★A lone pick (line2 empty) ends after the Target row: always measuring from yData2 left a
	//   transparent band the width of an unused Source row, which contradicted the tight-margin rule
	//   above.
	int32 needH = ((chars2 > 0) ? yData2 : yData1) + 2;
	uint32 logH = (needH > 0) ? (uint32)needH : 60u;

	// Settle the size (clamping included) and obtain the AGM port - the closing half shared with the
	// checkmark cursor (KCMCheckGlyph.h).
	InterfacePtr<IGraphicsPort> gPort(KCMCursorBitmapFinish(
		bitmapBuffer, width, height, hasAlpha, hiRes, logW, logH, maxLogW, maxLogH));
	if (gPort == nil)
		return;

	// The background is transparent (cleared to ARGB = 0 in full above). The setopacity is what makes
	// the strokes and text that follow opaque.
	gPort->setopacity(PMReal(1.0), kFalse);
	/* There is deliberately no background fill: it stays transparent, so no black box. */

	// The same checkmark the tool wears, drawn through the shared KCMDrawCheckGlyph so that its bend -
	// the hotspot (10,18) - is the click point (the same shape and coordinates as KCMCursorProvider.cpp).
	// The cursor keeps its shape while the numbers are up, which is what the user asked for.
	// ★It was once believed that "drawing the checkmark with stroke causes the flicker on the first
	// frame", and it had been backed off to rectfill dots. The real cause turned out to be **the
	// multi-stage cursor switching in BeginTracking**, plainly visible to the OS's hardware cursor
	// compositing (the cure is in KCMTracker.cpp's BeginTracking: the sampling was moved ahead of the
	// switch). stroke was innocent, so the checkmark came back. Which colour it takes is decided in one
	// place, KCMDrawCmykCursorCheck, shared with the empty-string guard above.
	KCMDrawCmykCursorCheck(gPort);

	// From the top: the "C M Y K" heading (aligned to the head of each column), the Target numbers, the
	// Source numbers. Every value is three digits so the columns line up, with t/s at the end. The font
	// size and the row positions were computed above; the drawing goes through KCMShowHalo.
	// The font is the press-time cache (sCmykCursorFont, which is what keeps the redraws of a drag from
	// looking it up by name), with a local fallback should it somehow be nil.
	IPMFont* font = sCmykCursorFont;
	InterfacePtr<IPMFont> fallbackFont;
	if (font == nil)
	{
		InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
		if (fontMgr != nil)
			fallbackFont = InterfacePtr<IPMFont>(fontMgr->QueryFont(fontMgr->GetDefaultFontName()));
		font = fallbackFont;
	}
	if (font != nil)
	{
		// The heading and the two data rows are drawn on the same fixed columns (x0, pitch) so the digits
		// line up vertically (pitch = three digits plus a gap) and each of C/M/Y/K stands directly above
		// its own column ---- the alignment the user asked for.
		const PMReal x0(6.0);
		const PMReal pitch = PMReal(fs) * PMReal(2.1);
		PMString hdr; hdr.SetTranslatable(kFalse); hdr.Append("C M Y K");
		KCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yHdr),   hdr);	// the heading
		KCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData1), line1);	// row 1 = the window under the mouse (t or s)
		KCMDrawColumns(gPort, font, PMReal(fs), x0, pitch, PMReal(yData2), line2);	// row 2 = the one compared with (empty in the lone
																						// mode, and skipped by itself)
	}
}

// The way in from KCMTracker.cpp. Where BeginTracking's CMYK branch succeeded, Pending is raised and
// the tracker calls ChangeModalCursor(CursorSpec(KCMTrackerCmykCursorProc(), ...)).
bool16 KCMTrackerHasPendingCmykCursor()          { return sCmykCursorPending; }
CreateCursorBitmapProc KCMTrackerCmykCursorProc() { return &KCMCmykCursorBitmapProc; }

// Black or inverted, for the tool's always-on checkmark cursor (see KCMCmykCursor.h).
// ★By the user's decision, black is decided by "Start is in force (the compared documents are alive)"
//   and nothing else. Which document is under the mouse is not looked at: while Start is in force,
//   Alt + left produces a value over any window (two lines over the Target or Source, one over a third
//   document), so the older rule of "black over the Target window only" had stopped matching what the
//   feature does. While stopped it is the inverted checkmark as before (a black rim, a white body).
//   With viewUnderMouse nil ---- not over a layout view at all ---- it stays inverted, which is the
//   cursor's default side.
// ★The viewUnderMouse argument is used ONLY to decide "are we over a layout view"; whose view it is
//   is never asked. Since the colour became document-independent, what the view holds cannot affect
//   it.
bool16 KCMToolCursorShouldBeBlack(IControlView* viewUnderMouse)
{
	if (viewUnderMouse == nil)
		return kFalse;
	return Utils<IKCMCompareFacade>()->ArmedDocsAlive();
}

// KCMTrackerUpdateCmykDrag (see KCMCmykCursor.h) - the live CMYK update during a drag.
// Called from the tracker's ContinueTracking (the mouse moved). It samples the CMYK again at the
// current mouse position and, where the value changed, updates sCmykCursorText and answers kTrue so
// that the caller redraws the cursor. A time throttle (50ms ≒ twenty times a second) keeps continuous
// rasterization from becoming expensive.
// Forward declarations; the definitions are just before KCMCmykBeginPress. They build the "no value"
// display for points off the page.
// hoverIsTarget = whether the first line (the window under the mouse) is labelled t or s.
static void KCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget);			// comparing: for the cursor (t/s)
static void KCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget);	// comparing: for the panel (letters + t/s)
static void KCMBuildCmykNoValueSolo(PMString& out);							// lone pick: one cursor line, unlabelled
static void KCMBuildCmykNoValuePanelSolo(PMString& out);						// lone pick: one panel line, unlabelled

// Are the CMYK documents fixed at press time (sCmykHoverDB / sCmykOtherDB) still open? The last line
// of defence against handing a released IDataBase to the sampler, should a document be closed mid-drag
// by one of the rare routes that can do it.
//   comparing … hover/other ARE the armed Target/Source, so the armed check is what answers
//               (KCMArmedDocsAlive goes as far as a Stop-equivalent clean-up when it fails).
//   lone pick … the one document under the mouse is looked for in the document list, nothing more (a
//               third document, or nothing armed at all, so the armed state has no bearing).
static bool16 KCMCmykDocsAlive()
{
	if (sCmykHoverDB == nil)
		return kFalse;
	if (sCmykOtherDB != nil)
		return Utils<IKCMCompareFacade>()->ArmedDocsAlive();
	ISession* session = GetExecutionContextSession();	// can be nil during shutdown
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return (docList != nil && docList->FindDocByDataBase(sCmykHoverDB) != nil) ? kTrue : kFalse;
}

bool16 KCMTrackerUpdateCmykDrag()
{
	if (!sCmykCursorPending)	// nothing to do outside the Alt + left CMYK mode
		return kFalse;

	// The mode fixed at press time (hover/other) is used as it is: the reference window never switches
	// mid-press.
	if (sCmykHoverDB == nil)
		return kFalse;
	const bool16 solo = (sCmykOtherDB == nil);

	// The 50ms throttle. steady_clock only moves forward, so there is no wrap to worry about. ★The
	// first sample of every press always gets through: the flag is put back at RevealEnd and Shutdown,
	// so it never carries from one press into the next.
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (sCmykDragThrottleStarted)
	{
		const long long ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - sCmykDragLastSample).count();
		if (ms < 50)
			return kFalse;
	}
	sCmykDragThrottleStarted = kTrue;
	sCmykDragLastSample = now;

	// Past the throttle (twenty times a second at most), check the documents are alive before handing
	// anything to the sampler. Closing a document mid-drag takes a rare route such as a script; the
	// check lives in KCMCmykDocsAlive and nowhere else.
	if (!KCMCmykDocsAlive())
		return kFalse;

	// Sample at the current mouse position. In the lone mode other is nil, so only hover is sampled (one
	// line). Off the page, off the window that was pressed, or a failure to read: show "no value (--- …)"
	// so that it is plain nothing was picked up. ★The previous value is deliberately NOT left standing,
	// because it would be read as the value here.
	//
	// ★★Re-reading the mouse position, and testing "have we left the window that was pressed", **used
	//   to sit inside the sampler on the model side**. They are questions put to a window, so this side
	//   took them back.
	//   ⚠**Drop those two and the coordinates of a DIFFERENT window are read as page coordinates of
	//   sCmykHoverDB.** The three conditions short-circuit through &&, in the same order and to the same
	//   effect as the three consecutive returns they replaced: any one of them failing means "no value".
	PMString panelMsg, cursorMsg;
	InterfacePtr<IControlView> viewUnderMouse(KCMQueryViewUnderMouse());
	PMReal mx = 0.0, my = 0.0;
	if (KCMFindDocDbForView(viewUnderMouse) != sCmykHoverDB ||
	    !KCMQueryMouseContentPoint(viewUnderMouse, mx, my) ||
	    // ★The spread on display is passed as well ---- without it, the colour of an ordinary page is
	    //   read while a master is being shown (their coordinates coincide).
	    !Utils<IKCMCompareFacade>()->SampleColorAt(sCmykHoverDB, sCmykOtherDB, sCmykHoverIsTarget,
	                                                 mx, my, KCMQuerySpreadUIDForView(viewUnderMouse),
	                                                 panelMsg, cursorMsg))
	{
		if (solo) { KCMBuildCmykNoValueSolo(cursorMsg); KCMBuildCmykNoValuePanelSolo(panelMsg); }
		else      { KCMBuildCmykNoValue(cursorMsg, sCmykHoverIsTarget);
		            KCMBuildCmykNoValuePanel(panelMsg, sCmykHoverIsTarget); }
	}
	if (cursorMsg == sCmykCursorText)	// the same value: nothing to redraw, and the panel says it already
		return kFalse;

	// The panel's status line follows the drag too. It is never forced into view - the same rule
	// KCMCmykBeginPress follows.
	KCMSetStatus(panelMsg);
	sCmykCursorText = cursorMsg;
	return kTrue;
}

// The "no value" display shown where no CMYK could be picked up, off the page for instance
// ("--- --- --- --- t/s"). The dashes are there so that "no colour was picked up here" can be seen at a
// glance. The labels are the usual t/s, and the first line is the hover side just as it is on success
// (t over the Target window, s over the Source).
static void KCMBuildCmykNoValue(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "--- --- --- --- t" : "--- --- --- --- s");	// t/s, the same short labels KCMColorSampler.cpp uses
	out.AppendW(UTF32TextChar(0x0A));	// the newline that starts the second line
	out.Append(hoverIsTarget ? "--- --- --- --- s" : "--- --- --- --- t");
}

// The panel's "no value" display: each value carries its own heading letter, then t/s. It matches what
// the panel shows when KCMSampleCmykAt succeeds (KCMAppendCmykLabeled in KCMColorSampler.cpp).
static void KCMBuildCmykNoValuePanel(PMString& out, bool16 hoverIsTarget)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- t" : "C--- M--- Y--- K--- s");
	out.AppendW(UTF32TextChar(0x0A));
	out.Append(hoverIsTarget ? "C--- M--- Y--- K--- s" : "C--- M--- Y--- K--- t");
}

// The one-line "no value" for a lone pick (stopped, or a third document while Start is in force). No
// t/s label, because there is only one document. On the cursor side KCMSplitTwoLines skips an empty
// second line by itself, so handing it one line breaks nothing.
static void KCMBuildCmykNoValueSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("--- --- --- ---");
}
static void KCMBuildCmykNoValuePanelSolo(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);
	out.Append("C--- M--- Y--- K---");
}

// KCMCmykClearPending (see KCMCmykCursor.h) - the default for "does this press put a CMYK cursor up"
// (it does not).
// ★It was cut out of the first line of KCMTrackerRevealBegin. That line runs **unconditionally, before
//   the gesture is classified**, which is why it needs a way in of its own, separate from
//   KCMCmykBeginPress below (which runs only in the Alt branch).
void KCMCmykClearPending()
{
	sCmykCursorPending = kFalse;	// only the CMYK branch decides otherwise
}

// KCMCmykBeginPress (see KCMCmykCursor.h) - the body of an Alt + left (CMYK) press.
// ★Cut out of KCMTrackerRevealBegin's CMYK branch, line for line. The reason for cutting it out was to
//   stop anything outside this file touching the press-time state. There is exactly one caller,
//   KCMTrackerRevealBegin in KCMPeekGesture.cpp.
void KCMCmykBeginPress()
{
	// Alt + left alone (no Shift, no Ctrl): sample the raw CMYK (0..255) at the click point and draw it
	// on the cursor itself.
	// ★What it takes to fire is only "there is a layout view, and a document, under the mouse" (widened
	//   at the user's request; it used to be the Target window alone while Start was in force). The
	//   window that was pressed decides between three cases:
	//   comparing, the Target window  … new against old (two lines: Target "t", then Source "s")
	//   comparing, the Source window  … the same comparison the other way up (Source "s", then Target "t")
	//   comparing, a third document, or stopped … a lone pick of that one document (one line, no label)
	// ★This block runs BEFORE the base CTracker::BeginTracking (see KCMTracker.cpp): the expensive
	//   sampling is finished ahead of the cursor switching so that the switch itself is instantaneous.
	InterfacePtr<IControlView> viewUnderMouse(KCMQueryViewUnderMouse());
	IDataBase* const hoverDB = KCMFindDocDbForView(viewUnderMouse);
	if (hoverDB != nil)
	{
		// The press-time font cache that keeps every cursor redraw (twenty a second at most) from looking
		// the font up by name through IFontMgr. It is released at RevealEnd.
		if (sCmykCursorFont == nil)
		{
			InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), UseDefaultIID());
			sCmykCursorFont = (fontMgr != nil) ? fontMgr->QueryFont(fontMgr->GetDefaultFontName()) : nil;
		}

		// Classify the pressed window's document as the Target, the Source, or neither, and fix the mode
		// (released at RevealEnd).
		// The page-pairing cache is prepared only in the comparing mode, so that the whole pairing is not
		// rebuilt on every sample; its direction is fixed at press time too, and it is dropped at
		// RevealEnd. The lone mode has no page pairing and needs none.
		IDataBase* otherDB      = nil;
		bool16     hoverIsTarget = kFalse;
		// ★At run time this asks the facade **as many as six times** (when the pressed window is the
		//   Source: ArmedDocsAlive, then GetArmedTargetDB / GetArmedSourceDB / GetArmedTargetDB, then
		//   BeginColorDrag, then SampleColorAt) ---- which is why it is held in an InterfacePtr. Utils.h
		//   says as much: get the interface once and keep it where you call it repeatedly.
		//   ⚠**`compare->` appears SEVEN times in the source**, and the two numbers differ because only
		//     one side of the if / else if ever runs. It is spelt out because the seven was once about to
		//     be written down as the count.
		InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
		if (compare->ArmedDocsAlive())	// comparing? The liveness check comes first so that no released db is compared against
		{
			if (hoverDB == compare->GetArmedTargetDB())      { otherDB = compare->GetArmedSourceDB(); hoverIsTarget = kTrue;  }
			else if (hoverDB == compare->GetArmedSourceDB()) { otherDB = compare->GetArmedTargetDB(); hoverIsTarget = kFalse; }
		}
		sCmykHoverDB       = hoverDB;
		sCmykOtherDB       = otherDB;
		sCmykHoverIsTarget = hoverIsTarget;

		const bool16 solo = (otherDB == nil);
		if (!solo)
			compare->BeginColorDrag(hoverDB, otherDB, hoverIsTarget);

		// ★The sampling point is read here (it used to be read inside the sampler).
		//   No "have we left the pressed window" test is needed at this point ---- hoverDB was just now
		//   derived from viewUnderMouse, so by definition they agree. Mid-drag the mouse moves, which is
		//   why KCMTrackerUpdateCmykDrag does need one.
		PMString panelMsg, cursorMsg;
		PMReal mx = 0.0, my = 0.0;
		// ★The spread on display is passed as well: a master page and an ordinary one share coordinates.
		if (!KCMQueryMouseContentPoint(viewUnderMouse, mx, my) ||
		    !compare->SampleColorAt(hoverDB, otherDB, hoverIsTarget, mx, my,
		                            KCMQuerySpreadUIDForView(viewUnderMouse), panelMsg, cursorMsg))
		{
			// Off the page and the like: show that nothing was picked up (the "---" display).
			if (solo) { KCMBuildCmykNoValueSolo(cursorMsg); KCMBuildCmykNoValuePanelSolo(panelMsg); }
			else      { KCMBuildCmykNoValue(cursorMsg, hoverIsTarget);
			            KCMBuildCmykNoValuePanel(panelMsg, hoverIsTarget); }
		}
		// Besides drawing the CMYK on the cursor itself (the tracker's ChangeModalCursor), the same value
		// goes to the panel's status line. ★KCMSetStatus never forces a hidden panel into view: shown, it
		// is seen; hidden, the state is remembered and nothing else happens. The panel is never made to
		// open, by the user's instruction.
		KCMSetStatus(panelMsg);
		sCmykCursorText    = cursorMsg;
		sCmykCursorPending = kTrue;
	}
}

// KCMCmykEndPress (see KCMCmykCursor.h) - the clean-up when the button is released.
// ★Cut out of KCMTrackerRevealEnd's CMYK part, line for line.
void KCMCmykEndPress()
{
	// Return and drop what the press was holding (taken in KCMCmykBeginPress; nothing is held outside a
	// press).
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	Utils<IKCMCompareFacade>()->EndColorDrag();
	// Let go of the CMYK mode (hover/other) that was fixed for this press.
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;
	sCmykDragThrottleStarted = kFalse;	// so the next press lets its first drag sample through

	// On release, clear the CMYK value the press had put on the panel's status line ---- the user asked
	// for the message to go when the hold ends. sCmykCursorPending is raised only where a press really
	// showed a CMYK value, so the clearing touches the colour compare alone and leaves the status of
	// reveal / peek / Check / Register and everything else where it is.
	// ★It is cleared with **a single space, not an empty string** (the user's instruction). Truly empty
	//   cannot be told from "never touched": the remembered status line lives on the model side, and the
	//   panel's AutoAttach shows the first-run hint whenever every saved piece of it is empty. One space
	//   looks the same as empty while still counting as touched ---- reopening the panel restores a
	//   space, and no hint appears.
	if (sCmykCursorPending)
	{
		PMString blank(" ");
		blank.SetTranslatable(kFalse);
		KCMSetStatus(blank);
		sCmykCursorText.Clear();
		sCmykCursorPending = kFalse;
	}
}

// KCMCmykShutdown (see KCMCmykCursor.h) - the clean-up at shutdown.
// ★Cut out of the CMYK part of KCMPeekStartup::Shutdown, line for line.
void KCMCmykShutdown()
{
	// ★Emptying the file-static PMString makes its static destructor a no-op when the plug-in is
	// unloaded. Nothing has ever come of it on Windows, but the unload order differs on Mac and not
	// carrying a heap buffer that far is the safer thing.
	sCmykCursorText.Clear();
	sCmykCursorPending = kFalse;	// ★do not leave it raised where the application quit mid-press ---- the
									//   same "nothing is held outside a press" as sCmykCursorFont and
									//   sCmykHoverDB below

	// ★Where the application ends during an Alt + left hold (a scripted quit, say) RevealEnd is never
	//   reached and sCmykCursorFont is left alive, so it is released here. On the ordinary route it is
	//   always nil outside a press and this does nothing.
	if (sCmykCursorFont != nil)
	{
		sCmykCursorFont->Release();
		sCmykCursorFont = nil;
	}
	// The same route can leave the press-time document pointers behind. They are only ever compared,
	// never dereferenced, but a released pointer is not carried past shutdown either.
	sCmykHoverDB       = nil;
	sCmykOtherDB       = nil;
	sCmykHoverIsTarget = kFalse;
	sCmykDragThrottleStarted = kFalse;	// nor is the drag throttle's flag left standing

	// The press-time page-pairing cache (hover -> other) is dropped the same way. It goes through the
	// facade rather than the free function it used to call.
	// ⚠**This is the one place that checks for nil**: during shutdown the utils boss may already be
	//   gone, and the Utils<>()->M() form would dereference nil. KCMCmykEndPress above is a button
	//   release ＝ ordinary running time, so it calls plainly.
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	if (compare != nil)
		compare->EndColorDrag();
}
