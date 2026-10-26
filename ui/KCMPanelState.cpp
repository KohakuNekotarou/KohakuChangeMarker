//========================================================================================
//
//  KCMPanelState.cpp
//
//  Saves and restores the settings toggles of the panel flyout as a private JSON file in the
//  user's preferences folder (see KCMPanelState.h). Nothing is written into InDesign's own data.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"		// XferByte / Seek / Flush / GetStreamState / Close - the file's bytes

// General includes:
#include "PMString.h"
#include "FileUtils.h"		// GetAppRoamingDataFolder / AppendPath / DoesFileExist / SysFileToPMString
#include "IDFile.h"
#include "StreamUtil.h"		// CreateFileStreamRead / CreateFileStreamWrite

#include <string>

// Project includes (the state accessors of each toggle):
#include "KCMPanelState.h"
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// reading and writing the print-marks setting, across the boundary
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMViewSync.h"			// KCMGetLayoutSync / KCMSetLayoutSync
#include "KCMPanelAlpha.h"		// KCMGetPanelTranslucent / KCMSetPanelTranslucent (Translucent Panel)
#include "KCMPanelTitle.h"		// KCMPanelTitle::Update (put the restored compare mode on the tab)

// The file name, directly under Roaming. ★No subfolder (see KCMPanelStateFile below and the
// account in KCMPanelState.h).
static const char* const kKCMPanelStateFileName = "KCMPanelState.json";

//----------------------------------------------------------------------------------------
// Resolving where to save
//----------------------------------------------------------------------------------------

// Returns, in outFile, an IDFile for KCMPanelState.json directly under the roaming preferences
// folder (the one with the locale in its path).
// ★No subfolder is created (user's instruction). Passing the file name straight to
//   GetAppRoamingDataFolder's subFolderName gives the IDFile **of the file** in that folder (the
//   SDK does the same in SnpShareAppResources.cpp and SuppUISysFileData.cpp). The parent folder
//   is one InDesign has already made for its preferences, so CreateFolderIfNeeded is not needed
//   (the old implementation needed it only because it opened under a "KCM" subfolder that did
//   not exist). kFalse when it cannot be resolved.
static bool16 KCMPanelStateFile(IDFile& outFile)
{
	return FileUtils::GetAppRoamingDataFolder(&outFile, PMString(kKCMPanelStateFileName));
}

//----------------------------------------------------------------------------------------
// A minimal JSON (written by hand, read permissively)
//   What is saved is a flat set of booleans, so it is handled here rather than through boost
//   (IJsonUtils).
//   ★The official class (`JSON` in `public/interfaces/utils/IJsonUtils.h`) and why a settings
//     file like this one does not use it are in KBS's `ui/KBSPanelState.cpp`, under "WHY NOT THE
//     SDK'S JSON CLASS" (in short: property_tree writes every value back as a quoted string, so a
//     bare true comes out as "true").
//   ⚠This pointed at the save/load block of `source/KCMPageCheck.cpp` until 2026-10-02. That
//     block went on 2026-09-27 (57b1278) and took the reasoning with it.
//
//   ★**The file's bytes go through the SDK's file stream** (2026-10-02, the user's call: "for the
//     panel settings and the like, use the official one") -- StreamUtil::CreateFileStreamRead /
//     CreateFileStreamWrite -> IPMStream, as SnpShareAppResources.cpp:182,187 opens its own
//     preferences file. It was stdio (FileUtils::OpenFile, fread / fwrite / fclose) until then,
//     kept because IPMStream's Close()/Flush() return void, so a full disk would go unnoticed.
//     ★That check is now made by **reading the file back**: the save is reported only when the
//     file reads back exactly as written (KCMSavePanelState). The same change, and the same
//     read-back, as KBS's ui/KBSPanelState.cpp on the same day.
//----------------------------------------------------------------------------------------

static const char* KCMBoolLiteral(bool16 b)
{
	return b ? "true" : "false";
}

