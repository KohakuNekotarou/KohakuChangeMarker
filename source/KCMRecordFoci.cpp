//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - one text focus per "=" record. See KCMRecordFoci.h.
//
//  KFC's KFCRowFoci is the model (Let, StillUsable, MakeFocus and InManager are its own, with KCM's names): what it
//  measured is kept as it measured it.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "ITextFocus.h"
#include "ITextFocusManager.h"
#include "ITextModel.h"
#include "IViewObjectChanges.h"	// a focus's target database (KCMRecordFocus.h)

// General includes:
#include "RangeData.h"

#include <map>

// Project includes:
#include "KCMRecordFoci.h"
#include "KCMCore.h"			// KCMIsDocDBOpen - the pointer compared, never read through
#include "KCMDiag.h"			// KCM_DIAG_LOG ("FOCUS make / move / let") and KCM_DIAG_FAULT ("focus-off")
#include "KCMID.h"				// kKCMRecordFocusBoss
#include "KCMRecordFocus.h"		// IsLost
#include "KCMThreadSafety.h"	// KCMIsMainThread - every entry point's gate

namespace
{
struct Kept
{
	ITextFocus*	focus;	// AddRef'd (NewFocus)
	IDataBase*	db;		// the document it was made in - compared, never read through before KCMIsDocDBOpen says yes
	UID			story;
	Kept() : focus(nil), db(nil), story(kInvalidUID) {}
};
std::map<int32, Kept> gKept;
int32 gLastId = 0;		// the numbers are this session's: never reused, so a record holding a number let go finds nothing

// Is f one of the foci this manager holds now? Its list walked, the pointer compared, nothing else read (KT's
// KTFocusProbe, where it was measured).
bool InManager(ITextFocusManager* mgr, const ITextFocus* f)
{
	if (mgr == nil || f == nil)
		return false;
	const int32 n = mgr->GetFocusCount();
	for (int32 i = 0; i < n; ++i)
	{
		InterfacePtr<ITextFocus> nth(mgr->QueryNthFocus(i));
		if (nth.get() == f)
			return true;
	}
	return false;
}

// Let one go: its target database cleared, then out of its story's manager, then our reference - the last two only while
// it is still in that manager's list. Otherwise it is dropped untouched: a leak beats a call into what may be gone (the
// close responder lets every focus of a closing document go while the document still stands - LetDocument). The target
// is cleared whenever the document stands, its story gone or not: a focus left behind must not keep pointing the
// view-interface records (KCMRecordFocus.h) at a database that will close.
void LetKept(int32 id, Kept& k)
{
	if (k.focus == nil)
		return;
	bool released = false;
	if (KCMIsDocDBOpen(k.db))
	{
		InterfacePtr<IViewObjectChanges> view(k.focus, UseDefaultIID());
		if (view != nil)
			view->SetTargetDB(nil);
		if (k.db->IsValidUID(k.story))
		{
			InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
			InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
			if (mgr != nil && InManager(mgr, k.focus))
			{
				mgr->RemoveFocus(k.focus);
				k.focus->Release();
				released = true;
			}
		}
	}
	KCM_DIAG_LOG("FOCUS let id=%d%s", (int)id, released ? "" : " (left as it was - its story or document is gone)");
	(void)id;
	(void)released;
	k.focus = nil;
}

// The kept focus, still usable: its document open, its story there, the focus in its story's manager and on that
// story's model. Anything else lets it go (the caller drops it from the map) and answers nil.
ITextFocus* StillUsable(int32 id, Kept& k)
{
	if (k.focus != nil && KCMIsDocDBOpen(k.db) && k.db->IsValidUID(k.story))
	{
		InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		InterfacePtr<ITextModel> focusModel(k.focus->QueryModel());
		if (mgr != nil && InManager(mgr, k.focus) && focusModel.get() == model.get())
			return k.focus;
	}
	LetKept(id, k);
	return nil;
}

// A focus of KCM's own class on `range` of the manager's story, its target the document's database (KCMRecordFocus.h:
// without a target its snapshots go nowhere, and an Undo leaves it behind as InDesign's own focus is left). AddRef'd;
// nil when none could be made. (Every range here is RangeData(start, end, kLeanForward): where start == end, a caret
// with that lean - the three-index form takes the lean for an empty range, RangeData.h.)
ITextFocus* MakeFocus(ITextFocusManager* mgr, IDataBase* db, const RangeData& range)
{
	ITextFocus* f = mgr->NewFocus(range, kKCMRecordFocusBoss);	// AddRef'd
	if (f == nil)
		return nil;
	InterfacePtr<IViewObjectChanges> view(f, UseDefaultIID());
	if (view != nil)
		view->SetTargetDB(db);
	return f;
}
}	// anonymous namespace

