//========================================================================================
//
//  KCMPageExport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <set>
#include <string>
#include <vector>

#include "IComposeScanner.h"		// GetCompleteStyleAt - the first character's size and font
#include "IDataBase.h"
#include "IDrawingStyle.h"
#include "IFontMgr.h"				// kOriginalWinOS - the name Word knows a font by
#include "IGeometry.h"
#include "IGraphicFrameData.h"		// QueryMCTextFrame - a page item that is a text frame
#include "IHierarchy.h"
#include "IMultiColumnTextFrame.h"	// TextStart / TextSpan - the frame's part of its story
#include "IPageList.h"				// GetPageIndex / GetPageString
#include "IPMFont.h"
#include "ISpread.h"				// GetItemsOnPage
#include "IStoryOptions.h"			// IsVertical
#include "ITextInset.h"				// the frame's insets
#include "ITextModel.h"
#include "FileUtils.h"
#include "GroupID.h"				// kGroupItemBoss - a group's items are the page's too
#include "PMMatrix.h"
#include "TextChar.h"
#include "TextIterator.h"
#include "TransformUtils.h"			// InnerToSpreadMatrix
#include "UIDList.h"
#include "UIDRef.h"

#include "KCMPageExport.h"
#include "KCMPageDocx.h"
#include "KCMPageSplice.h"
#include "KCMPlaceReach.h"			// KCMIsFrameHidden
#include "KCMStoryDocx.h"			// SettleForThisFormat - a box is compared the way the page format reads it back
#include "KCMStoryTextExport.h"		// KCMStoryFromDocument, and the folder and file the story export writes
#include "KCMTextRead.h"			// ReadStory - where each paragraph begins
#include "KCMTextWords.h"			// PMStringOfUtf8 - a UTF-8 reason onto the status line
#include "KCMZipStore.h"
#include "KCMPdfDoc.h"				// KCMIsPdfDoc - a PDF document has none of the reader's stories
#include "IKCMCompareFacade.h"		// kKCMPdfTargetRefusal

bool16 KCMReadPageStoryFacts(const UIDRef& story, KCMPageStoryFacts& out)
{
	out = KCMPageStoryFacts();
	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	bool16 placed = kTrue;
	// ★A STORY WHOSE FOOTNOTE REFERENCES CANNOT BE PLACED IS NOT READ - the import's own rule (its pour holds such a story
	//   whole), so no box is written that could never go back in.
	if (!KCMTextRead::ReadStory(story, paras, attrs, starts) || !KCMStoryFromDocument(story, out.fShape, placed) || !placed)
		return kFalse;
	for (size_t i = 0; i < paras.size() && i < attrs.size() && i < starts.size(); ++i)
	{
		if (attrs[i].IsCell() || attrs[i].IsFootnote())
			continue;
		out.fParas.push_back(paras[i]);
		out.fAttrs.push_back(attrs[i]);
		out.fStarts.push_back(starts[i]);
	}
	out.fReadable = (out.fParas.size() == out.fShape.fBody.size()) ? kTrue : kFalse;
	return out.fReadable;
}

bool16 KCMPagePartHoldsTableOrNote(ITextModel* model, TextIndex start, int32 span)
{
	if (model == nil || span <= 0)
		return kFalse;
	TextIterator iter(model, start);
	for (int32 i = 0; i < span; ++i, ++iter)
	{
		// ⚠TextIterator's value_type is UTF32TextChar, a class: GetValue() is the way out of it (KCMTextRead's note).
		const int32 c = static_cast<int32>((*iter).GetValue());
		if (c == kTextChar_Table || c == kTextChar_TableContinued || c == kTextChar_FootnoteMarker
			|| c == kTextChar_EndnoteMarker)
			return kTrue;
	}
	return kFalse;
}

