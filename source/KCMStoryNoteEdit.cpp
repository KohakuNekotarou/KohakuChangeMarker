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
	InterfacePtr<ICommand> create(CmdUtils::CreateCommand(kCreateFootnoteCmdBoss));
	if (create == nil)
		return kFailure;
	create->SetItemList(UIDList(model));
	InterfacePtr<IRangeData> range(create, UseDefaultIID());
	if (range == nil)
		return kFailure;
	range->Set(at, at);
	if (CmdUtils::ProcessCommand(create) != kSuccess)
	{
		whyNot = "the footnote could not be created";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}

	// ---- 3. the range the caller replaces ------------------------------------------------------
	// ★THE COMMAND HANDS BACK THE NOTE through its IUIDData, and the note's own thread says how far
	//   its text runs. The thread begins with the note's NUMBER (one character, which is the note
	//   itself and must not be touched) and ends with its closing return.
	InterfacePtr<IUIDData> noteData(create, UseDefaultIID());
	if (noteData == nil)
	{
		whyNot = "the new footnote could not be found again";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}
	InterfacePtr<ITextStoryThread> noteThread(noteData->GetRef(), UseDefaultIID());
	if (noteThread == nil)
	{
		whyNot = "the new footnote has no text of its own";
		whyNot.SetTranslatable(kFalse);
		return kFailure;
	}
	const TextIndex threadStart = noteThread->GetTextStart();
	const TextIndex threadEnd = noteThread->GetTextEnd();
	if (threadEnd <= threadStart + 1)
	{
		whyNot = "the new footnote is shorter than a footnote can be";
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
