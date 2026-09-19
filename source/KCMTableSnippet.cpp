//========================================================================================
//
//  KCMTableSnippet.cpp -- see the header.
//
//  THE CUTS ARE TEXT CUTS, NOT EDITS: a subtree is found by its tags and copied out untouched,
//  the way KCMPdfSpike's MakeObjectPart cuts a <Story> for an IDML part ("a cut cannot corrupt
//  what it does not touch"). Nothing inside a <Table> is read.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "IDocumentList.h"		// FindDocByDataBase - "is this database a document the session has"
#include "IDOMElement.h"
#include "IINXManager.h"
#include "IPMStream.h"
#include "ISession.h"
#include "AppFrameworkID.h"		// kActionExportPolicyBoss - the policy S17.8 measured to work
#include "ErrorUtils.h"			// GlobalErrorStatePreserver
#include "INXCoreID.h"			// IID_IINXEXPORTPOLICY
#include "PersistUtils.h"
#include "StreamUtil.h"

#include "KCMTableSnippet.h"
#include "KCMMemXferBytes.h"

namespace
{

/** The start of the `nth` occurrence of `needle` in [from, end) of text, or std::string::npos. */
size_t FindNth(const std::string& text, const char* needle, size_t from, size_t end, int32 nth)
{
	size_t at = from;
	const size_t len = std::strlen(needle);
	for (int32 seen = 0; ; ++seen)
	{
		at = text.find(needle, at);
		if (at == std::string::npos || at >= end)
			return std::string::npos;
		if (seen == nth)
			return at;
		at += len;
	}
}

/** [start, end) of the element whose start tag begins at `open` (open must point at "<Name"),
    matching nested elements of the SAME name. end = one past "</Name>". npos when unbalanced. */
size_t ElementEnd(const std::string& text, size_t open, const char* name)
{
	const std::string openTag = std::string("<") + name;			// "<Table" - followed by a space or '>'
	const std::string closeTag = std::string("</") + name + ">";
	int32 depth = 0;
	size_t at = open;
	for (;;)
	{
		const size_t nextOpen = text.find(openTag, at);
		const size_t nextClose = text.find(closeTag, at);
		if (nextClose == std::string::npos)
			return std::string::npos;
		if (nextOpen != std::string::npos && nextOpen < nextClose)
		{
			// "<Table" must not be "<TableStyle": the character after the name decides.
			const char after = (nextOpen + openTag.size() < text.size()) ? text[nextOpen + openTag.size()] : '\0';
			if (after == ' ' || after == '>' || after == '/')
				++depth;
			at = nextOpen + openTag.size();
			continue;
		}
		--depth;
		at = nextClose + closeTag.size();
		if (depth == 0)
			return at;
	}
}

/** Cut one element by name out of text - the first occurrence whose tag is exactly `name`. */
bool16 CutFirstElement(const std::string& text, const char* name, std::string& out)
{
	const std::string openTag = std::string("<") + name;
	size_t open = 0;
	for (;;)
	{
		open = text.find(openTag, open);
		if (open == std::string::npos)
			return kFalse;
		const char after = (open + openTag.size() < text.size()) ? text[open + openTag.size()] : '\0';
		if (after == ' ' || after == '>')
			break;
		open += openTag.size();
	}
	const size_t end = ElementEnd(text, open, name);
	if (end == std::string::npos)
		return kFalse;
	out = text.substr(open, end - open);
	return kTrue;
}

/** The value of attribute `name` inside the start tag that ends at `tagEnd`. Empty when absent. */
std::string AttributeIn(const std::string& text, size_t open, size_t tagEnd, const char* name)
{
	const std::string needle = std::string(" ") + name + "=\"";
	const size_t at = text.find(needle, open);
	if (at == std::string::npos || at >= tagEnd)
		return std::string();
	const size_t from = at + needle.size();
	const size_t to = text.find('"', from);
	if (to == std::string::npos || to > tagEnd)
		return std::string();
	return text.substr(from, to - from);
}

/** One outermost <Cell> of a table's XML, at or after `from`.
	@param outBodyStart, outBodyEnd the contents BETWEEN the tags - what a merge replaces.
	@param outEnd one past </Cell>, where the next search starts (so a nested table's cells, which
		stand inside this one, are never visited).
	@param outEmptyTag kTrue for <Cell … />, which has no contents and is left alone.
	@return kFalse when there is no next cell. */
bool16 NextCell(const std::string& table, size_t from, size_t& outBodyStart, size_t& outBodyEnd,
				size_t& outEnd, std::string& outName, bool16& outEmptyTag)
{
	static const char kOpen[] = "<Cell";
	static const char kClose[] = "</Cell>";
	size_t open = from;
	for (;;)
	{
		open = table.find(kOpen, open);
		if (open == std::string::npos)
			return kFalse;
		const char after = (open + 5 < table.size()) ? table[open + 5] : '\0';
		if (after == ' ' || after == '>' || after == '/')
			break;
		open += 5;					// <CellStyle…, <CellStyleGroup… - a different element
	}
	const size_t tagEnd = table.find('>', open);
	if (tagEnd == std::string::npos)
		return kFalse;
	outName = AttributeIn(table, open, tagEnd, "Name");
	if (tagEnd > open && table[tagEnd - 1] == '/')
	{
		outEmptyTag = kTrue;
		outBodyStart = tagEnd;
		outBodyEnd = tagEnd;
		outEnd = tagEnd + 1;
		return kTrue;
	}
	const size_t end = ElementEnd(table, open, "Cell");
	if (end == std::string::npos)
		return kFalse;
	outEmptyTag = kFalse;
	outBodyStart = tagEnd + 1;
	outBodyEnd = end - (sizeof(kClose) - 1);
	outEnd = end;
	return kTrue;
}

}	// anonymous namespace

