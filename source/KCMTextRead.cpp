//========================================================================================
//
//  KCMTextRead.cpp
//
//  See KCMTextRead.h for what this is for and why positions are taken from the walk, never counted.
//
//  The walk follows SnpInspectTextModel's InspectStoryThreads for its shape: QueryStoryThread
//  hands back one thread at a time, and `position + span` steps to the next one. That single loop
//  covers the body, every table cell and every footnote - which was the whole reason for the
//  migration away from the snippet XML (gone since 2026-09-03), whose parser had to know each of
//  those shapes by name and went silent on the ones it did not.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"		// what QueryAttributeAt hands back
#include "IAttributeStrand.h"	// kenten's run boundaries - see ScanKenten
#include "IComposeScanner.h"	// the way to an attribute's value over a range
#include "IKentenStyle.h"		// IKentenStyle::KentenKind, and Kenten_None for "no kenten"
#include "IRubyStrand.h"		// IRubyAttrStrand - ⚠the file is IRubyStrand.h, the class is not
#include "ITableModel.h"
#include "ITextAttrBoolean.h"	// kTAMojiRubyBoss - mono against group
#include "ITextAttrInt16.h"	// kTAKentenKindBoss / kTAKentenCharacterBoss - both are int16
#include "ITextAttrWideString.h"	// kTARubyStringBoss - the reading itself
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"
#include "ITextStoryThreadDictHier.h"

// General includes:
#include "CJKID.h"			// kRubyAttrStrandBoss, the two ruby attributes, and the kTAKenten* ones
#include "PMString.h"
#include "TableTypes.h"		// GridAddress, RowRange, ColRange
#include "TextChar.h"		// kTextChar_CR / kTextChar_Table / kTextChar_TableContinued
#include "TextID.h"			// kCharAttrStrandBoss - the strand kenten's attributes sit on
#include "TextIterator.h"
#include "UIDRef.h"
#include "WideString.h"

#include <algorithm>
#include <map>
#include <sstream>		// the kenten kind table's "unknown" spelling

// Project includes:
#include "KCMTextRead.h"

namespace
{

/* TableAt
   One table of the story: the dictionary that holds its cells, and where those cells begin.

   **THE ORDER IS THE THREAD BLOCK'S, NOT THE ANCHOR'S**, and that is not a detail: a nested
   table's anchor sits inside its parent's CELLS - past the whole body - so sorting by anchor puts
   it after a second top-level table that is written before it. The SDK states the rule at
   ITextStoryThreadDict::GetThreadBlockTextRange ("the location of the dictionary's thread block is
   determined by the location of the dictionary's anchor relative to other dictionaries"), and it
   is the order the XML was written in, which is the order the snippet route numbered tables in.
   ⚠KCMStoryCellBases (the reconciler of that route, gone since 2026-09-03) learned this the hard
    way; the rule is kept here because fTableOrdinal is what SplitRunAtPlaces tells cells apart by,
    and two versions of one document have to number the same table the same way.
*/
struct TableAt
{
	UID			fDictUID;
	TextIndex	fBlockStart;
};

bool16 EarlierBlock(const TableAt& a, const TableAt& b)
{
	return a.fBlockStart < b.fBlockStart;
}

/* CellPlace
   Where one cell's text begins, and which cell it is.

   ★THE DOCUMENT IS ASKED, NOT COUNTED - GetTextStart() on the cell's own thread. That was the whole
   difference from the XML route, which counted down the snippet and then had to check the total
   against the model (and refused the story when they disagreed).
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

/* BuildCellIndex
   Asks the document where every cell of every table starts, once per story.

   @return kFalse when a dictionary or a table cannot be opened at all. **A story with no tables is
	   not a failure**: it answers kTrue with an empty index.

   @warning MERGED CELLS ARE VISITED ONCE. IsAnchor is what says so - a merged cell is addressed by
	its anchor (ITableModel.h, at GridAddress), and the covered addresses have no thread of their
	own. Walking them anyway would ask QueryThread for a cell that is not there.
*/
bool16 BuildCellIndex(ITextModel* model, std::vector<CellPlace>& out)
{
	out.clear();

	InterfacePtr<ITextStoryThreadDictHier> hier(model, UseDefaultIID());
	if (hier == nil)
		return kTrue;		// no dictionary hierarchy at all - nothing but a body

	IDataBase* const db = ::GetDataBase(hier);
	if (db == nil)
		return kFalse;

	// Collect the tables. The walk starts at the story's own dictionary (which is not a table) and
	// follows the hierarchy; NextUID answers kInvalidUID when nothing follows. The shape is
	// SnpIterTableUseDictHier's, the one Adobe calls recommended.
	std::vector<TableAt> tables;
	for (UID next = ::GetUIDRef(hier).GetUID(); next != kInvalidUID; next = hier->NextUID(next))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, next, UseDefaultIID());
		if (dict == nil)
			return kFalse;

		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;		// the story's own dictionary - a dictionary IS a table exactly when
							// an ITableModel can be got from it (SnpIterTableUseDictHier)

