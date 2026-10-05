//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  NodeID class for the Story Edits tree. A node is the pair (row, change):
//
//    (-1, -1)        the hidden root
//    (row, -1)       a STORY row    -> index into KCMStoryList, in the order Build left it
//    (row, change)   a CHANGE row   -> index into that row's fChanges
//    (author, -1, -1) an AUTHOR row (the Track mode only, 2026-10-05) -> index into the facade's authors.
//                    ★In the Track mode every node also carries its row's AUTHOR in front - (author, row, change) -
//                    derived in the factories from the row (KCMStoryNodeAuthorOfRow), so two nodes naming the same
//                    row cannot disagree about it (KBS derives its font group the same way). Every other mode's
//                    author is -1, and the tree is the two levels it always was.
//
//  ★★THE TREE IS ALWAYS A HIERARCHY; THE PIXEL MODE JUST DOES NOT USE THE SECOND LEVEL.
//  Nothing switches trees between the modes (user's call: "take the story mode, with its levels,
//  as the main one; the pixel mode need not go deep even though the levels are there"). In the pixel mode no row has
//  children - IKCMStoryEditsFacade::GetChangeCount answers 0 because nothing filled fChanges in -
//  so the adapter grows no branches and the list looks exactly as it always has. One set of row
//  drawing, one click handler, one rebuild.
//
//  ⚠A NODE HOLDS INDICES, NOT DATA. The row's text, its kinds, and a change's positions are looked
//  up from the model through the boundary when they are needed. That is what lets a rebuild after a
//  new comparison be ClearTree + ChangeRoot: the nodes do not have to be told anything.
//
//  ⚠AND THEY ARE ONLY AS GOOD AS THE MOMENT THEY WERE MADE. The list is replaced whole by the next
//  comparison, so an index handed out before that names a different story afterwards. Everything
//  that reads one goes back through the Facade, which bounds-checks; nothing caches a row.
//
//  Written from KBS's KBSResultNodeID (a triple: chapter, font, hit), itself from the paneltreeview
//  sample's PnlTrvFileNodeID. Two levels rather than three (three in the Track mode, 2026-10-05: the
//  author is derived in the factories, the way KBS derives its font group, so that two nodes naming the
//  same row cannot disagree about it).
//
//========================================================================================

#ifndef __KCMStoryNodeID_h__
#define __KCMStoryNodeID_h__

#include "NodeID.h"
#include "IPMStream.h"
#include "PMString.h"

#include "KCMUIID.h"

/** The author of a Track row, or -1 outside the Track mode (KCMStoryTreeAdapter.cpp). ★Derived HERE, in the
	factories, so that two nodes naming the same row cannot disagree about its author (KBS derives its font group
	the same way). */
int32 KCMStoryNodeAuthorOfRow(int32 row);

/** One node of the Story Edits tree: (author, row, change). See the file comment for the shapes a node can take.
*/
class KCMStoryNodeID : public NodeIDClass
{
public:
	enum { kNodeType = kKCMStoryTreeWidgetBoss };

	/** The generic node the tree-view framework asks for (GetGenericNodeID) - the root's shape. */
	static NodeID_rv Create() { return new KCMStoryNodeID(); }

	/** The hidden root. */
	static NodeID_rv CreateRoot() { return new KCMStoryNodeID(-1, -1, -1); }

	/** An author row (the Track mode): 'a' indexes the facade's authors. */
	static NodeID_rv CreateAuthor(int32 a) { return new KCMStoryNodeID(a, -1, -1); }

	/** A story row. 'row' indexes the facade's rows (KCMStoryList, or the Track list in that mode). */
	static NodeID_rv CreateStory(int32 row) { return new KCMStoryNodeID(KCMStoryNodeAuthorOfRow(row), row, -1); }

	/** A change row under story 'row'. 'change' indexes that row's changes. */
	static NodeID_rv CreateChange(int32 row, int32 change) { return new KCMStoryNodeID(KCMStoryNodeAuthorOfRow(row), row, change); }

	virtual ~KCMStoryNodeID() {}

	virtual NodeType GetNodeType() const { return kNodeType; }

	virtual int32 Compare(const NodeIDClass* nodeID) const
	{
		const KCMStoryNodeID* other = static_cast<const KCMStoryNodeID*>(nodeID);
		// ★EVERY FIELD, ALWAYS. Identity runs through here, and a tree that holds two identities for
		//   one row loses selections and expansion state in ways that look random (KBS's note).
		// A nil is not expected - a NodeID owns its NodeIDClass and clones it on every copy - but it
		// answers "not equal" rather than 0, because 0 is the one answer that would let an unusable
		// node claim to BE this row.
		if (other == nil)
			return 1;
		if (fAuthor < other->fAuthor)	return -1;
		if (fAuthor > other->fAuthor)	return 1;
		if (fRow < other->fRow)			return -1;
		if (fRow > other->fRow)			return 1;
		if (fChange < other->fChange)	return -1;
		if (fChange > other->fChange)	return 1;
		return 0;
	}

	virtual NodeIDClass* Clone() const { return new KCMStoryNodeID(fAuthor, fRow, fChange); }

	virtual void Read(IPMStream* stream)
	{
		stream->XferInt32(fAuthor);
		stream->XferInt32(fRow);
		stream->XferInt32(fChange);
	}

	virtual void Write(IPMStream* stream) const
	{
		stream->XferInt32(const_cast<KCMStoryNodeID*>(this)->fAuthor);
		stream->XferInt32(const_cast<KCMStoryNodeID*>(this)->fRow);
		stream->XferInt32(const_cast<KCMStoryNodeID*>(this)->fChange);
	}

	/** The row's author in the Track mode (or the author an author row names); -1 in every other mode. */
	int32 GetAuthor() const { return fAuthor; }

	/** The story's 0-based index into the facade's rows (-1 = the root, or an author row). */
	int32 GetRow() const { return fRow; }

	/** The change's index within that row (-1 = this is NOT a change row). */
	int32 GetChange() const { return fChange; }

	/** Is this a change row - a leaf? */
	bool16 IsChangeRow() const { return fChange >= 0; }

	/** Is this a Track mode author row? */
	bool16 IsAuthorRow() const { return (fAuthor >= 0 && fRow < 0) ? kTrue : kFalse; }

	/** Is this the hidden root? */
	bool16 IsRoot() const { return (fAuthor < 0 && fRow < 0) ? kTrue : kFalse; }

	/** Debug aid, like the samples: makes tree-view asserts name the node. */
	virtual PMString GetDescription() const
	{
		PMString s("KCMStoryRow ");
		if (fAuthor >= 0)
		{
			s.Append("a");
			s.AppendNumber(fAuthor);
			s.Append(" ");
		}
		s.AppendNumber(fRow);
		if (fChange >= 0)
		{
			s.Append(":");
			s.AppendNumber(fChange);
		}
		s.SetTranslatable(kFalse);
		return s;
	}

private:
	// Private constructors force the factory methods, PnlTrvFileNodeID-style.
	KCMStoryNodeID() : fAuthor(-1), fRow(-1), fChange(-1) {}
	KCMStoryNodeID(int32 author, int32 row, int32 change) : fAuthor(author), fRow(row), fChange(change) {}

	int32 fAuthor;
	int32 fRow;
	int32 fChange;
};

#endif // __KCMStoryNodeID_h__

// End, KCMStoryNodeID.h.
