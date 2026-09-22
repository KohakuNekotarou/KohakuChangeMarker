//========================================================================================
//
//  KCMStoryNoteEdit.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "ICommand.h"
#include "IDataBase.h"
#include "IPMUnknown.h"
#include "IRangeData.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"
#include "IUIDData.h"
#include "CmdUtils.h"
#include "TextChar.h"			// kTextChar_FootnoteMarker
#include "TextID.h"				// kCreateFootnoteCmdBoss, IID_IFOOTNOTENUMBERING
#include "UIDList.h"
#include "UIDRef.h"
#include "WideString.h"

#include "KCMStoryNoteEdit.h"

bool16 KCMCanInsertNoteAt(ITextModel* model, TextIndex at)
{
	if (model == nil)
		return kFalse;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at));
	if (thread == nil)
		return kFalse;
	InterfacePtr<ITextStoryThreadDict> dict(UIDRef(::GetDataBase(model), thread->GetDictUID()), UseDefaultIID());
	if (dict == nil)
		return kFalse;
	// ★THE ID, NOT THE INTERFACE - see the header.
	InterfacePtr<IPMUnknown> numbering(dict, IID_IFOOTNOTENUMBERING);
	return (numbering != nil) ? kTrue : kFalse;
}

ErrorCode KCMInsertNoteAt(ITextModel* model, TextIndex at, TextIndex& outWordsFrom, TextIndex& outWordsTo,
						  PMString& whyNot)
{
	outWordsFrom = kInvalidTextIndex;
	outWordsTo = kInvalidTextIndex;
	if (model == nil)
		return kFailure;
	if (!KCMCanInsertNoteAt(model, at))
	{
		whyNot = "this place does not take a footnote";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;

	// ---- 1. the marker, standing in the body ---------------------------------------------------
	boost::shared_ptr<WideString> marker(new WideString);
	marker->Append(kTextChar_FootnoteMarker);
	InterfacePtr<ICommand> insert(cmds->InsertCmd(at, marker));
	if (insert == nil || CmdUtils::ProcessCommand(insert) != kSuccess)
	{
		whyNot = "the footnote's marker could not be put in";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}

	// ---- 2. the note, built around it ----------------------------------------------------------
	//
	// ⚠★★★**FROM HERE ON, EVERY WAY OUT TAKES THE MARKER BACK WITH IT.** The marker is already in
	//   the text, and a marker with no note behind it is not something a document should be left
	//   holding - the import goes straight on to the next note, so nothing else would ever clear
	//   it up. Deleting that one character is also what removes a note that WAS made (the same move
	//   KCMDeleteNoteAt makes), so one undo serves every failure below.
	bool16 made = kFalse;
	InterfacePtr<ICommand> create(CmdUtils::CreateCommand(kCreateFootnoteCmdBoss));
	if (create != nil)
	{
		create->SetItemList(UIDList(model));
		InterfacePtr<IRangeData> range(create, UseDefaultIID());
		if (range != nil)
		{
			range->Set(at, at);
			made = (CmdUtils::ProcessCommand(create) == kSuccess) ? kTrue : kFalse;
		}
	}
	if (!made)
	{
		InterfacePtr<ICommand> back(cmds->DeleteCmd(at, 1));
		if (back != nil)
			CmdUtils::ProcessCommand(back);
		whyNot = "the footnote could not be created";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}

	// ---- 3. the range the caller replaces ------------------------------------------------------
	// ★THE COMMAND HANDS BACK THE NOTE through its IUIDData, and the note's own thread says how far
	//   its text runs. The thread begins with the note's NUMBER (one character, which is the note
	//   itself and must not be touched) and ends with its closing return.
	TextIndex threadStart = kInvalidTextIndex;
	TextIndex threadEnd = kInvalidTextIndex;
	InterfacePtr<IUIDData> noteData(create, UseDefaultIID());
	if (noteData != nil)
	{
		InterfacePtr<ITextStoryThread> noteThread(noteData->GetRef(), UseDefaultIID());
		if (noteThread != nil)
		{
			threadStart = noteThread->GetTextStart();
			threadEnd = noteThread->GetTextEnd();
		}
	}
	if (threadStart == kInvalidTextIndex || threadEnd <= threadStart + 1)
	{
		// ⚠**THE NOTE GOES BACK TOO.** It was made, but nothing here can say where its words are,
		//   so leaving it would put an empty footnote in the document that nobody asked for and the
		//   caller has already been told could not be made.
		InterfacePtr<ICommand> back(cmds->DeleteCmd(at, 1));
		if (back != nil)
			CmdUtils::ProcessCommand(back);
		whyNot = "the new footnote's own text could not be found";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}
	outWordsFrom = threadStart + 1;		// past the number
	outWordsTo = threadEnd - 1;			// before the closing return
	return kSuccess;
}

ErrorCode KCMDeleteNoteAt(ITextModel* model, TextIndex markerAt, PMString& whyNot)
{
	if (model == nil)
		return kFailure;
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> del(cmds->DeleteCmd(markerAt, 1));
	if (del == nil || CmdUtils::ProcessCommand(del) != kSuccess)
	{
		whyNot = "the footnote's marker could not be taken out";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}
	return kSuccess;
}

// End, KCMStoryNoteEdit.cpp.
