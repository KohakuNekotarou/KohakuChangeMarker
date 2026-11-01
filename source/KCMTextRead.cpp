//========================================================================================
//
//  KCMTextRead.cpp
//
//  See KCMTextRead.h for what this is for and why positions are taken from the walk, never counted.
//
//  The walk follows SnpInspectTextModel's InspectStoryThreads for its shape: QueryStoryThread
//  hands back one thread at a time, and `position + span` steps to the next one. That single loop
//  covers the body, every table cell and every footnote.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"		// what QueryAttributeAt hands back
#include "IComposeScanner.h"	// the way to an attribute's value over a range
#include "IFootnoteNumber.h"	// the number a note's marker PRINTS - asked for, never counted (ScanNotes)
#include "IKentenStyle.h"		// IKentenStyle::KentenKind, and Kenten_None for "no kenten"
#include "IRubyStrand.h"		// IRubyAttrStrand - ⚠the file is IRubyStrand.h, the class is not
#include "ITextAttrBoolean.h"	// kTAMojiRubyBoss - mono against group; warichu and tate-chu-yoko on/off
#include "ITextAttrInt16.h"	// kTAKentenKindBoss / kTAKentenCharacterBoss - both are int16
#include "ITextAttrWideString.h"	// kTARubyStringBoss - the reading itself
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextUtils.h"		// CollectOwnedItems + OwnedItemDataList - how the SDK's own snippets find note markers

// General includes:
#include "CJKID.h"			// kRubyAttrStrandBoss, the two ruby attributes, and the kTAKenten* ones
#include "TextChar.h"		// kTextChar_CR / kTextChar_Table / kTextChar_TableContinued
#include "TextID.h"			// kCharAttrStrandBoss (kenten's strand) and kFootnoteReferenceBoss / kEndnoteAnchorBoss (the note markers)
#include "PersistUtils.h"	// ::GetClass - what a story thread hangs off (a footnote, or a note, or deleted text)
#include "TextAttributeRunIterator.h"	// the stretches of text with one value of the character marks (ScanCharMarks)
#include "Utils.h"			// Utils<ITextUtils> - the collector above
#include "TextIterator.h"
#include "UIDRef.h"
#include "WideString.h"

#include <algorithm>

// Project includes:
#include "KCMObjectPrint.h"		// KCMObjectPrintOf - an object placed in the text, its fingerprint
#include "KCMSkippedText.h"
#include "KCMTableShape.h"		// KCMReadTableShapes - where every cell of every table stands
#include "KCMTextRead.h"

namespace
{

/* CellPlace
   Where one cell's text begins, and which cell it is.

   THE DOCUMENT IS ASKED, NOT COUNTED - GetTextStart() on the cell's own thread.
*/
struct CellPlace
{
	TextIndex	fStart;
	int32		fTable;
	int32		fRow;
	int32		fCol;
};

bool16 EarlierCell(const CellPlace& a, const CellPlace& b)
{
	return a.fStart < b.fStart;
}

/* CellsOfTables
   Where every cell of every table starts, sorted so that the walk below can find a thread's place with one pass rather than
   a search per thread - out of KCMReadTableShapes' list, whose index IS the fTableOrdinal given here. ONE WALK OF THE TABLES:
   as two, the claim that a table's place in one list is its number in the other rested on two pieces of code keeping the
   same order and leaving out the same tables.
*/
void CellsOfTables(const std::vector<KCMTableShape>& tables, std::vector<CellPlace>& out)
{
	out.clear();
	for (size_t t = 0; t < tables.size(); ++t)
	{
		for (size_t c = 0; c < tables[t].fCells.size(); ++c)
		{
			const KCMTableCellPlace& cell = tables[t].fCells[c];
			CellPlace place;
			place.fStart = cell.fStart;
			place.fTable = static_cast<int32>(t);
			place.fRow = cell.fRow;
			place.fCol = cell.fCol;
			out.push_back(place);
		}
	}
	std::sort(out.begin(), out.end(), EarlierCell);
}

/* AttrRun
   One stretch of characters with something standing over it, in the model's own count. RUBY AND
   KENTEN BOTH COME BACK IN THIS SHAPE, which is the same decision KCMAttrSpan already made
   downstream ("fValue holds the READING for ruby and the KIND for kenten").

   NEITHER OF THEM IS IN THE TEXT. Ruby rides a strand beside it (kRubyAttrStrandBoss) and
   kenten is a set of character ATTRIBUTES (kTAKenten*Boss on kCharAttrStrandBoss), so nothing the
   walk above reads out of the characters can ever mention either: a story whose readings were
   retyped, or whose emphasis marks were changed from sesame dots to bullseyes, comes back
   byte-for-byte identical as text. That is the whole reason this exists.

   ⚠THE TWO ARE READ BY DIFFERENT MEANS AND MUST NOT BE MERGED INTO ONE SCAN. A strand answers
    "where does this run end"; an attribute has to be asked over a range that something else
    decided. ScanRuby and ScanCharMarks are therefore separate walks that happen to fill one shape.
*/
struct AttrRun
{
	TextIndex	fAt;
	int32		fLen;
	std::string	fValue;

	/** ⚠RUBY ONLY. Kenten is one mark per character by nature and has no group/mono distinction,
		so ScanCharMarks always leaves this kFalse - which is what KCMAttrSpan's own header says the
		comparison then relies on. */
	bool16		fGroup;

