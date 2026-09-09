//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  See KCMResourceDiff.h for what a key is and which way its one judgement leans.
//
//  ★THE PAIRING IS THE STRAIGHTFORWARD LOOP, on purpose. It is O(n*n) in the number of
//  definitions -- 171 for a four-page document, so about thirty thousand comparisons of short
//  strings, against an export that costs 78-157ms on its own. A map would save a millisecond and
//  cost a reader the ability to see, in one screen, exactly which item pairs with which. The two
//  loops that already exist in this mode (the kind count in KCMDescribeResourceSnapshot and the
//  ordinals in KCMParseResources) have the same shape for the same reason.
//
//  ★★WHY EACH SOURCE ITEM CAN BE TAKEN ONLY ONCE. Two definitions of a kind can share a key --
//  two sections whose Name is empty pair on position, and a document with a duplicate layer name
//  is not impossible. Claiming a source item as soon as it is matched turns that from a fault
//  into an ordering: the first target item takes the first source item, the second takes the
//  second. Without it, both target items would match the same source item and the second source
//  item would be reported as Removed while nothing was.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"			// GetRootUID - how an armed database becomes a document
#include "IDocument.h"

// General includes:
#include <sstream>				// number formatting for the reading port, as KCMStoryList does
#include <string>
#include <windows.h>			// ::GetTickCount - how long the whole comparison took

// Project includes:
#include "KCMCore.h"			// KCMArmedTargetDB / KCMArmedSourceDB - the panel's own two documents
#include "KCMResourceBytes.h"
#include "KCMResourceDiff.h"
#include "KCMResourceSnapshot.h"

/** How much of a body to show on each side of a difference in the reading port.

    Enough to read the attribute that moved together with its value; short enough that a hundred
    differences still come back as one answer a script can read. */
static const int32 kKCMDiffExcerptChars = 72;

/** How far BEFORE the difference an excerpt starts, so that the NAME of what changed is in it and
    not only its new value. See Excerpt for the reading that made this necessary. */
static const int32 kKCMDiffExcerptLeadIn = 26;

//========================================================================================
// The key
//========================================================================================
//
// ★The "is this a UID?" test itself lives in KCMResourceParse, because the parse needs it too --
//   it is what keeps UIDs out of the bodies being compared. Asking it there and answering it here
//   as well would be one question in two files.

PMString KCMResourceKeyOf(const KCMResourceItem& item)
{
	PMString key;
	key.SetTranslatable(kFalse);

	// [A] There is nothing to identify it with but its own name -- and that is enough, because
	// these kinds occur exactly once in a document (DocumentPreference, ViewPreference, ...).
	if (item.fSelf.IsEmpty())
	{
		key = item.fKind;
		return key;
	}

	// [B] A named Self. It already carries the kind ("Color/Black"), it is unique within the
	// document, and -- the point of the whole mode -- it is the SAME STRING in a document that
	// was built separately.
	if (!KCMIsOpaqueSelf(item.fSelf))
	{
		key = item.fSelf;
		return key;
	}

	// [C] An opaque UID. The Self is worthless across two documents, so the pairing falls back on
	// what a person would use: the name they typed and can rename.
	key = item.fKind;
	if (!item.fName.IsEmpty())
	{
		key.Append("#");
		key.Append(item.fName);
		return key;
	}

	// ...and where there is no name either (a Section's Name may be empty), the nth of a kind
	// pairs with the nth of the same kind. ⚠It is the weakest of the three and the only one that
	// can pair two definitions having nothing to do with each other. It earns its place because
	// the alternative is to pair nothing at all, which reports every such definition as added and
	// removed at the same time -- a wrong pairing shows one difference, no pairing shows two.
	key.Append("@");
	key.AppendNumber(item.fOrdinal);
	return key;
}

//========================================================================================
// The comparison
//========================================================================================

