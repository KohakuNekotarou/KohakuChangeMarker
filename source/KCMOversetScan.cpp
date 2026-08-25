//========================================================================================
//
//  KCMOversetScan.cpp
//
//  The detection itself (see KCMOversetScan.h). It walks every user-accessible story of one
//  document and collects the UIDs of the pages holding overset text. Overset is judged with
//  Utils<ITextUtils>::IsOverset, and the frame showing the "+" -- the frame of the last placed
//  parcel -- is found by logic copied inline from KBS's KBSOversetLocator.cpp, so that nothing
//  here depends on the KBS plug-in. Turning a frame into a page UID is left to KCMFramePageUID
//  (KCMCore.cpp), which answers kInvalidUID for a frame on no page, so pasteboard frames drop out
//  by themselves.
//  @warning **do not explain how that function works here.** Which API it prefers, which it falls
//    back on and what it validates belong to its own comments in KCMCore.cpp. The description of
//    its pre-move implementation lived on at the top of this file long after the function left it.
//    **Moving a function to another file does not move the comments and includes that describe it.**
//  The scan only reads, except that it brings stale composition up to date first
//  (RecomposeThruLastFrame): overset is the result of composition, so asking without composing can
//  report overflow that is already fixed and miss overflow that has just appeared. The whole scan
//  is wrapped in IDataBase::SaveRestoreModifiedState so that even a windowless document is not
//  left dirty.
//
//  **THE APPLICATION'S OWN PREFLIGHT CAN ALSO ENUMERATE OVERSET, AND IS NOT USED HERE.**
//    It exists in full -- the rule boss kOversetTextRuleBoss (PackageAndPreflightID.h:165), three
//    separate criteria (kPreflightRC_OversetTextFrame, **OversetFootnote**, OversetTableCell,
//    :938-940), and IPreflightFacade as the way in. **Three reasons not to use it, each decisive
//    on its own**:
//      (1) **The results carry no coordinates.** GetPreflightResults answers with triples of
//          "Node ID / error name / **page number as a string**". What KCM needs is the pasteboard
//          point of the "+", so this scan would still have to run afterwards.
//      (2) **It depends on the user's settings.** Nothing comes back unless the active profile has
//          the overset rules enabled, and TurnOnPreflighting rewrites the user's own preflight
//          settings to arrange that.
//      (3) **It is asynchronous** (a background idle loop). Find Overset has to answer the moment
//          it is pressed.
//    @warning **this is written down so that the question is not investigated from scratch again.**
//
//========================================================================================

#include "VCPlugInHeaders.h"

// The object model, text, tables and layout:
#include "IDataBase.h"			// GetRootUID / SaveRestoreModifiedState
#include "IStoryList.h"			// GetUserAccessibleStoryCount / GetNthUserAccessibleStoryUID
#include "ITextModel.h"			// QueryFrameList / GetPrimaryStoryThreadSpan / QueryTextParcelList
#include "IFrameList.h"			// IsOverset's argument / GetFirstDamagedFrameIndex (is the composition stale)
#include "IFrameListComposer.h"	// RecomposeThruLastFrame (bring composition up to date before asking)
#include "ITextParcelList.h"
#include "IParcelList.h"		// GetLastParcelKey / GetPreviousParcelKey / GetParcelFrameUID
#include "ITextUtils.h"			// IsOverset
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex (climbing out of a pushed-out cell)
#include "IGeometry.h"			// frame inner -> pasteboard, for the "+" point
// ILayoutUtils.h, IHierarchy.h and SpreadID.h are deliberately absent: all three were here for
//   KCMFramePageUID, which moved to KCMCore.cpp (and took its own includes with it).
//   **The function moved; the includes stayed behind.**
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "PMMatrix.h"
#include "PMPoint.h"			// PBPMPoint
#include "PMRect.h"				// the bottom-right corner from GetParcelBounds
// For the pass over single overflowing table cells (the red dot):
#include "ITextStoryThreadDictHier.h"	// NextUID (walks a story's thread dictionaries, hierarchy flattened)
#include "ITableModel.h"			// const_iterator / GetGridID / begin/end
#include "ITextStoryThreadDict.h"	// QueryThread(gridID), which kTableModelBoss carries
#include "ITextStoryThread.h"		// GetTextStart (a cell thread's first TextIndex)
#include "TableTypes.h"				// GridAddress / GridID (in source/public/includes)

// General:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "Utils.h"

