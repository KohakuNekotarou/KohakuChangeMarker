//========================================================================================
//
//  KCMPairChoice.cpp
//
//  The chosen pair, and the resolver that says which two a comparison runs on.
//
//  MODEL side. The two stages - resolve without opening, realise by opening - are explained in
//  KCMPairChoice.h, and the reason they are two is the flyout's grey state.
//
//  ★**Moved here from KCMComparisonRun.cpp on 2026-09-21**, with the file choices added. The
//  bodies below are that file's, carried over; what is new is the file end of each slot.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"				// GetExecutionContextSession -- nil during shutdown, so the type is spelled out
#include "IApplication.h"			// QueryApplication
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "FileUtils.h"			// DoesFileExist -- all a file choice is asked before a Start
#include "PersistUtils.h"			// ::GetUIDRef
#include "PMString.h"
#include "SDKFileHelper.h"		// GetPath -- a file choice shows its path on the panel

// Project includes:
#include "KCMPairChoice.h"
#include "KCMCore.h"				// KCMActiveDoc / KCMActiveDocDB / KCMArmedSourceDB
#include "KCMExternalSource.h"	// the lent Source: KCMIsExternalSource / KCMForgetExternalSource / KCMIsDbAlive
#include "KCMOrigin.h"			// ⛔the origin, until it goes

//----------------------------------------------------------------------------------------
// The slots
//
// **Databases, not documents, and never dereferenced.** The pointer is only ever handed to
// IDocumentList::FindDocByDataBase, which is how the rest of this plug-in asks whether a
// database is still open (KCMArmedDocsAlive, KCMHandleDocsClosed). A closed document's
// IDataBase may already be freed and its address reused, so a raw IDocument* held across a
// close would be worse, not better ([[uidref-reuse-after-close]]).
//
// **The address-reuse window is closed at the other end**: kAfterCloseDoc runs
// KCMForgetChosenDocsThatClosed the moment a document goes, so a stale pointer does not
// survive long enough for a newly opened document to be given its address. The liveness test
// inside KCMLiveChosenDoc below is the second line, not the first.
//----------------------------------------------------------------------------------------

static IDataBase* sChosenTargetDB = nil;
static IDataBase* sChosenSourceDB = nil;

// ⛔The chosen Source could be THE ORIGIN (KCMOrigin.h) rather than a database - a third kind of
// Source with nothing to point at until a comparison rehydrated it, so the choice was a flag.
// It goes with the origin itself.
static bool16 sChosenSourceIsOrigin = kFalse;

// ★**THE FILE CHOICES** (2026-09-21). A file is chosen with no document behind it: a Task Start
// saves a copy and names it, and Start is what opens one.
// ⚠**They do not fall when a document closes** - see KCMForgetChosenDocsThatClosed.
// ⚠**One end is never both kinds at once**: every setter below clears the other kind on its own
//   end, so "which kind is this end" has one answer rather than a precedence rule.
static IDFile sChosenTargetFile;
static IDFile sChosenSourceFile;
static bool16 sTargetIsFile = kFalse;
static bool16 sSourceIsFile = kFalse;

/*	Does a chosen file still exist?
	★**THIS RUNS EVERY TIME THE FLYOUT IS OPENED** (through KCMCanStartComparison), so it asks the
	  one cheap question and reads nothing else about the file.
*/
static bool16 KCMPairFileExists(const IDFile& file)
{
	return FileUtils::DoesFileExist(file);
}

// The document `db` names, or nil when it is not (or no longer) an open document.
// Takes the list rather than fetching it so that the close sweep, which already holds one, can
// use the same test.
static IDocument* KCMLiveChosenDoc(IDataBase* db, IDocumentList* docList)
{
	if (db == nil || docList == nil)
		return nil;
	return docList->FindDocByDataBase(db);
}

// The same test for callers that have no list in hand. nil during the shutdown sequence, which
// is the right answer: with no session there is no way to judge liveness at all.
static IDocument* KCMLiveChosenDoc(IDataBase* db)
{
	if (db == nil)
		return nil;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	return KCMLiveChosenDoc(db, docList);
}

