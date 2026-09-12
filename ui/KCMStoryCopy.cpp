//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMStoryCopy.h for what this is for. Three answers live here: which change the menu is
//  about, whether the item may be offered, and the copy itself.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IClipboardController.h"	// the session's clipboard - PrepareForCopy / CopyHasCompleted
#include "ICommand.h"
#include "IDataExchangeHandler.h"	// the text flavour's handler, which owns the scrap story
#include "ISession.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"			// InsertCmd - the only way words go into a text model
#include "ITextScrapData.h"			// the scrap story behind the text handler

// General includes:
#include "CmdUtils.h"
#include "PMFlavorTypes.h"			// kPMTextFlavor
#include "PMString.h"
#include "Utils.h"
#include "WideString.h"

// Project includes:
#include "KCMUIID.h"
#include "IKCMCompareFacade.h"		// GetCompareMode - the Story mode only
#include "IKCMStoryEditsFacade.h"	// GetChange - the words themselves
#include "KCMStoryCopy.h"
#include "KCMUIShared.h"				// KCMSetStatus - the panel's message line

namespace
{

/* Which change the change-row menu was popped over.

   ★FILE STATICS, for the reason KCMStoryRefresh gives for its row: they belong to "the menu that
   is up right now", not to any row widget, and row widgets are recycled as the list scrolls. One
   menu is up at a time.

   ★NOT CLEARED WHEN THE MENU CLOSES, and not needing to be: every reader asks the model for the
   change afresh (GetChange), so indexes from a comparison ago answer "no such change" rather than
   naming whatever sits at those numbers now.
*/
int32 gMenuRow = -1;
int32 gMenuChange = -1;

/* SourceTextOf
   The older side's text of a change, or an empty string when nothing stood there.

   ★THE FIELD DEPENDS ON THE KIND, and IKCMStoryEditsFacade::Change says why at its fWhat: a
   text row shows whichever side CHANGED, so a DELETION carries the removed (older) words in
   fText and the newer side in fOtherText, while a replacement or an insertion carries the
   newer words in fText and the older in fOtherText. A ruby row is the other way about and the
   same way every time - the target in fText, the source in fOtherText, and the READINGS in
   fRuby (target) / fOtherRuby (source).
*/
PMString SourceTextOf(const IKCMStoryEditsFacade::Change& c)
{
	if (c.fWhat == IKCMStoryEditsFacade::Change::kWhatAttr)
	{
		// ★THE READING, NOT THE BASE TEXT (user, 2026-09-12: "the ruby itself, not its parent").
		//   Only ruby (fAttrKind 1) carries a reading: kenten's fRuby is a KIND name, and the
		//   footnote / endnote rows carry nothing a reader would paste.
		if (c.fAttrKind != 1)
			return PMString();
		return c.fOtherRuby;			// empty for a ruby ADDED - nothing stood there
	}

	// A text change. fKind: 0 = replace, 1 = insert, 2 = delete.
	if (c.fKind == 2)
		return c.fText;					// the row shows what was removed: that IS the older text
	if (c.fKind == 0)
		return c.fOtherText;			// what the newer words replaced
	return PMString();					// an insertion: nothing stood on the older side
}

/* StashedChange
   The change the stash names, as the model holds it NOW. kFalse when the stash names nothing,
   or the list has been rebuilt out from under it.
*/
bool16 StashedChange(IKCMStoryEditsFacade::Change& out)
{
	if (gMenuRow < 0 || gMenuChange < 0)
		return kFalse;

	// ★THE STORY MODE ONLY. The list is shared with the Resources mode, whose child rows carry
	//   the same node class and index shape but are not text changes of a story.
	if (Utils<IKCMCompareFacade>()->GetCompareMode() != kKCMModeStory)
		return kFalse;

	return Utils<IKCMStoryEditsFacade>()->GetChange(gMenuRow, gMenuChange, out);
}

/* PlainTextOf
   The string with InDesign's own marker characters taken out, so that what reaches another
   application is words and nothing else.

   What is dropped: everything below U+0020 except the paragraph end (U+000D) and the tab, which
   covers the table anchor and continuation (0x16 / 0x17), the page-number and variable stand-in
   (0x18), the section marker (0x19) and the footnote / endnote references (0x04 / 0x05); the
   anchored-object stand-in (U+FFFC); and the note anchor (U+FEFF, which is also the byte-order
   mark). The list is TextChar.h's, read in memory indesign-special-text-characters.
*/
WideString PlainTextOf(const PMString& s)
{
	const WideString in(s);
	WideString out;
	for (WideString::const_iterator it = in.begin(); it != in.end(); ++it)
	{
		const UTF32TextChar ch = *it;
		const int32 v = static_cast<int32>(ch.GetValue());
		if (v < 0x20 && v != 0x0D && v != 0x09)
			continue;
		if (v == 0xFFFC || v == 0xFEFF)
			continue;
		out.push_back(ch);
	}
	return out;
}

/* CopyToClipboard
   The product's own way of putting a string on the clipboard - LinksUIPanelMenuComponent::
   CopyStringToScrap, line for line but for the nil tests: the text flavour's handler owns a
   scrap STORY, and the words are inserted into it through the ordinary text command. What other
   applications then receive is what InDesign externalises for any copied text.
*/
bool16 CopyToClipboard(const WideString& text)
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return kFalse;
	InterfacePtr<IClipboardController> clip(session, UseDefaultIID());
	if (clip == nil || !clip->IsValid())
		return kFalse;

