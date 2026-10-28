//========================================================================================
//
//  KCMPixelPages.cpp
//
//  app.kcmPixelPages - see KCMPixelPages.h. A test build's only: the whole body is inside #ifdef KCM_DIAG, so a shipping
//  build compiles this file to nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#ifdef KCM_DIAG

// Interface includes:
#include "IDataBase.h"
#include "IPageList.h"				// GetPageString - the page's name as the Pages panel shows it

// General includes:
#include "PMString.h"
#include "Utils.h"
#include <map>
#include <set>
#include <vector>

// Project includes:
#include "IKCMMarkData.h"
#include "KCMCore.h"				// KCMIsArmed / KCMArmedTargetDB / KCMArmedSourceDB
#include "KCMPixelPages.h"

namespace
{

void Flag(PMString& out, bool16 on)
{
	out.Append(on ? "\t1" : "\t0");
}

/** One side's rows. `partner` maps this side's page to the other side's (both pairings, normal and master). */
void AppendSide(PMString& out, const char* side, IDataBase* db, bool16 isTarget, IKCMMarkData* marks,
				const std::map<UID, UID>& partner)
{
	std::vector<UID> pages;
	marks->GetAllPageUIDs(db, pages);
	const size_t normalCount = pages.size();
	marks->GetMasterPageUIDs(db, pages);			// ADDS - after the normal pages (IKCMMarkData.h)

	std::set<UID> registered;
	marks->GetRegisteredPages(db, registered);		// ADDS - so it starts empty here

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());

	for (size_t i = 0; i < pages.size(); ++i)
	{
		const UID uid = pages[i];
		PMString name;
		name.SetTranslatable(kFalse);
		if (pageList != nil)
			pageList->GetPageString(uid, &name);

		out.Append("\n");
		out.Append(side);
		out.Append("\t");
		out.AppendNumber(static_cast<int32>(i));
		out.Append("\t");
		out.AppendNumber(static_cast<int32>(uid.Get()));
		out.Append("\t");
		out.Append(name);
		Flag(out, i >= normalCount);
		Flag(out, marks->IsPageOnHiddenSpread(db, uid));
		if (isTarget)
		{
			int32 cells = 0;
			int32 total = 0;
			Flag(out, marks->HasEntryForPage(uid));
			marks->GetChangeCells(uid, cells, total);	// kFalse and both 0 when the page has no entry
			out.Append("\t");
			out.AppendNumber(cells);
			out.Append("\t");
			out.AppendNumber(total);
		}
		else
		{
			Flag(out, marks->IsSourcePageMarked(uid));
			out.Append("\t-\t-");
		}
		Flag(out, marks->IsOverflowPage(db, uid, isTarget));
		Flag(out, registered.find(uid) != registered.end());
		const std::map<UID, UID>::const_iterator p = partner.find(uid);
		out.Append("\t");
		if (p != partner.end())
			out.AppendNumber(static_cast<int32>(p->second.Get()));
		else
			out.Append("-");
	}
}

void AddPairs(const std::vector<UID>& a, const std::vector<UID>& b, std::map<UID, UID>& aToB, std::map<UID, UID>& bToA)
{
	for (size_t i = 0; i < a.size() && i < b.size(); ++i)
	{
		aToB[a[i]] = b[i];
		bToA[b[i]] = a[i];
	}
}

}	// anonymous namespace

void KCMDescribePixelPages(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	// The summary first, then the header, whatever comes after: an answer with no rows still says why.
	Utils<IKCMMarkData> marks;
	IDataBase* const target = KCMArmedTargetDB();
	IDataBase* const source = KCMArmedSourceDB();
	if (!marks.Exists())
		out.Append("pixel: no mark data (IKCMMarkData is not on kUtilsBoss)");
	else if (!KCMIsArmed() || target == nil || source == nil)
		out.Append("pixel: not armed");
	else
	{
		// The marks' own pair is compared, never dereferenced (IKCMMarkData.h): marks built for another pair - or none
		// built, in another mode - are said so, and the rows then read 0 because no entry belongs to these pages.
		const bool16 marked = (marks->GetMarkedTargetDB() == target && marks->GetMarkedSourceDB() == source);
		out.Append(marked ? "pixel: armed, marks built for this pair" : "pixel: armed, no marks built for this pair");
	}
	out.Append("\nside\tindex\tuid\tname\tmaster\thidden\tchanged\tcells\ttotal\toverflow\tregistered\tpartner");
	if (!marks.Exists() || !KCMIsArmed() || target == nil || source == nil)
		return;

	IKCMMarkData* const m = marks;		// valid while `marks` lives - to the end of this function
	std::vector<UID> pairT, pairS;
	std::map<UID, UID> targetToSource, sourceToTarget;
	m->GetPagePairing(target, source, pairT, pairS);
	AddPairs(pairT, pairS, targetToSource, sourceToTarget);
	m->GetMasterPagePairing(target, source, pairT, pairS);		// clears both first (IKCMMarkData.h)
	AddPairs(pairT, pairS, targetToSource, sourceToTarget);

	AppendSide(out, "T", target, kTrue, m, targetToSource);
	AppendSide(out, "S", source, kFalse, m, sourceToTarget);
}

#endif // KCM_DIAG

// End, KCMPixelPages.cpp.