		TableAt at;
		at.fDictUID = next;
		at.fBlockStart = dict->GetThreadBlockTextRange().Start(nil);
		tables.push_back(at);
	}

	std::sort(tables.begin(), tables.end(), EarlierBlock);

	for (size_t t = 0; t < tables.size(); ++t)
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, tables[t].fDictUID, UseDefaultIID());
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (dict == nil || table == nil)
			return kFalse;

		// ⚠GridAddress is (row, column) - TableTypes.h. RowRange/ColRange carry start and count;
		//   `start + count` is used rather than End() so that this depends on the two fields the
		//   header shows outright.
		const RowRange rows = table->GetTotalRows();
		const ColRange cols = table->GetTotalCols();

		for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		{
			for (int32 c = cols.start; c < cols.start + cols.count; ++c)
			{
				const GridAddress addr(r, c);
				if (!table->IsValid(addr) || !table->IsAnchor(addr))
					continue;

				InterfacePtr<ITextStoryThread> thread(dict->QueryThread(table->GetGridID(addr)));
				if (thread == nil)
					continue;

				CellPlace place;
				place.fStart = thread->GetTextStart();
				place.fTable = static_cast<int32>(t);
				place.fRow = r;
				place.fCol = c;
				out.push_back(place);
			}
		}
	}

	// Sorted so that the walk below can find a thread's place with one pass rather than a search
	// per thread. (Both lists come out in TextIndex order anyway; the sort makes that a fact
	// rather than an assumption about how the dictionaries were laid out.)
	std::sort(out.begin(), out.end(), EarlierCell);
	return kTrue;
}

/* AttrRun
   One stretch of characters with something standing over it, in the model's own count. RUBY AND
   KENTEN BOTH COME BACK IN THIS SHAPE, which is the same decision KCMAttrSpan already made
   downstream ("fValue holds the READING for ruby and the KIND for kenten").

   ★★★NEITHER OF THEM IS IN THE TEXT. Ruby rides a strand beside it (kRubyAttrStrandBoss) and
   kenten is a set of character ATTRIBUTES (kTAKenten*Boss on kCharAttrStrandBoss), so nothing the
   walk above reads out of the characters can ever mention either: a story whose readings were
   retyped, or whose emphasis marks were changed from sesame dots to bullseyes, comes back
   byte-for-byte identical as text. That is the whole reason this exists.

   ⚠THE TWO ARE READ BY DIFFERENT MEANS AND MUST NOT BE MERGED INTO ONE SCAN. A strand answers
    "where does this run end"; an attribute has to be asked over a range that something else
    decided. ScanRuby and ScanKenten are therefore separate walks that happen to fill one shape.
*/
struct AttrRun
{
	TextIndex	fAt;
	int32		fLen;
	std::string	fValue;

	/** ⚠RUBY ONLY. Kenten is one mark per character by nature and has no group/mono distinction,
		so ScanKenten always leaves this kFalse - which is what KCMAttrSpan's own header says the
		comparison then relies on. */
	bool16		fGroup;

	AttrRun() : fAt(0), fLen(0), fGroup(kFalse) {}
};

/** The reading, out of the SDK's string and into the one the rest of this file speaks. */
void AppendWide(std::string& out, const WideString& w)
{
	for (int32 k = 0; k < w.CharCount(); ++k)
		KCMSnippetText::AppendUtf8(out, static_cast<int32>(w.GetChar(k).GetValue()));
}