namespace
{

/** One story, read once for every frame of it the pages meet. */
struct StoryFacts
{
	UID					fStory;
	KCMPageStoryFacts	fFacts;
};

const KCMPageStoryFacts& FactsOf(IDataBase* db, UID story, std::vector<StoryFacts>& cache)
{
	for (size_t i = 0; i < cache.size(); ++i)
	{
		if (cache[i].fStory == story)
			return cache[i].fFacts;
	}
	StoryFacts f;
	f.fStory = story;
	KCMReadPageStoryFacts(UIDRef(db, story), f.fFacts);
	cache.push_back(f);
	return cache.back().fFacts;
}

/** The text frames among `item` and, for a group, its items - each once, the graphic frame's UID. */
void CollectTextFrames(IDataBase* db, UID item, std::vector<UID>& out, std::set<UID>& seen)
{
	if (item == kInvalidUID || seen.count(item) > 0)
		return;
	seen.insert(item);
	InterfacePtr<IGraphicFrameData> frame(db, item, UseDefaultIID());
	if (frame != nil)
	{
		InterfacePtr<IMultiColumnTextFrame> mc(frame->QueryMCTextFrame());
		if (mc != nil)
		{
			out.push_back(item);
			return;
		}
	}
	if (db->GetClass(item) != kGroupItemBoss)
		return;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const int32 n = (hier != nil) ? hier->GetChildCount() : 0;
	for (int32 i = 0; i < n; ++i)
		CollectTextFrames(db, hier->GetChildUID(i), out, seen);
}

std::wstring WideOf(const PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* b = s.GrabUTF16Buffer(&n);
	return (b != nil && n > 0) ? std::wstring(reinterpret_cast<const wchar_t*>(b), static_cast<size_t>(n)) : std::wstring();
}

/** A page's name as part of a file name: what Windows refuses in a path made "_". */
std::wstring FileSafe(const std::wstring& name)
{
	std::wstring out = name;
	for (size_t i = 0; i < out.size(); ++i)
	{
		const wchar_t c = out[i];
		if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
			out[i] = L'_';
	}
	return out;
}

bool16 BoxBefore(const KCMPageDocx::Box& a, const KCMPageDocx::Box& b)
{
	// Top to bottom, then left to right - a hundredth of a point is the same line.
	const double dy = a.fY - b.fY;
	if (dy < -0.01 || dy > 0.01)
		return (dy < 0) ? kTrue : kFalse;
	return (a.fX < b.fX) ? kTrue : kFalse;
}

/** What the export counts as it goes, for its one sentence. */
struct Tally
{
	int32		fPages;			// files written
	int32		fBoxes;
	int32		fTableOrNote;
	int32		fHidden;
	int32		fEmpty;
	int32		fRefused;
	int32		fNotAPage;		// a master page, or no page of this document
	int32		fNoFrame;		// a page with no text frame
	PMString	fFirstRefusal;

	Tally() : fPages(0), fBoxes(0), fTableOrNote(0), fHidden(0), fEmpty(0), fRefused(0), fNotAPage(0), fNoFrame(0)
	{
		fFirstRefusal.SetTranslatable(kFalse);
	}

