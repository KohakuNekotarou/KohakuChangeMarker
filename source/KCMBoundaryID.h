//========================================================================================
//
//  KCMBoundaryID.h
//
//  The IDs that model (KohakuExtendScriptChangeMarker) and UI (KohakuChangeMarkerUI) must both
//  know **by the same value** -- and nothing else.
//
//  **THIS FILE EXISTS TWICE, WITH IDENTICAL CONTENT**, at
//  source/sdksamples/KCM/{source|ui}/KCMBoundaryID.h.
//  **Editing only one of them breaks the product silently. Always edit both.**
//  (Adobe ships the same shape: customconditionaltext/CusCondTxtRezDefs.h and the file of the
//  same name in customconditionaltextui, byte for byte identical.)
//
//  ★SINCE 2026-09-02 A THIRD PLUG-IN READS THE MODEL COPY: Kohaku InDesign MCP includes
//  IKCMCompareFacade.h - and through it this file - straight from source/sdksamples/KCM/source.
//  There is no third copy to keep in step, but the values here are now promised to a product
//  that ships on its own schedule: renumbering one breaks it as silently as it would break the
//  UI. (The vtable rule that goes with this is at the top of IKCMCompareFacade.h.)
//
//  WHY A COPY EACH SIDE, AND WHY THE SAME VALUE. An ID is unique by its VALUE, not per plug-in.
//  What lives here is the three kinds of thing one side writes and the other reads, and each of
//  them **stops working silently the moment the two values disagree** (the build still passes):
//
//    - a facade's IID   ... the model AddIns it to kUtilsBoss, the UI asks for Utils<IKCMxxx>()
//    - a protocol IID   ... the model passes it to ISubject::Change, the UI to AttachObserver
//    - a notification's MessageID ... the model sends it, the UI switches on it in Update()
//
//  So **both copies keep the model's prefix, 0x1EA500**. If the UI copy renamed them into its own
//  0x1EA580 the UI would stop receiving what the model sends.
//
//  What may live here: boundary IDs, and **types both sides must know by the same definition**
//  (KCMCompareMode below is one: not an ID, but a value the UI writes and the model reads and acts
//  on, so a definition on one side only would make the other side include its partner's header).
//  **Model-only IDs belong in KCMID.h, UI-only IDs in KCMUIID.h.**
//
//========================================================================================

#ifndef __KCMBoundaryID_h__
#define __KCMBoundaryID_h__

#include "SDKDef.h"

//----------------------------------------------------------------------------------------
// The DISPLAY STRINGS both sides must spell the same way. Not IDs, but if they disagree the
// product looks like two products ([[one-question-one-place]]: a version number and a display
// name are one fact about one product, and holding them separately guarantees they drift).
//
// Users: both .rc files (FileDescription / FileVersion), both .fr files (PluginVersion,
// ExtraPluginInfo, the About string, the menu bundle name) and both KCMLoc.h (kKCMAltKeyName).
//
// kKCMFileName (the output .pln name) is NOT here: it differs per side, so each side keeps its
// own -- the model in KCMID.h, the UI in KCMUIID.h.
//
// kKCMPluginName is here because the UI declares a `PluginDependency` on the model ("this UI is
// meaningless without it"), and that declaration needs the model's internal name and PluginID
// (guide gs-03:55). Naming your partner makes the name boundary information.
//----------------------------------------------------------------------------------------
#define kKCMCompanyKey	"KohakuNekotarou"	// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_".
#define kKCMCompanyValue	"KohakuNekotarou"	// Company name displayed externally.
#define kKCMDisplayName	"Kohaku Change Marker"	// Shown in the About menu item, the About box, and as the panel and tool name. Words separated by spaces, matching KBS's "Kohaku Search Panel".
#define kKCMVersion		"2.1.0"				// The product version, and it MUST be the same on both sides. It appears in the About box, in both .rc files as FileVersion, and in both PluginVersion resources. The history and the increments still to be submitted are kept in KCMID.h's long comment, which is the master copy.