/* IsFootnoteMarkerOnly
   True when the "reading" is nothing but InDesign's own footnote marker.

   ★★★NOT A GUESS - THE SDK NAMES THE CHARACTER: kTextChar_FootnoteMarker is U+0004
   (TextChar.h:37), and a footnote's first character carries it AS ITS RUBY READING. Measured
   2026-09-01 on work/kcm-selftest/footnote: the character at DOM index 7 is 'F' (the start of the
   note's own text) and its rubyString is [0004] and nothing else - so the reader below produced a
   ruby span standing over the marker, reported by the dump as `[+5 ruby:2+1]`.

   ⚠THE OLD ROUTE NEVER SAW THIS, AND NOT BY LUCK. A <Footnote> carries no base text in the
    snippet, so the parser made no span at all - and the panel has always agreed with that.
    **A reading nobody can read is not a change anybody can make**, so reporting one would be a
    regression this migration introduced rather than a fault it uncovered.

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

   ★THE STRAND IS ASKED FOR ONCE, AND ITS ABSENCE IS AN ANSWER: no strand means no ruby anywhere in
   this story, and the whole scan is skipped - "there is no ruby strand, which means there can't be
   any ruby here", the official walk this follows (SnpPerformTextAttrRuby::GetRubyStrandInfo).

   ⚠THE LOOP ADVANCES BY WHAT THE STRAND SAYS, not by one. GetRubyRun answers for the run the
    position falls in - ruby or not - so stepping by the length it hands back always lands on the
    next boundary, and len <= 0 is the official stop (the snippet breaks on exactly that).
    @warning the header calls that count "the distance from position to the end of the STRAND"
      (IRubyStrand.h:50), but the official walk treats it as the distance to the end of the RUN and
      steps by it - which is the only reading under which its loop terminates anywhere but the end
      of the story. The snippet is followed here, and the parallel run of the migration (gone with
      the old route) measured it right on every ruby pair (2026-09-01).

   ⚠THE WHOLE STORY, NOT JUST THE BODY - unlike KIDMCP's reader of the same shape, which stops at
    the body because it compares cells as little stories of their own. Here the cells' text is read
    in the same walk as the body (see ReadStory), so their ruby has to arrive with it or the two
    routes would disagree on every cell that carries one. ★KCM READ IT BEFORE, so dropping it would
    be a regression rather than a gap.

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
				// ★MONO OR GROUP IS READ, NOT INFERRED. The old route decided it from whether the
				//   XML carried a RubyType attribute; the document states it outright.
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

   ★THE TABLE IS THE OFFICIAL ONE, copied from codesnippets/SnpPerformTextAttrKenten.cpp's
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

	std::ostringstream unknown;
	unknown << "Kind" << kind;
	return unknown.str();
}

/* ScanKenten
   Every stretch of kenten in the story, in reading order.

   ★★★KENTEN IS NOT A STRAND - IT IS A SET OF CHARACTER ATTRIBUTES, and that is the whole
   difference from ScanRuby above. Ruby's strand knows where its own runs end (GetRubyRun answers
   with a length); an attribute has no runs of its own, so the walk has to be told where to stop by
   the strand the attributes SIT ON (kCharAttrStrandBoss). Its boundaries are where the character
   style or the local overrides change - never in the middle of either - so an attribute asked over
   one of them is answered for the whole of it.

   ⚠THE BOUNDARIES ARE OVER-FINE, AND THAT IS WHY THE MERGE BELOW EXISTS. A style change with no
    kenten in it still ends a run, so five characters marked with one kind can arrive as two runs.
    The user's own snippet is what settled the rule this has to honour: **five characters marked
    with one kind are ONE range**, where the same five with ruby are five (KCMSnippetText.h says so
    and its test still proves it). Merging adjacent runs of equal value is what makes both routes
    agree on that.

   ⚠OFF IS A VALUE, NOT AN ABSENCE. Turning kenten off writes Kenten_None into the attribute rather
    than removing it (SnpPerformTextAttrKenten does exactly that), so a run whose value is
    Kenten_None carries no mark and must produce no span - otherwise every character in a document
    that once had kenten anywhere would come back "marked".

   ★CUSTOM CARRIES ITS CHARACTER INTO THE VALUE. Two custom marks are the same mark only if they
   use the same glyph, so the code and its character set ride along in the string ("Custom:2:9679").
   The panel shows only the "Custom" part; the rest is there so that swapping one custom glyph for
   another is reported as the change it is.
*/
void ScanKenten(ITextModel* model, std::vector<AttrRun>& out)
{
	InterfacePtr<IAttributeStrand> strand(
		(IAttributeStrand*)model->QueryStrand(kCharAttrStrandBoss, IID_IATTRIBUTESTRAND));
	if (strand == nil)
		return;

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	const TextIndex total = model->TotalLength();
	for (TextIndex i = 0; i < total; )
	{
		// ⚠BOTH ARE ASKED, AND THE SHORTER ONE WINS. A kenten can come from a character style or
		//   from a local override, so a walk that stepped by only one of them would step straight
		//   over a change in the other.
		int32 styleLen = 0;
		int32 overrideLen = 0;
		strand->GetStyleUID(i, &styleLen);
		strand->GetLocalOverrides(i, &overrideLen);

		int32 len = styleLen;
		if (overrideLen > 0 && (len <= 0 || overrideLen < len))
			len = overrideLen;
		if (len <= 0)
			break;					// the strand has nothing further to say - the official stop
		if (i + len > total)
			len = static_cast<int32>(total - i);

		const TextIndex end = i + len;

		InterfacePtr<const IAttrReport> kindAttr(
			scanner->QueryAttributeAt(i, end, kTAKentenKindBoss));
		InterfacePtr<const ITextAttrInt16> kind(kindAttr, UseDefaultIID());

		if (kind != nil && kind->Get() != IKentenStyle::Kenten_None)
		{
			std::string value = KentenKindName(kind->Get());

			if (kind->Get() == IKentenStyle::Kenten_Custom)
			{
				InterfacePtr<const IAttrReport> charAttr(
					scanner->QueryAttributeAt(i, end, kTAKentenCharacterBoss));
				InterfacePtr<const ITextAttrInt16> customChar(charAttr, UseDefaultIID());

				// ★THE CHARACTER ITSELF, NOT ITS NUMBER (corrected 2026-09-01 after the panel was
				//   seen showing "Custom:0:8251" to a reader). The value has two readers - the
				//   comparison, which only needs two custom marks to differ when the glyphs differ,
				//   and the panel, which has to SHOW one. A number satisfies the first and fails
				//   the second, and the failure is the same shape as the one this feature was
				//   withdrawn for in August: something internal drawn where a person looks.
				// ⚠BMP ONLY, and that is the SDK's limit rather than this line's: the attribute is
				//   an int16 (SnpPerformTextAttrKenten's fCustomCharacter), so a glyph outside the
				//   basic plane cannot be named as a custom kenten at all.
				// ⚠THE CHARACTER SET ATTRIBUTE IS DELIBERATELY NOT READ. Measured 2026-09-01: the
				//   code came back 8251 = U+203B for a mark entered as 'kome', with
				//   kTAKentenCharacterSetBoss reading 0 - which the C++ enum calls kShiftJIS while
				//   the scripting DOM calls the same setting CHARACTER_INPUT. The two disagree, so
				//   the field cannot be used to decide how to read the code; the code was Unicode
				//   in the one case that was measured, and that is what this assumes. **A mark
				//   entered through the Shift-JIS or kuten boxes is therefore untested here** - it
				//   would compare correctly and could show the wrong glyph.
				if (customChar != nil && customChar->Get() != 0)
				{
					const uint16 code = static_cast<uint16>(customChar->Get());
					value += ":";
					if (code < 0x80)
					{
						value += static_cast<char>(code);
					}
					else if (code < 0x800)
					{
						value += static_cast<char>(0xC0 | (code >> 6));
						value += static_cast<char>(0x80 | (code & 0x3F));
					}
					else
					{
						value += static_cast<char>(0xE0 | (code >> 12));
						value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
						value += static_cast<char>(0x80 | (code & 0x3F));
					}
				}
			}

			// ★MERGED WHERE THEY TOUCH - see the warning above. Compared by VALUE as well as by
			//   position, so two different kinds meeting at a boundary stay two spans.
			if (!out.empty() && out.back().fValue == value &&
				out.back().fAt + out.back().fLen == i)
			{
				out.back().fLen += len;
			}
			else
			{
				AttrRun run;
				run.fAt = i;
				run.fLen = len;
				run.fValue = value;
				out.push_back(run);		// fGroup stays kFalse - kenten has no group/mono
			}
		}

		i += len;
	}
}