	void Refuse(const char* what, UID uid, const std::string& why)
	{
		++fRefused;
		if (!fFirstRefusal.IsEmpty())
			return;
		fFirstRefusal = what;
		fFirstRefusal.Append(" ");
		fFirstRefusal.AppendNumber(static_cast<int32>(uid.Get()));
		fFirstRefusal.Append(": ");
		fFirstRefusal.Append(KCMTextWords::PMStringOfUtf8(why));
		fFirstRefusal.SetTranslatable(kFalse);
	}
};

/** One frame as its box - kFalse (the tally says why) when it cannot be one. The box's number is given later. */
bool16 BoxOfFrame(IDataBase* db, UID frameUID, const PMMatrix& spreadToPage, const PMPoint& origin,
				  std::vector<StoryFacts>& cache, Tally& tally, KCMPageDocx::Box& out)
{
	InterfacePtr<IGraphicFrameData> frame(db, frameUID, UseDefaultIID());
	InterfacePtr<IMultiColumnTextFrame> mc((frame != nil) ? frame->QueryMCTextFrame() : nil);
	InterfacePtr<ITextModel> model((mc != nil) ? mc->QueryTextModel() : nil);
	if (model == nil)
		return kFalse;
	if (KCMIsFrameHidden(db, frameUID))
	{
		++tally.fHidden;
		return kFalse;
	}
	const TextIndex start = mc->TextStart();
	const int32 span = mc->TextSpan();
	if (span <= 0)
	{
		++tally.fEmpty;			// a frame of a thread the text has not reached
		return kFalse;
	}
	if (KCMPagePartHoldsTableOrNote(model, start, span))
	{
		++tally.fTableOrNote;
		return kFalse;
	}

	const UID storyUID = ::GetUID(model);
	const KCMPageStoryFacts& facts = FactsOf(db, storyUID, cache);
	if (!facts.fReadable)
	{
		tally.Refuse("frame", frameUID, "its story cannot be read the way the export reads it");
		return kFalse;
	}
	KCMPageSplice::Place from;
	KCMPageSplice::Place to;
	if (!KCMPageSplice::PlaceOfIndex(facts.fParas, facts.fAttrs, facts.fStarts, start, from)
		|| !KCMPageSplice::PlaceOfEnd(facts.fParas, facts.fAttrs, facts.fStarts, start, start + span, to))
	{
		tally.Refuse("frame", frameUID, "its text is not in its story's body");
		return kFalse;
	}
	const KCMStoryShape::Story piece = KCMPageSplice::Cut(facts.fShape, from, to);

	// ★THE PART GOES BACK EXACTLY AS IT WAS CUT - what the import will do with an untouched box.
	{
		KCMStoryShape::Story back = facts.fShape;
		std::string why;
		if (!KCMPageSplice::Splice(back, from, to, piece, why))
		{
			tally.Refuse("frame", frameUID, why);
			return kFalse;
		}
		if (!KCMStoryShape::Same(facts.fShape, back, why, kTrue))
		{
			tally.Refuse("frame", frameUID, "the part does not go back the way it was cut (" + why + ")");
			return kFalse;
		}
	}

	KCMPageDocx::Box box;
	box.fFrame = static_cast<int32>(frameUID.Get());
	box.fStory = static_cast<int32>(storyUID.Get());
	box.fStart = static_cast<int32>(start);
	box.fEnd = static_cast<int32>(start + span);
	KCMPageSplice::Fingerprint(piece, box.fPrintBytes, box.fPrintCrc);
	box.fContent = piece;

	// ---- where it stands: the frame's bounds, through the spread, on the page, from its top left ----------------
	InterfacePtr<IGeometry> geometry(db, frameUID, UseDefaultIID());
	if (geometry == nil)
		return kFalse;
	PMRect r = geometry->GetPathBoundingBox();
	const PMMatrix toSpread = ::InnerToSpreadMatrix(geometry);
	toSpread.Transform(&r);
	spreadToPage.Transform(&r);
	box.fX = ::ToDouble(r.Left() - origin.X());
	box.fY = ::ToDouble(r.Top() - origin.Y());
	box.fW = ::ToDouble(r.Width());
	box.fH = ::ToDouble(r.Height());
	InterfacePtr<ITextInset> inset(db, frameUID, UseDefaultIID());
	if (inset != nil)
	{
		const PMRect in = inset->GetRectInset();
		box.fInsetL = ::ToDouble(in.Left());
		box.fInsetT = ::ToDouble(in.Top());
		box.fInsetR = ::ToDouble(in.Right());
		box.fInsetB = ::ToDouble(in.Bottom());
	}
	InterfacePtr<IStoryOptions> options(mc->QueryStoryOptions());
	box.fVertical = (options != nil && options->IsVertical()) ? kTrue : kFalse;

	// ---- what the box's words look like: its first character's size and font --------------------------------------
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	IDrawingStyle* const style = (scanner != nil) ? scanner->GetCompleteStyleAt(start) : nil;
	if (style != nil)
	{
		box.fPointSize = ::ToDouble(style->GetPointSize());
		InterfacePtr<IPMFont> font(style->QueryFont());
		if (font != nil)
		{
			PMString name = font->GetPlatformDependentName(IFontMgr::kOriginalWinOS);
			if (name.IsEmpty())
				font->AppendFamilyNameNative(name);
			box.fFontName = name.GetUTF8String();
		}
	}

	// ★AND IT READS BACK AS WRITTEN - alone on a page of its own, in memory, nothing on disk.
	{
		KCMPageDocx::Page one;
		box.fN = 1;
		one.fBoxes.push_back(box);
		std::vector<KCMZipStore::Entry> parts;
		std::string why;
		KCMPageDocx::ReadResult read;
		if (!KCMPageDocx::WriteParts(one, parts, why) || !KCMPageDocx::ReadParts(parts, read, why))
		{
			tally.Refuse("frame", frameUID, why);
			return kFalse;
		}
		KCMStoryShape::Story settled = piece;
		KCMStoryDocx::SettleForThisFormat(settled);
		if (read.fBoxes.size() != 1 || !read.fBoxes[0].fWhyNot.empty()
			|| !KCMStoryShape::Same(settled, read.fBoxes[0].fBox.fContent, why, kTrue))
		{
			tally.Refuse("frame", frameUID, read.fBoxes.empty() ? std::string("the box does not read back")
								 : (!read.fBoxes[0].fWhyNot.empty() ? read.fBoxes[0].fWhyNot : "the box does not read back as written (" + why + ")"));
			return kFalse;
		}
	}
	out = box;
	return kTrue;
}

void AppendCount(PMString& out, const char* before, int32 n, const char* after)
{
	out.Append(before);
	out.AppendNumber(n);
	out.Append(after);
}

}	// anonymous namespace