// KCM's own headers:
#include "KCMCore.h"			// KCMFramePageUID (frame -> page; the one copy, shared with Story Edits)
#include "KCMOversetScan.h"


//========================================================================================
// The outport of the last placed parcel of this thread (the parcel list pos composes into).
// Walking backwards from the end, the first parcel with a valid frame (GetParcelFrameUID !=
// kInvalidUID) has its corner transformed parcel -> frame inner -> pasteboard and returned. This
// is the same computation as LocateInThread in KBS's KBSOversetLocator.cpp. kFalse when no parcel
// is placed at all. outFrame is the frame showing the "+", outPb the "+" point itself.
//
// **The same expression is right for vertical text too** (confirmed on the real thing). What is
//   taken is (Right, Bottom) in **parcel-local coordinates**, and GetParcelToFrameMatrix absorbs
//   the difference in writing direction: cycling through vertical overset with Find Overset and
//   Prev/Next lands exactly where InDesign draws the "+" (bottom-left, for vertical text).
//   @warning **do not read this as horizontal-only and add a branch on writing direction.**
//========================================================================================
static bool16 KCMLastPlacedOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
	UID& outFrame, PBPMPoint& outPb)
{
	if (textModel == nil || db == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		const UID frameUID = pl->GetParcelFrameUID(k);
		if (frameUID == kInvalidUID)
			continue;	// this piece is unplaced (overset); keep walking back towards a placed one

		InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
		if (frameGeo == nil)
			continue;

		const PMRect  parcelBounds  = pl->GetParcelBounds(k);				// parcel-local
		const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(k);			// parcel -> frame inner
		const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner -> pasteboard

		PMPoint corner(parcelBounds.Right(), parcelBounds.Bottom());		// the outport corner, parcel-local
		toFrame.Transform(&corner);
		toPasteboard.Transform(&corner);

		outFrame = frameUID;
		outPb    = PBPMPoint(corner.X(), corner.Y());
		return kTrue;
	}
	return kFalse;
}


//========================================================================================
// Is this thread composed anywhere at all -- does it have even one parcel with a frame UID?
//
// **This is what tells "this cell overflowed" apart from "this cell never got the chance to be
//   composed".** GetIsOverset **says yes to both**: to a cell whose own text runs past its bottom
//   edge, and to a cell in a table that was pushed out of its frame entirely and composed nothing.
//   The second is not a finding of its own -- **the frame is the finding** -- and the application's
//   preflight agrees: in a document with a pushed-out table InDesign reported two "Text Frame /
//   Overset text" entries and said nothing whatever about the ten cells inside them, while an
//   unguarded scan reported twelve (measured for KBS; full record in
//   docs/ai-notes/kbs-overset-scan.md).
//   The parcels are what distinguish them: an overflowing cell has its first lines on the page, so
//   some parcel carries a frame UID, whereas every parcel of a pushed-out cell answers kInvalidUID.
//
// This is the same walk as KCMLastPlacedOutport above, but that one goes on to build the geometry
//   of the "+" while this one only asks whether there was one. The callers want different things,
//   which is why they are separate (the same reason KBS keeps ThreadHasPlacedParcel apart from
//   KBSOversetLocator).
// Walking from the end is deliberate: an overflowing thread has its unplaced parcels at the end,
// so the failing path finds what it is looking for near the end and the succeeding path answers on
// its first step.
//========================================================================================
static bool16 KCMThreadHasPlacedParcel(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kFalse;

	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		if (pl->GetParcelFrameUID(k) != kInvalidUID)
			return kTrue;
	}
	return kFalse;
}


//========================================================================================
// Where the "+" of an overflowing thread is. The thread at pos is looked at first; if nothing of
// it is placed -- a cell pushed out of its frame along with its row, say -- the table anchors are
// climbed towards the parent thread and the "+" of the first ancestor with a placed frame is taken
// (the same shape as KBS's KBSFindOversetLocator). A guard stops both non-progress and deep nesting.
//
// @warning **neither of KCM's two callers ever reaches the climb below.** It is kept, but do not
//   read it as doing any work here:
//     - the cell pass ...... KCMThreadHasPlacedParcel has just answered true, so a pushed-out cell
//                            never gets this far and the first KCMLastPlacedOutport always answers
//     - the primary pass ... pos is the end of the primary thread, so InsideTable is false and the
//                            loop breaks on its first iteration
//   In other words **the callers rule out the situation this code describes** before calling it.
//   **It is alive in KBS, where this came from**: KBSFindOversetLocator is called from KBSJump too,
//     and that path has no HasPlacedParcel guard. **The same code lives or dies by its callers.**
//     It is kept identical to KBS's so that the next comparison between the two reads cleanly.
//========================================================================================
static bool16 KCMFindOversetOutport(ITextModel* textModel, IDataBase* db, TextIndex pos,
	UID& outFrame, PBPMPoint& outPb)
{
	if (KCMLastPlacedOutport(textModel, db, pos, outFrame, outPb))
		return kTrue;

	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		if (!Utils<ITableUtils>()->InsideTable(textModel, cur))
			break;
		const TextIndex up = Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur);
		if (up == cur)
			break;	// no progress
		cur = up;
		if (KCMLastPlacedOutport(textModel, db, cur, outFrame, outPb))
			return kTrue;
	}
	return kFalse;
}