// The first open document that is not `target` = the Source (the older version).
//
// ★**"First" is IDocumentList's order, which is the order the documents were OPENED -- and it is
//   NOT the order scripting reports.** app.documents is most-recently-active first, so a test
//   written against the DOM predicts the wrong Source. Measured 2026-08-31: with the DOM listing
//   third / new / old and `third` chosen as the Target, this returned `old` -- the one opened
//   earliest of the remaining two, where the DOM's own "first other" would have been `new`.
//   [[document-activation-is-presentation]] is the same trap for "which document is in front";
//   this is its ordering half.
//
// ★**nil `target` means "the first open document", and that case is now reachable**: a Target
//   chosen as a FILE has no document until Start opens one, so there is nothing to exclude. The
//   loop already did the right thing with nil - no line was needed for it, and this note is here
//   so that nobody adds one.
//
// ⚠**`d != target` is a pointer comparison on purpose, and KCMIsSameDoc is deliberately NOT used
//   here.** That function answers "are these two databases one document" -- the question for a
//   pair that reached the caller by two different roads (KCMToggleStartStop, where a clone
//   database is possible). Here both sides come off the SAME IDocumentList within one call, so
//   the question is not identity but "skip this element", and one document has one IDocument*.
//   Measured 2026-08-31: a Target chosen through FindDocByDataBase was correctly skipped by the
//   pointer GetNthDoc handed back. ⇒ Two comparisons, two questions; do not fold them into one.
static IDocument* KCMFirstOtherDoc(IDocument* target)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession() ? GetExecutionContextSession()->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return nil;
	const int32 n = docList->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* d = docList->GetNthDoc(i);
		if (d != nil && d != target)
			return d;
	}
	return nil;
}

//----------------------------------------------------------------------------------------
// The resolver (declared in KCMPairChoice.h)
//----------------------------------------------------------------------------------------

bool16 KCMResolveComparisonPair(KCMPairEnd& outTarget, KCMPairEnd& outSource)
{
	outTarget = KCMPairEnd();
	outSource = KCMPairEnd();

	// ⛔THE ORIGIN WINS while it is chosen (Task Start as it was): the Target is the document it
	//  was taken from, and the Source is not a database at all. The callers that start ask
	//  KCMChosenSourceIsOrigin FIRST and go to KCMOriginCompare; here the pair is reported as
	//  resolvable with an empty Source, which is what the menu's grey state needs to know.
	if (KCMChosenSourceIsOrigin())
	{
		outTarget.fDB = KCMOriginDocDB();
		return (outTarget.fDB != nil) ? kTrue : kFalse;
	}

	// The Target: a file choice, then a chosen document, then the active document.
	if (sTargetIsFile)
	{
		outTarget.fFile = sChosenTargetFile;
		outTarget.fIsFile = kTrue;
	}
	else
	{
		IDocument* target = KCMLiveChosenDoc(sChosenTargetDB);
		if (target == nil)
			target = KCMActiveDoc();
		outTarget.fDB = (target != nil) ? ::GetUIDRef(target).GetDataBase() : nil;
	}

	// The Source: a file choice, then the lent database, then a chosen document, then the first
	// open document that is not the Target.
	//
	// ★**THE LENT SOURCE WINS while it is chosen**: that is what lets the flyout's own Start
	//  compare against the task-start copy again after a Stop, exactly as it would against a
	//  chosen document. It stops being chosen when the lender releases it
	//  (KCMReleaseExternalSource) or when "Set as Source" names a real document instead.
	if (sSourceIsFile)
	{
		outSource.fFile = sChosenSourceFile;
		outSource.fIsFile = kTrue;
	}
	else if (KCMIsExternalSource(sChosenSourceDB))
	{
		outSource.fDB = sChosenSourceDB;
	}
	else
	{
		IDocument* source = KCMLiveChosenDoc(sChosenSourceDB);
		if (source == nil)
		{
			// ⚠**The automatic Source excludes THE TARGET** - and a Target chosen as a file has no
			//  document yet, so there is nothing to exclude and the first open document will do.
			//  KCMFirstOtherDoc takes nil for exactly that.
			InterfacePtr<IDocument> targetDoc(
				(!outTarget.fIsFile && outTarget.fDB != nil) ? outTarget.fDB : nil,
				(!outTarget.fIsFile && outTarget.fDB != nil) ? outTarget.fDB->GetRootUID() : kInvalidUID,
				UseDefaultIID());
			source = KCMFirstOtherDoc(targetDoc);
		}
		outSource.fDB = (source != nil) ? ::GetUIDRef(source).GetDataBase() : nil;
	}

	const bool16 targetOK = outTarget.fIsFile ? KCMPairFileExists(outTarget.fFile) : (outTarget.fDB != nil);
	const bool16 sourceOK = outSource.fIsFile ? KCMPairFileExists(outSource.fFile) : (outSource.fDB != nil);
	return (targetOK && sourceOK) ? kTrue : kFalse;
}

//----------------------------------------------------------------------------------------
// Realising an end (declared in KCMPairChoice.h)
//----------------------------------------------------------------------------------------

