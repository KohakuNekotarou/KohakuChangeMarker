//========================================================================================
//
//  KCMPlacePdf.cpp -- see the header. The two bodies were KCMReport.cpp's SetPlacePage and PlacePrefsRestorer until
//  2026-10-14, carried over; what is new is the crop being the caller's, the error state cleared on a failure, and the
//  restorer holding nothing when there is nothing to give back.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IPDFPlacePrefs.h"
#include "ISession.h"
#include "IWorkspace.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"			// GlobalErrorStatePreserver
#include "PDFID.h"				// kSetPDFPlacePrefsCmdBoss

// Project includes:
#include "KCMSentence.h"		// KCMSentence - a fixed sentence, never a key
#include "KCMPlacePdf.h"

IPDFPlacePrefs* KCMQuerySessionPdfPlacePrefs()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IWorkspace> workspace(session != nil ? session->QueryWorkspace() : nil);
	InterfacePtr<IPDFPlacePrefs> prefs(workspace, UseDefaultIID());
	return prefs.forget();
}

bool16 KCMSetPdfPlacePage(int32 page1, int32 cropTo, IPDFPlacePrefs* current, PMString& why)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetPDFPlacePrefsCmdBoss));
	InterfacePtr<IPDFPlacePrefs> data(cmd, UseDefaultIID());
	if (cmd == nil || data == nil)
	{
		why = KCMSentence("the PDF place preferences command could not be assembled");
		return kFalse;
	}
	if (current != nil)
		data->CopyData(current);
	data->SetPage(page1);
	data->SetAllPages(0xffffffff, kFalse);
	data->SetCropTo(cropTo);
	data->SetTransparentBackground(kFalse);
	data->SetShowPreview(kFalse);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// left standing it would drag the caller's next command down
		why = KCMSentence("the PDF place preferences could not be set");
		return kFalse;
	}
	return kTrue;
}

KCMPdfPlacePrefsRestorer::KCMPdfPlacePrefsRestorer(IPDFPlacePrefs* current)
	: fRestore(CmdUtils::CreateCommand(kSetPDFPlacePrefsCmdBoss))
{
	InterfacePtr<IPDFPlacePrefs> keep(fRestore, UseDefaultIID());
	if (keep == nil || current == nil)
		fRestore.reset(nil);
	else
		keep->CopyData(current);
}

KCMPdfPlacePrefsRestorer::~KCMPdfPlacePrefsRestorer()
{
	if (fRestore != nil)
	{
		GlobalErrorStatePreserver errorState;		// a destructor must not change the error the caller is reporting
		CmdUtils::ProcessCommand(fRestore);
	}
}

// End, KCMPlacePdf.cpp.