	AttrRun() : fAt(0), fLen(0), fGroup(kFalse) {}
};

/** The reading, out of the SDK's string and into the one the rest of this file speaks. */
void AppendWide(std::string& out, const WideString& w)
{
	for (int32 k = 0; k < w.CharCount(); ++k)
		KCMParaText::AppendUtf8(out, static_cast<int32>(w.GetChar(k).GetValue()));
}

/* IsFootnoteMarkerOnly
   True when the "reading" is nothing but InDesign's own footnote marker.

   NOT A GUESS - THE SDK NAMES THE CHARACTER: kTextChar_FootnoteMarker is U+0004
   (TextChar.h:37), and a footnote's first character carries it AS ITS RUBY READING. Measured
   on work/kcm-selftest/footnote: the character at DOM index 7 is 'F' (the start of the
   note's own text) and its rubyString is [0004] and nothing else - so the reader below produced a
   ruby span standing over the marker, reported by the dump as `[+5 ruby:2+1]`.

   **A reading nobody can read is not a change anybody can make.**

   ⚠ONLY WHEN IT IS THE WHOLE READING. A real reading that happened to contain the character
    alongside others is not this case, and is left alone.
*/
bool16 IsFootnoteMarkerOnly(const WideString& w)
{
	if (w.CharCount() != 1)
		return kFalse;
	return (static_cast<int32>(w.GetChar(0).GetValue())
			== static_cast<int32>(kTextChar_FootnoteMarker)) ? kTrue : kFalse;
}

/* ScanRuby
   Every ruby run in the story, in reading order.

   THE STRAND IS ASKED FOR ONCE, AND ITS ABSENCE IS AN ANSWER: no strand means no ruby anywhere in
   this story, and the whole scan is skipped - "there is no ruby strand, which means there can't be
   any ruby here", the official walk this follows (SnpPerformTextAttrRuby::GetRubyStrandInfo).

   ⚠THE LOOP ADVANCES BY WHAT THE STRAND SAYS, not by one. GetRubyRun answers for the run the
    position falls in - ruby or not - so stepping by the length it hands back always lands on the
    next boundary, and len <= 0 is the official stop (the snippet breaks on exactly that).
    @warning the header calls that count "the distance from position to the end of the STRAND"
      (IRubyStrand.h:50), but the official walk treats it as the distance to the end of the RUN and
      steps by it - which is the only reading under which its loop terminates anywhere but the end
      of the story. The snippet is followed here, and measured right on every ruby pair.

   ⚠THE WHOLE STORY, NOT JUST THE BODY: the cells' text is read in the same walk as the body (see
    ReadStory), so their ruby has to arrive with it.

   ⚠AN EMPTY READING IS NO RUBY. Not a defensive check - the SDK's own rule, stated in the same
    snippet: when the string comes back empty the ruby is off, whatever the strand says.
*/
void ScanRuby(ITextModel* model, std::vector<AttrRun>& out)
{
	InterfacePtr<IRubyAttrStrand> strand(
		(IRubyAttrStrand*)model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID));
	if (strand == nil)
		return;

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	const TextIndex total = model->TotalLength();
	for (TextIndex i = 0; i < total; )
	{
		int32 len = 0;
		TextIndex runBegin = i;
		const bool16 on = strand->GetRubyRun(i, &len, &runBegin);
		if (len <= 0)
			break;

		if (on)
		{
			// ⚠ASKED OVER THE WHOLE RUN, not at one character. An attribute is answered for a
			//   RANGE, and a range of one character would answer for one base character of a group
			//   ruby - which is how two characters sharing one reading turn into two runs each
			//   claiming the whole of it.
			const TextIndex end = i + len;

			InterfacePtr<const IAttrReport> readingAttr(
				scanner->QueryAttributeAt(i, end, kTARubyStringBoss));
			InterfacePtr<const ITextAttrWideString> reading(readingAttr, UseDefaultIID());

			AttrRun run;
			run.fAt = (runBegin >= 0 && runBegin <= i) ? runBegin : i;
			run.fLen = len + static_cast<int32>(i - run.fAt);
			// ⚠A FOOTNOTE'S MARKER RIDES THIS STRAND TOO - see IsFootnoteMarkerOnly. Left in, it
			//   would put a ruby span over every footnote in the document, none of which anybody
			//   typed. The reading is dropped rather than the run skipped, so the empty-reading
			//   rule below is the one place that decides what is not ruby.
			if (reading != nil && !IsFootnoteMarkerOnly(reading->Get()))
				AppendWide(run.fValue, reading->Get());

			if (!run.fValue.empty())
			{
				// MONO OR GROUP IS READ, NOT INFERRED: the document states it outright.
				//   ⚠kTAMojiRubyBoss IS kTrue FOR MONO (SnpRubyDataSettings::fMojiRuby) and
				//    KCMAttrSpan::fGroup is kTrue for GROUP - **the two are opposite**, and its
				//    ABSENCE means mono, which is InDesign's own default and not "unknown".
				InterfacePtr<const IAttrReport> mojiAttr(
					scanner->QueryAttributeAt(i, end, kTAMojiRubyBoss));
				InterfacePtr<const ITextAttrBoolean> moji(mojiAttr, UseDefaultIID());
				run.fGroup = ((moji != nil) && (moji->Get() == kFalse)) ? kTrue : kFalse;

				out.push_back(run);
			}
		}

		i += len;
	}
}

/* KentenKindName
   The SDK's own spelling for one kind of emphasis mark.

   THE TABLE IS THE OFFICIAL ONE, copied from codesnippets/SnpPerformTextAttrKenten.cpp's
   kSnpKentenKindTable rather than invented here: IKentenStyle declares the enum but no names, and
   a second vocabulary beside the one every snippet log already prints would mean two answers to
   "which mark is this". The panel shows these strings, so they are what a reader compares by eye
   against the Kenten panel.

   ⚠AN UNKNOWN VALUE IS NAMED, NOT DROPPED. A kind this build has never heard of still means the
    characters carry SOMETHING, and reporting "no kenten" for it would be a silent wrong answer -
    the one kind of answer this comparison must never give. It comes back as "Kind<n>", which
    compares correctly against itself and reads as unfamiliar to a person.
*/
std::string KentenKindName(int16 kind)
{
	switch (kind)
	{
		case IKentenStyle::Kenten_BlackSesameDot:	return "BlackSesameDot";
		case IKentenStyle::Kenten_WhiteSesameDot:	return "WhiteSesameDot";
		case IKentenStyle::Kenten_Fisheye:			return "Fisheye";
		case IKentenStyle::Kenten_BlackCircle:		return "BlackCircle";
		case IKentenStyle::Kenten_SmallBlackCircle:	return "SmallBlackCircle";
		case IKentenStyle::Kenten_Bullseye:			return "Bullseye";
		case IKentenStyle::Kenten_BlackTriangle:	return "BlackTriangle";
		case IKentenStyle::Kenten_WhiteTriangle:	return "WhiteTriangle";
		case IKentenStyle::Kenten_WhiteCircle:		return "WhiteCircle";
		case IKentenStyle::Kenten_SmallWhiteCircle:	return "SmallWhiteCircle";
		case IKentenStyle::Kenten_Custom:			return "Custom";
		default:									break;
	}

	return "Kind" + std::to_string(kind);
}

