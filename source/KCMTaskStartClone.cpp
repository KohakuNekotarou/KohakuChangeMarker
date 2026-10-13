//========================================================================================
//
//  KCMTaskStartClone.cpp
//
//  Task Start kept in memory. MODEL side. The reasoning is at the top of the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "ISession.h"
#include "PMString.h"

#include <stdio.h>
#include <time.h>

// Project includes:
#include "KCMTaskStartClone.h"
#include "KCMTaskStartSave.h"		// KCMTaskDocumentDB / KCMCanTakeTaskStartCopy - which document: the one rule
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB / KCMIsDocDBOpen
#include "KCMComparisonRun.h"		// KCMStopComparison
#include "KCMPairChoice.h"			// KCMChooseDBPair / KCMForgetChosenSourceIfDB
#include "KCMExternalSource.h"		// the lent Source: register, forget, ask
#include "KCMResourceSnapshot.h"	// KCMReadResourceList - the definitions, read from the LIVE document
#include "KCMModelNotify.h"			// KCMSayStatus

namespace {

IDataBase*		gClone = nil;		// the clone held, or nil - compared, and followed only to delete it
IDataBase*		gSource = nil;		// the document it was taken from - compared, and asked KCMIsDocDBOpen before a delete
KCMResourceList	gDefs;				// that document's definitions at that moment (the Resources mode's older side)
bool16			gHaveDefs = kFalse;
PMString		gDefsWhyNot;		// why there are none, when there are none ("source: ...")

/* "17:30:12" - the moment, for the panel's Source line. */
void ClockNow(PMString& out)
{
	time_t t = ::time(nil);
	struct tm local;
	::localtime_s(&local, &t);
	char buf[16];
	::sprintf_s(buf, sizeof(buf), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
	out = buf;
	out.SetTranslatable(kFalse);
}

/* The open document's name ("ch01.indd"), or "Untitled" when the database names none. */
void DocName(IDataBase* db, PMString& out)
{
	out = PMString("Untitled");
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	IDocument* const doc = (docList != nil && db != nil) ? docList->FindDocByDataBase(db) : nil;
	if (doc != nil)
		doc->GetName(out);
	out.SetTranslatable(kFalse);
}

/* Everything that names the clone forgets it - this holder, the Source choice and the lent registration - so that
   every liveness test in the plug-in (KCMIsDbAlive) answers "gone" BEFORE the memory is freed. */
void Forget(IDataBase* clone)
{
	gClone = nil;
	gSource = nil;
	gDefs.clear();
	gHaveDefs = kFalse;
	gDefsWhyNot.Clear();
	KCMForgetChosenSourceIfDB(clone);
	if (KCMIsExternalSource(clone))
		KCMForgetExternalSource();
}

/* ⚠★★★Deletes the clone ONLY while the document it was taken from is open (2026-08-31: DataBase::~DataBase ->
   DatabaseSnapshot::Clear -> access violation, when the document had gone first). Liveness is asked of the SOURCE,
   by KCM's one test (KCMIsDocDBOpen, a pointer comparison against the document list) - never of the clone, which
   answers "valid" even orphaned. A throw is swallowed: the callers run from menus, signals and shutdown. */
void DeleteWhileSourceOpen(IDataBase* clone, IDataBase* source, bool16 sourceKnownAlive)
{
	if (clone == nil)
		return;
	if (!sourceKnownAlive && (source == nil || !KCMIsDocDBOpen(source)))
		return;			// a leak, not a crash
	try
	{
		delete clone;
	}
	catch (...)
	{
	}
}

}	// namespace

bool16 KCMCanTakeTaskStartClone()
{
	return KCMCanTakeTaskStartCopy();
}

bool16 KCMTakeTaskStartClone(PMString& outWhyNot)
{
	outWhyNot.Clear();
	outWhyNot.SetTranslatable(kFalse);

	// 1. WHICH DOCUMENT - the chosen Target, else the active one (the one rule, KCMTaskDocumentDB), held from here.
	IDataBase* const docDB = KCMTaskDocumentDB();
	if (docDB == nil)
	{
		outWhyNot = "Task Start needs a document to take";
		return kFalse;
	}

	// 2. THE CLONE. Wrapped: the SDK holds no call to Clone(), so there is no example of how it fails.
	IDataBase* clone = nil;
	try
	{
		clone = docDB->Clone();
	}
	catch (...)
	{
		clone = nil;
	}
	if (clone == nil)
	{
		outWhyNot = "InDesign could not keep the document's state in memory.";
		return kFalse;
	}

	// 3. THE DEFINITIONS, from the LIVE document at the same moment: a clone cannot be exported, and the Resources
	//    mode compares exported definitions (50 pages took about 0.5 s, 2026-10-13). A failure here does not stop the
	//    Task Start - the Resources mode says why it has nothing to compare against.
	KCMResourceList defs;
	PMString defsWhyNot;
	const bool16 haveDefs = KCMReadResourceList(docDB, "source", defs, defsWhyNot);

	// 4. STOP: the marks on screen were made from a pair that is about to change.
	if (KCMIsArmed() && KCMArmedTargetDB() != nil)
		KCMStopComparison();

	// 5. THE PREVIOUS ONE GOES - after the stop, so nothing is drawing from it.
	KCMLetGoTaskStartClone(kTrue);

	// 6. HELD, REGISTERED, CHOSEN. ★Held BEFORE the pair is chosen: the Source slot's hook (KCMPairChoice.cpp) then
	//    sees the new clone as the one held, and lets nothing else go.
	gClone = clone;
	gSource = docDB;
	gDefs = defs;
	gHaveDefs = haveDefs;
	gDefsWhyNot = defsWhyNot;
	gDefsWhyNot.SetTranslatable(kFalse);

	PMString label("Task Start (in memory) - ");
	PMString name, clock;
	DocName(docDB, name);
	ClockNow(clock);
	label.Append(name);
	label.Append(" - ");
	label.Append(clock);
	label.SetTranslatable(kFalse);
	KCMRegisterExternalSource(clone, label);
	KCMChooseDBPair(docDB, clone);
	return kTrue;
}

bool16 KCMIsTaskStartClone(IDataBase* db)
{
	return (db != nil && db == gClone) ? kTrue : kFalse;
}

void KCMLetGoTaskStartClone(bool16 stopIfDrawing)
{
	IDataBase* const clone = gClone;
	IDataBase* const source = gSource;
	if (clone == nil)
		return;
	// A comparison drawing from it ends first: its marks were made from it.
	if (stopIfDrawing && KCMIsArmed() && KCMArmedSourceDB() == clone)
		KCMStopComparison();
	Forget(clone);
	DeleteWhileSourceOpen(clone, source, kFalse);
}

void KCMTaskStartCloneDocClosing(IDataBase* closing)
{
	if (closing == nil || gClone == nil || closing != gSource)
		return;
	IDataBase* const clone = gClone;
	Forget(clone);
	DeleteWhileSourceOpen(clone, closing, kTrue /*kBeforeCloseDoc: the document is still alive*/);
	KCMSayStatus("The Task Start kept in memory went with its document.");
}

void KCMTaskStartCloneSweep(IDocumentList* docList)
{
	if (gClone == nil || docList == nil)
		return;
	if (gSource != nil && docList->FindDocByDataBase(gSource) != nil)
		return;			// its document is still open
	// ⚠NOT DELETED: its document has gone, and a delete now is the crash of 2026-08-31.
	Forget(gClone);
}

bool16 KCMTaskStartCloneResources(KCMResourceList& out, PMString& whyNot)
{
	whyNot.Clear();
	whyNot.SetTranslatable(kFalse);
	if (gClone == nil)
	{
		whyNot = "source: no Task Start is kept in memory";
		return kFalse;
	}
	if (!gHaveDefs)
	{
		whyNot = gDefsWhyNot;	// already "source: ..." (KCMReadResourceList names the side)
		return kFalse;
	}
	out = gDefs;
	return kTrue;
}

// End, KCMTaskStartClone.cpp.