// The whole file, or false when it could not be read in full. ★A read that stopped part way through
//   is not used -- every toggle then keeps its default rather than "the settings that happened to be
//   in the part that arrived". Read the way the SDK's samples read a whole file: the size first (Seek
//   to the end answers where it got to), then exactly that many bytes from the start
//   (textimportfilter/TxtImpFilter.cpp:602-611, pdfvt/PDFVTUtils.cpp:141-147) -- the read never asks
//   past the end, so a short count, or the stream in kStreamStateFailure, can only be a read that
//   broke off. (fread and ferror until 2026-10-02.)
static bool KCMReadWholeFile(const IDFile& file, std::string& out)
{
	out.clear();
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamRead(file));
	if (stream == nil)
		return false;
	const int64 size = stream->Seek(0, kSeekFromEnd);
	stream->Seek(0, kSeekFromStart);
	bool ok = (size >= 0 && size <= static_cast<int64>(0x7FFFFFFF));	// XferByte counts in int32
	if (ok && size > 0)
	{
		out.resize(static_cast<size_t>(size));
		const int32 n = stream->XferByte(reinterpret_cast<uchar*>(&out[0]), static_cast<int32>(size));
		ok = (n == static_cast<int32>(size)) && (stream->GetStreamState() != kStreamStateFailure);
	}
	stream->Close();
	if (!ok)
		out.clear();
	return ok;
}

// Report a failed save on the panel's status line. ★The wording is this short on purpose: the
// status line is narrow, and what overflows is shortened by the widget, not by the reader.
static void KCMSaySaveFailed(const char* what)
{
	PMString err(what);
	err.SetTranslatable(kFalse);
	KCMSetStatus(err, kTrue /*forceRedrawNow*/);
}

// Where the value of "key" begins ＝ just past the first ':' that follows it; npos when the key
// is not there, or has no ':' after it. ★The two readers below opened with exactly this.
static size_t KCMJsonValueStart(const std::string& text, const char* key)
{
	std::string needle("\"");
	needle += key;
	needle += "\"";

	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return std::string::npos;
	const size_t colon = text.find(':', k + needle.size());
	return (colon == std::string::npos) ? std::string::npos : colon + 1;
}

// Finds "key" in text and reads the true/false after the first ':' that follows it; defVal when
// there is none.
static bool16 KCMJsonReadBool(const std::string& text, const char* key, bool16 defVal)
{
	size_t p = KCMJsonValueStart(text, key);
	if (p == std::string::npos)
		return defVal;

	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;

	if (text.compare(p, 4, "true") == 0)
		return kTrue;
	if (text.compare(p, 5, "false") == 0)
		return kFalse;
	return defVal;
}

// Finds "key" in text and reads the "string" after the first ':' that follows it; empty when
// there is none.
// ★★**Why it is not a bool**: the compare mode is an enum, and written as
//   `"storyMode": true/false` **the meaning of a saved file would change on the day a third mode
//   arrives** (false could no longer say whether it means "pixel" or "not story"). Written by
//   name, that day costs one more reader and nothing else.
static std::string KCMJsonReadString(const std::string& text, const char* key)
{
	const size_t p = KCMJsonValueStart(text, key);
	if (p == std::string::npos)
		return std::string();

	const size_t open = text.find('"', p);
	if (open == std::string::npos)
		return std::string();
	const size_t close = text.find('"', open + 1);
	if (close == std::string::npos)
		return std::string();

	return text.substr(open + 1, close - open - 1);
}

//----------------------------------------------------------------------------------------
// Saving (called from "Save Panel Settings" on the flyout)
//----------------------------------------------------------------------------------------