bool16 KCMDiffResources(const KCMResourceList& source, const KCMResourceList& target,
						KCMResourceChangeList& out, KCMResourceDiffStats& stats)
{
	out.clear();
	stats = KCMResourceDiffStats();
	stats.fSourceItems = static_cast<int32>(source.size());
	stats.fTargetItems = static_cast<int32>(target.size());

	const int32 sourceCount = stats.fSourceItems;
	const int32 targetCount = stats.fTargetItems;

	// ⚠A container throws when it cannot grow, and this runs on the model side, which is reached
	//   from paths where an exception is fatal. A half-built list is worse than none: it would
	//   read as "these definitions were removed" when nothing was. So the whole body is guarded
	//   and a failure comes back through the return value, the way KCMParseResources reports one.
	try
	{
		// The keys, worked out once each. Inside the search below, every source key would be
		// recomputed once per target item.
		K2Vector<PMString> sourceKeys;
		K2Vector<bool16> sourceTaken;
		for (int32 s = 0; s < sourceCount; ++s)
		{
			sourceKeys.push_back(KCMResourceKeyOf(source[s]));
			sourceTaken.push_back(kFalse);
		}

		for (int32 t = 0; t < targetCount; ++t)
		{
			const PMString targetKey = KCMResourceKeyOf(target[t]);

			int32 match = -1;
			for (int32 s = 0; s < sourceCount; ++s)
			{
				if (!sourceTaken[s] && sourceKeys[s] == targetKey)
				{
					match = s;
					break;
				}
			}

			if (match < 0)
			{
				KCMResourceChange added;
				added.fKind = target[t].fKind;
				added.fKey = targetKey;
				added.fWhat = kKCMResourceAdded;
				added.fTargetBody = target[t].fBody;
				out.push_back(added);
				++stats.fAdded;
				continue;
			}

			sourceTaken[match] = kTrue;
			++stats.fPaired;

			const bool16 sameBody = (source[match].fBody == target[t].fBody);

			// ----- the sieve, measured rather than assumed (design §8-2).
			// ⚠Only pairs that actually CARRY a StyleUniqueId are counted. Most definitions have
			//   none, and counting those would fill "same id" with elements that have no id at
			//   all -- a number that agrees with everything and therefore measures nothing.
			const bool16 hasId = (!source[match].fUniqueId.IsEmpty() || !target[t].fUniqueId.IsEmpty());
			if (hasId)
			{
				const bool16 sameId = (source[match].fUniqueId == target[t].fUniqueId);
				if (sameBody && sameId)
					++stats.fAgreeSameId;
				else if (sameBody && !sameId)
					++stats.fAgreeOtherId;
				else if (!sameBody && sameId)
					++stats.fDifferSameId;
				else
					++stats.fDifferOtherId;
			}

			if (sameBody)
				continue;

			KCMResourceChange changed;
			changed.fKind = target[t].fKind;
			changed.fKey = targetKey;
			changed.fWhat = kKCMResourceChanged;
			changed.fSourceBody = source[match].fBody;
			changed.fTargetBody = target[t].fBody;
			out.push_back(changed);
			++stats.fChanged;
		}

		// Whatever the Target never claimed was in the Source alone.
		for (int32 s = 0; s < sourceCount; ++s)
		{
			if (sourceTaken[s])
				continue;

			KCMResourceChange removed;
			removed.fKind = source[s].fKind;
			removed.fKey = sourceKeys[s];
			removed.fWhat = kKCMResourceRemoved;
			removed.fSourceBody = source[s].fBody;
			out.push_back(removed);
			++stats.fRemoved;
		}
	}
	catch (...)
	{
		out.clear();
		return kFalse;
	}
	return kTrue;
}

//========================================================================================
// The reading port (app.kcmResourceDiff)
//========================================================================================

namespace
{

std::string Num(int32 n)
{
	std::ostringstream os;
	os << n;
	return os.str();
}

/** One TSV field: whatever a definition holds, with the two characters a TSV cannot carry taken
    out. XML bodies contain tabs and newlines wherever the document's own text did. */
std::string Field(const PMString& text)
{
	std::string s = text.GetUTF8String();
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r')
			s[i] = ' ';
	}
	return s;
}

/** How many characters two bodies share from the start. Where they stop agreeing is where the
    change is, which is the only part of a 4KB definition worth putting in an answer. */
int32 CommonPrefix(const PMString& a, const PMString& b)
{
	const int32 lengthA = static_cast<int32>(a.CharCount());
	const int32 lengthB = static_cast<int32>(b.CharCount());
	const int32 shorter = (lengthA < lengthB) ? lengthA : lengthB;

	int32 i = 0;
	while (i < shorter && a.GetWChar(i).GetValue() == b.GetWChar(i).GetValue())
		++i;
	return i;
}

/** kKCMDiffExcerptChars characters of `body` around `from`, as a TSV field, prefixed with where
    the difference itself is. Empty bodies (an Added item has no Source side) come back as "-".

    ★It starts a little BEFORE the difference, and that is not decoration: a difference lands on
    an attribute's VALUE, so an excerpt beginning exactly there shows the new value and never the
    name of what changed. Measured 2026-09-09 - a reading came back as `old.indd" ZeroPoint=...`
    and the attribute that differed could not be named from it at all. */
