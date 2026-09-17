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

#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "TextID.h"

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