// (KCMFramePageUID, frame UID -> page UID, lives in KCMCore.cpp. The Story Edits list needed the
//  same question answered -- "which page is this story's first frame on" -- and copying it there
//  would have put two implementations of one thing in the plug-in, which is the split KCM has been
//  bitten by before. It is declared in KCMCore.h.)


//========================================================================================
// Is the thread at pos overset? A table cell is outside the reach of
// ITextUtils::IsOverset(IFrameList*), having no frame list of its own, so the thread's
// ITextParcelList answers instead through its own GetIsOverset.
// @warning **do not go back to testing GetParcelFrameUID(GetLastParcelKey()) == kInvalidUID: it
//   does not work for cells.** ITextParcelList's own header says why (see
//   GetParcelContainsOversetContent): a parcel composing complex content such as a table can be
//   overset "without this TextParcelList itself being overset", and a cell's parcel is placed --
//   its frame UID is valid -- so the test never fires and every overflowing cell is missed.
//   GetIsOverset is the proper judgement, "anything but the final CR failed to compose", and it
//   deliberately does not count "only the final CR" as overset, which matches both InDesign's red
//   dot and the DOM's cell.overflows.
//========================================================================================
static bool16 KCMThreadIsOverset(ITextModel* textModel, TextIndex pos)
{
	if (textModel == nil)
		return kFalse;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kFalse;
	return tpl->GetIsOverset();		// the proper per-thread overset judgement
}


