//========================================================================================
//
//  KCMRingAdornment.h
//
//  Draws the comparison marks (rings, slashes, ticks, old-folio badges) as a **global page
//  item adornment**. It carries no drawing code of its own: given a spread it calls
//  KCMDrawEventHandler::DrawSpreadMarks(), so **the picture is the single implementation**
//  that the removed draw-event route also called. What this route adds is who does the
//  calling, and the transparency declaration.
//
//  WHY IT EXISTS -- THE SOLID BLOCK AT PDF 1.3
//  --------------------------------------------------------------------------------------
//  A translucent ring is drawn as a grey alpha server plus a solid vector fill
//  (KCMDrawRingForPrint). At PDF 1.4 that comes out translucent. At **PDF 1.3 it comes out
//  translucent only on pages that already contain transparency**: 1.3 has no transparency of
//  its own, so the only thing that can produce it is the flattener, and the flattener touches
//  a page only when something on that page needs flattening. Where it does not run, the alpha
//  server's mask is never resolved and **the ring becomes a solid block**.
//  @warning the print-submission presets used in Japan are nearly all 1.3 (PDF/X-1a, X-3, the
//  magazine ad delivery preset -- all Acrobat 4), so this is the ordinary case, not the rare one.
//
//  Fixing it means making the transparency manager see that the page has transparency. The
//  interface that declares that is **IAdornmentFlattenerUsage** (`IsFlattenerRequired_`), and
//  **it is only ever asked on an adornment boss**: of the six bosses in the running
//  application that carry IID_IADORNMENTFLATTENERUSAGE, all six are adornments. A draw event
//  handler sits on a service boss, so it is not one of the parties that gets asked. The reason
//  is mechanical -- the flattener gathers artwork and then rasterises it, so the only things it
//  can ask are the gathered objects, while a draw event happens during the drawing that follows.
//  (Record: memory print-drawing-alpha-flattener,
//  docs/ai-notes/kescm-pdf-transparency-2026-08-16.md.)
//
//  TWO CONDITIONS, NOT ONE
//  --------------------------------------------------------------------------------------
//   (1) **Have something to declare with** -- IAdornmentFlattenerUsage above. That is the
//       whole reason the marks are drawn from an adornment at all.
//   (2) **Be asked** -- the flattener runs only when the document has transparency, and that
//       is decided solely by `IXPManager`'s list of page items that carry transparency.
//       **A session-global adornment belongs to no item, so it can never join that list by
//       itself.** The .cpp puts a representative item on the list while an export runs.
//  @warning **(1) on its own leaves a whole class of documents broken** -- the ones with no
//  transparency anywhere, which is exactly what a print submission here normally is. Stopping
//  at "(1) went in and the test file was fixed" ships the bug to the readers who need it most.
//
//  THE DOCUMENT IS NOT CHANGED BY A SINGLE BYTE
//  --------------------------------------------------------------------------------------
//  The adornment is **not attached to any page item of the document**. It is added once to the
//  session's IID_IGLOBALPAGEITEMADORNMENTLIST, and CShape walks its own list and the global
//  list together on all four routes (CShape::DrawPageItemAdornments,
//  UnionPageItemAdornmentPaintedBBox, UnionPrintingPageItemAdornmentPaintedBBox and
//  InvalPageItemAdornments -- each makes the same call twice, the second time with the global list).
//  @warning `AddAdornment` on an item of the document **persists into the .indd** (its second
//  argument is "dirty the document", not "keep it out of the file"). Registering globally
//  avoids that entirely.
//  Pages and spreads are page items too (kSpreadBoss and kPageBoss both carry IID_ISHAPE and
//  IID_IPAGEITEMADORNMENTLIST), so **a page holding no text and no artwork is still drawn on**.
//
//  CONSTRAINTS
//  --------------------------------------------------------------------------------------
//   1. **This has to live in the model plug-in.** The UI's PDF export runs on a background
//      thread, where a `kUIPlugIn` boss is invisible (nil, with no warning).
//      @warning that is necessary and not sufficient: **the registration itself does not cross
//      threads**. An adornment added on the main thread is not on the session of a background
//      thread's execution context. Hence the registration is driven by a **startup service that
//      runs once per execution context** (kKCMRingAdornmentStartupBoss), which is what puts the
//      marks into the UI's (background) PDF export.
//   2. **On the session means on every document.** Which document the marks belong to is
//      decided where it always was, in DrawSpreadMarks (KCMIsSameDoc); nothing here filters.
//   3. **Closing a document does not remove the registration** -- Shutdown has to
//      (KCMRingAdornmentStartup::Shutdown, at the end of the .cpp).
//   4. **Nothing can be drawn outside the pasteboard** (a real clip). The marks are inside the
//      page, so it does not bite.
//
//========================================================================================
#ifndef __KCMRingAdornment_h__
#define __KCMRingAdornment_h__