/* KentenValue
   The value one stretch of kenten carries: its kind, and for a custom mark its character as well.

   CUSTOM CARRIES ITS CHARACTER INTO THE VALUE. Two custom marks are the same mark only if they
   use the same glyph, so the code and its character set ride along in the string ("Custom:2:9679").
   The panel shows only the "Custom" part; the rest is there so that swapping one custom glyph for
   another is reported as the change it is.
*/
std::string KentenValue(int16 kind, const ITextAttrInt16* customChar)
{
	std::string value = KentenKindName(kind);
	if (kind != IKentenStyle::Kenten_Custom)
		return value;

	// THE CHARACTER ITSELF, NOT ITS NUMBER. The value has two readers - the comparison, which only
	//   needs two custom marks to differ when the glyphs differ, and the panel, which has to SHOW one.
	// ⚠BMP ONLY, and that is the SDK's limit rather than this line's: the attribute is
	//   an int16 (SnpPerformTextAttrKenten's fCustomCharacter), so a glyph outside the
	//   basic plane cannot be named as a custom kenten at all.
	// ⚠THE CHARACTER SET ATTRIBUTE IS DELIBERATELY NOT READ. Measured: the
	//   code came back 8251 = U+203B for a mark entered as 'kome', with
	//   kTAKentenCharacterSetBoss reading 0 - which the C++ enum calls kShiftJIS while
	//   the scripting DOM calls the same setting CHARACTER_INPUT. The two disagree, so
	//   the field cannot be used to decide how to read the code; the code was Unicode
	//   in the one case that was measured, and that is what this assumes. **A mark
	//   entered through the Shift-JIS or kuten boxes is therefore untested here** - it
	//   would compare correctly and could show the wrong glyph.
	if (customChar != nil && customChar->Get() != 0)
	{
		value += ":";
		KCMParaText::AppendUtf8(value, static_cast<int32>(static_cast<uint16>(customChar->Get())));
	}
	return value;
}

/** One stretch more of a mark: joined to the last when they touch and hold one value, so that five characters marked with
	one kind are ONE range (the rule KCMParaAttrs::fKenten carries, the opposite of ruby) however the runs fall. */
void AppendMarkRun(std::vector<AttrRun>& out, TextIndex at, int32 len, const std::string& value)
{
	if (!out.empty() && out.back().fValue == value && out.back().fAt + out.back().fLen == at)
	{
		out.back().fLen += len;
		return;
	}
	AttrRun run;
	run.fAt = at;
	run.fLen = len;
	run.fValue = value;
	out.push_back(run);			// fGroup stays kFalse - neither kind has group/mono
}

/* ScanCharMarks
   Every stretch of KENTEN, of the manual WARICHU (kTAWarichuAttrBoss) and of TATE-CHU-YOKO (kTATatechuyokoAttrBoss) in the story, in
   reading order.

   THESE ARE NOT STRANDS - THEY ARE CHARACTER ATTRIBUTES, and that is the whole difference from ScanRuby above: ruby's strand knows
   where its own runs end; an attribute has no runs of its own. **The walk is the SDK's own for exactly this**, TextAttributeRunIterator
   ("each range of text that has one consistent set of values" - SnpInspectTextModel.cpp, TKIPanelWidgetObserver.cpp), one walk for
   the four attributes.
   ⚠**NOT THE CHARACTER STRAND'S STYLE AND OVERRIDE BOUNDARIES WITH THE VALUE AT EACH RUN'S HEAD** (measured): that is
    true of a mark from a character style or an override, and false of one from a PARAGRAPH STYLE, which draws no
    boundary there - a kenten put on
    the second paragraph by its style was not reported at all, and one taken off the first was reported over all three.

   ⚠OFF IS A VALUE, NOT AN ABSENCE. Turning a mark off writes Kenten_None or kFalse into the attribute rather than removing it
    (SnpPerformTextAttrKenten; SnpPerformTextAttrWarichu.cpp:280, the tate-chu-yoko snippet's :318), so such a stretch carries no mark and must
    produce no span - otherwise every character in a document that once had one anywhere would come back "marked".
   Measured (a script on a scratch document): one character can carry BOTH a warichu and a tate-chu-yoko - inside each
    other, in either order of applying them - which is why they are lists of their own.
   ⚠**THE VALUE WRITTEN FOR THOSE TWO IS A PLACEHOLDER.** A span's value is the characters it covers, and those are only known in the
    paragraph's own count - ClosePara replaces it (KCMParaText::SetSpanValuesToText). It is not empty only so that nothing between
    here and there mistakes the run for "no value".
   ⚠UNMEASURED: whether two warichu (or two tate-chu-yoko) set separately on touching characters are one on the page. Joined here.
*/
void ScanCharMarks(ITextModel* model, std::vector<AttrRun>& outKenten, std::vector<AttrRun>& outWarichu, std::vector<AttrRun>& outTcy)
{
	const TextIndex total = model->TotalLength();
	if (total <= 0)
		return;

	K2Vector<InDesign::TextRange> ranges;
	ranges.push_back(InDesign::TextRange(model, 0, total));
	K2Vector<ClassID> attributes;
	attributes.push_back(kTAKentenKindBoss);
	attributes.push_back(kTAKentenCharacterBoss);
	attributes.push_back(kTAWarichuAttrBoss);
	attributes.push_back(kTATatechuyokoAttrBoss);

	for (TextAttributeRunIterator run(ranges.begin(), ranges.end(), attributes.begin(), attributes.end()); run; ++run)
	{
		const RangeData range = run.GetRunRange();
		const TextIndex at = range.Start(nil);
		const int32 len = range.Length();
		if (len <= 0)
			continue;

		InterfacePtr<const ITextAttrInt16> kind(static_cast<const ITextAttrInt16*>(run->QueryByClassID(kTAKentenKindBoss, ITextAttrInt16::kDefaultIID)));
		if (kind != nil && kind->Get() != IKentenStyle::Kenten_None)
		{
			InterfacePtr<const ITextAttrInt16> customChar(static_cast<const ITextAttrInt16*>(run->QueryByClassID(kTAKentenCharacterBoss, ITextAttrInt16::kDefaultIID)));
			AppendMarkRun(outKenten, at, len, KentenValue(kind->Get(), customChar));
		}
		InterfacePtr<const ITextAttrBoolean> warichu(static_cast<const ITextAttrBoolean*>(run->QueryByClassID(kTAWarichuAttrBoss, ITextAttrBoolean::kDefaultIID)));
		if (warichu != nil && warichu->Get() != kFalse)
			AppendMarkRun(outWarichu, at, len, "on");
		InterfacePtr<const ITextAttrBoolean> tcy(static_cast<const ITextAttrBoolean*>(run->QueryByClassID(kTATatechuyokoAttrBoss, ITextAttrBoolean::kDefaultIID)));
		if (tcy != nil && tcy->Get() != kFalse)
			AppendMarkRun(outTcy, at, len, "on");
	}
}