// The modifier key as the USER sees it named. The implementation does not branch: the SDK's
// IEvent already absorbs the difference (OptionAltKeyDown = Alt on Windows, Option on the Mac;
// CmdKeyDown = Ctrl / Command), so only the wording changes.
// A string literal, so it concatenates in place both in .fr StringTables and in C++
// (e.g. "Hold Left + " kKCMAltKeyName "="). MACINTOSH is defined by the Mac build's xcconfig
// (GCC_PREPROCESSOR_DEFINITIONS) and by odfrc alike.
// **Both sides need it**: the UI's How to Use text (KCMUI_enUS.fr's kKCMHintKey and the Japanese
// in ui/KCMLoc.h) and the model's own KCMLoc.h must use the same spelling.
#ifdef MACINTOSH
#define kKCMAltKeyName	"Option"
#else
#define kKCMAltKeyName	"Alt"
#endif

// The model plug-in's internal name (used by the ID system and by the .rc InternalName). Left as
// it is for compatibility. The UI names it in its `PluginDependency`, paired with kKCMPluginID.
#define kKCMPluginName	"KohakuExtendScriptChangeMarker"

//----------------------------------------------------------------------------------------
// The model side's prefix.
//
// **Both copies carry this value** (the UI keeps its own kKCMUIPrefix in KCMUIID.h). Adobe issued
// the 256-slot range 0x1EA500 - 0x1EA5FF, split in half: model 0x1EA500, UI 0x1EA580. How the
// range was obtained and how it is divided is written out in full in **KCMID.h**.
//
// kKCMStringPrefix lives here too, because **string keys have to be globally unique** (guide
// vol2-12:71 -- unlike widget IDs, they cannot be borrowed). Keys that move to the UI therefore
// keep the `kKCMStringPrefix "..."` form: not one key value changes and the string table can be
// moved across whole.
//----------------------------------------------------------------------------------------
#define kKCMPrefixNumber	0x1EA500
#define kKCMPrefix		RezLong(kKCMPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKCMStringPrefix	SDK_DEF_STRINGIZE(kKCMPrefixNumber)	// The string equivalent of the unique prefix number for this plug-in.

// The model plug-in's PluginID. It is boundary information because **the UI names it as the
// dependency it declares** (guide gs-03:55, "a UI plug-in is meaningless without its model";
// worked example = transparencyeffectui/TranFxUI.fr:77-86).
// The UI's own PluginID is kKCMUIPluginID (KCMUIID.h) and is a different value. **The dependency
// runs one way, UI -> model**, so the model has no business knowing the UI's -- knowing it would
// itself be the dependency running backwards.
DECLARE_PMID(kPlugInIDSpace, kKCMPluginID, kKCMPrefix + 0)

//----------------------------------------------------------------------------------------
// Facade InterfaceIDs -- the counters at which the UI asks the model for something. The model
// AddIns each implementation to kUtilsBoss.
//
// Worked example = sdksamples/customconditionaltext's IID_ICUSCONDTXTFACADE.
// @warning the implementation AddIn'd must always be our own. Adding an SDK-supplied
// implementation to an existing boss collides with other vendors and the plug-in fails to load,
// and the unit of collision is the ImplementationID, not the IID.
//----------------------------------------------------------------------------------------
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMCOMPAREFACADE, kKCMPrefix + 4)	// ask the comparison engine
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMMARKDATA, kKCMPrefix + 5)	// READ the comparison result (read-only: marks are built in one place only, the facade above)
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMPAGEFLAGSFACADE, kKCMPrefix + 6)	// write Register (Added/Removed) and Check
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMSTORYEDITSFACADE, kKCMPrefix + 7)	// READ the Story Edits list (read-only but for RefreshRow)
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMBOOKFACADE, kKCMPrefix + 8)	// ask for a book comparison
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMSTORYMARKFACADE, kKCMPrefix + 10)	// put the Story mode's marks up and take them down. The numbering skips +9, which the notification protocol below had already taken.