//========================================================================================
// Walk every cell of every table in the story and add the page UID of each cell that is
// overflowing on its own (the red dot, with the parent frame not overset). A cell's text is a
// separate thread of the same ITextModel, at a higher TextIndex, so the primary thread's IsOverset
// cannot see it: each cell's first TextIndex is fetched through the story thread dictionaries (the
// official worked example is SnpIterTableUseDictHier.cpp), judged by KCMThreadIsOverset, and, when
// it is overset, located with KCMFindOversetOutport and KCMFramePageUID.
//   **Tables are reached through the thread-dictionary hierarchy, not ITableModelList.** Either
//     works, but the SDK says outright which is which: the snippet that uses ITableModelList calls
//     itself "an older way" and names the "better technique" (SnpIterTableStories.cpp). The walk
//     below is SnpIterTableUseDictHier.cpp's.
//     **This is also what brings nested tables in by contract**:
//     ITextStoryThreadDictHier::NextUID flattens the hierarchy, so a table anchored inside a cell
//     lands in the same list as a top-level one. On the older route "nested tables should come
//     back too" was an assumption resting on measurement, with "add recursion if any are missed"
//     left as homework.
//   **Because the list is flat, there is no recursion here** -- adding it would walk the tables
//     inside cells twice and report their cells twice.
//   How: start from the story's own UID (kTextStoryBoss) and follow NextUID through the
//     dictionaries. A dictionary that also answers to ITableModel is a table; one that does not is
//     the primary story itself, which is how the two are told apart. Each table is walked with
//     const_iterator, and every element goes GetGridID -> dict->QueryThread -> GetTextStart.
//   @warning **the iterator does not return anchors, it returns every grid element**
//     (ITableModel.h:391-392, "traverse through the **GridAddress locations**"). CellIterator's
//     implementation lives inside the application and not in the SDK, so the contract is all there
//     is -- but **IsAnchor existing at all is the proof that an address need not be one**.
//     **What makes it one report per cell is the QueryThread below answering nil for anything that
//     is not an anchor.** @warning the header does not say so: all it promises is a thread for
//     "the given key". That non-anchor elements come back nil is measured behaviour, and it is
//     also what makes a merged cell reported once however many squares it covers.
//     @warning **the official example has no nil check** -- SnpIterTableUseDictHier.cpp passes
//       QueryThread's result straight into GetTextStart -- so **copying it faithfully crashes on a
//       non-anchor grid element**. This differs from the example deliberately. KBS carries the
//       same check and says so in the same words ("The official walk is these same three lines
//       without the test").
//     @warning **a walk that does not go through QueryThread has to exclude non-anchors itself
//       with IsAnchor** (reading a cell's attributes or GetCellType directly, say).
//   @warning **footnotes do not come through this walk**: only dictionaries carrying an
//     ITableModel are looked at, so a footnote's dictionary passes straight by. **Nothing is missed
//     by that**: a footnote that does not fit makes **the frame list itself overset**, so (1) below
//     has already caught it (measured for KBS: 64 characters of body text with a 4,183-character
//     footnote under it, and the last frame reports overflows=true).
//     @warning Adobe's preflight makes OversetFootnote a criterion of its own (see the top of this
//     file), so "shouldn't footnotes be counted separately?" will be asked again. **Read these
//     three lines when it is.**
//   Cost: the sum of rows x columns, which is heavy for a large table. Find Overset is on demand,
//     so that is acceptable.
//   @warning **a cheap "does this table hold an overflowing cell" test does exist** and is
//     deliberately not used: ITextParcelList::GetParcelContainsOversetContent answers, in one call
//     per parcel, whether complex content such as tables or footnotes is overset. **Measurement is
//     why it was rejected** (KBS compared the two over five documents): (1) for tables the answers
//     agree exactly; (2) which is faster **goes both ways** -- 500 stories with no table at all
//     took the walk 70us against the parcels' 1,540us, while a single table with 861 overflowing
//     cells took the walk 53us against the parcels' 24us -- so it is not reliably cheap as a
//     pre-test; (3) the only disagreement is **an overflowing footnote**, which the parcels do see
//     and which (1) already catches anyway.
//========================================================================================
static void KCMCollectOversetCells(IDataBase* db, const UIDRef& storyRef, ITextModel* textModel,
	std::vector<KCMOversetLoc>& out)
{
	if (db == nil || textModel == nil)
		return;
	// The hierarchy of dictionaries hangs off kTextStoryBoss (one ITextStoryThreadDict per table).
	InterfacePtr<ITextStoryThreadDictHier> dictHier(textModel, UseDefaultIID());
	if (dictHier == nil)
		return;

	// The walk starts at the story's own UID: kTextStoryBoss carries a dictionary too, for the
	// primary story thread, and that one falls out at the ITableModel query below.
	for (UID nextUID = storyRef.GetUID(); nextUID != kInvalidUID; nextUID = dictHier->NextUID(nextUID))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, nextUID, UseDefaultIID());
		if (dict == nil)
			continue;

		// Is this dictionary a table's? kTableModelBoss carries the dictionary and an ITableModel
		// together, while kTextStoryBoss carries only the dictionary -- which is how the primary
		// story thread is told apart.
		InterfacePtr<ITableModel> tableModel(dict, UseDefaultIID());
		if (tableModel == nil)
			continue;

		for (ITableModel::const_iterator it(tableModel->begin()), end(tableModel->end()); it != end; ++it)
		{
			const GridAddress ga = *it;					// a grid element, not necessarily an anchor (see above)
			const GridID gridID = tableModel->GetGridID(ga);
			InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));	// ref+1
			if (thread == nil)
				continue;
			int32 span = 0;
			const TextIndex cellPos = thread->GetTextStart(&span);	// the cell thread's first TextIndex
			if (span <= 0)
				continue;								// an empty cell cannot overflow
			if (!KCMThreadIsOverset(textModel, cellPos))
				continue;
			// **Did this cell overflow, or did it never get the chance to be composed?**
			//   (KCMThreadHasPlacedParcel above.) When a table is pushed out of its frame, every
			//   cell in it answers yes to GetIsOverset, so without this guard a 4x2 table reports
			//   eight "overflows" -- and since KCMFindOversetOutport below climbs to an ancestor,
			//   **all eight land on the parent frame's single "+"**, so Prev/Next stops at the same
			//   spot eight times over with only the ordinal changing. Nothing is lost by skipping
			//   them: the frame's own overflow is already caught by the primary-thread pass at (1).
			if (!KCMThreadHasPlacedParcel(textModel, cellPos))
				continue;
			UID frameUID = kInvalidUID; PBPMPoint pb;
			if (!KCMFindOversetOutport(textModel, db, cellPos, frameUID, pb))
				continue;
			const UID pageUID = KCMFramePageUID(db, frameUID);
			if (pageUID != kInvalidUID)
				out.push_back(KCMOversetLoc(pageUID, pb));
		}
	}
}