/* ScanNotes
   FOOTNOTE and ENDNOTE references, as one-character spans standing where each marker stands.

   WHY THEY TRAVEL WITH RUBY AND KENTEN (user's request: "the page shows a 1 above
   the character - show it in the row the way ruby is shown"). A reference is a CHARACTER, not an
   attribute: U+0004, a footnote's and an endnote's alike (only what the text owns there tells the
   two apart, which is why this walks the owned items), standing in the text. Read as text it draws
   nothing, so the row showed a "□" where a note had been added and the reader could not tell what
   had happened. Reported as a span with the NUMBER as its value, the row draws that number over
   the marker exactly as it draws a reading over its base text -- which is what the page does too.

   **THE NUMBER IS ASKED FOR, NEVER COUNTED.** IFootnoteNumber::GetNumberString answers with the
   text InDesign itself prints, so a document that restarts its numbering per page or per section
   still agrees with the row. Counting the markers here would be right only for the default
   setting. The route is the SDK's own: codesnippets/SnpManipulateTextFootnotes.cpp collects the
   owned items, keeps the ones whose class is kFootnoteReferenceBoss, and asks each for its
   number; SnpManipulateTextEndnotes.cpp does the same with kEndnoteAnchorBoss.
   **AND THE ENDNOTE ANCHOR ANSWERS THE SAME INTERFACE** - IEndnoteAnchorData names
   IFootnoteNumber in as many words - so one loop serves both and there is no second way to be
   wrong about a number.

   ⚠**THE WHOLE STORY, NOT THE PRIMARY THREAD** - and that is a deliberate departure from the
    snippets, which both ask for GetPrimaryStoryThreadSpan(). MEASURED
    (work/kcm-selftest/footnote2/cellfoot): a footnote inside a TABLE CELL is not in the primary
    thread, so with the snippets' range its marker was collected by nobody - while ReadStory, which
    walks every thread, had already taken that marker OUT of the text. The row then showed the note's
    words arriving and **nothing at all about the note**, which is worse than the "□" this whole
    feature replaced. ⇒ the range ReadStory collects `owned` over is the model's own TotalLength.
   ⚠A REFERENCE WHOSE NUMBER CANNOT BE READ still becomes a span, with "?" for its value: the
    marker IS there, and reporting nothing would be the one wrong answer (the same rule
    KentenKindName follows for a kind it does not know).
*/
void ScanNotes(IDataBase* db, const OwnedItemDataList& owned, const KCMSkippedText& skipped, std::vector<AttrRun>& outFootnotes,
			   std::vector<AttrRun>& outEndnotes)
{
	for (int32 i = 0; i < static_cast<int32>(owned.size()); ++i)
	{
		const bool16 isFootnote = (owned[i].fClassID == kFootnoteReferenceBoss) ? kTrue : kFalse;
		const bool16 isEndnote  = (owned[i].fClassID == kEndnoteAnchorBoss) ? kTrue : kFalse;
		if (!isFootnote && !isEndnote)
			continue;			// an inline, an anchored object, a note - none of them is a numbered reference
		if (skipped.Contains(owned[i].fAt))
			continue;			// a marker in text the page does not set (KCMSkippedText.h)

		// THE RUN STANDS ON THE MARKER. Which character the span sits on is the paragraph's to say (TakeNotesFor).
		AttrRun run;
		run.fAt = owned[i].fAt;
		run.fLen = 1;

		// ASKED OF THE OBJECT, not of the settings: the settings say how numbering WORKS, this
		//   says what this one note's number IS.
		PMString numberString;
		InterfacePtr<IFootnoteNumber> noteNumber(db, owned[i].fUID, UseDefaultIID());
		if (noteNumber != nil)
			noteNumber->GetNumberString(IFootnoteNumber::kFootnoteReferenceInText, numberString);

		run.fValue = numberString.GetUTF8String();
		if (run.fValue.empty())
			run.fValue = "?";		// the marker is real even when its number is not readable

		if (isFootnote)
			outFootnotes.push_back(run);
		else
			outEndnotes.push_back(run);
	}
}

