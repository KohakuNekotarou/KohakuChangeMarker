//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  See KCMParagraphStyle.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IAttributeStrand.h"
#include "ICommand.h"
#include "IDocument.h"
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "IStyleInfo.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "TextChar.h"
#include "TextID.h"
#include "TextIterator.h"

#include "KCMParagraphStyle.h"

//----------------------------------------------------------------------------------------
UID KCMParagraphStyleAt(ITextModel* model, TextIndex at)
{
	if (model == nil || at < 0)
		return kInvalidUID;
	// The paragraph strand keeps the applied style per paragraph run - the road SnpManipulateTextStyle
	// takes (QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND)).
	InterfacePtr<IAttributeStrand> strand(static_cast<IAttributeStrand*>(
		model->QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND)));
	if (strand == nil)
		return kInvalidUID;
	int32 count = 0;
	return strand->GetStyleUID(at, &count);
}

//----------------------------------------------------------------------------------------
UID KCMNextParagraphStyle(IDataBase* db, UID style)
{
	if (db == nil || style == kInvalidUID)
		return style;
	InterfacePtr<IStyleInfo> info(db, style, UseDefaultIID());
	if (info == nil)
		return style;
	const UID next = info->GetNextStyle();
	return (next == kInvalidUID) ? style : next;
}

//----------------------------------------------------------------------------------------
ErrorCode KCMApplyNextStyleAfter(ITextModel* model, TextIndex prevAt, TextIndex newStart, int32 newLength)
{
	if (model == nil || newLength <= 0)
		return kSuccess;
	IDataBase* db = ::GetDataBase(model);
	const UID previous = KCMParagraphStyleAt(model, prevAt);
	if (previous == kInvalidUID)
		return kSuccess;					// nothing to go by: the new paragraphs keep what they inherited
	const UID first = KCMNextParagraphStyle(db, previous);
	if (first == previous)
		return kSuccess;					// [Same Style] all the way down: inherited, overrides and all

	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	// ★autoNextStyle: the first paragraph gets `first`, the second first's next style, and so on - what
	//   pressing Return again and again does. replaceOverrides: the previous paragraph's hand adjustments
	//   are not carried into a paragraph of another style.
	InterfacePtr<ICommand> apply(cmds->ApplyStyleCmd(newStart, newLength, first, kParaAttrStrandBoss,
													 kTrue /*replaceOverrides*/, kTrue /*autoNextStyle*/));
	if (apply == nil)
		return kFailure;
	const ErrorCode err = CmdUtils::ProcessCommand(apply);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

//----------------------------------------------------------------------------------------
void KCMSnapshotChainAfter(ITextModel* model, TextIndex removedFrom, TextIndex removedTo, KCMChainAfter& out)
{
	out.fStarts.clear();
	out.fChained.clear();
	if (model == nil || removedTo <= removedFrom)
		return;

	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(removedFrom, &threadStart, &threadSpan));
	if (thread == nil)
		return;
	const TextIndex threadEnd = threadStart + threadSpan;		// past the thread's own last return
	if (removedTo >= threadEnd)
		return;

	IDataBase* db = ::GetDataBase(model);
	// The "before" of the first following paragraph is the one being taken out: its style is read off
	// its last character, which both removal shapes hold inside [removedFrom, removedTo).
	UID previous = KCMParagraphStyleAt(model, removedTo - 1);

	// Every paragraph after the removal: the first starts at removedTo - or one further on when the
	// character at removedTo is the removed paragraph's OWN return ("\rTEXT" leaves that return behind
	// as the paragraph before's; measured 2026-09-19: read as a paragraph of its own it was "not chained"
	// and stopped the walk at once, so nothing was ever re-styled) - and each character after a return
	// from there, except the thread's final return, which ends the last paragraph.
	TextIndex start = removedTo;
	{
		TextIterator at(model, removedTo);
		if (static_cast<int32>((*at).GetValue()) == kTextChar_CR)
			start = removedTo + 1;
	}
	if (start >= threadEnd)
		return;
	TextIterator iter(model, start);
	for (TextIndex i = start; i < threadEnd; ++i, ++iter)
	{
		const int32 cp = static_cast<int32>((*iter).GetValue());
		if (cp != kTextChar_CR)
			continue;
		const UID own = KCMParagraphStyleAt(model, start);
		out.fStarts.push_back(start);
		out.fChained.push_back((own != kInvalidUID && own == KCMNextParagraphStyle(db, previous)) ? kTrue : kFalse);
		previous = own;
		start = i + 1;
	}
}

//----------------------------------------------------------------------------------------
ErrorCode KCMRechainAfterRemoval(ITextModel* model, TextIndex removedFrom, int32 removedCount,
								 const KCMChainAfter& chain)
{
	if (model == nil || chain.fStarts.empty())
		return kSuccess;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(removedFrom, &threadStart, &threadSpan));
	if (thread == nil || removedFrom <= threadStart)
		return kSuccess;				// the first paragraph of its place went: nothing stands before the chain

	IDataBase* db = ::GetDataBase(model);
	TextIndex prevAt = removedFrom - 1;		// the last character of the paragraph now standing before the chain
	ErrorCode err = kSuccess;
	for (size_t i = 0; i < chain.fStarts.size(); ++i)
	{
		if (!chain.fChained[i])
			break;						// chosen by hand: the chain ends here, and so does the walk
		const TextIndex at = chain.fStarts[i] - removedCount;
		const UID previous = KCMParagraphStyleAt(model, prevAt);
		const UID wanted = KCMNextParagraphStyle(db, previous);
		const UID own = KCMParagraphStyleAt(model, at);
		if (wanted != kInvalidUID && own != wanted)
		{
			const ErrorCode e = KCMApplyParagraphStyle(model, at, 1, wanted, kFalse /*keep its overrides*/);
			if (e != kSuccess)
				err = e;
		}
		prevAt = at;
	}
	return err;
}

//----------------------------------------------------------------------------------------
PMString KCMParagraphStylePath(IDataBase* db, UID style)
{
	PMString path;
	path.SetTranslatable(kFalse);
	if (db == nil || style == kInvalidUID)
		return path;
	InterfacePtr<IStyleGroupHierarchy> node(db, style, IID_ISTYLEGROUPHIERARCHY);
	if (node == nil)
		return path;
	path = node->GetFullPath();
	path.SetTranslatable(kFalse);
	return path;
}

//----------------------------------------------------------------------------------------
UID KCMFindParagraphStyle(IDataBase* db, const PMString& path)
{
	if (db == nil || path.IsEmpty())
		return kInvalidUID;
	InterfacePtr<IDocument> document(db, db->GetRootUID(), UseDefaultIID());
	if (document == nil)
		return kInvalidUID;
	// The workspace's paragraph style manager - as hiddentext's HidTxtCommands asks it.
	InterfacePtr<IStyleGroupManager> styles(document->GetDocWorkSpace(), IID_IPARASTYLEGROUPMANAGER);
	if (styles == nil)
		return kInvalidUID;
	return styles->FindByName(path);
}

//----------------------------------------------------------------------------------------
ErrorCode KCMApplyParagraphStyle(ITextModel* model, TextIndex start, int32 length, UID style,
								 bool16 replaceOverrides)
{
	if (model == nil || style == kInvalidUID)
		return kFailure;
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	InterfacePtr<ICommand> apply(cmds->ApplyStyleCmd(start, length, style, kParaAttrStrandBoss,
													 replaceOverrides, kFalse /*autoNextStyle*/));
	if (apply == nil)
		return kFailure;
	const ErrorCode err = CmdUtils::ProcessCommand(apply);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

// End, KCMParagraphStyle.cpp.
