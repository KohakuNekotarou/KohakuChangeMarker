//========================================================================================
//
//  KCMPageMarksCmd.cpp
//
//  The command that writes the ticks and the cat paws. The reasoning is in the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IDataBase.h"
#include "ISubject.h"

// General includes:
#include "CPMUnknown.h"
#include "CmdUtils.h"
#include "Command.h"
#include "ErrorUtils.h"
#include "PMString.h"
#include "UIDList.h"

// Project includes:
#include "KCMID.h"
#include "KCMMarksObserver.h"		// KCMMarksEnsureObserver -- the listener has to be there first
#include "KCMPageMarksCmd.h"
#include "KCMPageMarksDoc.h"		// KCMMarksWriteOnePage -- the label format lives over there
#include "KCMDiag.h"				// KCM_DIAG_FAULT("marks-write-fail") - a failed write, measured (test builds only)
#include "KCMModelNotify.h"			// KCMSayStatus - the reader's door says a failure

//========================================================================================
// The parameter interface.
//
//  It is declared HERE rather than in a header of its own because nothing outside this file has
//  any business filling it in: the only door is KCMMarksWrite() at the bottom, and a data
//  interface other files can reach is an invitation to build the command by hand and skip the
//  observer. (Adobe splits IBPIData into a header because two commands share it; ours has one
//  user.)
//
//  Non-persistent, deliberately. It is a PARAMETER -- what the command was asked to do -- and not
//  document data. The document data is the script labels the command writes.
//========================================================================================
class IKCMPageMarksCmdData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKCMPAGEMARKSCMDDATA };

	virtual void								Set(const std::vector<KCMPageMarks>& pages,
													const PMString& undoName) = 0;
	virtual const std::vector<KCMPageMarks>&	GetPages() const = 0;
	virtual const PMString&						GetUndoName() const = 0;

	/** Do()'s one answer back (2026-10-30): did it start writing before it stopped? kFalse until it does - a
		failure before that changed nothing. KCMMarksWrite hands it to its caller. */
	virtual void								SetWroteSome() = 0;
	virtual bool16								WroteSome() const = 0;
};

class KCMPageMarksCmdData : public CPMUnknown<IKCMPageMarksCmdData>
{
public:
	KCMPageMarksCmdData(IPMUnknown* boss) : CPMUnknown<IKCMPageMarksCmdData>(boss), fWroteSome(kFalse) {}
	virtual ~KCMPageMarksCmdData() {}

	virtual void Set(const std::vector<KCMPageMarks>& pages, const PMString& undoName)
	{
		fPages = pages;
		fUndoName = undoName;
		fWroteSome = kFalse;
	}
	virtual const std::vector<KCMPageMarks>&	GetPages() const	{ return fPages; }
	virtual const PMString&						GetUndoName() const	{ return fUndoName; }
	virtual void								SetWroteSome()		{ fWroteSome = kTrue; }
	virtual bool16								WroteSome() const	{ return fWroteSome; }

private:
	std::vector<KCMPageMarks>	fPages;
	PMString					fUndoName;
	bool16						fWroteSome;
};

CREATE_PMINTERFACE(KCMPageMarksCmdData, kKCMPageMarksCmdDataImpl)

//========================================================================================
// The command.
//========================================================================================
class KCMPageMarksCmd : public Command
{
public:
	KCMPageMarksCmd(IPMUnknown* boss) : Command(boss) {}
	virtual ~KCMPageMarksCmd() {}

	/** Writing labels allocates, and half a written set of marks is worse than none. */
	bool16 LowMemIsOK() const { return kFalse; }

protected:
	virtual void		Do();
	virtual void		DoNotify();
	virtual PMString*	CreateName();
};

CREATE_PMINTERFACE(KCMPageMarksCmd, kKCMSetPageMarksCmdImpl)

/* The pages come from the DATA interface, not from fItemList.
   Both hold the same UIDs -- KCMMarksWrite() below builds the item list out of the very same
   vector -- but they answer different questions. fItemList is what the framework needs in order to
   know which database the step belongs to and which objects it touched; the data interface is what
   each page should CARRY, and only it has the paws' coordinates. Reading the pages from one and
   the payload from the other by matching index would make the two orders a thing that can drift.
*/
void KCMPageMarksCmd::Do()
{
	InterfacePtr<IKCMPageMarksCmdData> data(this, UseDefaultIID());
	if (data == nil)
	{
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
		return;
	}

	IDataBase* const db = fItemList.GetDataBase();
	if (db == nil)
	{
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
		return;
	}

	// A test build can make the whole write fail here, as a page it cannot read makes it fail: what
	// the callers say then is measured (KCMDiag.h, marks-write-fail).
	if (KCM_DIAG_FAULT("marks-write-fail"))
	{
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
		return;
	}

	const std::vector<KCMPageMarks>& pages = data->GetPages();

	// ★EVERY PAGE IS ASKED BEFORE ANY IS WRITTEN (2026-10-30). A page the write cannot reach (deleted since the list
	//   was made - M5-D1 handed the clear one) now stops it with the document untouched, instead of after the pages
	//   before it were written - which only a rollback could take back, and only while the error stays set
	//   (KCMMarksWriteForReader in the header says why that is not always so).
	for (size_t i = 0; i < pages.size(); ++i)
	{
		if (!KCMMarksPageWritable(db, pages[i].fPage))
		{
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
			return;
		}
	}

	for (size_t i = 0; i < pages.size(); ++i)
	{
		// A test build can make the write stop at its SECOND page, after the first went through (KCMDiag.h,
		// marks-write-fail-second): the half-done write, measured.
		if (i == 1 && KCM_DIAG_FAULT("marks-write-fail-second"))
		{
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
			return;
		}
		data->SetWroteSome();		// from here on the document may have changed
		if (KCMMarksWriteOnePage(db, pages[i].fPage, pages[i].fCheck, pages[i].fPaws) != kSuccess)
		{
			// Stop at the first failure and let the sequence roll the whole thing back. Half of a
			// "tick these five pages" is a state the reader never asked for, and the rollback costs
			// nothing -- the sequence is already open ([[command-sequence-rollback-on-error]]).
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
			return;
		}
	}
}