/* ScanObjects
   OBJECTS PLACED IN THE TEXT, as one-character spans on their anchors (U+FFFC - a character the text counts), each
   valued with what it is and its fingerprint (KCMObjectPrint.h). The owned items come off the same walk the note
   markers do (ScanNotes) - the whole story, cells and notes included - and the kInlineBoss ones are the objects, inline
   and anchored alike.
   ⚠ONLY WHEN ReadStory IS ASKED (`withObjects`): a fingerprint is a SaveAll of the object and everything in it.
   ⚠An object whose bytes cannot be written becomes no span at all: nothing can be said about it either way.
*/
void ScanObjects(IDataBase* db, const OwnedItemDataList& owned, const KCMSkippedText& skipped, std::vector<AttrRun>& out)
{
	for (int32 i = 0; i < static_cast<int32>(owned.size()); ++i)
	{
		if (owned[i].fClassID != kInlineBoss)
			continue;			// a note's marker, a table - not an object placed in the text
		if (skipped.Contains(owned[i].fAt))
			continue;			// in text the page does not set (KCMSkippedText.h)
		std::string value;
		if (!KCMObjectPrintOf(db, owned[i].fUID, value))
			continue;
		AttrRun run;
		run.fAt = owned[i].fAt;
		run.fLen = 1;
		run.fValue = value;
		out.push_back(run);
	}
}

/* CountUncounted
   How many of a paragraph's uncounted positions stand before `at` -- the whole of the difference
      between the model's count and the text's, at one point.
*/
int32 CountUncounted(const std::vector<TextIndex>& uncounted, TextIndex at)
{
	// A WALK, DELIBERATELY. A paragraph holds one of these per table standing inside it, which is
	//   almost always none and never many, so anything cleverer would cost more to read than it
	//   saves to run.
	int32 n = 0;
	for (size_t k = 0; k < uncounted.size() && uncounted[k] < at; ++k)
		++n;
	return n;
}

/* MarkRuns
   One kind of mark over a story: its runs in TextIndex order, and how far the walk has got through them.

   **ONE CURSOR PER LIST, AND THEY CANNOT BE SHARED.** The lists are walked in step with the paragraphs but are not the same
   length, so a shared cursor would drag one of them past its own runs.
*/
struct MarkRuns
{
	std::vector<AttrRun>	fRuns;
	size_t					fNext;

	MarkRuns() : fNext(0) {}
};

/* TakeAttrFor
   The ruby or kenten standing over one paragraph, in the paragraph's own count.

   THE CURSOR WALKS FORWARD WITH THE PARAGRAPHS. Both lists are in TextIndex order, so a run that
   ended before this paragraph began can never be wanted again - but a run that REACHES PAST the
   paragraph's end must stay, because the next paragraph still has to see it. That is why only the
   first kind moves the cursor.

   ⚠CLIPPED TO THE PARAGRAPH. KCMAttrSpan positions are offsets INSIDE one paragraph (its header
    says so), and the diff downstream cuts rows by them, so a span reaching past the end would put
    a mark on characters that are not there.
*/
void TakeAttrFor(MarkRuns& marks, TextIndex paraStart, TextIndex paraEnd,
				 const std::vector<TextIndex>& uncounted, KCMAttrSpanList& out)
{
	const std::vector<AttrRun>& runs = marks.fRuns;
	size_t& cursor = marks.fNext;

	while (cursor < runs.size() && (runs[cursor].fAt + runs[cursor].fLen) <= paraStart)
		++cursor;

	for (size_t i = cursor; i < runs.size() && runs[i].fAt < paraEnd; ++i)
	{
		const TextIndex runEnd = runs[i].fAt + runs[i].fLen;
		const TextIndex from = (runs[i].fAt > paraStart) ? runs[i].fAt : paraStart;
		const TextIndex to = (runEnd < paraEnd) ? runEnd : paraEnd;
		if (to > from)
		{
			// ⚠THE DISTANCE IS NOT THE OFFSET. KCMAttrSpan counts "the first character of the BASE TEXT, within its
			//   paragraph", and the base text is what the paragraph SHOWS - so every position the model counts but does
			//   not show has to come back out again. A table CAN stand in the middle of a paragraph: "あい[0016]うえ" is
			//   ONE paragraph of six characters (measured: work/kcm-selftest/midtable), and without this the reading
			//   over う would be reported as standing over え.
			//   ⚠THE LENGTH IS CORRECTED TOO: a run reaching across the table's character covers one FEWER character of
			//    text than of model.
			//   ⚠**AND THE LENGTH CAN COME OUT 0.** The guard above (`to > from`) is in the MODEL's count while the
			//    length here is in the TEXT's, so a run standing over NOTHING BUT uncounted positions passes the first
			//    and measures nothing
			//    in the second: a ruby can be applied to a table's own anchor, and InDesign keeps it there although it
			//    draws nowhere (memory indesign-special-text-characters; measured: work/kcm-selftest/anchorruby
			//    reported `edits=1` for
			//    a change on no character anybody can see).
			//    ⇒ **A span over no text is not a span**, the rule the reader applies to an empty reading
			//    (KCMParaText.h,
			//      "AN EMPTY RUBY STRING IS NO RUBY").
			const int32 skipBefore = CountUncounted(uncounted, from);
			const int32 textLen = static_cast<int32>(to - from)
								  - (CountUncounted(uncounted, to) - skipBefore);
			if (textLen > 0)
				out.push_back(KCMAttrSpan(static_cast<int32>(from - paraStart) - skipBefore,
										  textLen, runs[i].fValue, runs[i].fGroup));
		}
	}
}

/* StoryMarks
   Every kind of mark over a story.

   A further kind costs a field here and one line in ReadStory.
*/
struct StoryMarks
{
	MarkRuns	fRuby;
	MarkRuns	fKenten;
	MarkRuns	fFootnote;
	MarkRuns	fEndnote;
	MarkRuns	fWarichu;
	MarkRuns	fTcy;
	MarkRuns	fObject;	// objects placed in the text - empty unless ReadStory was asked
};

/* ParaBeingRead
   The paragraph the walk is in the middle of: a new value for each paragraph, and for each thread.
*/
struct ParaBeingRead
{
	std::string				fText;
	TextIndex				fLineStart;			// where the paragraph really begins, before anything it starts with
	TextIndex				fStart;				// where its first character stands
	std::vector<TextIndex>	fUncounted;			// positions INSIDE it that the model counts and the text does not
	std::vector<TextIndex>	fAnchors;			// the TABLES' anchors among them (KCMParaAttrs::fTableAnchorsAt)
	int32					fLeadingAnchors;	// how many tables it begins with
	TextIndex				fLeadingNote;		// where the first note reference it begins with stands; -1 when it begins with none