bool16 KCMExportPagesAsWord(IDataBase* db, const UIDList& pages, const IDFile& parent, PMString& outMessage)
{
	outMessage.Clear();
	outMessage.SetTranslatable(kFalse);
	if (db == nil)
	{
		outMessage = "Export Pages to Word: there is no document";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	if (KCMIsPdfDoc(db))
	{
		outMessage = kKCMPdfTargetRefusal;
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}
	PMString parentPath;
	FileUtils::IDFileToPMString(parent, parentPath);
	if (parentPath.IsEmpty())
	{
		outMessage = "Export Pages to Word: the chosen folder could not be read";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	// ★★READING COMPOSES, AND COMPOSING DIRTIES - the document is left exactly as clean as it was found.
	IDataBase::SaveRestoreModifiedState guard(db);

	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
	{
		outMessage = "Export Pages to Word: the document has no pages";
		outMessage.SetTranslatable(kFalse);
		return kFalse;
	}

	const std::wstring stem = KCMDocumentFileStem(db);
	std::vector<StoryFacts> cache;
	Tally tally;
	std::vector< std::vector<KCMZipStore::Entry> > files;
	std::vector<std::wstring> leaves;
	std::set<UID> done;

	// ---- every page, in memory first: no folder is made until there is a file to put in it --------------------------
	for (int32 i = 0; i < pages.Length(); ++i)
	{
		const UID pageUID = pages[i];
		if (done.count(pageUID) > 0)
			continue;
		done.insert(pageUID);
		InterfacePtr<IGeometry> pageGeo(db, pageUID, UseDefaultIID());
		InterfacePtr<IHierarchy> pageHier(db, pageUID, UseDefaultIID());
		// ⚠A MASTER PAGE IS NOT IN THE PAGE LIST - its items are every page's, not a page's to edit.
		if (pageGeo == nil || pageHier == nil || pageList->GetPageIndex(pageUID) < 0)
		{
			++tally.fNotAPage;
			continue;
		}
		InterfacePtr<ISpread> spread(db, pageHier->GetSpreadUID(), UseDefaultIID());
		if (spread == nil)
		{
			++tally.fNotAPage;
			continue;
		}
		int32 pgPos = -1;
		for (int32 p = 0; p < spread->GetNumPages() && pgPos < 0; ++p)
		{
			if (spread->GetNthPageUID(p) == pageUID)
				pgPos = p;
		}
		if (pgPos < 0)
		{
			++tally.fNotAPage;
			continue;
		}

		// The page's own coordinates, from its top left (KCMPageNumberMarker.cpp walks a page the same way).
		const PMMatrix pageToSpread = ::InnerToSpreadMatrix(pageGeo);
		const PMMatrix spreadToPage = pageToSpread.Inverse();
		const PMRect pageBounds = pageGeo->GetPathBoundingBox();
		const PMPoint origin(pageBounds.Left(), pageBounds.Top());

		UIDList items(db);
		spread->GetItemsOnPage(pgPos, &items, kFalse /*not the page itself*/, kFalse /*no pasteboard*/,
							   kFalse /*not the slug's*/);
		std::vector<UID> frames;
		std::set<UID> seen;
		for (int32 k = 0; k < items.Length(); ++k)
			CollectTextFrames(db, items[k], frames, seen);
		if (frames.empty())
		{
			++tally.fNoFrame;
			continue;
		}

		KCMPageDocx::Page page;
		page.fPageUid = static_cast<int32>(pageUID.Get());
		PMString name;
		pageList->GetPageString(pageUID, &name, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);
		page.fPageName = name.GetUTF8String();
		page.fWidth = ::ToDouble(pageBounds.Width());
		page.fHeight = ::ToDouble(pageBounds.Height());
		for (size_t f = 0; f < frames.size(); ++f)
		{
			KCMPageDocx::Box box;
			if (BoxOfFrame(db, frames[f], spreadToPage, origin, cache, tally, box))
				page.fBoxes.push_back(box);
		}
		if (page.fBoxes.empty())
			continue;
		// Top to bottom, then left to right - a handful of boxes: an insertion sort.
		for (size_t a = 1; a < page.fBoxes.size(); ++a)
		{
			for (size_t b = a; b > 0 && BoxBefore(page.fBoxes[b], page.fBoxes[b - 1]); --b)
				std::swap(page.fBoxes[b], page.fBoxes[b - 1]);
		}
		for (size_t b = 0; b < page.fBoxes.size(); ++b)
			page.fBoxes[b].fN = static_cast<int32>(b) + 1;

		std::vector<KCMZipStore::Entry> parts;
		std::string why;
		if (!KCMPageDocx::WriteParts(page, parts, why))
		{
			tally.Refuse("page", pageUID, why);
			continue;
		}
		// ★TWO PAGES OF ONE NAME (two sections numbered alike) get two files.
		std::wstring leaf = stem + L" p" + FileSafe(WideOf(name));
		for (int32 again = 2; ; ++again)
		{
			bool16 taken = kFalse;
			for (size_t l = 0; l < leaves.size() && !taken; ++l)
				taken = (leaves[l] == leaf + L".docx") ? kTrue : kFalse;
			if (!taken)
				break;
			leaf = stem + L" p" + FileSafe(WideOf(name)) + L" (" + std::to_wstring(again) + L")";
		}
		leaves.push_back(leaf + L".docx");
		files.push_back(parts);
		tally.fBoxes += static_cast<int32>(page.fBoxes.size());
	}

	// ---- the files ----------------------------------------------------------------------------------------------
	IDFile folder;
	bool16 folderMade = kFalse;
	int32 notWritten = 0;
	if (!files.empty())
	{
		folderMade = KCMMakeDatedExportFolder(parent, stem + L" pages", folder);
		for (size_t i = 0; folderMade && i < files.size(); ++i)
		{
			std::string docx;
			KCMZipStore::Write(files[i], docx);
			PMString leaf;
			leaf.AppendW(reinterpret_cast<const UTF16TextChar*>(leaves[i].c_str()));
			leaf.SetTranslatable(kFalse);
			IDFile file = folder;
			FileUtils::AppendPath(&file, leaf);
			if (KCMWriteFileBytes(file, docx))
				++tally.fPages;
			else
				++notWritten;
		}
	}

	// ---- one sentence --------------------------------------------------------------------------------------------
	outMessage = "Export Pages to Word: ";
	if (files.empty())
		outMessage.Append("nothing to write");
	else if (!folderMade)
		outMessage.Append("the export folder could not be created");
	else
	{
		PMString path;
		FileUtils::IDFileToPMString(folder, path);
		AppendCount(outMessage, "", tally.fPages, " page(s)");
		AppendCount(outMessage, " (", tally.fBoxes, " text box(es)) to ");
		outMessage.Append(path);
		if (notWritten > 0)
			AppendCount(outMessage, ", ", notWritten, " file(s) could not be written (is the folder writable, is the disk full?)");
	}
	const int32 leftOut = tally.fTableOrNote + tally.fHidden + tally.fEmpty + tally.fRefused;
	if (leftOut > 0)
	{
		AppendCount(outMessage, "; ", leftOut, " frame(s) left out (");
		bool16 first = kTrue;
		if (tally.fTableOrNote > 0) { AppendCount(outMessage, "", tally.fTableOrNote, " with a table or a note"); first = kFalse; }
		if (tally.fHidden > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fHidden, " hidden"); first = kFalse; }
		if (tally.fEmpty > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fEmpty, " empty"); first = kFalse; }
		if (tally.fRefused > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fRefused, " refused - the first: "); outMessage.Append(tally.fFirstRefusal); }
		outMessage.Append(") - their stories can go out whole with Export Story Text...");
	}
	if (tally.fNoFrame > 0)
		AppendCount(outMessage, "; ", tally.fNoFrame, " page(s) with no text frame");
	if (tally.fNotAPage > 0)
		AppendCount(outMessage, "; ", tally.fNotAPage, " master or unknown page(s) passed over");
	outMessage.SetTranslatable(kFalse);
	return (tally.fPages > 0) ? kTrue : kFalse;
}

// End, KCMPageExport.cpp.
