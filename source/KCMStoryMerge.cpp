//========================================================================================
//
//  KCMStoryMerge.cpp -- see the header.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line. The harness answers it with a stub of its own (work/kcm-storyhtml-test).
#include "VCPlugInHeaders.h"

#include "KCMStoryMerge.h"
#include "KCMParaText.h"		// AppendUtf8
#include "KCMTextDiff.h"		// ToCodePoints / Diff - the same engine the comparison runs on

namespace KCMStoryMerge
{

namespace
{

typedef KCMTextDiff::Change Change;

/** Whether the origin range [s, s+k) is clear of every change in `b`: neither overlapping it nor
	touching it. ★TOUCHING COUNTS (the design, 6-4): two insertions at one place, or a deletion
	ending where the other side's begins, cannot be ordered by anybody but a person. */
bool16 ClearOf(const std::vector<Change>& b, int32 s, int32 k)
{
	for (size_t i = 0; i < b.size(); ++i)
	{
		const int32 bs = b[i].aStart;
		const int32 be = b[i].aStart + b[i].aCount;
		if (!(be < s || s + k < bs))
			return kFalse;
	}
	return kTrue;
}

/** An origin position that is clear of b's changes, moved into the coordinates of b's other side. */
int32 ToNow(const std::vector<Change>& b, int32 s)
{
	int32 delta = 0;
	for (size_t i = 0; i < b.size(); ++i)
	{
		if (b[i].aStart + b[i].aCount <= s)
			delta += b[i].bCount - b[i].aCount;
	}
	return s + delta;
}

}	// anonymous namespace

void MergePara(const KCMStoryHtml::Para& origin, const KCMStoryHtml::Para& after, const KCMStoryHtml::Para& now,
			   ParaResult& out)
{
	out = ParaResult();
	out.fMerged = now;

	std::vector<int32> o, w, n;
	KCMTextDiff::ToCodePoints(origin.fText, &o, nil);
	KCMTextDiff::ToCodePoints(after.fText, &w, nil);
	KCMTextDiff::ToCodePoints(now.fText, &n, nil);

	std::vector<Change> a;
	std::vector<Change> b;
	KCMTextDiff::Diff(o, w, a);
	KCMTextDiff::Diff(o, n, b);
	if (a.empty())
		return;					// nothing from Word: the document stands as it is

	std::vector<int32> merged = n;
	for (size_t i = a.size(); i > 0; --i)
	{
		const Change& c = a[i - 1];
		if (!ClearOf(b, c.aStart, c.aCount))
		{
			out.fWhys.push_back("the document changed the same words");
			continue;
		}
		const int32 at = ToNow(b, c.aStart);
		merged.erase(merged.begin() + at, merged.begin() + at + c.aCount);
		merged.insert(merged.begin() + at, w.begin() + c.bStart, w.begin() + c.bStart + c.bCount);
		++out.fApplied;
	}

	std::string text;
	for (size_t i = 0; i < merged.size(); ++i)
		KCMParaText::AppendUtf8(text, merged[i]);
	out.fMerged.fText = text;
}

}	// namespace KCMStoryMerge

// End, KCMStoryMerge.cpp.
