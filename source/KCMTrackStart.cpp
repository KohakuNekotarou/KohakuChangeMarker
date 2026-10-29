//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - "Compare with Tracked Changes". See KCMTrackStart.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IDataBase.h"
#include "PMString.h"
#include "WideString.h"

#include "KCMSentence.h"		// KCMSentence - a fixed sentence, never a key
#include "KCMBoundaryID.h"			// kKCMMarksRebuiltMessage / kKCMTargetToFrontMessage
#include "KCMComparisonRun.h"		// KCMStartComparisonOn / KCMStopComparison
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMBeginTrackListing / KCMEndTrackListing
#include "KCMDiag.h"				// KCM_DIAG_LOG - test builds only
#include "KCMModelNotify.h"			// KCMNotify / KCMGetSessionStatus
#include "KCMTaskStartSave.h"		// KCMTaskDocumentDB - which document
#include "KCMTrackStart.h"

namespace
{
// The last line of the status line - after a Start, the comparison's own report line ("tracked changes=N authors=M",
// KCMCore.cpp). Carried into this road's sentence because the caller puts that sentence on the same line, over it -
// the reason the import carries its start's words too (KCMStoryTextImport.cpp).
PMString LastStatusLine()
{
	PMString status;
	KCMGetSessionStatus(status);
	const WideString all(status);
	WideString line;
	for (WideString::const_iterator it = all.begin(); it != all.end(); ++it)
	{
		if (*it == 0x0A || *it == 0x0D)
			line.Clear();
		else
			line.Append(*it);
	}
	PMString out(line);
	out.SetTranslatable(kFalse);
	return out;
}
}	// namespace

bool16 KCMCanStartTrackChanges()
{
	return (KCMTaskDocumentDB() != nil) ? kTrue : kFalse;
}

bool16 KCMStartTrackChanges(PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);

	// 1. WHICH DOCUMENT - decided first and held (RUN-65): the stop below can bring another window forward.
	IDataBase* const docDB = KCMTaskDocumentDB();
	if (docDB == nil)
	{
		outMessage = KCMSentence("Compare with Tracked Changes needs a document");
		return kFalse;
	}

	// 2. STOP a running comparison - a Track listing too (pressed again = read again, on the document named now). The
	//    Stop lowers the listing (KCMStopComparison's first step), so the setting it leaves is the one this listing goes
	//    back to.
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		KCMStopComparison();

	// 3. THE LISTING: raised, then the ordinary start on the Target alone - the procedure every Start runs, so the
	//    reading, its bar and its Cancel, the marks and the arming are one road (KCMStartComparisonOn; a nil Source is
	//    a Track listing's alone - KCMModeNeedsSource).
	KCMBeginTrackListing();
	const bool16 started = KCMStartComparisonOn(docDB, nil);
	KCM_DIAG_LOG("track start: started=%d armed=%d", (int)started, (int)KCMIsArmed());
	if (!started || !KCMIsArmed())
	{
		// A Cancel at the reading's bar, or a failure: nothing is armed, so the listing goes, and the panel is drawn in
		// the mode the reader had chosen again (the tab, the heading - the start's own notification came before this).
		KCMEndTrackListing();
		KCMNotify(kKCMMarksRebuiltMessage);
		PMString startSaid;
		KCMGetSessionStatus(startSaid);
		outMessage = KCMSentence("The Track listing did not start");
		if (startSaid.CharCount() > 0)
		{
			outMessage.Append(" (");
			outMessage.Append(startSaid);
			outMessage.Append(")");
		}
		outMessage.Append(".");
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// 4. The Target to the front (the import's message, KCMStoryTextImport.cpp), and the sentence.
	KCMNotify(kKCMTargetToFrontMessage);
	outMessage = KCMSentence("Track: ");
	outMessage.Append(LastStatusLine());
	outMessage.SetTranslatable(kFalse);
	return kTrue;
}

// End, KCMTrackStart.cpp.