/* THE HALF THAT MAKES UNDO WORK. The header carries the two quotations that say why the
   notification has to be a ModelChange raised on a subject that lives inside a database with undo
   support. The document's own subject is such a subject; the application's, where the model half's
   other notifications go (KCMModelNotify.h), is not.
*/
void KCMPageMarksCmd::DoNotify()
{
	IDataBase* const db = fItemList.GetDataBase();
	if (db == nil)
		return;

	const UID root = db->GetRootUID();
	if (root == kInvalidUID)
		return;

	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
	if (subject == nil)
		return;

	// Nil lazy notification data, on purpose: the observer re-reads the whole document. What
	// changed is deliberately NOT carried, because at undo the runtimes either queue nil or ask the
	// Do data to clone itself "swapping the adds and deletes", and IObserver.h:101-106 requires
	// every observer to cope with nil regardless ("the observer would usually refresh its entire
	// state by re-examining all objects of interest"). One path that is always right beats two of
	// which one is hardly ever exercised.
	subject->ModelChange(kKCMSetPageMarksCmdBoss, IID_IKCMPAGEMARKS, this);
}

/* What Edit > Undo says. The caller chose the words; this hands them over.
   @warning untranslatable, like every other string the model half produces (KCMModelNotify.h).
*/
PMString* KCMPageMarksCmd::CreateName()
{
	InterfacePtr<IKCMPageMarksCmdData> data(this, UseDefaultIID());
	PMString* name = new PMString(data != nil ? data->GetUndoName() : PMString("Change Marks"));
	name->SetTranslatable(kFalse);
	return name;
}

//========================================================================================
// The only door.
//========================================================================================
ErrorCode KCMMarksWrite(IDataBase* db, const std::vector<KCMPageMarks>& pages, const char* undoName,
                        bool16* outWroteSome)
{
	if (outWroteSome != nil)
		*outWroteSome = kFalse;
	if (db == nil)
		return kFailure;
	if (pages.empty())
		return kSuccess;		// nothing asked for, and no empty step left on the undo stack

	// THE LISTENER GOES ON BEFORE THE COMMAND RUNS, not after. The observer is what puts the
	// session store back, so a command that ran while nobody was attached would write the document
	// and leave the screen showing the old marks -- which the reader would meet as "the first tick
	// after opening does nothing".
	KCMMarksEnsureObserver(db);

	PMString name(undoName);
	name.SetTranslatable(kFalse);

	// SequenceContext rather than SequencePtr: when the caller is already inside a sequence this
	// JOINS it, so the write is part of that sequence - a script's ENTIRE_SCRIPT step holds it, and a
	// sequence outside that is aborted takes it back with everything else. A SequencePtr would close
	// it on its own, and an abort outside could no longer reach it (KBS measured that;
	// memory command-sequence-rollback-on-error). (Load, which put back the ticks and the paws as ONE
	// step this way, went on 2026-09-07.)
	// Its destructor rolls the database back if the global error code is set, and that is what
	// turns the early return in Do() into "nothing happened" - when it opened the sequence itself;
	// joined, the rollback waits for the sequence outside (KCMMarksWriteForReader in the header).
	CmdUtils::SequenceContext seq(&name);

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kKCMSetPageMarksCmdBoss));
	if (cmd == nil)
		return kFailure;

	InterfacePtr<IKCMPageMarksCmdData> data(cmd, UseDefaultIID());
	if (data == nil)
		return kFailure;

	UIDList items(db);
	for (size_t i = 0; i < pages.size(); ++i)
		if (pages[i].fPage != kInvalidUID)
			items.Append(pages[i].fPage);
	if (items.Length() == 0)
		return kSuccess;

	cmd->SetItemList(items);
	data->Set(pages, name);

	const ErrorCode result = CmdUtils::ProcessCommand(cmd);
	if (outWroteSome != nil)
		*outWroteSome = data->WroteSome();
	return result;
}

//========================================================================================
// The door for the reader's writes (declared in KCMPageMarksCmd.h, where the reasoning is).
//========================================================================================
ErrorCode KCMMarksWriteForReader(IDataBase* db, const std::vector<KCMPageMarks>& pages, const char* undoName)
{
	bool16 wroteSome = kFalse;
	const ErrorCode result = KCMMarksWrite(db, pages, undoName, &wroteSome);
	if (result != kSuccess)
	{
		KCMSayStatus("Could not write the marks into the document.");
		if (!wroteSome)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// said, and nothing changed: nothing for any sequence to roll back
	}
	return result;
}

// End, KCMPageMarksCmd.cpp.