//========================================================================================
// KCMCollectOversetLocations (declared in KCMOversetScan.h)
//========================================================================================
void KCMCollectOversetLocations(IDataBase* db, std::vector<KCMOversetLoc>& outLocs)
{
	if (db == nil)
		return;

	// Read only: a lazy recomposition triggered by the scan must not leave the document dirty (the
	// same rule KBS and KESCL follow).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return;

	const int32 n = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < n; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			continue;

		// **(0) Bring stale composition up to date before asking anything.**
		//   Overset is **the result of composition**, not a current state: both tests below only
		//   read back what the composer last said, so a story edited since it was last composed
		//   reports overflow the user has already fixed, or stays silent about overflow that has
		//   just appeared.
		//   The official route is SnpInspectTextModel.cpp (look at the damaged index, then
		//   RecomposeThruLastFrame); KBS runs the same three lines in KBSOversetScanEngine and in
		//   KBSJump::RecomposeIfDamaged.
		//   **The cell pass at (2) reads settled composition as well** -- @warning **but not
		//     because composing the frame list settles the tables in it**. That is a question
		//     [[text-composition-damage-and-recompose]] records as unverified, and it must not be
		//     asserted here. **The real reason is that the test the cells use,
		//     ITextParcelList::GetIsOverset(), composes merely by being asked** (measured for KBS).
		//     So (2) would read current composition even without this step. What needs (0) is (1)'s
		//     IsOverset(frameList), which does not resolve the frame list's damage by itself.
		//   @warning this function has one caller, KCMApplyOversetForDoc, but **that one has four**
		//     (Find Overset, Refresh Overset, Start and Stop; they are enumerated in
		//     KCMOversetApply.h).
		//     **"This never runs during a draw event" can only be said by counting all four**: two
		//     UI menu handlers; Start, after KCMDoMarkChangesDoc has finished rasterising every
		//     page; and Stop, after the marks have been dropped. None of them is inside a draw
		//     event. **Count again if a caller is added.**
		//   @warning **composing dirties the document.** The SaveRestoreModifiedState at the top of
		//   this function does not prevent that; it restores the clean flag **only if the document
		//   was clean on the way in** (IDataBase.h -- an already dirty document sets fDB to nil and
		//   the guard does nothing, so the user's own edits are never disclaimed).
		//   This call is not what introduces the side effect: asking a thread about its composition
		//   **composes it merely by asking**, so IsOverset and GetIsOverset below did the same
		//   thing on their own -- which is why the guard was here before this step existed (KBS
		//   records the same reasoning). Composing explicitly only does thoroughly and to the end
		//   what was already happening implicitly, and the wax it produces is the wax the next draw
		//   would have produced.
		InterfacePtr<IFrameList> frameList(textModel->QueryFrameList());
		if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}

		// (1) The primary thread's overflow -- the red "+" on an ordinary frame. IsOverset, which
		//     exists for exactly this, decides; from the end of the thread the outport of the last
		//     placed frame gives the "+" point and its page. An empty story (span <= 0) is skipped.
		//     @warning **do not continue out of the loop here**: a table cell can overflow on its
		//     own even when its parent does not, and (2) is what catches that.
		if (frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
		{
			const int32 span = textModel->GetPrimaryStoryThreadSpan();
			if (span > 0)
			{
				UID frameUID = kInvalidUID; PBPMPoint pb;
				if (KCMFindOversetOutport(textModel, db, span - 1, frameUID, pb))
				{
					const UID pageUID = KCMFramePageUID(db, frameUID);
					if (pageUID != kInvalidUID)
						outLocs.push_back(KCMOversetLoc(pageUID, pb));	// kInvalidUID = pasteboard only, skipped
				}
			}
		}

		// (2) A table cell overflowing on its own (the red dot). The parent thread's IsOverset
		//     cannot see it, so every cell of every table is judged from its own thread's first
		//     TextIndex -- and this runs for every story, including those whose parent is fine.
		KCMCollectOversetCells(db, storyRef, textModel, outLocs);
	}
}

// End of KCMOversetScan.cpp
