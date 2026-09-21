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
#include "IStyleInfo.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "TextID.h"

// (Six more includes went with the restore's eight functions on 2026-09-21: the document, the two
//  style-group headers, the story thread, TextChar and TextIterator.)

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

// (⛔**EIGHT FUNCTIONS STOOD HERE AND WENT ON 2026-09-21**, every one of them the restore's:
//  KCMSnapshotChainAfter and KCMRechainAfterRemoval (the paragraphs after one taken out, and the
//  next-style chain moved along so that the styles below a removal stayed right); the style path
//  and the lookup by path; KCMApplyParagraphStyle; ParagraphPositions and KCMReadParagraphStyles;
//  KCMSnapshotChainFrom; and KCMRestoreParagraphStyles, which put the styles back in two layers
//  when a whole paragraph was written back. ★What is left is the one the IMPORT calls.
//  The reasoning they carried - why the chain is read before the write, and why a following
//  paragraph is only put back while it still wears what the take-in left it - is kept in
//  docs/ai-notes/kcm-restore-retired-2026-09-21.md.)

// End, KCMParagraphStyle.cpp.