bool16 KCMMergeTableCells(const std::string& olderTableXml, const std::string& liveTableXml,
						  std::string& outMerged, int32& outKept)
{
	outMerged.clear();
	outKept = 0;
	if (olderTableXml.empty())
		return kFalse;

	// The live table's outermost cells, by the address each start tag carries.
	std::map<std::string, std::string> live;
	{
		size_t at = 0, bodyStart = 0, bodyEnd = 0, end = 0;
		std::string name;
		bool16 emptyTag = kFalse;
		while (NextCell(liveTableXml, at, bodyStart, bodyEnd, end, name, emptyTag))
		{
			if (!name.empty() && !emptyTag)
				live[name] = liveTableXml.substr(bodyStart, bodyEnd - bodyStart);
			at = end;
		}
	}

	// Task Start's table, copied through - with the live contents put in wherever both sides have
	// that address and the two differ. Everything outside the cells (the table's own tag, the
	// column and row elements) is Task Start's, untouched.
	size_t at = 0, bodyStart = 0, bodyEnd = 0, end = 0, copiedTo = 0;
	std::string name;
	bool16 emptyTag = kFalse;
	while (NextCell(olderTableXml, at, bodyStart, bodyEnd, end, name, emptyTag))
	{
		at = end;
		if (name.empty() || emptyTag)
			continue;
		const std::map<std::string, std::string>::const_iterator it = live.find(name);
		if (it == live.end())
			continue;										// a cell Task Start alone has: it comes back as it was
		if (it->second == olderTableXml.substr(bodyStart, bodyEnd - bodyStart))
			continue;										// the same on both sides: nothing to keep
		outMerged.append(olderTableXml, copiedTo, bodyStart - copiedTo);
		outMerged += it->second;
		copiedTo = bodyEnd;
		++outKept;
	}
	outMerged.append(olderTableXml, copiedTo, olderTableXml.size() - copiedTo);
	return kTrue;
}

bool16 KCMCutTableXml(const char* xml, size_t size, UID storyUID, int32 ordinal, std::string& outTable)
{
	outTable.clear();
	if (xml == nil || size == 0 || ordinal < 0)
		return kFalse;
	const std::string text(xml, size);

	// The story: the <Story …> whose start tag carries Self="u<hex>" (the origin's INX and a story's
	// own INX both name it by the document's uid - measured 2026-09-19, u15a = 346).
	char self[32];
	std::snprintf(self, sizeof(self), "Self=\"u%x\"", static_cast<unsigned>(storyUID.Get()));
	size_t storyOpen = 0;
	size_t storyEnd = std::string::npos;
	for (;;)
	{
		storyOpen = text.find("<Story ", storyOpen);
		if (storyOpen == std::string::npos)
			return kFalse;
		const size_t tagEnd = text.find('>', storyOpen);
		if (tagEnd == std::string::npos)
			return kFalse;
		if (text.substr(storyOpen, tagEnd - storyOpen).find(self) != std::string::npos)
		{
			storyEnd = ElementEnd(text, storyOpen, "Story");
			break;
		}
		storyOpen = tagEnd;
	}
	if (storyEnd == std::string::npos)
		return kFalse;

	// The ordinal-th "<Table " inside it, in document order - nested tables counted, which is the order
	// KCMTextRead numbers them in (a nested table's cells begin after the cell that holds it).
	const size_t tableOpen = FindNth(text, "<Table ", storyOpen, storyEnd, ordinal);
	if (tableOpen == std::string::npos)
		return kFalse;
	const size_t tableEnd = ElementEnd(text, tableOpen, "Table");
	if (tableEnd == std::string::npos || tableEnd > storyEnd)
		return kFalse;
	outTable = text.substr(tableOpen, tableEnd - tableOpen);
	return kTrue;
}

