//========================================================================================
//
//  KCMXmlInject.cpp -- see the header. Pure byte work; nothing here touches the SDK.
//
//  The scan is a single pass with one output position (`pos`, the first input byte not yet
//  written) and one search position (`scan`). Each event -- a <Story or <Spread open tag, and
//  inside a story its first <ParagraphStyleRange or its </Story> -- writes the input up to that
//  point, writes what the event calls for, and moves on. Nothing is buffered.
//
//========================================================================================

// The plug-in builds every .cpp through the precompiled header; the offline test supplies an
// empty VCPlugInHeaders.h of its own (work/kcm-origin-test), so this line serves both builds.
#include "VCPlugInHeaders.h"

#include "KCMXmlInject.h"
#include <string.h>
#include <stdlib.h>				// strtod - the page size of <DocumentPreference>

const char* const kKCMOriginUidLabelKey = "KcmOriginUid";

namespace
{

const char* const kStoryOpen   = "<Story ";
const char* const kSpreadOpen  = "<Spread ";
const char* const kStoryClose  = "</Story>";
const char* const kRangeOpen   = "<ParagraphStyleRange";
const char* const kPropsOpen   = "<Properties>";
const char* const kLabelOpen   = "<Label>";
const char* const kSelfAttr    = "Self=\"";
// The sacrificial range, in two halves around the token the caller supplies.
const char* const kDummyHead   =
	"<ParagraphStyleRange AppliedParagraphStyle=\"ParagraphStyle/$ID/NormalParagraphStyle\">"
	"<CharacterStyleRange AppliedCharacterStyle=\"CharacterStyle/$ID/[No character style]\">"
	"<Content>";
const char* const kDummyTail   = "</Content><Br /></CharacterStyleRange></ParagraphStyleRange>";

bool16 StartsWith(const char* xml, size_t size, size_t at, const char* literal)
{
	const size_t n = ::strlen(literal);
	return (at + n <= size && ::memcmp(xml + at, literal, n) == 0) ? kTrue : kFalse;
}

/** The index of `literal` at or after `from`, or size when absent. */
size_t Find(const char* xml, size_t size, size_t from, const char* literal)
{
	const size_t n = ::strlen(literal);
	if (n == 0 || size < n)
		return size;
	for (size_t i = from; i + n <= size; ++i)
		if (xml[i] == literal[0] && ::memcmp(xml + i, literal, n) == 0)
			return i;
	return size;
}

size_t SkipSpace(const char* xml, size_t size, size_t at)
{
	while (at < size && (xml[at] == ' ' || xml[at] == '\t' || xml[at] == '\r' || xml[at] == '\n'))
		++at;
	return at;
}

bool16 Emit(KCMByteSink& out, const char* bytes, size_t count)
{
	return (count == 0) ? kTrue : out.Write(bytes, count);
}

bool16 EmitLiteral(KCMByteSink& out, const char* literal)
{
	return Emit(out, literal, ::strlen(literal));
}

/** Writes <KeyValuePair Key="KcmOriginUid" Value="<self>" /> */
bool16 EmitPair(KCMByteSink& out, const char* self, size_t selfLen)
{
	return EmitLiteral(out, "<KeyValuePair Key=\"") && EmitLiteral(out, kKCMOriginUidLabelKey)
		&& EmitLiteral(out, "\" Value=\"") && Emit(out, self, selfLen) && EmitLiteral(out, "\" />");
}

/** Right after an open tag (pos is the index just past its '>'): write the label into the element,
    merging with a <Properties> / <Label> that is already there. Advances `pos` past whatever it
    consumed of the input. kFalse when the sink refused. */
bool16 EmitLabel(const char* xml, size_t size, size_t& pos, KCMByteSink& out, const char* self, size_t selfLen)
{
	const size_t next = SkipSpace(xml, size, pos);
	if (StartsWith(xml, size, next, kPropsOpen))
	{
		// keep the bytes up to and including <Properties>
		const size_t afterProps = next + ::strlen(kPropsOpen);
		if (!Emit(out, xml + pos, afterProps - pos))
			return kFalse;
		pos = afterProps;
		const size_t inner = SkipSpace(xml, size, pos);
		if (StartsWith(xml, size, inner, kLabelOpen))
		{
			const size_t afterLabel = inner + ::strlen(kLabelOpen);
			if (!Emit(out, xml + pos, afterLabel - pos))
				return kFalse;
			pos = afterLabel;
			return EmitPair(out, self, selfLen);
		}
		return EmitLiteral(out, kLabelOpen) && EmitPair(out, self, selfLen) && EmitLiteral(out, "</Label>");
	}
	return EmitLiteral(out, kPropsOpen) && EmitLiteral(out, kLabelOpen) && EmitPair(out, self, selfLen)
		&& EmitLiteral(out, "</Label></Properties>");
}

}	// namespace