bool16 KCMRealisePairEnd(const KCMPairEnd& end, IDataBase*& outDB, PMString& why)
{
	why.Clear();
	outDB = nil;

	if (!end.fIsFile)
	{
		outDB = end.fDB;
		return (outDB != nil) ? kTrue : kFalse;
	}

	// ⬜**THE OPENING GOES HERE** (the next step of the 2026-09-21 rework). Nothing chooses a file
	//   yet, so no caller can reach this line; it answers rather than asserting so that the step
	//   that adds the first file chooser cannot be taken without this one.
	why = PMString("The chosen file cannot be opened yet.");
	why.SetTranslatable(kFalse);
	return kFalse;
}

//----------------------------------------------------------------------------------------
// The chosen pair (declared in KCMPairChoice.h)
//----------------------------------------------------------------------------------------

// **The active document is resolved here, on the model side.** The flyout item that calls this
// has no business naming a document -- IActiveContext::GetContextDocument is what "the active
// document" means in this plug-in (KCMActiveDoc), and asking it in one place is what keeps the
// menu and the comparison agreeing about which document that is
// ([[document-activation-is-presentation]] -- GetNthDoc(0) and GetFrontDocument each mean
// something else).
//   **Asked through KCMActiveDocDB, not KCMActiveDoc plus a GetUIDRef written out here**: that
//   pair IS KCMActiveDocDB (KCMCore.cpp), and it is what the UI's UpdateActionStates already goes
//   through the facade to reach (GetActiveDocDB) when it decides whether to grey these two items.
//   Spelled out a second time, the greying and the setting would be two answers to one question.
//
// **Setting the same document as both is allowed.** The reader may well want to point at one
// document twice while working out which is which; what refuses is the Start
// (KCMToggleStartStop), where a comparison of a document against itself is meaningless.
bool16 KCMSetChosenTargetToActive()
{
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
		return kFalse;			// the flyout greys the item in this case; this guards a document closing while the menu stands open
	sChosenTargetDB = db;
	sTargetIsFile = kFalse;		// ★a document replaces a file choice on the same end
	return kTrue;
}

bool16 KCMSetChosenSourceToActive()
{
	IDataBase* db = KCMActiveDocDB();
	if (db == nil)
		return kFalse;
	// A real document replaces the lent Source. Its registration goes with it -- unless a
	// comparison is still drawing from it, in which case the lender's Release ends it later
	// (KCMExternalSource.h: registered while chosen OR armed).
	if (KCMIsExternalSource(sChosenSourceDB) && KCMArmedSourceDB() != sChosenSourceDB)
		KCMForgetExternalSource();
	// ⛔A real document replaces the origin as well, and the origin is released with the choice.
	if (sChosenSourceIsOrigin)
	{
		sChosenSourceIsOrigin = kFalse;
		KCMReleaseOrigin();
	}
	sChosenSourceDB = db;
	sSourceIsFile = kFalse;		// ★a document replaces a file choice on the same end
	return kTrue;
}

void KCMChooseDBPair(IDataBase* targetDB, IDataBase* sourceDB)
{
	// ⛔The origin gives way, exactly as it does to "Set as Source". Left standing, the resolver
	//   would go on preferring it over the pair chosen right here.
	if (sChosenSourceIsOrigin)
	{
		sChosenSourceIsOrigin = kFalse;
		KCMReleaseOrigin();
	}
	sChosenTargetDB = targetDB;
	sChosenSourceDB = sourceDB;
	sTargetIsFile = kFalse;		// ★one end, one kind of choice
	sSourceIsFile = kFalse;
}

void KCMForgetChosenSourceIfDB(IDataBase* db)
{
	// ⚠Compared, never dereferenced - the caller is on its way to freeing it.
	if (db != nil && sChosenSourceDB == db)
		sChosenSourceDB = nil;
}

IDataBase* KCMChosenTargetDB()	{ return (KCMLiveChosenDoc(sChosenTargetDB) != nil) ? sChosenTargetDB : nil; }

// ★THE SOURCE MAY BE THE LENT DATABASE (KCMExternalSource.h), which is in no document list: it is
//  "live" for exactly as long as it is registered, and the lender's Release is what ends that.
IDataBase* KCMChosenSourceDB()
{
	if (KCMIsExternalSource(sChosenSourceDB))
		return sChosenSourceDB;
	return (KCMLiveChosenDoc(sChosenSourceDB) != nil) ? sChosenSourceDB : nil;
}

//----------------------------------------------------------------------------------------
// The file choices (declared in KCMPairChoice.h)
//----------------------------------------------------------------------------------------

void KCMSetChosenTargetFile(const IDFile& file)
{
	sChosenTargetFile = file;
	sTargetIsFile = kTrue;
	sChosenTargetDB = nil;		// ★one end, one kind of choice
}