	explicit ParaBeingRead(TextIndex at) : fLineStart(at), fStart(at), fLeadingAnchors(0), fLeadingNote(-1) {}
};

/* TakeNotesFor
   The note references standing in one paragraph, as spans of its text.

   THE SPAN SITS ON A CHARACTER BESIDE THE MARKER, NOT ON THE MARKER, and it has to: the marker is taken out of the text
   (ReadStory), and a span over no text is not a span. THE ONE BEFORE IT, which is where the page puts the number: at the top right of the
   word the note hangs off - counted past what the text leaves out, so a marker right after a table stands on the word before the table.
   A MARKER WITH NO CHARACTER BEFORE IT - the paragraph begins with it - SITS ON THE PARAGRAPH'S FIRST: without that a
   footnote at a paragraph's start is reported by its words alone, and an endnote there, whose words live in another
   story, not at all
   (measured).
   ⚠A paragraph with no character at all has nothing to sit on: the note's own words are what is left to report it.
   ⚠THE CURSOR WALKS FORWARD WITH THE PARAGRAPHS, as TakeAttrFor's does; a marker belongs to one paragraph, so it moves past each.
*/
void TakeNotesFor(MarkRuns& marks, const ParaBeingRead& para, TextIndex paraEnd, KCMAttrSpanList& out)
{
	const std::vector<AttrRun>& runs = marks.fRuns;
	size_t& cursor = marks.fNext;

	while (cursor < runs.size() && runs[cursor].fAt < para.fLineStart)
		++cursor;

	for ( ; cursor < runs.size() && runs[cursor].fAt < paraEnd; ++cursor)
	{
		if (para.fText.empty())
			continue;
		const TextIndex marker = runs[cursor].fAt;
		const int32 charsBefore = (marker < para.fStart) ? 0 : static_cast<int32>(marker - para.fStart) - CountUncounted(para.fUncounted, marker);
		out.push_back(KCMAttrSpan((charsBefore > 0) ? charsBefore - 1 : 0, 1, runs[cursor].fValue, runs[cursor].fGroup));
	}
}

