//========================================================================================
//
//  KCMTextRead.cpp
//
//  See KCMTextRead.h for what this is for and why the snippet's own order cannot be trusted for
//  positions.
//
//  The walk follows SnpInspectTextModel's InspectStoryThreads for its shape. Today it reads the
//  BODY only (GetPrimaryStoryThreadSpan); the thread enumeration that brings in table cells and
//  footnotes arrives in the next tasks of the plan.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITextModel.h"

// General includes:
#include "PMString.h"
#include "TextChar.h"		// kTextChar_CR - the paragraph boundary
#include "TextIterator.h"
#include "UIDRef.h"

#include <boost/thread/recursive_mutex.hpp>	// the same shape KCMThreadSafety uses
#include <sstream>

// Project includes:
#include "KCMTextRead.h"

//----------------------------------------------------------------------------------------
bool16 KCMTextRead::ReadStory(const UIDRef& storyRef,
							  std::vector<std::string>& outParas,
							  std::vector<KCMParaAttrs>& outAttrs,
							  std::vector<int32>& outStarts)
{
	// **EMPTIED FIRST, ALL THREE.** The caller may hand in vectors that already hold the other
	// route's answer (that is exactly what KCMCompareReadRoutes does), and a reader that appended
	// would silently report agreement on a doubled list.
	outParas.clear();
	outAttrs.clear();
	outStarts.clear();

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	// ★THE BODY ONLY IN THIS TASK, and it is GetPrimaryStoryThreadSpan rather than TotalLength:
	//   a story's table cells and footnotes are threads of the same model living PAST the body
	//   (ITableTextContent.h:41-44), so reading to TotalLength() here would append their text to
	//   the body's paragraphs at positions that name body characters.
	//   ⚠A story containing either will therefore DISAGREE with the old route until those tasks
	//     land. That is expected, and the parallel run is what says so out loud.
	const TextIndex bodyLength = model->GetPrimaryStoryThreadSpan();
	if (bodyLength <= 0)
		return kTrue;		// an empty story is not a failure - it simply has no paragraphs

	std::string text;
	TextIndex paraStart = 0;

	TextIterator iter(model, 0);
	for (TextIndex i = 0; i < bodyLength; ++i, ++iter)
	{
		// ⚠TextIterator's value_type is UTF32TextChar, a CLASS - it does not convert to an
		//   integer on its own. GetValue() is the way out of it.
		const int32 cp = static_cast<int32>((*iter).GetValue());

		if (cp == kTextChar_CR)
		{
			outParas.push_back(text);
			outAttrs.push_back(KCMParaAttrs());
			outStarts.push_back(static_cast<int32>(paraStart));
			text.clear();
			paraStart = i + 1;
			continue;
		}

		// ⚠ONE CODE POINT PER CHARACTER. Every position handed out here is counted the way
		//   InDesign counts text positions - a surrogate pair is ONE TextIndex - and the diff
		//   downstream counts the same way. Encoding a character as two would put the two counts
		//   out of step, and the comparison would quote the right words at the wrong place.
		KCMSnippetText::AppendUtf8(text, cp);
	}

	// A story always ends with a break, so the loop above normally closes the last paragraph
	// itself. This catches the one that does not - and an empty tail is NOT pushed, or every
	// story would end with a paragraph nobody wrote.
	if (!text.empty())
	{
		outParas.push_back(text);
		outAttrs.push_back(KCMParaAttrs());
		outStarts.push_back(static_cast<int32>(paraStart));
	}

	return kTrue;
}

//----------------------------------------------------------------------------------------
//  The parallel run. See the header for why it exists and why it is off by default.
//
//  **File-scope statics, not function-local ones** - guide vol1-07 names function-local statics
//  as the thing to remove, and KCMThreadSafety follows the same rule for its mutex.
//----------------------------------------------------------------------------------------

static boost::recursive_mutex	sKCMReadCompareMutex;
static bool16					sKCMReadCompareOn = kFalse;
static std::string				sKCMReadCompareReport;

/** How many disagreements are spelt out in full before the report stops describing them.

	★THE COUNT IS NOT CAPPED, ONLY THE DESCRIPTION. "How many differ" and "which ones" are two
	questions, and capping the first is how an instrument stops being able to raise an alarm.
*/
static const size_t kKCMMaxDetails = 12;

//----------------------------------------------------------------------------------------
bool16 KCMStoryReadCompareIsOn()
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	return sKCMReadCompareOn;
}