void KCMSavePanelState()
{
	IDFile file;
	if (!KCMPanelStateFile(file))
	{
		KCMSaySaveFailed("Save failed (folder)");
		return;
	}

	// Build the current state into a JSON string.
	// ★It is asked many times, so the interface is taken once into an InterfacePtr (`Utils.h:74-80`
	//   ＝ "if you want to use a utility interface in several places, get it once and save it in an
	//   InterfacePtr"). ⚠**The official text names no number** -- "three or more" is a rule of
	//   thumb of ours, and an older comment here presented it as official.
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	std::string json;
	json += "{\n";
	json += "  \"version\": 1,\n";
	// ⚠★★**"Print comparison marks" (printMarks) is deliberately not written here** (user’s
	//   instruction).
	//   ★**This is the specification, not a forgotten save** ＝ that toggle changes **what comes
	//     out on paper and in a PDF**, not just what is on screen, so every launch starts from the
	//     default OFF and marks reach an output only in a session where they were switched ON
	//     deliberately. (It used to be saved ＝ the behaviour of the released 1.3.0; the change is
	//     written up in `source/KCMID.h`.)
	//   ⚠**Older settings files still contain "printMarks"**, and the reader below looks up keys by
	//     name, so it is simply ignored; the next time this function runs the key disappears.
	//   ⚠**The opacity (opacity25) IS still saved** ＝ that one is "how it looks when it does
	//     appear", and it adds nothing to an output.
	json += "  \"opacity25\": ";              json += KCMBoolLiteral(compare->GetMarkOpacity25());                json += ",\n";
	// ★"Mark colour" (Red/Cyan). ⚠**It was missing here when the feature was added** (found and
	//   fixed in a later review) ＝ choosing a colour and saving still **came back red after a
	//   restart**. Save itself reported success, so it looked like "I saved it and it does not
	//   work".
	//   ★Why a bool is right: there are only two values (Red/Cyan). It is not a setting that grows a
	//     third one, so the reason compareMode is a string (at KCMJsonReadString above) does not
	//     apply here.
	json += "  \"markColorCyan\": ";          json += KCMBoolLiteral(compare->GetMarkColorCyan());                json += ",\n";
	// ("holdToHideMarks" went with its toggle; left in an older file, it is simply never read.)
	json += "  \"showTgtMarks\": ";           json += KCMBoolLiteral(compare->GetShowTargetMarks());              json += ",\n";
	json += "  \"showSrcMarks\": ";           json += KCMBoolLiteral(compare->GetShowSourceMarks());              json += ",\n";
	json += "  \"showOldNumbers\": ";         json += KCMBoolLiteral(compare->GetShowOldPageNumbers());           json += ",\n";
	json += "  \"showStoryIds\": ";           json += KCMBoolLiteral(compare->GetShowStoryIds());                 json += ",\n";
	json += "  \"syncLayoutViews\": ";        json += KCMBoolLiteral(KCMGetLayoutSync());                       json += ",\n";
	json += "  \"ignorePageNumberMarker\": "; json += KCMBoolLiteral(compare->GetIgnorePageNumberMarker());               json += ",\n";
	json += "  \"pairPagesByUid\": ";         json += KCMBoolLiteral(compare->GetPairPagesByUid());                       json += ",\n";
	json += "  \"translucentPanel\": ";       json += KCMBoolLiteral(KCMGetPanelTranslucent());                 json += ",\n";
	json += "  \"translucentPagesPanel\": ";  json += KCMBoolLiteral(KCMGetPagesPanelTranslucent());            json += ",\n";
	json += "  \"translucentBookDialog\": ";  json += KCMBoolLiteral(KCMGetBookDialogTranslucent());            json += ",\n";
	// ★The compare mode (user’s instruction). ⚠**The only non-bool item**, so unlike the lines above
	//   its value is quoted. ⚠**Older settings files do not have this key**, and the reader takes
	//   "the current value when it is absent", so reading one simply leaves the default (Pixel).
	json += "  \"compareMode\": \"";
	// ⚠**A switch, not a ternary** (2026-09-09). As `== kKCMModeStory ? "story" : "pixel"` a panel
	//   left in the Resources mode was SAVED AS PIXEL - the setting silently changed itself the
	//   moment it was written down.
	switch (compare->GetCompareMode())
	{
		case kKCMModeStory:		json += "story";		break;
		case kKCMModeResources:	json += "resources";	break;
		case kKCMModeTrack:		json += "track";		break;
		default:				json += "pixel";		break;
	}
	json += "\"\n";
	json += "}\n";

	// ★A partial write (a full disk, say) must not be reported as "saved" with a path. The byte count
	//   and the stream's state are checked, and then the file is READ BACK and must come out exactly as
	//   written: IPMStream's Flush and Close return nothing, so a write that fails on its way to the
	//   disk shows only as a file shorter than the text (stdio's fclose reported it until 2026-10-02).
	{
		InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(file, kOpenOut | kOpenTrunc));
		if (stream == nil)
		{
			KCMSaySaveFailed("Save failed (open)");
			return;
		}
		const int32 size = static_cast<int32>(json.size());
		const int32 wrote = stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(json.data())), size);
		stream->Flush();
		const bool failed = (wrote != size) || (stream->GetStreamState() == kStreamStateFailure);
		stream->Close();
		if (failed)
		{
			KCMSaySaveFailed("Save failed (write)");
			return;
		}
	}
	std::string readBack;
	if (!KCMReadWholeFile(file, readBack) || readBack != json)
	{
		KCMSaySaveFailed("Save failed (write)");
		return;
	}

	// Show the full path in the panel’s status line (user’s request: from a modal to the panel).
	// ★The path alone -- a label such as "Settings saved:" would overflow the line.
	// ⚠**Do not copy the dimensions here.** They belong to kKCMStatusTextWidgetID in `ui/KCMUI.fr`
	//   (a **KCMStatusTextWidget**, self-drawn since the message area needed two colours -- it was a
	//   StaticMultiLineTextWidget before that). **How many lines fit is not a constant either**: the
	//   box takes as many whole lines as its height allows, which is four on a Japanese UI and six on
	//   an English one. An older note here wrote both the widget type and "4 lines" as facts, and the
	//   same numbers had been scattered across three files before that.
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append(FileUtils::SysFileToPMString(file));
	KCMSetStatus(msg, kTrue /*forceRedrawNow*/);
}