int32 KCMRecordFoci::Make(IDataBase* db, UID story, TextIndex start, TextIndex end)
{
	if (!KCMIsMainThread() || db == nil || story == kInvalidUID || start < 0 || end < start)
		return 0;
	// (Fault switch focus-off, a test build's only - KCMDiag.h: no record gets a focus, the way KCM worked before - so a
	//  test that passes with foci is known to have measured what they add.)
	if (KCM_DIAG_FAULT("focus-off"))
		return 0;
	if (!KCMIsDocDBOpen(db) || !db->IsValidUID(story))
		return 0;
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
	if (model == nil || mgr == nil || end > model->TotalLength())
		return 0;
	ITextFocus* f = MakeFocus(mgr, db, RangeData(start, end, RangeData::kLeanForward));
	if (f == nil)
		return 0;
	Kept k;
	k.focus = f;
	k.db = db;
	k.story = story;
	const int32 id = ++gLastId;
	gKept[id] = k;
	KCM_DIAG_LOG("FOCUS make id=%d story=%d at %d-%d", (int)id, (int)story.Get(), (int)start, (int)end);
	return id;
}

KCMRecordFoci::State KCMRecordFoci::Where(int32 id, IDataBase* db, UID story, TextIndex& outStart, TextIndex& outEnd)
{
	if (!KCMIsMainThread() || id <= 0)
		return kNone;
	std::map<int32, Kept>::iterator it = gKept.find(id);
	if (it == gKept.end() || it->second.db != db || it->second.story != story)
		return kNone;
	ITextFocus* f = StillUsable(it->first, it->second);
	if (f == nil)
	{
		gKept.erase(it);
		return kNone;
	}
	const RangeData r = f->GetCurrentRange();
	outStart = r.Start(nil);
	outEnd = r.End();
	return KCMRecordFocus::IsLost(f) ? kLost : kFollowed;
}

void KCMRecordFoci::MoveTo(int32 id, TextIndex start, TextIndex end, bool16 inStep)
{
	if (!KCMIsMainThread() || id <= 0 || start < 0 || end < start)
		return;
	std::map<int32, Kept>::iterator it = gKept.find(id);
	if (it == gKept.end())
		return;
	ITextFocus* f = StillUsable(it->first, it->second);
	if (f == nil)
	{
		gKept.erase(it);
		return;
	}
	InterfacePtr<ITextModel> model(f->QueryModel());
	if (model == nil || end > model->TotalLength())
		return;
	// Inside a write's step the move is marked first, so the step's snapshot holds it and the step's Undo and Redo put
	// the focus where the step found it and where it left it (KCMRecordFocus.h) - marked even when it does not move.
	if (inStep)
		f->PreDirtyFocus();
	const RangeData r = f->GetCurrentRange();
	if (r.Start(nil) == start && r.End() == end)
		return;
	KCM_DIAG_LOG("FOCUS move id=%d %d-%d -> %d-%d%s", (int)id, (int)r.Start(nil), (int)r.End(), (int)start, (int)end,
				 inStep ? "" : " (outside a step)");
	f->SetRange(kFalse, RangeData(start, end, RangeData::kLeanForward));
}

void KCMRecordFoci::Let(int32 id)
{
	if (!KCMIsMainThread() || id <= 0)
		return;
	std::map<int32, Kept>::iterator it = gKept.find(id);
	if (it == gKept.end())
		return;
	LetKept(it->first, it->second);
	gKept.erase(it);
}

void KCMRecordFoci::LetDocument(IDataBase* db)
{
	if (!KCMIsMainThread() || db == nil)
		return;
	for (std::map<int32, Kept>::iterator it = gKept.begin(); it != gKept.end(); )
	{
		if (it->second.db == db)
		{
			LetKept(it->first, it->second);
			it = gKept.erase(it);
		}
		else
			++it;
	}
}

void KCMRecordFoci::LetAll()
{
	if (!KCMIsMainThread())
		return;
	for (std::map<int32, Kept>::iterator it = gKept.begin(); it != gKept.end(); ++it)
		LetKept(it->first, it->second);
	gKept.clear();
}

void KCMRecordFoci::LetAllBut(const std::set<int32>& keep)
{
	if (!KCMIsMainThread())
		return;
	for (std::map<int32, Kept>::iterator it = gKept.begin(); it != gKept.end(); )
	{
		if (keep.find(it->first) == keep.end())
		{
			LetKept(it->first, it->second);
			it = gKept.erase(it);
		}
		else
			++it;
	}
}

// End, KCMRecordFoci.cpp.