bool16 KCMParseSelfUid(const char* text, size_t length, uint32& outUid)
{
	if (text == nil || length < 2 || text[0] != 'u')
		return kFalse;
	uint32 value = 0;
	for (size_t i = 1; i < length; ++i)
	{
		const char c = text[i];
		uint32 digit = 0;
		if (c >= '0' && c <= '9')      digit = static_cast<uint32>(c - '0');
		else if (c >= 'a' && c <= 'f') digit = static_cast<uint32>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') digit = static_cast<uint32>(c - 'A' + 10);
		else return kFalse;
		if (value > 0x0FFFFFFF)
			return kFalse;		// more than 32 bits
		value = (value << 4) | digit;
	}
	outUid = value;
	return kTrue;
}

namespace
{

/** The value of attribute `name` inside tag [tagStart, tagEnd), or kFalse. The value is the bytes
    between the quotes, not copied. */
bool16 AttributeOf(const char* xml, size_t tagStart, size_t tagEnd, const char* name,
				   size_t& outValueBegin, size_t& outValueEnd)
{
	const size_t n = ::strlen(name);
	for (size_t i = tagStart; i + n + 2 < tagEnd; ++i)
	{
		// the name must start an attribute: preceded by a space, followed by ="
		if (xml[i - 1] != ' ' && xml[i - 1] != '\t' && xml[i - 1] != '\n' && xml[i - 1] != '\r')
			continue;
		if (::memcmp(xml + i, name, n) != 0 || xml[i + n] != '=' || xml[i + n + 1] != '"')
			continue;
		const size_t begin = i + n + 2;
		size_t end = begin;
		while (end < tagEnd && xml[end] != '"')
			++end;
		if (end >= tagEnd)
			return kFalse;
		outValueBegin = begin;
		outValueEnd = end;
		return kTrue;
	}
	return kFalse;
}

bool16 ValueIs(const char* xml, size_t begin, size_t end, const char* literal)
{
	const size_t n = ::strlen(literal);
	return (end - begin == n && ::memcmp(xml + begin, literal, n) == 0) ? kTrue : kFalse;
}

/** A positive number from the attribute's bytes (at most 63 of them). */
bool16 NumberOf(const char* xml, size_t begin, size_t end, double& out)
{
	if (end <= begin || end - begin > 63)
		return kFalse;
	char buf[64];
	::memcpy(buf, xml + begin, end - begin);
	buf[end - begin] = '\0';
	char* stop = nil;
	const double v = ::strtod(buf, &stop);
	if (stop == buf || v <= 0.0)
		return kFalse;
	out = v;
	return kTrue;
}

}	// namespace