//----------------------------------------------------------------------------------------
// Restoring (called at startup from KCMUIStartup::Startup, once per session; the call from the
//   panel’s AutoAttach stays as a no-op safety net through the internal guard. See
//   KCMPanelState.h)
//----------------------------------------------------------------------------------------

void KCMLoadPanelStateIfPresent()
{
	static bool16 sLoaded = kFalse;
	if (sLoaded)
		return;
	sLoaded = kTrue;	// try once per session, whether or not it succeeds

	IDFile file;
	if (!KCMPanelStateFile(file))
		return;
	if (!FileUtils::DoesFileExist(file))
		return;		// no saved data = first run. The defaults stand

	std::string text;
	if (!KCMReadWholeFile(file, text))
		return;		// ★Do not apply a partially read text: every toggle keeps its default (KCMReadWholeFile).
	if (text.empty())
		return;

	// ---- apply to each toggle ----
	// ★Order: the display toggles that feed the opacity go in before SetPrintMarks is called.
	//   SetPrintMarks recomputes the always-on screen opacity from the current choice
	//   (KCMBaseScreenOpacity), so its inputs have to be in place first.
	//   ⚠★That input **moved from "Hold to Hide Marks" to "Always Show Marks on Target"** when Hold
	//     was removed. The ordering requirement did not change ＝ SetShowTargetMarks below must come
	//     before SetPrintMarks. **Look at this dependency before reordering these lines.**
	InterfacePtr<IKCMCompareFacade> compare(Utils<IKCMCompareFacade>().QueryUtilInterface());
	compare->SetShowTargetMarks   (KCMJsonReadBool(text, "showTgtMarks",    compare->GetShowTargetMarks()));
	compare->SetShowSourceMarks   (KCMJsonReadBool(text, "showSrcMarks",    compare->GetShowSourceMarks()));
	compare->SetShowOldPageNumbers(KCMJsonReadBool(text, "showOldNumbers",  compare->GetShowOldPageNumbers()));

	// ⚠★★**The print marks (printMarks) are not restored** (user’s instruction; the save side does
	//   not write them either). ∴ every launch starts from the default OFF ＝ marks reach an output
	//   only in a session where they were switched ON deliberately.
	// ★**SetPrintMarks is still called** because the opacity is what has to be restored and this is
	//   its only way in (`SetMarkOpacity25` looks up `KCMActiveDoc()` inside and writes to the status
	//   line ＝ it is the flyout’s implementation in `KCMComparisonRun.cpp`, not a route to take at
	//   startup).
	//   ⇒ The first argument passes **the current print flag through unchanged** (the default kFalse,
	//     this early).
	const bool16 opacity25  = KCMJsonReadBool(text, "opacity25",  compare->GetMarkOpacity25());
	compare->SetPrintMarks(compare->GetPrintMarks(), opacity25, nil);	// db=nil: set the flag only (nothing is armed yet, so there is nothing to redraw)

	// ★The mark colour (added later, to make up for the miss when the feature went in).
	// ★**It is safe at startup**: `KCMDoSetMarkColor` (KCMCore.cpp) **returns immediately when the
	//   value does not change**, and on the run where it does it only redraws the db it took from
	//   `KCMActiveDoc()`. With no document open that is `KCMInvalidateDB(nil)`, which is harmless.
	// ★**No follow-up is needed**: unlike the opacity, the colour is **re-read by
	//   `SelectedMarkColor()` on every draw**, so nothing has to ask for `KCMStoryMarksRefresh()`.
	//   (The full reason is at kKCMPopupColorRedActionID in KCMActionComponent.cpp.) ∴ unlike the
	//   opacity above, one line does it.
	compare->SetMarkColor(KCMJsonReadBool(text, "markColorCyan", compare->GetMarkColorCyan()));

	KCMSetLayoutSync            (KCMJsonReadBool(text, "syncLayoutViews",         KCMGetLayoutSync()));
	compare->SetIgnorePageNumberMarker(
		KCMJsonReadBool(text, "ignorePageNumberMarker", compare->GetIgnorePageNumberMarker()));
	compare->SetPairPagesByUid(
		KCMJsonReadBool(text, "pairPagesByUid", compare->GetPairPagesByUid()));	// absent in a file saved before 2026-09-13 = the default stands
	// A flag alone at startup, like the rest: nothing is drawn until a document is, and the first
	// draw of a spread builds the table of story holders (KCMRingAdornment.cpp §1.5).
	compare->SetShowStoryIds(
		KCMJsonReadBool(text, "showStoryIds", compare->GetShowStoryIds()));	// absent in a file saved before 2026-09-13 = the default (OFF) stands

	// ★No window is touched here, and none could be: this restore runs at startup
	//   (KCMUIStartup::Startup), when the panel does not exist yet. What actually applies the
	//   translucency is the panel’s AutoAttach and the kPaletteVisibilityChangedMessage subscription
	//   (KCMPanelAlpha.cpp).
	//   ★It is not purely "restore a flag" though: restoring ON makes KCMSetPanelTranslucent install
	//     a Win32 event hook (the only way to catch a transition that changes nothing but where the
	//     panel sits). While there is no panel the callback returns immediately, so the startup
	//     sequence is unaffected.
	KCMSetPanelTranslucent      (KCMJsonReadBool(text, "translucentPanel",       KCMGetPanelTranslucent()));
	KCMSetPagesPanelTranslucent (KCMJsonReadBool(text, "translucentPagesPanel",  KCMGetPagesPanelTranslucent()));
	// ★The dialog’s own. The note above applies unchanged and is **simpler** here: there the case
	//   was "the panel does not exist yet", while for a dialog "not open" is the ordinary state, and
	//   KCMBookDialog.cpp hands its window over every time it opens. Restoring the flag is enough.
	KCMSetBookDialogTranslucent (KCMJsonReadBool(text, "translucentBookDialog",  KCMGetBookDialogTranslucent()));

	// ★★The compare mode (user’s instruction).
	//   ⚠**Calling SetCompareMode here is safe** ＝ it is contracted (IKCMCompareFacade.h) to change
	//     the setting only and not to redo a running comparison. Whether to recompare is the
	//     caller’s decision: the flyout does, **and this startup restore does not**. That branch
	//     exists for exactly this place.
	//   ⚠With the key absent the current value stands (＝ an older settings file stays Pixel). A
	//     spelling we do not know is treated the same way ---- reading a file saved by a later
	//     version with more modes, "leave it alone" breaks less than "I do not know it, so make it
	//     Pixel".
	//   ★**Every mode is spelled out, and none of them is the `else`.** The rule above - an unknown
	//     spelling leaves the value alone - only holds while no branch quietly collects the leftovers.
	const std::string mode = KCMJsonReadString(text, "compareMode");
	if (mode == "story")
		compare->SetCompareMode(kKCMModeStory);
	else if (mode == "pixel")
		compare->SetCompareMode(kKCMModePixel);
	else if (mode == "resources")
		compare->SetCompareMode(kKCMModeResources);
	else if (mode == "track")
		compare->SetCompareMode(kKCMModeTrack);

	// ★Bring the tab name into line with the restored state too. On the run called from startup
	//   (KCMUIStartup::Startup) there is no panel yet, so it returns quietly inside and the name is
	//   really written by the panel’s AutoAttach ---- which calls the same function. This call is
	//   here for the run where the panel already exists.
	KCMPanelTitle::Update();
	// ("translucentToolbox" went with its feature. Left in an older settings file it does no harm:
	//  it is simply no longer read, since KCMJsonReadBool looks keys up by name.)
}

// End, KCMPanelState.cpp.