void KCMCutTableStyleGroups(const char* xml, size_t size, std::string& outGroups)
{
	outGroups.clear();
	if (xml == nil || size == 0)
		return;
	const std::string text(xml, size);
	std::string one;
	if (CutFirstElement(text, "RootCellStyleGroup", one))
	{
		outGroups += one;
		outGroups += "\n";
	}
	if (CutFirstElement(text, "RootTableStyleGroup", one))
	{
		outGroups += one;
		outGroups += "\n";
	}
}

void KCMBuildTableSnippet(const std::string& tableXml, const std::string& styleGroups, std::string& outSnippet)
{
	// The dress, copied from a snippet InDesign itself wrote (table-only-frame.idms, 2026-09-19) and cut
	// to what the import needs: the PI header (⚠type="snippet" - the importer refuses type="action"),
	// the style groups, one spread with one frame, one story with the table. The uids are the ones
	// that file used; the import renumbers everything.
	static const char kHead[] =
		"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
		"<?aid style=\"50\" type=\"snippet\" readerVersion=\"6.0\" featureSet=\"257\" product=\"21.0(2)\" ?>\n"
		"<?aid SnippetType=\"PageItem\"?>\n"
		"<Document DOMVersion=\"21.0\" Self=\"d\">\n";
	static const char kSpread[] =
		"<Spread Self=\"uf0\">\n"
		"<TextFrame Self=\"u1ab\" ParentStory=\"u197\" PreviousTextFrame=\"n\" NextTextFrame=\"n\" ContentType=\"TextType\""
		" ParentInterfaceChangeCount=\"\" TargetInterfaceChangeCount=\"\" LastUpdatedInterfaceChangeCount=\"\" OverriddenPageItemProps=\"\""
		" HorizontalLayoutConstraints=\"FlexibleDimension FixedDimension FlexibleDimension\" VerticalLayoutConstraints=\"FlexibleDimension FixedDimension FlexibleDimension\""
		" FlexItemWidthMode=\"FlexFixed\" FlexItemHeightMode=\"FlexFixed\" GradientFillStart=\"0 0\" GradientFillLength=\"0\" GradientFillAngle=\"0\""
		" GradientStrokeStart=\"0 0\" GradientStrokeLength=\"0\" GradientStrokeAngle=\"0\" Locked=\"false\" LocalDisplaySetting=\"Default\""
		" GradientFillHiliteLength=\"0\" GradientFillHiliteAngle=\"0\" GradientStrokeHiliteLength=\"0\" GradientStrokeHiliteAngle=\"0\""
		" AppliedObjectStyle=\"ObjectStyle/$ID/[Normal Text Frame]\" Visible=\"true\" Name=\"$ID/\" ItemTransform=\"1 0 0 1 0 0\">\n"
		"<Properties>\n<PathGeometry>\n<GeometryPathType PathOpen=\"false\">\n<PathPointArray>\n"
		"<PathPointType Anchor=\"0 0\" LeftDirection=\"0 0\" RightDirection=\"0 0\" />\n"
		"<PathPointType Anchor=\"0 400\" LeftDirection=\"0 400\" RightDirection=\"0 400\" />\n"
		"<PathPointType Anchor=\"400 400\" LeftDirection=\"400 400\" RightDirection=\"400 400\" />\n"
		"<PathPointType Anchor=\"400 0\" LeftDirection=\"400 0\" RightDirection=\"400 0\" />\n"
		"</PathPointArray>\n</GeometryPathType>\n</PathGeometry>\n</Properties>\n"
		"<TextFramePreference TextColumnCount=\"1\" TextColumnFixedWidth=\"400\" TextColumnMaxWidth=\"0\">\n"
		"<Properties>\n<InsetSpacing type=\"list\">\n<ListItem type=\"unit\">0</ListItem>\n<ListItem type=\"unit\">0</ListItem>\n"
		"<ListItem type=\"unit\">0</ListItem>\n<ListItem type=\"unit\">0</ListItem>\n</InsetSpacing>\n</Properties>\n"
		"</TextFramePreference>\n"
		"<TextWrapPreference Inverse=\"false\" ApplyToMasterPageOnly=\"false\" TextWrapSide=\"BothSides\" TextWrapMode=\"None\">\n"
		"<Properties>\n<TextWrapOffset Top=\"0\" Left=\"0\" Bottom=\"0\" Right=\"0\" />\n</Properties>\n</TextWrapPreference>\n"
		"</TextFrame>\n"
		"</Spread>\n";
	static const char kStoryOpen[] =
		"<Story Self=\"u197\" AppliedTOCStyle=\"n\" UserText=\"true\" IsEndnoteStory=\"false\" TrackChanges=\"false\" StoryTitle=\"$ID/\" AppliedNamedGrid=\"n\">\n"
		"<StoryPreference OpticalMarginAlignment=\"false\" OpticalMarginSize=\"9.2125984251969\" FrameType=\"TextFrameType\" StoryOrientation=\"Horizontal\" StoryDirection=\"LeftToRightDirection\" />\n"
		"<InCopyExportOption IncludeGraphicProxies=\"true\" IncludeAllResources=\"false\" />\n"
		"<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/$ID/NormalParagraphStyle\">\n"
		"<CharacterStyleRange AppliedCharacterStyle=\"CharacterStyle/$ID/[No character style]\">\n";
	static const char kStoryClose[] =
		"\n</CharacterStyleRange>\n</ParagraphStyleRange>\n</Story>\n"
		"</Document>\n";

	outSnippet.clear();
	outSnippet.reserve(sizeof(kHead) + styleGroups.size() + sizeof(kSpread) + sizeof(kStoryOpen) + tableXml.size() + sizeof(kStoryClose));
	outSnippet += kHead;
	outSnippet += styleGroups;
	outSnippet += kSpread;
	outSnippet += kStoryOpen;
	outSnippet += tableXml;
	outSnippet += kStoryClose;
}