//----------------------------------------------------------------------------------------
void KCMSetStoryReadCompare(bool16 on)
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	sKCMReadCompareOn = on;

	// **CLEARED EITHER WAY.** A report left over from the last run, read after a re-arm, would be
	// answering about a comparison the reader is no longer looking at.
	sKCMReadCompareReport.clear();
}

//----------------------------------------------------------------------------------------
void KCMGetStoryReadCompareReport(PMString& out)
{
	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);

	const char* const text = sKCMReadCompareReport.empty()
							 ? (sKCMReadCompareOn ? "armed, nothing compared yet" : "off")
							 : sKCMReadCompareReport.c_str();

	// ⚠NOT TRANSLATABLE. It is a diagnostic, and a PMString built from a c-string is treated as a
	//   translation key unless it is told otherwise.
	out.SetCString(text, PMString::kEncodingASCII);
	out.SetTranslatable(kFalse);
}

//----------------------------------------------------------------------------------------
void KCMCompareReadRoutes(const UIDRef& storyRef,
						  const std::vector<std::string>& oldParas,
						  const std::vector<KCMParaAttrs>& oldAttrs,
						  const std::vector<int32>& oldStarts,
						  const char* which)
{
	std::vector<std::string> newParas;
	std::vector<KCMParaAttrs> newAttrs;
	std::vector<int32> newStarts;

	const bool16 read = KCMTextRead::ReadStory(storyRef, newParas, newAttrs, newStarts);

	std::ostringstream line;
	line << which << ": ";

	if (!read)
	{
		line << "NEW ROUTE COULD NOT READ THE STORY (old had " << oldParas.size() << " paragraphs)";
	}
	else if (oldParas.size() != newParas.size())
	{
		// **THE COUNT FIRST, AND NOTHING ELSE.** Comparing entry by entry across lists of
		// different lengths would report every paragraph after the first difference, which buries
		// the one fact that matters.
		line << "PARAGRAPH COUNT DIFFERS old=" << oldParas.size() << " new=" << newParas.size();
	}
	else
	{
		size_t textDiffs = 0, startDiffs = 0, placeDiffs = 0;
		std::ostringstream detail;
		size_t shown = 0;

		for (size_t i = 0; i < newParas.size(); ++i)
		{
			const bool16 sameText = (oldParas[i] == newParas[i]) ? kTrue : kFalse;
			const bool16 sameStart = (i < oldStarts.size() && i < newStarts.size()
									  && oldStarts[i] == newStarts[i]) ? kTrue : kFalse;

			// ⚠THE CELL IDENTITY IS PART OF "THE SAME ANSWER". SplitRunAtPlaces cuts rows by it,
			//   so two readers that agree on every character and every position can still put a
			//   row in the wrong place if they disagree here.
			const bool16 samePlace = (i < oldAttrs.size() && i < newAttrs.size()
									  && oldAttrs[i].fTableOrdinal == newAttrs[i].fTableOrdinal
									  && oldAttrs[i].fCellRow == newAttrs[i].fCellRow
									  && oldAttrs[i].fCellCol == newAttrs[i].fCellCol) ? kTrue : kFalse;

			if (!sameText) ++textDiffs;
			if (!sameStart) ++startDiffs;
			if (!samePlace) ++placeDiffs;

			if ((!sameText || !sameStart || !samePlace) && shown < kKCMMaxDetails)
			{
				++shown;
				detail << " [" << i << ":";
				if (!sameText)
					detail << "text";
				if (!sameStart)
					detail << "start(" << (i < oldStarts.size() ? oldStarts[i] : -1)
						   << "/" << (i < newStarts.size() ? newStarts[i] : -1) << ")";
				if (!samePlace)
					detail << "place";
				detail << "]";
			}
		}

		if (textDiffs == 0 && startDiffs == 0 && placeDiffs == 0)
		{
			line << "agree (" << newParas.size() << " paragraphs)";
		}
		else
		{
			line << "DIFFER text=" << textDiffs << " start=" << startDiffs
				 << " place=" << placeDiffs << " of " << newParas.size() << detail.str();
			if (shown < textDiffs + startDiffs + placeDiffs && shown == kKCMMaxDetails)
				line << " ...";
		}
	}

	boost::recursive_mutex::scoped_lock lock(sKCMReadCompareMutex);
	if (!sKCMReadCompareReport.empty())
		sKCMReadCompareReport += "\n";
	sKCMReadCompareReport += line.str();
}

// End, KCMTextRead.cpp.