/* ClosePara
   One finished paragraph: its text, where it stands, and everything riding over it.

   IT IS ONE FUNCTION BECAUSE A PARAGRAPH ENDS IN TWO PLACES - at its carriage return, and at the
   end of a thread that has none - and a paragraph closed one way but not the other would carry its
   ruby only sometimes.
*/
void ClosePara(std::vector<std::string>& outParas,
			   std::vector<KCMParaAttrs>& outAttrs,
			   std::vector<int32>& outStarts,
			   const ParaBeingRead& para,
			   const KCMParaAttrs& place,
			   TextIndex paraEnd,
			   StoryMarks& marks)
{
	outParas.push_back(para.fText);
	outStarts.push_back(static_cast<int32>(para.fStart));

	KCMParaAttrs attrs = place;		// the cell identity, which is the same for every paragraph here

	// The table or note reference the paragraph BEGINS with: the start was moved past them, so they are
	//   the distance from where the paragraph really begins (KCMParaAttrs::fLeadingUncounted).
	attrs.fLeadingUncounted = static_cast<int32>(para.fStart - para.fLineStart);
	if (para.fLeadingNote >= 0)
		attrs.fLeadingNoteBack = static_cast<int32>(para.fStart - para.fLeadingNote);

	// **THE SKIPPED POSITIONS TRAVEL WITH THE PARAGRAPH, IN THE TEXT'S COUNT.** Everything this
	//   file hands out is counted the way the panel reads it - the table's own characters left out
	//   - and everything downstream eventually has to ask the DOCUMENT about one of those positions
	//   (a mark, a jump, a selection). That crossing was made by adding the offset to the
	//   paragraph's start, which is right only while the paragraph holds nothing but its text;
	//   measured: the midtable pair's one reported change selected the table's anchor
	//   rather than the character after it. Recording them here is what lets
	//   KCMParaText::ModelOffsetInParagraph put it back.
	//   ⚠**THE k-th ONE HAS k BEFORE IT**, so its place in the text is its model position less the
	//    paragraph's start and less the ones already met. They arrive in order, which is what makes
	//    that subtraction the whole of the conversion.
	attrs.fUncountedAt.reserve(para.fUncounted.size());
	for (size_t k = 0; k < para.fUncounted.size(); ++k)
		attrs.fUncountedAt.push_back(static_cast<int32>(para.fUncounted[k] - para.fStart) -
									 static_cast<int32>(k));

	// AND WHERE EACH TABLE STANDS, IN THE SAME COUNT (the import matrix's X06 - KCMParaAttrs::fTableAnchorsAt):
	//   the anchors alone. One the paragraph begins with stands at 0; one inside it is uncounted itself, so the positions
	//   met before it come off exactly as they do for fUncountedAt.
	for (int32 k = 0; k < para.fLeadingAnchors; ++k)
		attrs.fTableAnchorsAt.push_back(0);
	for (size_t k = 0; k < para.fAnchors.size(); ++k)
		attrs.fTableAnchorsAt.push_back(static_cast<int32>(para.fAnchors[k] - para.fStart) - CountUncounted(para.fUncounted, para.fAnchors[k]));

	// THE COUNT IS CROSSED IN ONE PLACE, CountUncounted, for what stands over a character (TakeAttrFor) and for a
	//   note's marker, which stands beside one (TakeNotesFor).
	TakeAttrFor(marks.fRuby, para.fStart, paraEnd, para.fUncounted, attrs.fRuby);
	TakeAttrFor(marks.fKenten, para.fStart, paraEnd, para.fUncounted, attrs.fKenten);
	TakeNotesFor(marks.fFootnote, para, paraEnd, attrs.fFootnote);
	TakeNotesFor(marks.fEndnote, para, paraEnd, attrs.fEndnote);

	// A WARICHU'S AND A TATE-CHU-YOKO'S VALUE IS ITS OWN CHARACTERS, and they are only
	//   known here, in the text's count that TakeAttrFor has just put the spans into.
	TakeAttrFor(marks.fWarichu, para.fStart, paraEnd, para.fUncounted, attrs.fWarichu);
	TakeAttrFor(marks.fTcy, para.fStart, paraEnd, para.fUncounted, attrs.fTcy);
	KCMParaText::SetSpanValuesToText(attrs.fWarichu, para.fText);
	KCMParaText::SetSpanValuesToText(attrs.fTcy, para.fText);

	// The objects placed in the paragraph, through the same door - their anchors are characters the text
	//   counts, so the crossing is the one every other span takes.
	TakeAttrFor(marks.fObject, para.fStart, paraEnd, para.fUncounted, attrs.fObject);
	outAttrs.push_back(attrs);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
uint64 KCMTextRead::MarksPrint(ITextModel* model)
{
	if (model == nil)
		return 0;
	std::vector<AttrRun> runs[4];
	ScanRuby(model, runs[0]);
	ScanCharMarks(model, runs[1], runs[2], runs[3]);
	uint64 print = 14695981039346656037ULL;		// FNV-1a, as the words' print is
	const auto fold = [&print](uint64 value) { print ^= value; print *= 1099511628211ULL; };
	for (int32 kind = 0; kind < 4; ++kind)
	{
		fold(static_cast<uint64>(kind));
		for (size_t r = 0; r < runs[kind].size(); ++r)
		{
			fold(static_cast<uint64>(runs[kind][r].fAt));
			fold(static_cast<uint64>(runs[kind][r].fLen));
			for (size_t c = 0; c < runs[kind][r].fValue.size(); ++c)
				fold(static_cast<unsigned char>(runs[kind][r].fValue[c]));
		}
	}
	return print;
}

//----------------------------------------------------------------------------------------
bool16 KCMTextRead::ReadStory(const UIDRef& storyRef,
							  std::vector<std::string>& outParas,
							  std::vector<KCMParaAttrs>& outAttrs,
							  std::vector<int32>& outStarts,
							  bool16 withObjects,
							  std::vector<KCMTableShape>* outTables)
{
	// **EMPTIED FIRST, ALL THREE.** A caller may hand in vectors that already hold something, and a
	// reader that appended would silently report a doubled list.
	outParas.clear();
	outAttrs.clear();
	outStarts.clear();

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	// WHAT THE PAGE DOES NOT SET, asked once and handed to every walk below (KCMSkippedText.h).
	KCMSkippedText skipped;
	if (!skipped.Build(model))
		return kFalse;

	std::vector<KCMTableShape> tables;
	if (!KCMReadTableShapes(model, skipped, tables))
		return kFalse;
	std::vector<CellPlace> cells;
	CellsOfTables(tables, cells);

	// THE RUBY IS READ IN THE SAME BREATH AS THE TEXT: a comparison is a photograph of one moment, and text from one
	//   instant beside ruby from another puts two moments in one row.
	//   ⚠NOTHING MAY RUN BETWEEN THIS AND THE WALK BELOW. No command, no recompose, no second
	//    document - anything that edits the story between them would date one against the other.
	StoryMarks marks;
	ScanRuby(model, marks.fRuby.fRuns);

	// KENTEN, WARICHU AND TATE-CHU-YOKO COME OFF THE SAME MOMENT, for the reason stated just above: a story read here and
	//   its emphasis marks read after some command ran would be two photographs in one row.
	ScanCharMarks(model, marks.fKenten.fRuns, marks.fWarichu.fRuns, marks.fTcy.fRuns);

	// AND THE NOTE MARKERS, OFF THE SAME MOMENT AGAIN. Their numbers are read here
	//   too, so a row shows the number the page showed when the comparison ran - not the one the
	//   document would print after the next edit.
	OwnedItemDataList owned;
	Utils<ITextUtils> textUtils;
	if (textUtils && model->TotalLength() > 0)
		textUtils->CollectOwnedItems(model, 0, model->TotalLength() - 1, &owned);
	IDataBase* const db = ::GetDataBase(model);
	if (db != nil)
		ScanNotes(db, owned, skipped, marks.fFootnote.fRuns, marks.fEndnote.fRuns);

	// AND THE OBJECTS PLACED IN THE TEXT, off the same moment - but only when asked (the header says why).
	if (withObjects && db != nil)
		ScanObjects(db, owned, skipped, marks.fObject.fRuns);

	const TextIndex total = model->TotalLength();
	size_t nextCell = 0;
	// ⚠**NOT THE MARKERS ABOVE.** This numbers the footnote THREADS as the walk meets them, which
	//   is what tells one note's paragraphs from another's (KCMParaAttrs::fFootnoteOrdinal). The
	//   markers are spans and are counted by nobody - they carry the number InDesign gave them.
	int32 nextFootnote = 0;

	// ONE LOOP FOR THE BODY, THE CELLS AND THE FOOTNOTES. QueryStoryThread hands back the
	//   thread containing a position together with where it starts and how long it is; stepping by
	//   `position + span` walks them all in TextIndex order. Nothing here has to know what shapes a
	//   story can contain.
	TextIndex position = 0;
	while (position < total)
	{
		int32 span = 0;
		InterfacePtr<const ITextStoryThread> thread(model->QueryStoryThread(position, &position, &span));
		if (thread == nil || span <= 0)
			break;

		const TextIndex threadEnd = position + span;

		// A THREAD IN TEXT THE PAGE DOES NOT SET is stepped over whole: deleted text and
		//   everything standing in it - a footnote or a table deleted under Track Changes keeps a
		//   thread of its own, which the test below would otherwise take for a live one.
		if (skipped.Contains(position))
		{
			position = threadEnd;
			continue;
		}

		// Which place is this? The body unless a cell begins exactly here. The cells are in
		// TextIndex order and so are the threads, so one cursor walks both.
		KCMParaAttrs place;			// the defaults are "body text"
		while (nextCell < cells.size() && cells[nextCell].fStart < position)
			++nextCell;
		if (nextCell < cells.size() && cells[nextCell].fStart == position)
		{
			place.fTableOrdinal = cells[nextCell].fTable;
			place.fCellRow = cells[nextCell].fRow;
			place.fCellCol = cells[nextCell].fCol;
			++nextCell;
		}
		else if (position != 0)
		{
			// A THREAD THAT IS NEITHER THE BODY NOR A CELL HANGS OFF SOMETHING STANDING IN THE STORY - the body is the
			//   one that starts at zero - AND THE THREAD IS ASKED WHAT IT IS. A footnote's thread IS its reference
			//   (kFootnoteReferenceBoss aggregates ITextStoryThread - docs/ai-notes/iid-boss-dictionary.md), so the
			//   thread's own class answers. Everything else that hangs a thread off the body - a note, text deleted
			//   under Track Changes
			//   (kDeletedTextBoss), hidden conditional text (kHiddenTextBoss / kHidTxtModelBoss) - is NOT the story's
			//   text as the page sets it, and is stepped over: none of its paragraphs is reported, and the character it
			//   hangs off stays where
			//   it is in the body, invisible. An edit inside a NOTE makes no row: a note does not print.
			//   A FOOTNOTE'S PARAGRAPHS ARE READ LIKE ANY OTHERS AND KEEP THEIR REAL TextIndex: IDMS and .icml put a
			//   footnote's text in the MIDDLE of the body, the text model keeps it past the end.
			//   ⚠NUMBERED IN THE ORDER THE THREADS COME OUT, which is TextIndex order. A footnote inside a footnote can
			//   be made
			//    (deleting the outer one takes the inner with it -
			//    docs/ai-notes/kcm-word-footnote-add-delete-probe-2026-09-22.md);
			//    each is a row of its own (the suite story-notes, SM-F).
			if (::GetClass(thread) != kFootnoteReferenceBoss)
			{
				position = threadEnd;
				continue;
			}
			place.fFootnoteOrdinal = nextFootnote++;
		}

		ParaBeingRead para(position);

		TextIterator iter(model, position);
		for (TextIndex i = position; i < threadEnd; ++i, ++iter)
		{
			// ⚠TextIterator's value_type is UTF32TextChar, a CLASS - it does not convert to an
			//   integer on its own. GetValue() is the way out of it.
			const int32 cp = static_cast<int32>((*iter).GetValue());

			// A TABLE'S OWN CHARACTERS ARE NOT TEXT, BUT THEY ARE POSITIONS. The model holds kTextChar_Table for the
			//   anchor plus one kTextChar_TableContinued per body row after the first (header and footer rows have
			//   none); they are not
			//   text, so they are not put into the paragraph (the panel would show them as a gap, and the diff would
			//   count them as characters that changed). They still move the index, and a paragraph standing behind one
			//   starts AFTER it.
			//   ⚠THEY DO NOT ALWAYS SIT AT A PARAGRAPH BOUNDARY. Measured: inserting a table at the third insertion
			//     point of "あいうえ" leaves ONE paragraph reading [3042 3044 **0016** 3046 3048 000d]
			//     (work/kcm-selftest/midtable)
			//     ⇒ every position inside such a paragraph is one further along in the model than in its text, which is
			//     why the ones met here are recorded rather than merely stepped over.
			if (cp == kTextChar_Table || cp == kTextChar_TableContinued)
			{
				// The ANCHOR is the table's place (KCMParaAttrs::fTableAnchorsAt); a continuation is one more row of it.
				if (para.fText.empty())
				{
					para.fStart = i + 1;
					if (cp == kTextChar_Table)
						++para.fLeadingAnchors;
				}
				else
				{
					para.fUncounted.push_back(i);
					if (cp == kTextChar_Table)
						para.fAnchors.push_back(i);
				}
				continue;
			}

			// A NOTE'S MARKER IS A POSITION, NOT TEXT. U+0004 - a footnote's and an
			//   endnote's alike; kTextChar_EndnoteMarker is a name in TextChar.h that the body does not
			//   hold, and the test below names it at no cost - draws nothing at all, so read as text it
			//   put a "□" in the row and the reader was shown a one-character change nobody could
			//   identify - which is what the user reported. They are reported as SPANS instead, carrying
			//   the number the page prints (ScanNotes), and the same treatment the table's own
			//   characters get keeps the two counts in step.
			//   ⚠**AND THAT IS WHY THEY HAVE TO COME OUT OF THE TEXT.** Left in, the paragraph
			//     holding a new note counts as a paragraph whose WORDS changed, and an attribute
			//     found in such a paragraph says nothing of its own when its characters are gone
			//     (KCMParaText::PairMarksAcross) - which is what a marker that has just been added
			//     would read as. The span would be built and then thrown away, and the row would say
			//     what it said before.
			if (cp == kTextChar_FootnoteMarker || cp == kTextChar_EndnoteMarker)
			{
				if (para.fText.empty())
				{
					if (para.fLeadingNote < 0)
						para.fLeadingNote = i;
					para.fStart = i + 1;
				}
				else
					para.fUncounted.push_back(i);
				continue;
			}

			if (cp == kTextChar_CR)
			{
				ClosePara(outParas, outAttrs, outStarts, para, place, i, marks);
				para = ParaBeingRead(i + 1);
				continue;
			}

			// ⚠ONE CODE POINT PER CHARACTER. Every position handed out here is counted the way
			//   InDesign counts text positions - a surrogate pair is ONE TextIndex - and the diff
			//   downstream counts the same way. Encoding a character as two would put the two
			//   counts out of step, and the comparison would quote the right words at the wrong
			//   place.
			KCMParaText::AppendUtf8(para.fText, cp);
		}

		// A thread always ends with a carriage return (ITextStoryThread's own contract, and
		// SnpInspectTextModel checks for it as a corruption test), so the loop normally closes the
		// last paragraph itself. This catches the one that does not - and an empty tail is NOT
		// pushed, or every thread would end with a paragraph nobody wrote.
		if (!para.fText.empty())
			ClosePara(outParas, outAttrs, outStarts, para, place, threadEnd, marks);

		position = threadEnd;
	}

	if (outTables != nil)
		outTables->swap(tables);
	return kTrue;
}

// End, KCMTextRead.cpp.