bool16 KCMReadDocumentPreference(const char* xml, size_t size, KCMDocSetupFromXml& out)
{
	if (xml == nil)
		return kFalse;
	const size_t tag = Find(xml, size, 0, "<DocumentPreference ");
	if (tag >= size)
		return kFalse;
	const size_t tagEnd = Find(xml, size, tag, ">");
	if (tagEnd >= size)
		return kFalse;
	const size_t attrsFrom = tag + 1;		// past '<', so the name test can look one byte back

	size_t b = 0, e = 0;
	double w = 0, h = 0;
	if (AttributeOf(xml, attrsFrom, tagEnd, "PageWidth", b, e) && NumberOf(xml, b, e, w)
		&& AttributeOf(xml, attrsFrom, tagEnd, "PageHeight", b, e) && NumberOf(xml, b, e, h))
	{
		out.fPageWidth = w;
		out.fPageHeight = h;
		out.fHasSize = kTrue;
	}
	if (AttributeOf(xml, attrsFrom, tagEnd, "FacingPages", b, e))
	{
		if (ValueIs(xml, b, e, "true"))       { out.fFacingPages = kTrue;  out.fHasFacing = kTrue; }
		else if (ValueIs(xml, b, e, "false")) { out.fFacingPages = kFalse; out.fHasFacing = kTrue; }
	}
	if (AttributeOf(xml, attrsFrom, tagEnd, "PageBinding", b, e))
	{
		if (ValueIs(xml, b, e, "RightToLeft"))      { out.fBinding = 1;  out.fHasBinding = kTrue; }
		else if (ValueIs(xml, b, e, "LeftToRight")) { out.fBinding = 0;  out.fHasBinding = kTrue; }
		else if (ValueIs(xml, b, e, "DefaultValue")){ out.fBinding = -1; out.fHasBinding = kTrue; }
	}
	return kTrue;
}

bool16 KCMInjectForRehydration(const char* xml, size_t size, const char* sacrificialText,
							   KCMByteSink& out, int32* outStories, int32* outSpreads)
{
	int32 stories = 0, spreads = 0;
	if (outStories) *outStories = 0;
	if (outSpreads) *outSpreads = 0;
	if (xml == nil || sacrificialText == nil || sacrificialText[0] == '\0')
		return kFalse;

	size_t pos = 0;			// the first input byte not yet written
	size_t scan = 0;		// where the search for the next event starts
	bool16 storyWantsDummy = kFalse;	// inside a <Story>, before its first range

	while (scan < size)
	{
		const size_t story  = Find(xml, size, scan, kStoryOpen);
		const size_t spread = Find(xml, size, scan, kSpreadOpen);
		size_t range = size, storyEnd = size;
		if (storyWantsDummy)
		{
			range    = Find(xml, size, scan, kRangeOpen);
			storyEnd = Find(xml, size, scan, kStoryClose);
		}

		// the nearest event decides
		size_t next = story;
		if (spread < next)   next = spread;
		if (range < next)    next = range;
		if (storyEnd < next) next = storyEnd;
		if (next >= size)
			break;

		if (storyWantsDummy && next == range)
		{
			if (!Emit(out, xml + pos, next - pos) || !EmitLiteral(out, kDummyHead)
				|| !EmitLiteral(out, sacrificialText) || !EmitLiteral(out, kDummyTail))
				return kFalse;
			pos = next;
			scan = next + 1;
			storyWantsDummy = kFalse;
			continue;
		}
		if (storyWantsDummy && next == storyEnd)
		{
			storyWantsDummy = kFalse;		// an empty story: no range, no dummy
			scan = next + 1;
			continue;
		}

		// a <Story or <Spread open tag
		const bool16 isStory = (next == story) ? kTrue : kFalse;
		const size_t tagEnd = Find(xml, size, next, ">");
		if (tagEnd >= size)
			return kFalse;					// malformed: no closing '>'
		const size_t selfAt = Find(xml, size, next, kSelfAttr);
		if (selfAt >= tagEnd)
		{
			scan = tagEnd + 1;				// no Self: leave the element alone
			continue;
		}
		const size_t selfBegin = selfAt + ::strlen(kSelfAttr);
		const size_t selfEnd = Find(xml, size, selfBegin, "\"");
		if (selfEnd >= tagEnd || xml[tagEnd - 1] == '/')
		{
			scan = tagEnd + 1;				// self-closing, or an unterminated Self: leave it alone
			continue;
		}

		if (!Emit(out, xml + pos, tagEnd + 1 - pos))
			return kFalse;
		pos = tagEnd + 1;
		if (!EmitLabel(xml, size, pos, out, xml + selfBegin, selfEnd - selfBegin))
			return kFalse;
		scan = pos;
		if (isStory) { ++stories; storyWantsDummy = kTrue; }
		else         { ++spreads; }
	}

	if (!Emit(out, xml + pos, size - pos))
		return kFalse;
	if (outStories) *outStories = stories;
	if (outSpreads) *outSpreads = spreads;
	return kTrue;
}

// End, KCMXmlInject.cpp.
