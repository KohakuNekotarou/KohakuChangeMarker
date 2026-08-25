//========================================================================================
//
//  $File: $
//
//  Owner:
//
//  $Author: $
//
//  $DateTime: $
//
//  $Revision: $
//
//  $Change: $
//
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or
//  distribution of it requires the prior written permission of Adobe.
//
//========================================================================================
//
// Model-side implementations only. The UI half keeps its own list in ui/KCMUIFactoryList.h.
//
// A missing entry fails in complete silence: writing CREATE_PMINTERFACE is not enough, and an
// implementation that is not listed here is simply never created. The build succeeds, the
// plug-in loads, the menu item appears, and that one feature quietly does nothing.
// So never extend this list from memory. Grep for BOTH CREATE_PMINTERFACE and
// CREATE_PERSIST_PMINTERFACE and match the results against this list one for one - grepping
// only the plain form has already come close to dropping three implementations.
//
// KCMDrawEventHandler is deliberately absent: it no longer derives from IDrwEvtHandler and is
// not an implementation at all, only the home of DrawSpreadMarks and the shared static state.
// Everything on screen and on paper is drawn through the adornment below.
REGISTER_PMINTERFACE(KCMRingAdornmentShape, kKCMRingAdornmentImpl)	// The one path that draws the comparison marks (KCMRingAdornment.cpp). Holds no drawing code itself; calls KCMDrawEventHandler::DrawSpreadMarks.
REGISTER_PMINTERFACE(KCMRingFlattenerUsage, kKCMRingFlattenerUsageImpl)	// The declaration half of the same adornment - it tells the transparency manager that the adornment uses transparency. Without it the ring comes out as a solid block in PDF 1.3.
REGISTER_PMINTERFACE(KCMRingAdornmentStartup, kKCMRingAdornmentStartupImpl)	// Registers the two above with the session, once per execution context, background threads included. Page item adornments have no service mechanism of their own, so this does by hand what the text adornment gets for free.
REGISTER_PMINTERFACE(KCMPeekStartup, kKCMPeekStartupImpl)	// Model-side startup/shutdown (KCMPeek.cpp). Its UI counterpart is KCMUIStartup.
REGISTER_PMINTERFACE(KCMDocResponder, kKCMDocResponderImpl)	// Clears the tracking state once a document is really closed. The ServiceProvider side names an Adobe-supplied implementation in the .fr.
REGISTER_PMINTERFACE(KCMBeforeSaveDocResponder, kKCMBeforeSaveResponderImpl)	// Restores the spreads hidden by Hide Unchanged before the document is saved. ServiceProvider likewise Adobe-supplied.
REGISTER_PMINTERFACE(KCMPDFExportSetup, kKCMPDFExportSetupImpl)	// Adds the adornment to the transparency list in BeginExport and takes it off again in EndExport (KCMRingAdornment.cpp). An asynchronous export is handed a cloned db, so the original document is never touched. There is no print-side counterpart: it would work and would make the marks denser, but it was dropped on the grounds that print does not need that precision - see section 5 of KCMRingAdornment.cpp for the A/B and how to bring it back.
REGISTER_PMINTERFACE(KCMScriptProvider, kKCMScriptProviderImpl)	// Serves every published property on its own - app.kcmStatus / app.kcmBookResult plus the four story change counters. All read-only, no methods. A ScriptProvider is not UI, which is why it is on this side.
REGISTER_PMINTERFACE(KCMCompareFacade, kKCMCompareFacadeImpl)	// What the UI asks of the comparison engine (AddIn on kUtilsBoss, KCMFacades.cpp).
REGISTER_PMINTERFACE(KCMMarkData, kKCMMarkDataImpl)	// How the UI reads comparison results (same AddIn; read-only).
REGISTER_PMINTERFACE(KCMPageFlagsFacade, kKCMPageFlagsFacadeImpl)	// How the UI sets the Register / Check page flags (same AddIn).
REGISTER_PMINTERFACE(KCMStoryEditsFacade, kKCMStoryEditsFacadeImpl)	// How the UI reads the Story Edits list (same AddIn; read-only apart from RefreshRow).
REGISTER_PMINTERFACE(KCMBookFacade, kKCMBookFacadeImpl)	// How the UI asks for a book comparison (same AddIn).
REGISTER_PMINTERFACE(KCMStoryMarkFacade, kKCMStoryMarkFacadeImpl)	// How the UI shows and hides the Story-mode marks (same AddIn).
REGISTER_PMINTERFACE(KCMStoryMarkerAdornment, kKCMStoryMarkerAdornmentImpl)	// Global text adornment that lays a colored ground under changed characters (KCMStoryMarker.cpp). It lives on the model side because File > Export > PDF runs on a background thread, which is never handed to a kUIPlugIn - marks that have to reach paper and PDF cannot live in the UI half.
REGISTER_PMINTERFACE(KCMStoryMarkerExpiryTask, kKCMStoryMarkerExpiryImpl)	// IIdleTask that withdraws the jump flash after about a second (KCMStoryMarkerExpiry.cpp). It is on this side because the marker starts and stops it; leaving it in the UI would invert the dependency.