bool16 KCMExportStoryInx(IDataBase* db, UID storyUID, KCMMemXferBytes& out)
{
	// The call KCMPdfSpike's S17.8 measured (ExportElementAsInxWith), on a story: what comes out is the
	// <Story> the IDML holds, byte for byte, dressed as <?aid type="action"?><Document>…</Document>.
	if (db == nil || storyUID == kInvalidUID)
		return kFalse;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IDocumentList> docList(session != nil ? session->QueryDocumentList() : nil);
	// ★★★THE DATABASE HAS TO BE ONE THE SESSION KNOWS ABOUT - the guard KCMResourceSnapshot.cpp:62-93
	//   states at length: handed a cloned database, ExportINX does not fail and does not return nil,
	//   THE PROCESS IS GONE. There is nothing to nil-check afterwards; the only defence is to refuse
	//   before starting. A KIDMCP comparison lends exactly such a database as its Source.
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return kFalse;
	InterfacePtr<IDOMElement> element(db, storyUID, UseDefaultIID());
	InterfacePtr<IINXManager> inx(session->QueryINXManager());
	// ⚠IINXExportPolicy is forward-declared only in the SDK: received as IPMUnknown and C-cast, the
	//   way the product's own InCopyImportProvider.cpp does (KCMPdfSpike says so at length).
	InterfacePtr<IPMUnknown> holder((IPMUnknown*)::CreateObject(kActionExportPolicyBoss, IID_IINXEXPORTPOLICY));
	if (element == nil || inx == nil || holder == nil)
		return kFalse;
	IINXExportPolicy* const policy = (IINXExportPolicy*)holder.get();
	InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&out, kFalse, kFalse));
	if (stream == nil)
		return kFalse;
	IDOMElement::ElementList roots;
	roots.push_back(element);
	ErrorCode err = kFailure;
	{
		// ★THE CALLER'S ERROR STATE IS KEPT OUT OF THIS (KCMResourceSnapshot.cpp:134-143): an error
		//   raised by the export would otherwise stand in the global state and pull down the very next
		//   command - and the next command here is the one that puts the table back.
		GlobalErrorStatePreserver errorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		inx->BeginExportSession();
		element->Reset();		// inside the session: IDOMElement.h:58-60 - the interface is for use under INX context
		err = inx->ExportINX(roots, policy, stream, kSuppressUI);
		element->Reset();		// and again when done (IDOMElement.h:54-56), so no cache is left on the story
		inx->EndExportSession();
	}
	stream->Flush();
	return (err == kSuccess && out.GetSize() > 0) ? kTrue : kFalse;
}

// End, KCMTableSnippet.cpp.