/* TakeAttrFor
   The ruby or kenten standing over one paragraph, in the paragraph's own count.

   ★THE CURSOR WALKS FORWARD WITH THE PARAGRAPHS. Both lists are in TextIndex order, so a run that
   ended before this paragraph began can never be wanted again - but a run that REACHES PAST the
   paragraph's end must stay, because the next paragraph still has to see it. That is why only the
   first kind moves the cursor.

   ⚠CLIPPED TO THE PARAGRAPH. KCMAttrSpan positions are offsets INSIDE one paragraph (its header
    says so), and the diff downstream cuts rows by them, so a span reaching past the end would put
    a mark on characters that are not there.
*/
int32 CountUncounted(const std::vector<TextIndex>& uncounted, TextIndex at)
{
	// ★A WALK, DELIBERATELY. A paragraph holds one of these per table standing inside it, which is
	//   almost always none and never many, so anything cleverer would cost more to read than it
	//   saves to run.
	int32 n = 0;
	for (size_t k = 0; k < uncounted.size() && uncounted[k] < at; ++k)
		++n;
	return n;
}

void TakeAttrFor(const std::vector<AttrRun>& runs, size_t& cursor,
				 TextIndex paraStart, TextIndex paraEnd,
				 const std::vector<TextIndex>& uncounted, KCMAttrSpanList& out)
{
	while (cursor < runs.size() && (runs[cursor].fAt + runs[cursor].fLen) <= paraStart)
		++cursor;

	for (size_t i = cursor; i < runs.size() && runs[i].fAt < paraEnd; ++i)
	{
		const TextIndex runEnd = runs[i].fAt + runs[i].fLen;
		const TextIndex from = (runs[i].fAt > paraStart) ? runs[i].fAt : paraStart;
		const TextIndex to = (runEnd < paraEnd) ? runEnd : paraEnd;
		if (to > from)
		{
			// ⚠★★★THE DISTANCE IS NOT THE OFFSET. KCMAttrSpan counts "the first character of the
			//   BASE TEXT, within its paragraph", and the base text is what the paragraph SHOWS -
			//   so every position the model counts but does not show has to come back out again.
			//   MEASURED 2026-09-01: a table CAN stand in the middle of a paragraph -
			//   "あい[0016]うえ" came back as ONE paragraph of six characters
			//   (work/kcm-selftest/midtable) - which is the case the rest of the diff assumes away
			//   (KCMStoryDiffRun, at RunSide: "a table's own character ... sits exactly at a
			//   paragraph boundary"). ★IT IS NOT ALWAYS TRUE, and without this the reading over
			//   う would be reported as standing over え.
			//   ⚠THE LENGTH IS CORRECTED TOO, not just the start: a run reaching across the
			//    table's character covers one FEWER character of text than of model.
			const int32 skipBefore = CountUncounted(uncounted, from);
			out.push_back(KCMAttrSpan(static_cast<int32>(from - paraStart) - skipBefore,
									  static_cast<int32>(to - from)
										  - (CountUncounted(uncounted, to) - skipBefore),
									  runs[i].fValue, runs[i].fGroup));
		}
	}
}

