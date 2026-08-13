//========================================================================================
//
//  KESCMFacades.cpp
//
//  The boundary interfaces the UI is allowed to use, implemented as thin forwarders to the
//  model's internal functions.
//
//  Created 2026-08-13 for the model/UI split (Stage 1). Stage 1 Task 11 puts the first one
//  here (IKESCMCompareFacade); Tasks 12-15 add the other four to this same file.
//
//  These bodies deliberately contain no logic. Every one of them forwards to a function that
//  already existed and already worked; the point of this file is to give those functions an
//  address the other plug-in can reach. Putting logic here would mean the same decision lived
//  in two places.
//
//  All of them are AddIn'd to kUtilsBoss (see KESCM.fr), so the UI reaches them with
//  Utils<IKESCMxxx>(). The implementations are our own -- adding somebody else's stock
//  implementation to an existing boss is how you collide with another vendor's plug-in and
//  fail to load, and the unit of collision is the ImplementationID, not the IID.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "CPMUnknown.h"

// Project includes:
#include "KESCMID.h"
#include "IKESCMCompareFacade.h"
#include "KESCMComparisonRun.h"		// ToggleStartStop / Stop / StartFor / CanStart / print marks
#include "KESCMCore.h"				// MarkChanges / ClearMarks / DoSetPrintMarks / getters
#include "KESCMPeek.h"				// armed docs alive / peek / RefreshSelectedPages / base opacity
#include "KESCMModelNotify.h"		// GetSessionStatus
#include "KESCMOversetApply.h"		// ApplyOversetForDoc / OversetScanTargetDB
#include "KESCMHideUnchanged.h"		// the Hide Unchanged toggle and its state

//========================================================================================
// KESCMCompareFacade -- IKESCMCompareFacade
//
// ★The interface carries six methods the plan's draft did not have. They were found by
// grepping for the actual callers before writing this file (Global Constraints: "check the
// move table against the real code"): HideUnchangedToggle / GetHideUnchangedOn /
// GetOversetScanTargetDB are all called from KESCMActionComponent.cpp, and ArmedDocsAlive /
// ShowPeekUnderMouse / GetBaseScreenOpacity from KESCMPeekGesture.cpp, KESCMCmykCursor.cpp
// and KESCMActionComponent.cpp -- every one of them a UI-side file. Left out, six calls would
// have kept crossing the boundary as free functions.
//========================================================================================
class KESCMCompareFacade : public CPMUnknown<IKESCMCompareFacade>
{
public:
	KESCMCompareFacade(IPMUnknown* boss) : CPMUnknown<IKESCMCompareFacade>(boss) {}

	virtual void		ToggleStartStop()		{ KESCMToggleStartStop(); }
	virtual void		StopComparison()		{ KESCMStopComparison(); }
	virtual void		StartComparisonFor(IDocument* target, IDocument* source)
													{ KESCMStartComparisonFor(target, source); }
	virtual bool16		CanStartComparison()	{ return KESCMCanStartComparison(); }

	virtual bool16		IsArmed()				{ return KESCMIsArmed(); }
	virtual IDataBase*	GetArmedTargetDB()		{ return KESCMArmedTargetDB(); }
	virtual IDataBase*	GetArmedSourceDB()		{ return KESCMArmedSourceDB(); }

	virtual ErrorCode	MarkChanges(IDataBase* targetDB, IDataBase* sourceDB,
								PMString& outReport, bool16 allowIncremental)
								{ return KESCMDoMarkChangesDoc(targetDB, sourceDB, outReport, allowIncremental); }
	virtual bool16		RefreshSelectedPages(int32* outPages, int32* outChanged,
								bool16* outCancelled, int32* outFailed)
								{ return KESCMRefreshComparisonForSelectedPages(outPages, outChanged, outCancelled, outFailed); }
	virtual bool16		RefreshComparisonAvailable()	{ return KESCMRefreshComparisonAvailable(); }
	virtual void		ClearMarks(IDataBase* db)		{ KESCMDoClearMarks(db); }

	virtual void		SetPrintMarks(bool16 printFlag, bool16 opacity25Flag, IDataBase* db)
													{ KESCMDoSetPrintMarks(printFlag, opacity25Flag, db); }
	virtual void		TogglePrintMarks()		{ KESCMTogglePrintMarks(); }
	virtual void		SetMarkOpacity25(bool16 op25)	{ KESCMSetMarkOpacity25(op25); }
	virtual bool16		GetPrintMarks()			{ return KESCMGetPrintMarks(); }
	virtual bool16		GetMarkOpacity25()		{ return KESCMGetMarkOpacity25(); }

	virtual void		GetSessionStatus(PMString& out)	{ KESCMGetSessionStatus(out); }

	virtual bool16		ArmedDocsAlive()		{ return KESCMArmedDocsAlive(); }
	virtual void		ShowPeekUnderMouse(IDataBase* targetDB, IDataBase* sourceDB)
													{ KESCMPeekShowUnderMouse(targetDB, sourceDB); }
	virtual PMReal		GetBaseScreenOpacity()	{ return KESCMBaseScreenOpacity(); }

	virtual void		ApplyOversetForDoc(IDataBase* db)	{ KESCMApplyOversetForDoc(db); }
	virtual IDataBase*	GetOversetScanTargetDB()	{ return KESCMOversetScanTargetDB(); }

	virtual void		ResetHideUnchanged(bool16 restoreSpreads)
													{ KESCMResetHideUnchanged(restoreSpreads); }
	virtual IDataBase*	GetHideUnchangedDB()	{ return KESCMGetHideUnchangedDB(); }
	virtual IDataBase*	GetHideUnchangedSrcDB()	{ return KESCMGetHideUnchangedSrcDB(); }
	virtual void		HideUnchangedToggle()	{ KESCMHideUnchangedToggle(); }
	virtual bool16		GetHideUnchangedOn()	{ return KESCMGetHideUnchangedOn(); }
};

CREATE_PMINTERFACE(KESCMCompareFacade, kKESCMCompareFacadeImpl)

// End of KESCMFacades.cpp.
