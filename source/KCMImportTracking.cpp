//========================================================================================
//
//  KCMImportTracking.cpp
//
//  See KCMImportTracking.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"
#include "ICommand.h"
#include "ISession.h"
#include "IStringData.h"
#include "ITrackChangesSettings.h"	// ITrackChangeStorySettings - on kTextStoryBoss (iid-boss-dictionary)
#include "IUserInfo.h"				// on kWorkspaceBoss (iid-boss-dictionary)
#include "IWorkspace.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"				// a failed switch's error cleared - the import's next command follows at once
#include "InCopySharedID.h"			// kSetUserNameCmdBoss, kSetRedlineTrackingCmdBoss
#include "PersistUtils.h"			// ::GetUIDRef
#include "UIDList.h"

// Project includes:
#include "KCMImportTracking.h"

const char* const kKCMImportAuthorName = "KohakuChangeMarker";

namespace
{

/* The application's user name, through its command (a workspace setting is model state too).
   ⚠NO CALLER IN THE SDK USES kSetUserNameCmdBoss - the dictionary gives only its data
    (IID_ISTRINGDATA). The item list is the workspace the name lives on; whether it is needed at all
    is measured on the application (plan 2026-09-24-kcm-import-track-changes-p1, Task 3), not known.
   ★THERE IS A HIGHER DOOR, AND IT IS NOT TAKEN ON PURPOSE (re-audit M18, 2026-09-27):
    Utils<IUserInfoUtils>()->SetInCopyUsername / GetInCopyUsername (IUserInfoUtils.h:44-55), the
    successor the deprecated IInCopyBridgeUtils names. Two reasons it is not used here:
      1. SetInCopyUsername RETURNS VOID. KCMImportAuthor puts the old name back only when the switch
         happened (fSwitched), and this command's ErrorCode is the only thing that says so.
      2. It has no caller in the SDK either, and whether it fires this same command (an undo step, the
         kSetUserNameCmdResponderService responders) is unmeasured.
    GetInCopyUsername is avoided for a third reason: its default kFullUI raises a name dialog when the
    name is empty or "Unknown User Name" (IUserInfoUtils.h:52) - IUserInfo is read directly instead. */
ErrorCode SetUserName(const PMString& name)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetUserNameCmdBoss));
	InterfacePtr<IStringData> data(cmd, IID_ISTRINGDATA);
	if (ws == nil || cmd == nil || data == nil)
		return kFailure;
	data->Set(name);
	cmd->SetItemList(UIDList(::GetUIDRef(ws)));
	// ★A FAILURE'S ERROR IS CLEARED HERE (2026-09-25, the Word round trip re-check, item 2): both switches sit between
	//   the import's own commands, and the next one must not run with it standing (CmdUtils.h:74). The caller learns
	//   of the failure from the return value.
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

/* A story's change tracking on or off - Adobe's own form, InCopyDocUtils.cpp:2356
   (source/open/components/incopyfileactions/utils). */
ErrorCode SetTracking(const UIDRef& story, bool16 on)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetRedlineTrackingCmdBoss));
	InterfacePtr<IBoolData> data(cmd, IID_IBOOLDATA);
	if (cmd == nil || data == nil)
		return kFailure;
	data->Set(on);
	cmd->SetItemList(UIDList(story));
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// the same reason as SetUserName's
	return err;
}

}	// anonymous namespace

KCMImportAuthor::KCMImportAuthor() : fSwitched(kFalse)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<IUserInfo> info(ws, UseDefaultIID());
	if (info == nil)
		return;
	fOld = info->GetUserName();
	PMString name(kKCMImportAuthorName);
	name.SetTranslatable(kFalse);
	fSwitched = (SetUserName(name) == kSuccess) ? kTrue : kFalse;
}

KCMImportAuthor::~KCMImportAuthor()
{
	if (fSwitched)
		SetUserName(fOld);
}

KCMStoryTrackingOn::KCMStoryTrackingOn(const UIDRef& story) : fStory(story), fWas(kTrue)
{
	InterfacePtr<ITrackChangeStorySettings> settings(story, UseDefaultIID());
	// ⚠A story that cannot say is treated as ALREADY tracking: then nothing is switched, and so
	//  nothing has to be put back.
	fWas = (settings != nil) ? settings->GetIsTracking() : kTrue;
	// ★A SWITCH THAT DID NOT HAPPEN IS NOT PUT BACK (2026-09-25): the story is then written untracked, and nothing was
	//   changed that the destructor would have to undo.
	if (!fWas && SetTracking(fStory, kTrue) != kSuccess)
		fWas = kTrue;
}

KCMStoryTrackingOn::~KCMStoryTrackingOn()
{
	if (!fWas)
		SetTracking(fStory, kFalse);
}