/* ClosePara
   One finished paragraph: its text, where it stands, and everything riding over it.

   ★IT IS ONE FUNCTION BECAUSE A PARAGRAPH ENDS IN TWO PLACES - at its carriage return, and at the
   end of a thread that has none - and a paragraph closed one way but not the other would carry its
   ruby only sometimes. That is the kind of fault the parallel run would report as a single
   disagreement in one story out of a hundred.
*/
void ClosePara(std::vector<std::string>& outParas,
			   std::vector<KCMParaAttrs>& outAttrs,
			   std::vector<int32>& outStarts,
			   const std::string& text,
			   const KCMParaAttrs& place,
			   TextIndex paraStart, TextIndex paraEnd,
			   const std::vector<AttrRun>& ruby, size_t& rubyCursor,
			   const std::vector<AttrRun>& kenten, size_t& kentenCursor,
			   std::vector<TextIndex>& uncounted)
{
	outParas.push_back(text);
	outStarts.push_back(static_cast<int32>(paraStart));

	KCMParaAttrs attrs = place;		// the cell identity, which is the same for every paragraph here
	// ⚠ONE CURSOR EACH. The two lists are walked in step with the paragraphs but are not the same
	//   length, so a shared cursor would drag one of them past its own runs.
	TakeAttrFor(ruby, rubyCursor, paraStart, paraEnd, uncounted, attrs.fRuby);
	TakeAttrFor(kenten, kentenCursor, paraStart, paraEnd, uncounted, attrs.fKenten);
	outAttrs.push_back(attrs);

	// ⚠EMPTIED HERE, WHERE THE PARAGRAPH ENDS, so that the two places a paragraph can close cannot
	//   disagree about it - the same reason this function exists at all.
	uncounted.clear();
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
bool16 KCMTextRead::ReadStory(const UIDRef& storyRef,
							  std::vector<std::string>& outParas,
							  std::vector<KCMParaAttrs>& outAttrs,
							  std::vector<int32>& outStarts)
{
	// **EMPTIED FIRST, ALL THREE.** A caller may hand in vectors that already hold something (the
	// migration's parallel run handed in the other route's answer), and a reader that appended
	// would silently report a doubled list.
	outParas.clear();
	outAttrs.clear();
	outStarts.clear();

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return kFalse;

	std::vector<CellPlace> cells;
	if (!BuildCellIndex(model, cells))
		return kFalse;

	// ★THE RUBY IS READ IN THE SAME BREATH AS THE TEXT. A comparison is a photograph of one moment,
	//   and text from one instant beside ruby from another puts two moments in one row - the rule
	//   KCMSnippetText.h states, kept here. The rule is "one moment", not "one source": this file's
	//   text comes from the model, so its ruby does too.
	//   ⚠NOTHING MAY RUN BETWEEN THIS AND THE WALK BELOW. No command, no recompose, no second
	//    document - anything that edits the story between them would date one against the other.
	std::vector<AttrRun> ruby;
	ScanRuby(model, ruby);

	// ★KENTEN COMES OFF THE SAME MOMENT, for the reason stated just above: a story read here and
	//   its emphasis marks read after some command ran would be two photographs in one row.
	std::vector<AttrRun> kenten;
	ScanKenten(model, kenten);

	const TextIndex total = model->TotalLength();
	size_t nextCell = 0;
	size_t nextRuby = 0;
	size_t nextKenten = 0;
	int32 nextFootnote = 0;

	// ★★★ONE LOOP FOR THE BODY, THE CELLS AND THE FOOTNOTES. QueryStoryThread hands back the
	//   thread containing a position together with where it starts and how long it is; stepping by
	//   `position + span` walks them all in TextIndex order. Nothing here has to know what shapes a
	//   story can contain - which is precisely what the XML route could not manage, since it has to
	//   recognise every element by name and goes silent on the ones it does not (<Footnote>).
	TextIndex position = 0;
	while (position < total)
	{
		int32 span = 0;
		InterfacePtr<const ITextStoryThread> thread(model->QueryStoryThread(position, &position, &span));
		if (thread == nil || span <= 0)
			break;

		const TextIndex threadEnd = position + span;

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
			// ★★★A THREAD THAT IS NEITHER THE BODY NOR A CELL IS A FOOTNOTE (or an endnote), and
			//   THE BODY IS THE ONE THAT STARTS AT ZERO - every other thread of a story hangs off
			//   something standing in it. ⚠That is the whole of the test, and it is worth saying
			//   plainly: a shape this walk has never met would be counted as a footnote here.
			//   The three footnote pairs (work/kcm-selftest/footnote) proved the shape it does
			//   meet; ⚠nothing prints the place of every paragraph any more (the parallel run did,
			//   and went with the old route on 2026-09-03), so an unmet shape would show up only as
			//   a row cut in an odd place.
			//
			//   ★THEY ARE READ AS PARAGRAPHS LIKE ANY OTHERS AND KEEP THEIR REAL TextIndex, which
			//   is what the XML route could never do: IDMS and .icml both put a footnote's text in
			//   the MIDDLE of the body, while the text model keeps it past the end. Measured
			//   2026-08-31: the old parser produced "BBFOOTBB" - a string that exists nowhere in
			//   the document - and the story was then refused with no differences at all, whether
			//   the edit was in the body or in the note (work/kcm-selftest/footnote/README.md).
			//
			//   ⚠NUMBERED IN THE ORDER THE THREADS COME OUT, which is TextIndex order. Unlike
			//    tables there is no nesting to reorder (a footnote inside a footnote is not a
			//    thing), so no equivalent of EarlierBlock is needed.
			place.fFootnoteOrdinal = nextFootnote++;
		}

		std::string text;
		TextIndex paraStart = position;
		bool16 paraHasCharacters = kFalse;

		// Positions INSIDE the paragraph being built that the model counts and the text does not.
		// ⚠It is cleared by ClosePara, not here, so that a paragraph ending either way clears it.
		std::vector<TextIndex> uncounted;

		TextIterator iter(model, position);
		for (TextIndex i = position; i < threadEnd; ++i, ++iter)
		{
			// ⚠TextIterator's value_type is UTF32TextChar, a CLASS - it does not convert to an
			//   integer on its own. GetValue() is the way out of it.
			const int32 cp = static_cast<int32>((*iter).GetValue());

			// ★A TABLE'S OWN CHARACTERS ARE NOT TEXT, BUT THEY ARE POSITIONS. The model holds
			//   kTextChar_Table for the anchor plus one kTextChar_TableContinued per row after the
			//   first; they are not text, so they are not put into the paragraph (the panel would
			//   show them as a gap, and the diff would count them as characters that changed).
			//   They still move the index, and a paragraph standing behind one starts AFTER it.
			//   ⚠★★★THEY DO NOT ALWAYS SIT AT A PARAGRAPH BOUNDARY, whatever the rest of the diff
			//     assumes (KCMStoryDiffRun, at RunSide). MEASURED 2026-09-01: inserting a table at
			//     the third insertion point of "あいうえ" leaves ONE paragraph reading
			//     [3042 3044 **0016** 3046 3048 000d] - the character stands BETWEEN two of the
			//     paragraph's own (work/kcm-selftest/midtable). ⇒ every position inside such a
			//     paragraph is one further along in the model than in its text, which is why the
			//     ones met here are recorded rather than merely stepped over.
			//     @warning the OLD route refuses a story shaped like this outright (measured:
			//       "stories changed=0 edits=0"), so nothing downstream has ever had to face one.
			if (cp == kTextChar_Table || cp == kTextChar_TableContinued)
			{
				if (!paraHasCharacters)
					paraStart = i + 1;
				else
					uncounted.push_back(i);
				continue;
			}

			if (cp == kTextChar_CR)
			{
				ClosePara(outParas, outAttrs, outStarts, text, place, paraStart, i,
						  ruby, nextRuby, kenten, nextKenten, uncounted);
				text.clear();
				paraStart = i + 1;
				paraHasCharacters = kFalse;
				continue;
			}

			// ⚠ONE CODE POINT PER CHARACTER. Every position handed out here is counted the way
			//   InDesign counts text positions - a surrogate pair is ONE TextIndex - and the diff
			//   downstream counts the same way. Encoding a character as two would put the two
			//   counts out of step, and the comparison would quote the right words at the wrong
			//   place.
			KCMSnippetText::AppendUtf8(text, cp);
			paraHasCharacters = kTrue;
		}

		// A thread always ends with a carriage return (ITextStoryThread's own contract, and
		// SnpInspectTextModel checks for it as a corruption test), so the loop normally closes the
		// last paragraph itself. This catches the one that does not - and an empty tail is NOT
		// pushed, or every thread would end with a paragraph nobody wrote.
		if (!text.empty())
			ClosePara(outParas, outAttrs, outStarts, text, place, paraStart, threadEnd,
					  ruby, nextRuby, kenten, nextKenten, uncounted);

		position = threadEnd;
	}

	return kTrue;
}

// End, KCMTextRead.cpp.