#include "BaseType.h"		// bool16

class IDataBase;

// Adds the adornment to the session's global page item adornment list.
// **Call it once per execution context**, background threads included; the reasoning is at the
// definition.
// @warning **if this fails, not one mark is drawn.** The draw-event route that used to draw
// them when registration failed was removed and this is the only route left (the decision was
// "if the registration fails it is acceptable for no marks to appear").
void KCMRingAdornmentRegister();

// Takes it off again (application shutdown). @warning it lives on the session, so it stays
// there until something removes it.
void KCMRingAdornmentUnregister();

// **Joining and leaving IXPManager's item-has-transparency list is deliberately not exposed
// here.** It is done inside the .cpp, driven by the PDF export events, and by nothing else.
//
//   @warning **without it some documents still come out solid at PDF 1.3.** `IXPManager` keeps
//     a list of **page items** that have transparency and answers "does this document have
//     transparency" from it (IXPManager.h, "mainly for determining whether there's XP in the
//     document"), and **an adornment is not an item, so it never enters that list**. With an
//     empty list the export never starts the flattener, `IsFlattenerRequired_` is never asked,
//     the alpha server's mask is never resolved, and the ring's bounding box -- the page frame,
//     hence the whole page -- comes out solid.
//     Measured: adding one 50% rectangle to a document with no transparency fixed it, and
//     removing it broke it again (400,404 solid pixels against a translucent picture). So the
//     list itself is the problem, not a stale cache.
//
//   **The list is held only for the duration of an export.** The only caller is the PDF export
//     service at the end of the .cpp; starting, stopping and toggling a comparison never touch
//     it. The reason is that **the list persists into the .indd** (measured: it survives a save
//     and is not re-validated on reopen), so holding it for the length of a comparison means
//     that the moment the reader saves, **a record with nothing behind it is baked into their
//     document**. Details and IDs are with kKCMPDFExportSetupBoss in KCMID.h.
//   **Printing deliberately does not do it.** @warning not because it has no effect, but
//     because it is not wanted: it does make the printed marks denser (measured 16,076 against
//     8,407 coloured pixels, **and neither of them is solid**), and the judgement was that print
//     does not need that precision, since what goes to the printing company is the PDF. The A/B
//     conditions and how to bring it back are in KCMRingAdornment.cpp, section 5.
//   @warning **there is no API that tells the flattener to run** -- `IFlattenerSettings::
//     SetFlattenerEnabled` is the switch that "defeats flattener altogether". Joining the list
//     is the only way in.


// The value behind `document.kcmTransparencyItemCount`: how many items this document currently
// has on the item-has-transparency list (`IXPManager::GetNumItemsWithXP`). -1 if db is nil or
// the XPManager cannot be had.
// @warning **it is not "how many KCM put there"** -- real transparency (a drop shadow, an
// opacity below 100) puts items on the same list. To judge by it, compare against the same
// document that KCM has not been run on.
// Its purpose is to measure from outside whether the "only during an export" rule above is
// holding. The list persists into the .indd, so save -> close -> reopen -> read says directly
// whether anything was written.
int32 KCMGetNumItemsWithXP(IDataBase* db);

// **The insurance: put the item-has-transparency list back to what the document itself says.**
// Every item the list holds now is sent `ItemXPChanged(kXPC_RemovedSomeXP)` in one call, which
// is not "take it off" but "ask again" (IXPManager.h) -- an item with real transparency (a drop
// shadow, an opacity under 100) answers "still transparent" and stays; a declaration KCM left
// behind has nothing behind it and goes.
// **When it can matter**: the declarations above are made only during an export and only on
// the database the export hands over (a clone, for an asynchronous PDF -- measured 2026-09-05:
// the document's own list reads 0 before and after an export with the marks printing). So this
// guards the cases nobody has measured: a synchronous route handing over the real document and
// then never reaching EndExport (a crash mid-way), and documents that a development build
// before that rule baked a declaration into. The user's call (2026-09-05): keep the insurance.
// **Called at Start (both documents) and at Stop (the armed pair)**, on the main thread only,
// and **never while this thread is exporting** -- mid-export the declarations up are the right
// ones and re-asking would take them down. Wrapped in SaveRestoreModifiedState: it changes
// nothing the reader would want to save. nil, or a db without an XPManager, does nothing.
// @warning **not KCMSetItemXPState(db, kKCMXPListRemove)**: that one picks each spread's
//   representative item afresh (the first item on the page), and the item chosen when the
//   declaration went up need not be the one chosen now if items came and went in between.
//   Reading the list itself is what makes this exact.
void KCMRevalidateItemXPList(IDataBase* db);

#endif // __KCMRingAdornment_h__
