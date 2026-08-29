//========================================================================================
//
//  KCMPanelState.cpp
//
//  Saves and restores the settings toggles of the panel flyout as a private JSON file in the
//  user's preferences folder (see KCMPanelState.h). Nothing is written into InDesign's own data.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "PMString.h"
#include "FileUtils.h"		// GetAppRoamingDataFolder / AppendPath / OpenFile / DoesFileExist / SysFileToPMString
#include "IDFile.h"

#include <string>
#include <cstdio>			// FILE / fread / fwrite / fclose

// Project includes (the state accessors of each toggle):
#include "KCMPanelState.h"
#include "Utils.h"					// Utils<IKCMCompareFacade>()
#include "IKCMCompareFacade.h"	// reading and writing the print-marks setting, across the boundary
#include "KCMUIShared.h"	// panel / status line / nav readout / tool button (split from KCMCore.h on 2026-08-13)
#include "KCMViewSync.h"			// KCMGetLayoutSync / KCMSetLayoutSync
#include "KCMScrollMap.h"			// KCMGetScrollMapEnabled / KCMSetScrollMapEnabled
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
//   ★The example of the official class (`JSON` in `public/interfaces/utils/IJsonUtils.h`), and
//     the full account of why this does not use it, are in the save/load block of
//     `source/KCMPageCheck.cpp`.
//   ★**The reason for stdio (FileUtils::OpenFile) rather than IPMStream is in the same place**:
//     IPMStream's Close()/Flush() return void, so a full disk cannot be detected.
//----------------------------------------------------------------------------------------

static const char* KCMBoolLiteral(bool16 b)
{
	return b ? "true" : "false";
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
	json += "  \"syncLayoutViews\": ";        json += KCMBoolLiteral(KCMGetLayoutSync());                       json += ",\n";
	json += "  \"scrollbarMap\": ";           json += KCMBoolLiteral(KCMGetScrollMapEnabled());                 json += ",\n";
	json += "  \"ignorePageNumberMarker\": "; json += KCMBoolLiteral(compare->GetIgnorePageNumberMarker());               json += ",\n";
	json += "  \"translucentPanel\": ";       json += KCMBoolLiteral(KCMGetPanelTranslucent());                 json += ",\n";
	json += "  \"translucentPagesPanel\": ";  json += KCMBoolLiteral(KCMGetPagesPanelTranslucent());            json += ",\n";
	json += "  \"translucentBookDialog\": ";  json += KCMBoolLiteral(KCMGetBookDialogTranslucent());            json += ",\n";
	// ★The compare mode (user’s instruction). ⚠**The only non-bool item**, so unlike the lines above
	//   its value is quoted. ⚠**Older settings files do not have this key**, and the reader takes
	//   "the current value when it is absent", so reading one simply leaves the default (Pixel).
	json += "  \"compareMode\": \"";
	json += (compare->GetCompareMode() == kKCMModeStory ? "story" : "pixel");
	json += "\"\n";
	json += "}\n";

	FILE* fp = FileUtils::OpenFile(file, "wb");
	if (fp == nil)
	{
		KCMSaySaveFailed("Save failed (open)");
		return;
	}
	// ★Check the byte count AND the result of fclose: a partial write (a full disk, say) must not be
	//   reported as "saved" with a path.
	const size_t wrote = fwrite(json.data(), 1, json.size(), fp);
	const int closed = fclose(fp);
	if (wrote != json.size() || closed != 0)
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

	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return;
	std::string text;
	char buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		text.append(buf, n);
	const bool readFailed = (ferror(fp) != 0);
	fclose(fp);
	if (readFailed)
		return;		// ★Do not apply a partially read text (the same discipline as KCMReadWholeFile in
					//   KCMPageCheck.cpp): every toggle keeps its default.
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
	KCMSetScrollMapEnabled      (KCMJsonReadBool(text, "scrollbarMap",           KCMGetScrollMapEnabled()));
	compare->SetIgnorePageNumberMarker(
		KCMJsonReadBool(text, "ignorePageNumberMarker", compare->GetIgnorePageNumberMarker()));

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
	const std::string mode = KCMJsonReadString(text, "compareMode");
	if (mode == "story")
		compare->SetCompareMode(kKCMModeStory);
	else if (mode == "pixel")
		compare->SetCompareMode(kKCMModePixel);

	// ★Bring the tab name into line with the restored state too. On the run called from startup
	//   (KCMUIStartup::Startup) there is no panel yet, so it returns quietly inside and the name is
	//   really written by the panel’s AutoAttach ---- which calls the same function. This call is
	//   here for the run where the panel already exists.
	KCMPanelTitle::Update();
	// ("translucentToolbox" went with its feature. Left in an older settings file it does no harm:
	//  it is simply no longer read, since KCMJsonReadBool looks keys up by name.)
}

// End, KCMPanelState.cpp.
