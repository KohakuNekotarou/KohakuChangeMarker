//========================================================================================
//
//  KCMUIShared.h
//
//  Shared UI-side API: the panel, its status line, its navigation readout, and the tool
//  button. Everything here touches widgets.
//
//  Created 2026-08-13 by splitting KCMCore.h. Behaviour unchanged.
//
//  ★INVARIANT: no model-side file may include this header. When a model-side file needs to
//  tell the UI something, it emits a notification instead (KCMModelNotify.h). A model
//  plug-in cannot instantiate UI bosses -- the query returns nil with no error and no
//  warning -- so this rule is what keeps the split working after Stage 2.
//
//  ★It holds. No file under source/ includes this header -- the three model-side files that
//  mention it by name say in so many words that they deliberately do not. The header was
//  created so the violations would be countable while they were being removed; what is left
//  of it now is the rule above.
//
//========================================================================================

#ifndef __KCMUIShared_h__
#define __KCMUIShared_h__

#include "BaseType.h"
#include "OMTypes.h"		// WidgetID
#include "PMString.h"

class IControlView;

// The plug-in's own panel if it is showing, nil if hidden. The
// "session -> app -> panelMgr -> GetVisiblePanel" idiom lives only here. Absorbs the
// shutdown path where session goes nil.
IControlView*	KCMGetVisibleOwnPanel();

// One widget of the showing panel, or nil -- panel hidden, or no such widget. The
// "panel -> IPanelControlData -> FindWidget" idiom lives here for the reason the one above it
// does: every caller of KCMGetVisibleOwnPanel went straight on to write it out.
// ⚠IT LOOKS THE PANEL UP EACH TIME, so the callers that want SEVERAL widgets, or the panel
//   itself, take the IPanelControlData their own way on purpose.
IControlView*	KCMFindPanelWidget(const WidgetID& id);

// Bring the showing panel's ON/OFF display (Target/Source names, icon, toggle label) in line
// with the current armed state. Does nothing when the panel is hidden -- AutoAttach reflects
// the real state on re-show.
void			KCMRefreshPanel();

// Write the panel's status line. forceRedrawNow=kTrue paints immediately instead of waiting
// for the next event loop (used before a blocking comparison loop).
//
// ★UI-internal. **The model side never calls this** -- it uses KCMNotifyStatus()
//   (KCMModelNotify.h). The callers here are KCMModelChangeObserver, which receives that
//   notification, and the UI’s own direct actions (menu, buttons, a row click).
// ★Either route also **stores the string on the model side** (this function calls
//   KCMStoreSessionStatus), which is where app.kcmStatus answers from -- so the property
//   still returns the right value with the panel closed.
void			KCMSetStatus(const PMString& s, bool16 forceRedrawNow = kFalse);

// The same, for a message written out where it is used.
// ★IT MARKS THE STRING UNTRANSLATABLE, which every one of these call sites did by hand: a
//   finished sentence left translatable turns into something else the moment it matches an entry
//   of the built-in table ("Source:" came out as a style-source phrase in a Japanese locale).
// ⚠A message that IS a key must not come through here. Translate it and hand over the PMString.
void			KCMSetStatus(const char* s, bool16 forceRedrawNow = kFalse);

// The same message area, written **with the colour boundaries marked**.
// ★`label` is the heading (it always takes a line of its own and is never trimmed away),
//   `mid` is **the changed characters** = the theme’s text colour, and `pre`/`post` are the
//   context on either side = faded toward the background.
// ★★KCMSetStatus above is not a special case of this one but **the same container filled
//   differently** = (empty, empty, s, empty, empty). An ordinary message is therefore one
//   piece in one colour, which draws exactly like the stock static text it replaced.
// ★Callers: the jump from a change row (KCMStoryJump.cpp), which shows the other side of that
//   edit, and the panel’s own restore when it is re-shown (KCMPanelObserver.cpp) -- the
//   pieces are kept on the model side, so a re-shown panel gets the colours back too.
// ⚠When it overflows, **the context is trimmed from the outside in** (the changed characters
//   always survive). Same rule as a change row’s cell.
// ★`ruby` is the reading **drawn over `mid`** (only when a ruby change was clicked).
//   ⚠It is the reading on the **old** side -- the list shows the new one, so a reading that
//     was taken away appears nowhere else.
//   ★A row with ruby "uses two lines to look like one", so the box holds one line fewer
//     (KCMStatusTextView.cpp).
void			KCMSetStatusSegments(const PMString& label, const PMString& pre,
									   const PMString& mid, const PMString& post,
									   const PMString& ruby, int32 attrKind);