	clip->PrepareForCopy();

	InterfacePtr<IDataExchangeHandler> textHandler(clip->QueryHandler(kPMTextFlavor));
	if (textHandler == nil)
		return kFalse;
	clip->SetActiveHandler(textHandler);

	InterfacePtr<ITextScrapData> scrapData(textHandler, UseDefaultIID());
	if (scrapData == nil)
		return kFalse;
	scrapData->Clear();

	InterfacePtr<ITextModel> model(scrapData->GetStoryRef(), UseDefaultIID());
	InterfacePtr<ITextModelCmds> modelCmds(model, UseDefaultIID());
	if (modelCmds == nil)
		return kFalse;

	// TextModel takes its words as a shared WideString.
	boost::shared_ptr<WideString> stringData(new WideString(text));
	InterfacePtr<ICommand> insertCmd(modelCmds->InsertCmd(0, stringData));
	if (insertCmd == nil || CmdUtils::ProcessCommand(insertCmd) != kSuccess)
		return kFalse;

	clip->CopyHasCompleted();
	return kTrue;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStorySetMenuChange
//----------------------------------------------------------------------------------------

void KCMStorySetMenuChange(int32 rowIndex, int32 changeIndex)
{
	gMenuRow = rowIndex;
	gMenuChange = changeIndex;
}

//----------------------------------------------------------------------------------------
// KCMChangeRowCanCopySource
//----------------------------------------------------------------------------------------

bool16 KCMChangeRowCanCopySource()
{
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
		return kFalse;

	// ★GREYED WHEN THERE IS NOTHING TO COPY - an insertion, a ruby added, a kenten or footnote
	//   row - rather than offered and then found empty. An item that copies an empty string
	//   would clear the clipboard for nothing the reader asked for.
	return SourceTextOf(change).IsEmpty() ? kFalse : kTrue;
}

//----------------------------------------------------------------------------------------
// KCMChangeRowCopySource
//----------------------------------------------------------------------------------------

bool16 KCMChangeRowCopySource()
{
	// The same test the menu was greyed by, asked again at the moment of acting: a menu that is
	// already up is not re-tested by anybody else, and the comparison may have been stopped in
	// between.
	IKCMStoryEditsFacade::Change change;
	if (!StashedChange(change))
	{
		KCMSetStatus("copy: no change to copy from.");
		return kFalse;
	}

	const WideString text = PlainTextOf(SourceTextOf(change));
	if (text.empty())
	{
		KCMSetStatus("copy: nothing on the Source side.");
		return kFalse;
	}

	if (!CopyToClipboard(text))
	{
		KCMSetStatus("copy: the clipboard refused.");
		return kFalse;
	}

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append("copied Source text (");
	msg.AppendNumber(static_cast<int32>(text.CharCount()));
	msg.Append(text.CharCount() == 1 ? " character)" : " characters)");
	KCMSetStatus(msg);
	return kTrue;
}

// End, KCMStoryCopy.cpp.
