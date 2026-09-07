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
};

class KCMPageMarksCmdData : public CPMUnknown<IKCMPageMarksCmdData>
{
public:
	KCMPageMarksCmdData(IPMUnknown* boss) : CPMUnknown<IKCMPageMarksCmdData>(boss) {}
	virtual ~KCMPageMarksCmdData() {}

	virtual void Set(const std::vector<KCMPageMarks>& pages, const PMString& undoName)
	{
		fPages = pages;
		fUndoName = undoName;
	}
	virtual const std::vector<KCMPageMarks>&	GetPages() const	{ return fPages; }
	virtual const PMString&						GetUndoName() const	{ return fUndoName; }

private:
	std::vector<KCMPageMarks>	fPages;
	PMString					fUndoName;
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

	const std::vector<KCMPageMarks>& pages = data->GetPages();
	for (size_t i = 0; i < pages.size(); ++i)
	{
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
ErrorCode KCMMarksWrite(IDataBase* db, const std::vector<KCMPageMarks>& pages, const char* undoName)
{
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
	// JOINS it, which is what lets Load put back the ticks and the paws as ONE step instead of two.
	// Its destructor rolls the database back if the global error code is set, and that is what
	// turns the early return in Do() into "nothing happened".
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

	return CmdUtils::ProcessCommand(cmd);
}

// End, KCMPageMarksCmd.cpp.