void KCMSetChosenSourceFile(const IDFile& file)
{
	// A file replaces whatever stood on this end, of whichever kind - the same courtesies "Set as
	// Source" pays when a document replaces them.
	if (KCMIsExternalSource(sChosenSourceDB) && KCMArmedSourceDB() != sChosenSourceDB)
		KCMForgetExternalSource();
	if (sChosenSourceIsOrigin)
	{
		sChosenSourceIsOrigin = kFalse;
		KCMReleaseOrigin();
	}
	sChosenSourceFile = file;
	sSourceIsFile = kTrue;
	sChosenSourceDB = nil;		// ★one end, one kind of choice
}

bool16 KCMChosenTargetFile(IDFile& out)
{
	if (!sTargetIsFile)
		return kFalse;
	out = sChosenTargetFile;
	return kTrue;
}

bool16 KCMChosenSourceFile(IDFile& out)
{
	if (!sSourceIsFile)
		return kFalse;
	out = sChosenSourceFile;
	return kTrue;
}

void KCMChosenTargetFileLabel(PMString& out)
{
	out.Clear();
	if (!sTargetIsFile)
		return;
	SDKFileHelper helper(sChosenTargetFile);
	out = helper.GetPath();
	out.SetTranslatable(kFalse);
}

void KCMChosenSourceFileLabel(PMString& out)
{
	out.Clear();
	if (!sSourceIsFile)
		return;
	SDKFileHelper helper(sChosenSourceFile);
	out = helper.GetPath();
	out.SetTranslatable(kFalse);
}

//----------------------------------------------------------------------------------------
// Closing and clearing (declared in KCMPairChoice.h)
//----------------------------------------------------------------------------------------

// **Each choice is judged on its own**, so closing one of the two documents leaves the other one
// chosen: that is the whole point of stating the pair rather than inferring it. The pointers are
// compared, never dereferenced.
//
// ★★**THE FILE CHOICES ARE NOT TOUCHED HERE, AND THAT IS DELIBERATE.** A Task Start copy whose
//   window the reader closes is still on disk, so the next Start opens it again. Dropping the
//   choice would make closing a window quietly undo the Task Start.
void KCMForgetChosenDocsThatClosed(IDocumentList* docList)
{
	if (docList == nil)
		return;					// no way to judge liveness; the choices stay, and KCMLiveChosenDoc still guards every read
	if (sChosenTargetDB != nil && docList->FindDocByDataBase(sChosenTargetDB) == nil)
		sChosenTargetDB = nil;
	// KCMIsDbAlive, not the bare list test: the lent Source is in no list and must survive an
	// unrelated document closing. Its own end is the lender's Release, never this sweep.
	if (sChosenSourceDB != nil && !KCMIsDbAlive(docList, sChosenSourceDB))
		sChosenSourceDB = nil;
	// ⛔The origin goes with its document (the user's rule), and the choice with the origin.
	KCMForgetOriginIfDocClosed(docList);
	if (sChosenSourceIsOrigin && !KCMHasOrigin())
		sChosenSourceIsOrigin = kFalse;
}

// The model's Shutdown drops everything, in the same slot and for the same reason as the peek's
// armed state (KCMPeekStartup::Shutdown): left standing, a kAfterCloseDoc responder arriving
// after shutdown reaches KCMForgetChosenDocsThatClosed and weighs a stale pointer against the
// live document list. The normal order -- documents close, then Shutdown -- should never allow
// that, so this is defensive. Assignment only, nothing dereferenced, and idempotent, so it is
// safe at any point in the shutdown sequence.
void KCMClearChosenDocs()
{
	sChosenTargetDB = nil;
	sChosenSourceDB = nil;
	KCMForgetExternalSource();	// the lent Source is a choice too, and this is the shutdown slot for choices
	sChosenSourceIsOrigin = kFalse;
	KCMReleaseOrigin();			// "Clear Target and Source" drops the origin too (the user's rule, 2026-09-12)
	sTargetIsFile = kFalse;		// ★the file choices go here and NOWHERE ELSE - a closing document
	sSourceIsFile = kFalse;		//   leaves them standing (KCMForgetChosenDocsThatClosed)
}

//----------------------------------------------------------------------------------------
// ⛔The origin's two (declared in KCMPairChoice.h), until the origin goes
//----------------------------------------------------------------------------------------

bool16 KCMChosenSourceIsOrigin()	{ return (sChosenSourceIsOrigin && KCMOriginDocDB() != nil) ? kTrue : kFalse; }

void KCMChooseOriginPair(IDataBase* originDocDB)
{
	if (originDocDB == nil)
		return;
	if (KCMIsExternalSource(sChosenSourceDB))
		KCMForgetExternalSource();		// the lent database gives way, as it does to "Set as Source"
	sChosenTargetDB = originDocDB;
	sChosenSourceDB = nil;
	sChosenSourceIsOrigin = kTrue;
	sTargetIsFile = kFalse;
	sSourceIsFile = kFalse;
}

// End, KCMPairChoice.cpp.