// Start / stop the UI-side observer of the model's notifications (KCMModelChangeObserver.cpp).
// Attached in Startup; detached in Shutdown **before** the panel is taken down.
void			KCMAttachModelChangeObserver();
void			KCMDetachModelChangeObserver();

// The Prev/Next position readout ("3/12") and the enabled state of both buttons. Empty
// posText clears. Does nothing when the panel is hidden.
void			KCMSetNavPosition(const PMString& posText, bool16 navButtonsEnabled);

// Bring the panel's tool button into line with the toolbox: WHICH OF THE TWO TOOLS IT WEARS
// (the comparison tool or the stamp) and whether it looks pressed.
// ★★ONE BUTTON, TWO TOOLS (the user's design, 2026-09-04: "one place, two tools, the way the
//   toolbox does it"). Two widgets share one frame and exactly one is shown; this is the only
//   function that decides which, so the panel and the toolbox cannot disagree
//   ([[one-question-one-place]]).
// ★It takes no argument on purpose -- it READS the real state (KCMIsOwnToolActive /
//   KCMIsPawToolActive) rather than being told. Being told is what let the old
//   KCMSetToolButtonSelected(kTrue/kFalse) be called with a stale answer.
// ⚠With neither tool active it leaves the face alone: the button keeps the last tool used, which
//   is what a toolbox slot does with its flyout. Does nothing when the panel is hidden.
void			KCMSyncToolButton();

// How long the panel's tool button has to be held for the press to mean "the OTHER tool".
// ★0.4 s is InDesign's own feel for a press-and-hold on a toolbox slot; below about 0.25 an
//   ordinary click starts being taken for a hold.
// ⚠★★★SECONDS, NOT MILLISECONDS, and that was MEASURED rather than read. IEvent::GetTime is
//   documented as "a DWORD, so it'll roll over after ~47 days" (IEvent.h:144) -- which reads as
//   milliseconds, and 2^32 ms really is 49 days, so the wording confirms the wrong guess. It
//   answers SECONDS: a 900 ms press came back as 0.9059, printed through the panel's own status
//   line on 2026-09-04. Written as 400 the threshold would have been 400 SECONDS and the hold
//   could never once have fired -- which is exactly the symptom that led here.
static const PMReal kKCMToolButtonHoldSeconds = 0.4;

// One press of the panel's tool button, decided and carried out.
//   pawFace : the button was wearing the stamp tool's face (rather than the comparison tool's)
//   heldRaw : how long the button was down, in SECONDS as IEvent::GetTime counts them, or a
//             negative number when no button-down reached the handler at all. The handler
//             measures and this decides, so the threshold lives in one place.
// ★★THE RULE, in one place: a HOLD always goes to the other tool (the toolbox's press-and-hold,
//   which is what the user asked for); a CLICK chooses the tool on show. ⚠Clicking the tool that
//   is already active therefore does nothing, exactly as clicking an already-current toolbox slot
//   does nothing -- switching is what the hold is for.
void			KCMToolButtonPressed(bool16 pawFace, const PMReal& heldRaw);

// Make this plug-in's tool the active tool. Returns kTrue when it actually became active.
// Does nothing in a run configuration without a toolbox.
bool16			KCMActivateOwnTool();

// Is this plug-in's tool the active tool right now? Used to restore the button's pressed
// look when the panel is rebuilt, instead of writing a fixed default.
bool16			KCMIsOwnToolActive();

// The same pair for the cat-paw stamp tool, which shares the panel's tool button with the one
// above. Declared here rather than beside the stamp's own code for the reason the two above are:
// the panel is the caller, and the panel reads one header.
bool16			KCMActivatePawTool();
bool16			KCMIsPawToolActive();

// Open the distribution URL from "About this plug-in" in the default browser. Called when
// the panel illustration is clicked.
void			KCMOpenAboutURL();

#endif // __KCMUIShared_h__