std::string Excerpt(const PMString& body, int32 from)
{
	const int32 length = static_cast<int32>(body.CharCount());
	if (length == 0)
		return "-";
	if (from >= length)
		return "@" + Num(from) + " <ends here>";

	int32 start = from - kKCMDiffExcerptLeadIn;
	if (start < 0)
		start = 0;

	PMString piece;
	piece.SetTranslatable(kFalse);
	const int32 stop = ((start + kKCMDiffExcerptChars) < length) ? (start + kKCMDiffExcerptChars) : length;
	for (int32 i = start; i < stop; ++i)
		piece.AppendW(body.GetWChar(i));

	// The position reported is where the DIFFERENCE is, not where the excerpt starts: the two
	// sides are quoted from the same offset, so one number describes both.
	return "@" + Num(from) + " " + Field(piece);
}

const char* WhatWord(KCMResourceChangeKind what)
{
	if (what == kKCMResourceAdded)
		return "Added";
	if (what == kKCMResourceRemoved)
		return "Removed";
	return "Changed";
}

}	// anonymous namespace

void KCMDescribeResourceDiff(PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	// ★The two documents the PANEL is working on, not the first two that happen to be open. A
	//   reading taken from anywhere else would say nothing about the product.
	IDataBase* const targetDB = KCMArmedTargetDB();
	IDataBase* const sourceDB = KCMArmedSourceDB();
	if (targetDB == nil || sourceDB == nil)
	{
		out = "FAILED: no comparison is armed - press Start first, so this reads the same two documents the panel does";
		return;
	}

	InterfacePtr<IDocument> targetDoc(targetDB, targetDB->GetRootUID(), UseDefaultIID());
	InterfacePtr<IDocument> sourceDoc(sourceDB, sourceDB->GetRootUID(), UseDefaultIID());
	if (targetDoc == nil || sourceDoc == nil)
	{
		out = "FAILED: one of the two armed databases has no document";
		return;
	}

	const uint32 began = ::GetTickCount();

	KCMResourceBytes sourceXml;
	KCMResourceBytes targetXml;
	PMString whyNot;

	if (!KCMTakeResourceSnapshot(sourceDoc.get(), sourceXml, whyNot))
	{
		out = "FAILED: source snapshot: ";
		out.Append(whyNot);
		return;
	}
	if (!KCMTakeResourceSnapshot(targetDoc.get(), targetXml, whyNot))
	{
		out = "FAILED: target snapshot: ";
		out.Append(whyNot);
		return;
	}

	KCMResourceList sourceItems;
	KCMResourceList targetItems;
	if (!KCMParseResources(sourceXml, sourceItems, whyNot))
	{
		out = "FAILED: source parse: ";
		out.Append(whyNot);
		return;
	}
	if (!KCMParseResources(targetXml, targetItems, whyNot))
	{
		out = "FAILED: target parse: ";
		out.Append(whyNot);
		return;
	}

	KCMResourceChangeList changes;
	KCMResourceDiffStats stats;
	if (!KCMDiffResources(sourceItems, targetItems, changes, stats))
	{
		out = "FAILED: ran out of memory while pairing the definitions";
		return;
	}

	const uint32 took = ::GetTickCount() - began;

	// ----- the summary, then a header line, then one line per difference.
	// ⚠The header comes back even when nothing differs: "no differences" is a real answer and has
	//   to read differently from the property not being there at all (which is ERR:55).
	std::string s = "Resources: source " + Num(stats.fSourceItems) + " items, target "
				  + Num(stats.fTargetItems) + " items; paired " + Num(stats.fPaired)
				  + ", added " + Num(stats.fAdded)
				  + ", removed " + Num(stats.fRemoved)
				  + ", changed " + Num(stats.fChanged)
				  + "; sieve agree-same-id " + Num(stats.fAgreeSameId)
				  + ", agree-other-id " + Num(stats.fAgreeOtherId)
				  + ", differ-same-id " + Num(stats.fDifferSameId)
				  + ", differ-other-id " + Num(stats.fDifferOtherId)
				  + "; " + Num(static_cast<int32>(took)) + " ms\r\n";

	s += "what\tkind\tkey\tsource\ttarget\r\n";

	for (int32 i = 0; i < static_cast<int32>(changes.size()); ++i)
	{
		const KCMResourceChange& change = changes[i];

		// Where the two bodies stop agreeing is where the change is. For an Added or a Removed
		// item there is only one body, so the excerpt starts at the beginning.
		const int32 from = (change.fWhat == kKCMResourceChanged)
						 ? CommonPrefix(change.fSourceBody, change.fTargetBody)
						 : 0;

		s += std::string(WhatWord(change.fWhat)) + "\t"
		   + Field(change.fKind) + "\t"
		   + Field(change.fKey) + "\t"
		   + Excerpt(change.fSourceBody, from) + "\t"
		   + Excerpt(change.fTargetBody, from) + "\r\n";
	}

	out.SetUTF8String(s);
	out.SetTranslatable(kFalse);	// document text rides in here, the way the Story rows port does
}

// End, KCMResourceDiff.cpp.