//----------------------------------------------------------------------------------------
// The notification protocol IID.
//
// The only direction the model talks in. The model knows nothing about the UI: it throws a Change
// at the application's subject under a protocol IID of our own, and if nobody is listening
// nothing happens (which is what makes it safe under InDesign Server). The UI AttachObservers
// with this IID.
//----------------------------------------------------------------------------------------
DECLARE_PMID(kInterfaceIDSpace, IID_IKCMMODELCHANGEOBSERVER, kKCMPrefix + 9)

//----------------------------------------------------------------------------------------
// Notification MessageIDs -- how the model tells the UI what changed.
//
// kMessageIDSpace was untouched by KCM, so these start at +0. There is one listener, the UI's
// KCMModelChangeObserver, which switches on the changeID.
//----------------------------------------------------------------------------------------
DECLARE_PMID(kMessageIDSpace, kKCMMarksRebuiltMessage,      kKCMPrefix + 0)	// a comparison ran and the marks were rebuilt (affects the Prev/Next cursor, the scrollbar map, the Pages panel thumbnails and the Story Edits list)
DECLARE_PMID(kMessageIDSpace, kKCMMarksClearedMessage,      kKCMPrefix + 1)	// Stop removed the marks
DECLARE_PMID(kMessageIDSpace, kKCMPageFlagsChangedMessage,  kKCMPrefix + 2)	// Register (Added/Removed) or Check changed
DECLARE_PMID(kMessageIDSpace, kKCMStoryEditsRebuiltMessage, kKCMPrefix + 3)	// the Story Edits model was rebuilt
DECLARE_PMID(kMessageIDSpace, kKCMStatusTextMessage,        kKCMPrefix + 4)	// the status line changed (the string itself is read back through the facade's GetSessionStatus)
DECLARE_PMID(kMessageIDSpace, kKCMOversetRescannedMessage,  kKCMPrefix + 5)	// the overset scan produced a new result
DECLARE_PMID(kMessageIDSpace, kKCMComparisonDocsClosedMessage, kKCMPrefix + 6)	// a document being compared was closed and the Stop-equivalent clean-up has finished.
																					// Kept SEPARATE from Stop (kKCMMarksClearedMessage) because the UI's clean-up differs in three ways:
																					//   1. rebuilding the thumbnails is deferred to the next idle (during the switch of the front
																					//      document ForceRedraw does not take, and the frames stay on screen -- measured);
																					//   2. during a close-all it is HELD until every document is gone, then run once;
																					//   3. if Find Overset is on by itself the strip STAYS -- only the red band is redrawn.
																					// The payload carries up to three databases of documents that are STILL ALIVE (target, older
																					// version, source-side frames). Never pass a closed one: the listener dereferences it.

//----------------------------------------------------------------------------------------
// The comparison mode
//----------------------------------------------------------------------------------------
// What a comparison actually does. Chosen in the panel's flyout, held by the model (the model is
// what reads the value and runs on it, so the getter and setter live on IKCMCompareFacade).
//
// **A type rather than an ID, and here on purpose.** This header is the one place BOTH plug-ins
// include (they have to, to agree on the IIDs), which makes it the safest home for a type that
// appears on the boundary: put it in either side's own header and the other side ends up
// including its partner's.
//
// **THE TWO RESULTS ARE NEVER HELD AT ONCE.** Changing the mode re-runs the comparison. Keeping
// both would create two answers to "what is on screen right now" ([[one-question-one-place]]).
enum KCMCompareMode
{
	kKCMModePixel = 0,	// the default: rasterize the pages and compare pixels (KCM's original comparison)
	kKCMModeStory = 1		// compare the stories' text, paragraph by paragraph and then character by character
};

#endif // __KCMBoundaryID_h__
