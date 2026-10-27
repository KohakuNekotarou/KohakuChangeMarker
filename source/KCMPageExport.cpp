//========================================================================================
//
//  KCMPageExport.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "IAdobeMediaMgmtMetaData.h"	// the document's xmpMM ids - what a page file names its document by
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
#include "ITextUtils.h"				// CollectOwnedItems - an endnote's reference is told by what the text owns there
#include "TextID.h"					// kEndnoteAnchorBoss
#include "Utils.h"
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
		// (An endnote's reference is caught by the footnote's character - KCMPagePartHoldsEndnote says why.)
		const int32 c = static_cast<int32>((*iter).GetValue());
		if (c == kTextChar_Table || c == kTextChar_TableContinued || c == kTextChar_FootnoteMarker
			|| c == kTextChar_EndnoteMarker)
			return kTrue;
	}
	return kFalse;
}

bool16 KCMPagePartHoldsEndnote(ITextModel* model, TextIndex start, int32 span)
{
	// ★BY WHAT THE TEXT OWNS THERE, NOT BY ITS CHARACTER (2026-10-26 - the first version asked for kTextChar_EndnoteMarker
	//   and wrote an endnote's frame, page-tables-notes PT1): in the text an endnote's reference is the same U+0004 as a
	//   footnote's, and only its owned item says which (KIDMCP measured it on 2026-10-13 - KIDMCPCallValues' OwnedWordAt).
	//   The route is the SDK's own: SnpManipulateTextEndnotes.cpp collects the owned items and keeps kEndnoteAnchorBoss.
	//   Nested, so that one standing in a table's cell counts too.
	if (model == nil || span <= 0)
		return kFalse;
	Utils<ITextUtils> textUtils;
	if (!textUtils)
		return kTrue;			// cannot be told: the frame is left out rather than written with a note nobody can carry
	OwnedItemDataList owned;
	textUtils->CollectOwnedItems(model, start, span, &owned, kTrue);
	for (size_t i = 0; i < owned.size(); ++i)
	{
		if (owned[i].fClassID == kEndnoteAnchorBoss)
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

/** The text frames standing INSIDE `item` at any depth - a frame pasted into a shape, a button's or a multi-state object's
	(2026-10-27, the re-inspection's M17 - measured: a text frame inside a rectangle was neither written nor said). Counted,
	not written: a box is a frame of the page itself (KCMPageExport.h). A text frame's own children are its columns. */
int32 CountNestedTextFrames(IDataBase* db, UID item, std::set<UID>& seen)
{
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const int32 n = (hier != nil) ? hier->GetChildCount() : 0;
	int32 found = 0;
	for (int32 i = 0; i < n; ++i)
	{
		const UID child = hier->GetChildUID(i);
		if (child == kInvalidUID || seen.count(child) > 0)
			continue;
		seen.insert(child);
		InterfacePtr<IGraphicFrameData> frame(db, child, UseDefaultIID());
		InterfacePtr<IMultiColumnTextFrame> mc((frame != nil) ? frame->QueryMCTextFrame() : nil);
		if (mc != nil)
			++found;
		else
			found += CountNestedTextFrames(db, child, seen);
	}
	return found;
}

/** The text frames among `item` and, for a group, its items - each once, the graphic frame's UID. The ones standing inside
	any other object onto `outNested` (CountNestedTextFrames). */
void CollectTextFrames(IDataBase* db, UID item, std::vector<UID>& out, std::set<UID>& seen, int32& outNested)
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
	{
		outNested += CountNestedTextFrames(db, item, seen);
		return;
	}
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const int32 n = (hier != nil) ? hier->GetChildCount() : 0;
	for (int32 i = 0; i < n; ++i)
		CollectTextFrames(db, hier->GetChildUID(i), out, seen, outNested);
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
	int32		fEndnote;		// a frame holding an endnote's marker (its words are another story's - 2026-10-26)
	int32		fHidden;
	int32		fEmpty;
	int32		fRefused;
	int32		fNotAPage;		// a master page, or no page of this document
	int32		fNoFrame;		// a page with no text frame
	int32		fThreaded;		// a frame that is not its story's only one (2026-10-26 - BoxOfFrame says why)
	int32		fOverset;		// a frame whose words do not all show
	int32		fVerticalTable;	// a vertical frame whose story holds a table (2026-10-26: Word cannot lay it out)
	int32		fNested;		// a text frame inside another object - pasted into a shape, a button's... (2026-10-27)
	PMString	fFirstRefusal;
	std::vector<PMString>	fThreadedPages;	// where they are, each page once - the sentence names them (the author: "say it
	std::vector<PMString>	fOversetPages;	//   in the panel")
	std::vector<PMString>	fVerticalTablePages;
	std::vector<PMString>	fNestedPages;

	Tally() : fPages(0), fBoxes(0), fEndnote(0), fHidden(0), fEmpty(0), fRefused(0), fNotAPage(0), fNoFrame(0),
			  fThreaded(0), fOverset(0), fVerticalTable(0), fNested(0)
	{
		fFirstRefusal.SetTranslatable(kFalse);
	}

	/** `pageName` onto `pages`, unless it is there already. */
	static void NotePage(std::vector<PMString>& pages, const PMString& pageName)
	{
		if (std::find(pages.begin(), pages.end(), pageName) == pages.end())
			pages.push_back(pageName);
	}

	/** "p1, p3" - the pages as the files name them. */
	static PMString Pages(const std::vector<PMString>& pages)
	{
		PMString out;
		for (size_t i = 0; i < pages.size(); ++i)
		{
			if (i > 0)
				out.Append(", ");
			out.Append("p");
			out.Append(pages[i]);
		}
		out.SetTranslatable(kFalse);
		return out;
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

/** The code points of a UTF-8 text - what a KCMPageSplice::Place's offset counts. */
int32 CodePointsOf(const std::string& utf8)
{
	int32 n = 0;
	for (size_t i = 0; i < utf8.size(); ++i)
	{
		if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80)
			++n;
	}
	return n;
}

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
	// ★★ONE FRAME, ONE WHOLE STORY (2026-10-26, the author: "a threaded frame is not written - export it with Story"; a page
	//   file is for catalogue pages of many frames, and a threaded story - a long one - goes out with Export Story Text). A
	//   frame that is not its story's only one is left out, and so is an OVERSET one: a box holds what its frame shows, and
	//   the import puts a box back over its whole story (KCMPageImport - Box::fWhole), which would take the hidden words out.
	//   The two are said apart, with their pages (the caller's sentence).
	if (!mc->GetIsOnlyMCF())
	{
		++tally.fThreaded;
		return kFalse;
	}
	const TextIndex start = mc->TextStart();
	const int32 span = mc->TextSpan();
	if (span <= 0)
	{
		++tally.fEmpty;			// no text composed into it
		return kFalse;
	}
	if (mc->GetIsOverset())
	{
		++tally.fOverset;
		return kFalse;
	}
	// (an endnote's marker: its words are another story's, which no box carries - 2026-10-26; a table and a footnote go in)
	if (KCMPagePartHoldsEndnote(model, start, span))
	{
		++tally.fEndnote;
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
	// ★★THE BOX IS THE STORY ITSELF (2026-10-26 - its tables and footnotes with it): the frame is the story's only one and
	//   shows all of it (above), so nothing is cut out of the story and nothing has to go back into it; only the direction
	//   is the box's, not the words' (KCMPageSplice::Cut's rule). The frame's range is still asked to be the whole body.
	const int32 last = static_cast<int32>(facts.fShape.fBody.size()) - 1;
	if (last < 0 || from.fPara != 0 || from.fOffset != 0 || to.fPara != last
		|| to.fOffset != CodePointsOf(facts.fShape.fBody[static_cast<size_t>(last)].fText))
	{
		tally.Refuse("frame", frameUID, "the frame does not show its whole story");
		return kFalse;
	}
	KCMStoryShape::Story piece = facts.fShape;
	piece.fVertical = kFalse;

	// ⛔A VERTICAL FRAME WHOSE STORY HOLDS A TABLE IS NOT WRITTEN (2026-10-26, measured in Word 2007: a vertical text box
	//   holding a table is not laid out - no position, no PDF); named with its page, in the caller's sentence.
	InterfacePtr<IStoryOptions> options(mc->QueryStoryOptions());
	const bool16 vertical = (options != nil && options->IsVertical()) ? kTrue : kFalse;
	if (vertical && !piece.fTables.empty())
	{
		++tally.fVerticalTable;
		return kFalse;
	}

	KCMPageDocx::Box box;
	box.fFrame = static_cast<int32>(frameUID.Get());
	box.fStory = static_cast<int32>(storyUID.Get());
	box.fStart = static_cast<int32>(start);
	box.fEnd = static_cast<int32>(start + span);
	KCMPageSplice::Fingerprint(piece, box.fPrintBytes, box.fPrintCrc);
	box.fContent = piece;
	box.fWhole = kTrue;			// the frame is its story's only one and shows all of it (above)

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
	box.fVertical = vertical;

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

void KCMDocumentIds(IDataBase* db, std::string& outId, std::string& outOriginal)
{
	outId.clear();
	outOriginal.clear();
	if (db == nil)
		return;
	InterfacePtr<IAdobeMediaMgmtMetaData> mm(db, db->GetRootUID(), UseDefaultIID());
	if (mm == nil)
		return;
	outId = mm->GetDocumentID().GetUTF8String();
	outOriginal = mm->GetOrigDocumentID().GetUTF8String();
}

KCMIdsMatch KCMDocumentIdsMatch(IDataBase* db, const std::string& id, const std::string& original)
{
	if (id.empty() && original.empty())
		return kKCMIdsNone;
	std::string mine;
	std::string myOriginal;
	KCMDocumentIds(db, mine, myOriginal);
	if (!id.empty() && id == mine)
		return kKCMIdsThis;
	if (!original.empty() && original == myOriginal)
		return kKCMIdsOriginal;
	return kKCMIdsOther;
}

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

	// ★NO DOCUMENT NAME IN THE FILES' NAMES (2026-10-26, the author: "the document may be renamed after the export - decide by
	//   the page alone"): "p<page>.docx" in "Pages <date>"; which document they came from is the tag's, by its ids.
	std::string documentId;
	std::string originalDocumentId;
	KCMDocumentIds(db, documentId, originalDocumentId);
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

		PMString name;
		pageList->GetPageString(pageUID, &name, kFalse, kFalse, kDefaultPageType, kTrue, kTrue);
		name.SetTranslatable(kFalse);

		UIDList items(db);
		spread->GetItemsOnPage(pgPos, &items, kFalse /*not the page itself*/, kFalse /*no pasteboard*/,
							   kFalse /*not the slug's*/);
		std::vector<UID> frames;
		std::set<UID> seen;
		int32 nested = 0;
		for (int32 k = 0; k < items.Length(); ++k)
			CollectTextFrames(db, items[k], frames, seen, nested);
		if (nested > 0)
		{
			tally.fNested += nested;
			Tally::NotePage(tally.fNestedPages, name);
		}
		if (frames.empty())
		{
			if (nested == 0)
				++tally.fNoFrame;		// (a page whose text frames all stand inside other objects is named with them)
			continue;
		}

		KCMPageDocx::Page page;
		page.fPageUid = static_cast<int32>(pageUID.Get());
		page.fPageName = name.GetUTF8String();
		page.fWidth = ::ToDouble(pageBounds.Width());
		page.fHeight = ::ToDouble(pageBounds.Height());
		page.fDocumentId = documentId;
		page.fOriginalDocumentId = originalDocumentId;
		for (size_t f = 0; f < frames.size(); ++f)
		{
			const int32 threadedBefore = tally.fThreaded;
			const int32 oversetBefore = tally.fOverset;
			const int32 verticalTableBefore = tally.fVerticalTable;
			KCMPageDocx::Box box;
			if (BoxOfFrame(db, frames[f], spreadToPage, origin, cache, tally, box))
				page.fBoxes.push_back(box);
			if (tally.fThreaded > threadedBefore)
				Tally::NotePage(tally.fThreadedPages, name);
			if (tally.fOverset > oversetBefore)
				Tally::NotePage(tally.fOversetPages, name);
			if (tally.fVerticalTable > verticalTableBefore)
				Tally::NotePage(tally.fVerticalTablePages, name);
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
		std::wstring leaf = L"p" + FileSafe(WideOf(name));
		for (int32 again = 2; ; ++again)
		{
			bool16 taken = kFalse;
			for (size_t l = 0; l < leaves.size() && !taken; ++l)
				taken = (leaves[l] == leaf + L".docx") ? kTrue : kFalse;
			if (!taken)
				break;
			leaf = L"p" + FileSafe(WideOf(name)) + L" (" + std::to_wstring(again) + L")";
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
		folderMade = KCMMakeDatedExportFolder(parent, L"Pages", folder);
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
	const int32 leftOut = tally.fEndnote + tally.fHidden + tally.fEmpty + tally.fRefused;
	if (leftOut > 0)
	{
		AppendCount(outMessage, "; ", leftOut, " frame(s) left out (");
		bool16 first = kTrue;
		if (tally.fEndnote > 0) { AppendCount(outMessage, "", tally.fEndnote, " with an endnote marker"); first = kFalse; }
		if (tally.fHidden > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fHidden, " hidden"); first = kFalse; }
		if (tally.fEmpty > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fEmpty, " empty"); first = kFalse; }
		if (tally.fRefused > 0) { AppendCount(outMessage, first ? "" : ", ", tally.fRefused, " refused - the first: "); outMessage.Append(tally.fFirstRefusal); }
		outMessage.Append(") - their stories can go out whole with Export Story Text...");
	}
	// ★THE TWO THE AUTHOR ASKED TO BE TOLD OF (2026-10-26), each with where it is.
	if (tally.fThreaded > 0)
	{
		AppendCount(outMessage, "; ", tally.fThreaded, " threaded frame(s) not written (");
		outMessage.Append(Tally::Pages(tally.fThreadedPages));
		outMessage.Append(") - a threaded story goes out whole with Export Story Text...");
	}
	if (tally.fOverset > 0)
	{
		AppendCount(outMessage, "; ", tally.fOverset, " overset frame(s) not written (");
		outMessage.Append(Tally::Pages(tally.fOversetPages));
		outMessage.Append(") - their words do not all show: fit them first, or use Export Story Text...");
	}
	if (tally.fVerticalTable > 0)
	{
		AppendCount(outMessage, "; ", tally.fVerticalTable, " vertical frame(s) with a table not written (");
		outMessage.Append(Tally::Pages(tally.fVerticalTablePages));
		outMessage.Append(") - Word cannot lay a table out in a vertical text box: use Export Story Text...");
	}
	if (tally.fNested > 0)
	{
		AppendCount(outMessage, "; ", tally.fNested, " text frame(s) inside another object not written (");
		outMessage.Append(Tally::Pages(tally.fNestedPages));
		outMessage.Append(") - a frame pasted into a shape, a button's: their stories can go out whole with Export Story Text...");
	}
	if (tally.fNoFrame > 0)
		AppendCount(outMessage, "; ", tally.fNoFrame, " page(s) with no text frame");
	if (tally.fNotAPage > 0)
		AppendCount(outMessage, "; ", tally.fNotAPage, " master or unknown page(s) passed over");
	outMessage.SetTranslatable(kFalse);
	return (tally.fPages > 0) ? kTrue : kFalse;
}

// End, KCMPageExport.cpp.
