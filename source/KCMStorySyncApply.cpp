//========================================================================================
//
//  KCMStorySyncApply.cpp -- see the header.
//
//  ★**THE WRITES BELOW WERE MOVED HERE WHOLE FROM KCMStoryTextImport.cpp** (2026-09-23, S0b of the
//  import rebuild): ApplyParagraph, InsertParagraphs, DeleteParagraphs and the footnote pieces are
//  the same code, comments and all - what changed is who tells them what to write. What to write is
//  KCMStorySync's to say now (the plan); the judging that stood beside them in the import went.
//  ⚠Some comments below still name that judging (the pour's places, "the merge") - they describe how
//  the same writes were reached before, and the writes themselves did not change.
//
//========================================================================================

// ⚠FIRST AND UNGUARDED: the plug-in builds with /Yu, which discards everything up to and including
//  this line.
#include "VCPlugInHeaders.h"

#include "KCMStorySyncApply.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ICommand.h"
#include "ITableCommands.h"			// rows, columns, merges - a table made Word's shape (S1/S2)
#include "ITableModel.h"
#include "ITableModelList.h"		// how many tables a story holds - whether InsertTable made one
#include "ITableUtils.h"			// InsertTable - a table Word added (S3a)
#include "ITextParcelList.h"		// GetParcelContaining - how wide the frame is where a table goes
#include "IParcelList.h"			// GetParcelBounds
#include "PMRect.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"
#include "ITextStoryThread.h"		// the thread a paragraph stands in - a write may not leave it
#include "CmdUtils.h"
#include "TextIterator.h"			// the characters a write is about to take out, read before it does
#include "TextChar.h"				// kTextChar_Table / kTextChar_TableContinued - which side of a table
#include "ErrorUtils.h"
#include "WideString.h"

#include "KCMStorySync.h"			// ApplyToShape / RenumberNotesByThread - the finished shape, for the attributes
#include "KCMStoryAttrPour.h"		// the ruby and the kenten, after the words are in
#include "KCMStoryRestore.h"		// KCMCreateWordsWriteCmd - one answer to "replace, insert or delete"
#include "KCMParaText.h"			// ModelOffsetInParagraph / AppendUtf8
#include "KCMStoryNoteEdit.h"		// making and unmaking a footnote
#include "KCMParagraphStyle.h"		// the next style for a paragraph put in after another
#include "KCMTextDiff.h"			// ToCodePoints / Diff
#include "KCMTextRead.h"			// ReadStory - the document, read the way the export read it
#include "KCMStoryTextExport.h"		// KCMTableRefsOfStory - the tables by the reading's ordinals

namespace
{

/** The one character at `at`, or -1 outside the story. */
int32 CharAt(ITextModel* model, TextIndex at)
{
	if (model == nil || at < 0 || at >= model->TotalLength())
		return -1;
	TextIterator iter(model, at);
	return static_cast<int32>((*iter).GetValue());
}

/** kTrue when any code point of the text in [from, to) is one the reader may not move or delete.

	★**AN OBJECT'S CHARACTER, NOT EVERY INVISIBLE ONE** (2026-09-17 afternoon, the user's request: a forced
	  line break added or removed is taken in). This asked KCMStoryShape::IsInvisible until then, which
	  turned away a forced line break, a zero width space and an indent-to-here as if they were tables -
	  while writing the document back asks KCMParaText::IsObjectCharacter. Now the pour asks the same
	  question: those characters carry nothing, and text commands write them whole. */
bool16 RangeTouchesObject(const std::vector<int32>& cps, int32 from, int32 to)
{
	for (int32 k = from; k < to && k < static_cast<int32>(cps.size()); ++k)
	{
		if (k >= 0 && KCMParaText::IsObjectCharacter(cps[k]))
			return kTrue;
	}
	return kFalse;
}

/** The cells of one row, in the order the exporter wrote them: by column, anchors only.

	The document's paragraphs name their (row, column); the file's cells are a plain list. Sorting
	the columns that actually occur is what turns one into the other - and it agrees with the
	exporter by construction, because that walked the anchors in column order too. */
void ColumnsOfRow(const std::vector<KCMParaAttrs>& attrs, int32 table, int32 row,
				  std::vector<int32>& outCols)
{
	outCols.clear();
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		if (!attrs[i].IsCell() || attrs[i].fTableOrdinal != table || attrs[i].fCellRow != row)
			continue;
		bool16 seen = kFalse;
		for (size_t k = 0; k < outCols.size() && !seen; ++k)
			seen = (outCols[k] == attrs[i].fCellCol) ? kTrue : kFalse;
		if (!seen)
			outCols.push_back(attrs[i].fCellCol);
	}
	std::sort(outCols.begin(), outCols.end());
}

/*	ApplyParagraph
	The edits between one paragraph of the document and the same paragraph of the file.

	★★**MINIMAL EDITS, BACK TO FRONT.** Replacing the whole paragraph would be simpler and would
	throw away every attribute on the parts nobody touched - the ruby and the kenten this format
	works so hard to carry. So the two are diffed by code point and only the runs that differ are
	written, starting from the end so that the earlier positions are still true when they are used.

	@param outRefused kTrue when the paragraph was turned away, or when a write failed part way
	       through it. ⚠**REFUSED AND WRITTEN ARE NOT EXCLUSIVE, WHICH IS WHY THIS IS NOT THE
	       RETURN VALUE.** The judging happens before anything is written, so a refusal there
	       costs nothing - but a command that fails in the MIDDLE of the loop leaves the writes
	       that went in ahead of it, and the paragraph then holds neither the document's words
	       nor the file's. Answering with a count alone hid the failure (the old -1 turned into a
	       success as soon as one write had gone in); answering with -1 alone hid the change.
	@param fileTableOffsets the text offsets of the tables standing in the FILE's version of this
	       paragraph, ascending - what decides which side of a table an insertion goes (2026-09-17).
	@return how many writes went in - 0 when none did, refused or not.
*/
int32 ApplyParagraph(ITextModel* model, TextIndex paraStart, const KCMParaAttrs& attrs,
					 const std::string& docText, const std::string& fileText,
					 const std::vector<int32>& fileTableOffsets,
					 PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;

	// ⚠(2026-09-24, S3b X06) "the same words" is no longer "nothing to do": words moved across a table read the
	//  same and put the table elsewhere. The early return is below, once the document's tables are known.
	std::vector<int32> a;
	std::vector<int32> b;
	KCMTextDiff::ToCodePoints(docText, &a, nil);
	KCMTextDiff::ToCodePoints(fileText, &b, nil);

	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	if (cmds == nil)
	{
		whyNot = "the story cannot be edited";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	// ---- where each change lands in the MODEL, judged before anything is written ------------------
	//
	// ★★★**A RANGE HAS A START AND AN END, AND THEY ARE COUNTED DIFFERENTLY** (2026-09-17). A table
	//   standing inside the paragraph puts characters in the model that the text does not have, and
	//   ModelOffsetInParagraph answers for a START - "the character at text offset t" stands AFTER a
	//   table standing at t (KCMParaText.h says so, and that a range ENDING there comes back one wide).
	//   This used to take the END from the same function, so changing the last characters before a
	//   table took the table's own anchor out with them - in the copy, silently. The end is now "just
	//   past the last character": ModelOffsetInParagraph(last) + 1.
	// ★★**A CHANGE ACROSS A TABLE IS DONE IN PIECES**, one for each side, so the table stays - and since
	//   the same day's G1/G2 the FILE's table position says which of the new words go on which side
	//   (KCMParaText::CutChangeAtObjects). A replacement across a NOTE REFERENCE is still refused: the
	//   file does not carry where a reference stands, so nothing can say which side its words belong on.
	// ★★★**AND EVERY PIECE IS CHECKED AGAINST THE DOCUMENT BEFORE ANYTHING GOES IN** (the same day,
	//   after the crash): the range has to stay inside this paragraph's own story thread, and what it
	//   takes out has to BE the characters the reading gave. A position that has gone stale fails the
	//   second test even when it lands inside a thread of the right length - which is exactly how the
	//   crash began (cell A's deletion landed, three characters long, on cell C).
	struct Piece
	{
		size_t	fChange;	// which change of `changes`
		int32	fFrom;		// model offset from the paragraph's start
		int32	fCount;		// model characters it takes out (0 = an insertion)
		int32	fAStart;	// the same run in the text's count
		int32	fACount;
		int32	fBStart;	// the FILE's words this piece puts in (an insertion cut at a table has two)
		int32	fBCount;
	};
	std::vector<Piece> pieces;

	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(paraStart, &threadStart, &threadSpan));
		if (thread == nil)
		{
			whyNot = "the paragraph's story thread could not be found (nothing was written)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	// ★★**THE TABLES STANDING IN THIS PARAGRAPH, AS THE DOCUMENT HAS THEM** (2026-09-17, G1) - read off
	//   the model, because the paragraph's attributes do not say which of its uncounted positions are
	//   tables (a note's marker is one too), and a table standing at the very START is not among them
	//   at all: the reader moved the paragraph's start past it.
	std::vector<KCMParaText::KCMTableInPara> tables;
	{
		TextIndex lead = paraStart;
		while (lead > threadStart)
		{
			const int32 cp = CharAt(model, lead - 1);
			if (cp != kTextChar_Table && cp != kTextChar_TableContinued)
				break;
			--lead;
		}
		for (TextIndex m = lead; m < paraStart; ++m)
		{
			if (CharAt(model, m) == kTextChar_Table)
			{
				KCMParaText::KCMTableInPara t;
				t.fTextOffset = 0;
				t.fModelOffset = static_cast<int32>(m - paraStart);
				tables.push_back(t);
			}
		}
		for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
		{
			const TextIndex m = paraStart + attrs.fUncountedAt[k] + static_cast<int32>(k);
			if (CharAt(model, m) == kTextChar_Table)
			{
				KCMParaText::KCMTableInPara t;
				t.fTextOffset = attrs.fUncountedAt[k];
				t.fModelOffset = static_cast<int32>(m - paraStart);
				tables.push_back(t);
			}
		}
	}

	std::vector<int32> docTableOffsets;
	for (size_t j = 0; j < tables.size(); ++j)
		docTableOffsets.push_back(tables[j].fTextOffset);

	if (docText == fileText && docTableOffsets == fileTableOffsets)
		return 0;

	// ★PIECE BY PIECE WHERE THE TABLES STAND (2026-09-24, S3b X06) - the diff the comparison made (ComparePara):
	//   words moved across a table are words put in on one side and taken out on the other, and the anchor stays.
	std::vector<KCMTextDiff::Change> changes;
	const bool16 diffed = (!docTableOffsets.empty() && docTableOffsets.size() == fileTableOffsets.size())
						  ? KCMTextDiff::DiffInPieces(a, docTableOffsets, b, fileTableOffsets, changes)
						  : KCMTextDiff::Diff(a, b, changes);
	if (!diffed)
	{
		whyNot = "the paragraph differs too much to place the changes";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	// ⚠**NOTHING IS WRITTEN UNTIL EVERY CHANGE HAS BEEN JUDGED**, so a paragraph turned away
	//   here is turned away whole. ★What that cannot rule out is a write that FAILS half way
	//   through the loop below; the reader is TOLD about that one (outRefused) rather than it
	//   being counted as a success, which is what used to happen.
	for (size_t c = 0; c < changes.size(); ++c)
	{
		const KCMTextDiff::Change& ch = changes[c];
		if (RangeTouchesObject(a, ch.aStart, ch.aStart + ch.aCount)
			|| RangeTouchesObject(b, ch.bStart, ch.bStart + ch.bCount))
		{
			whyNot = "a change would add, move or delete a character InDesign hangs an object on "
					 "(an anchored object, a note reference, a page number, an index marker)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	for (size_t c = 0; c < changes.size(); ++c)
	{
		const KCMTextDiff::Change& ch = changes[c];

		// ★★**FIRST CUT WHERE THE TABLES STAND, THE DOCUMENT'S PAIRED WITH THE FILE'S** (G1/G2): which side
		//   of a table words go on, and which of them are before it and which after, only the file's own
		//   table position says. `表の前の文章` + `後の文` is 章 before the table and 表の gone after it.
		std::vector<KCMParaText::KCMObjectPiece> parts;
		KCMParaText::CutChangeAtObjects(docTableOffsets, fileTableOffsets, ch.aStart, ch.aCount,
										ch.bStart, ch.bCount, parts);
		for (size_t q = 0; q < parts.size(); ++q)
		{
			const KCMParaText::KCMObjectPiece& part = parts[q];

			if (part.fACount <= 0)
			{
				// An insertion: in front of the first table the file puts after these words, or after
				// everything standing there when none does.
				const int32 ob = part.fObjectsBefore;
				Piece p;
				p.fChange = c;
				p.fFrom = (ob >= 0 && static_cast<size_t>(ob) < tables.size()
						   && tables[static_cast<size_t>(ob)].fTextOffset == part.fAStart)
						  ? tables[static_cast<size_t>(ob)].fModelOffset
						  : KCMParaText::ModelOffsetInParagraph(attrs, part.fAStart);
				p.fCount = 0;
				p.fAStart = part.fAStart;
				p.fACount = 0;
				p.fBStart = part.fBStart;
				p.fBCount = part.fBCount;
				pieces.push_back(p);
				continue;
			}

			// Then cut where anything else the text leaves out stands BETWEEN two of its characters - a
			// note's reference, whose place the file does not carry (KCMStoryShape::Para) - or a table
			// the pairing above could not place.
			const int32 partEnd = part.fAStart + part.fACount;
			std::vector<int32> cuts;
			for (size_t k = 0; k < attrs.fUncountedAt.size(); ++k)
			{
				const int32 u = attrs.fUncountedAt[k];
				if (u > part.fAStart && u < partEnd && (cuts.empty() || cuts.back() != u))
					cuts.push_back(u);
			}
			if (!cuts.empty() && part.fBCount > 0)
			{
				whyNot = "a change replaces words on both sides of a table or a note reference standing inside "
						 "the paragraph (which side the new words belong on cannot be told - edit the two sides apart)";
				whyNot.SetTranslatable(kFalse);
				outRefused = kTrue;
				return 0;
			}

			int32 s = part.fAStart;
			for (size_t k = 0; k <= cuts.size(); ++k)
			{
				const int32 e = (k < cuts.size()) ? cuts[k] : partEnd;
				Piece p;
				p.fChange = c;
				p.fFrom = KCMParaText::ModelOffsetInParagraph(attrs, s);
				p.fCount = KCMParaText::ModelOffsetInParagraph(attrs, e - 1) + 1 - p.fFrom;
				p.fAStart = s;
				p.fACount = e - s;
				p.fBStart = part.fBStart;		// only ever words when there is one piece (a replacement
				p.fBCount = part.fBCount;		// across a note reference was refused just above)
				pieces.push_back(p);
				s = e;
			}
		}
	}

	for (size_t i = 0; i < pieces.size(); ++i)
	{
		const Piece& p = pieces[i];
		const TextIndex at = paraStart + p.fFrom;
		bool16 inPlace = (at >= threadStart && p.fCount >= 0
						  && at + p.fCount <= threadStart + threadSpan - 1		// never the thread's own end
						  && p.fCount == p.fACount) ? kTrue : kFalse;
		if (inPlace && p.fCount > 0)
		{
			WideString standing;
			TextIterator iter(model, at);
			iter.AppendToStringAndIncrement(&standing, p.fCount);
			if (standing.CharCount() != p.fACount)
				inPlace = kFalse;
			for (int32 k = 0; inPlace && k < p.fACount; ++k)
			{
				if (static_cast<int32>(standing.GetChar(k).GetValue()) != a[static_cast<size_t>(p.fAStart + k)])
					inPlace = kFalse;
			}
		}
		if (!inPlace)
		{
			whyNot = "the copy does not hold the words where they were read, so nothing of this paragraph "
					 "was written (please report this - it is a fault of the plug-in, not of the file)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return 0;
		}
	}

	int32 written = 0;
	for (size_t i = pieces.size(); i > 0; --i)
	{
		const Piece& piece = pieces[i - 1];
		const int32 from = piece.fFrom;

		WideString words;
		if (piece.fBCount > 0)
		{
			std::string text;
			for (int32 k = piece.fBStart; k < piece.fBStart + piece.fBCount
								   && k < static_cast<int32>(b.size()); ++k)
				KCMParaText::AppendUtf8(text, b[k]);

			PMString asString;
			asString.SetUTF8String(text);		// marks it not translatable, which is what we want
			words = WideString(asString);
		}

		// ★★A DELETION IS A DeleteCmd (2026-09-17, the user's call: "match the official way") - the one
		//   place that decides, shared with the restore. (Replace against Delete was NOT what crashed:
		//   both crashed at the same place - the positions were stale; see KCMApplyStoryTextToCopy.)
		const int32 count = piece.fCount;
		InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, paraStart + from, count, words));
		if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			whyNot = (written > 0)
					 ? "a write failed part way through a paragraph, which now holds neither the "
					   "document's words nor the file's (a locked story or layer?)"
					 : "the write failed (a locked story or layer?)";
			whyNot.SetTranslatable(kFalse);
			outRefused = kTrue;
			return written;
		}
		++written;
	}
	return written;
}

/** One write of the text pass: a paragraph's words (ApplyParagraph), new paragraphs, or paragraphs
	taken out. */
struct Job
{
	enum { kParagraph = 0, kInsert = 1, kDelete = 2 };

	int32								fKind;
	/** ★**THE ORDER**: writes go from the highest key down. Twice the position the write starts at, so
		that a write standing at the same position can be put before (+1) or after (-1) the paragraph
		whose start that is - new paragraphs after a paragraph's return go in before that paragraph's own
		words are rewritten (their position is read before those words move it), and new paragraphs in
		front of a place's first paragraph go in after (the paragraph's words are written at the positions
		they were read at). */
	int64								fKey;
	size_t								fPara;			// kParagraph: index into paras / attrs / starts
	const KCMStoryShape::Para*			fFile;			// kParagraph: the same paragraph in the file
	TextIndex							fAt;			// kInsert: where; kDelete: from
	TextIndex							fTo;			// kDelete: up to, not including
	bool16								fAfterReturn;	// kInsert: "\rNEW" after a return, not "NEW\r" before a paragraph
	std::vector<const KCMStoryShape::Para*>	fNew;		// kInsert: the file's new paragraphs, in order

	size_t								fStep;			// which step of the plan this write carries out

	Job() : fKind(kParagraph), fKey(0), fPara(0), fFile(nil), fAt(0), fTo(0), fAfterReturn(kFalse), fStep(0) {}
};

/** Where a paragraph's return stands: after its text and anything standing at its end. */
TextIndex ReturnOfParagraph(int32 paraStart, const KCMParaAttrs& attrs, const std::string& text)
{
	return static_cast<TextIndex>(paraStart)
		   + KCMParaText::ModelOffsetInParagraph(attrs, KCMParaText::CountCodePoints(text));
}

/*	NoteMarkersOfStory
	Where every footnote marker stands in the story, in the order the notes are NUMBERED.

	★**THE ORDER IS THE READER'S OWN**: KCMTextRead numbers the notes as it meets their threads,
	  which is TextIndex order, and a note's thread stands in the same order as its marker. So the
	  n-th entry here is the marker of the note KCMParaAttrs calls n.
	⚠**A NOTE'S OWN FIRST CHARACTER IS A MARKER TOO** (it rides the note's leading uncounted
	 positions), and it is not the body's. Paragraphs inside a note are passed over.
*/
void NoteMarkersOfStory(ITextModel* model, const std::vector<KCMParaAttrs>& attrs,
						const std::vector<int32>& starts, std::vector<TextIndex>& out)
{
	out.clear();
	for (size_t i = 0; i < attrs.size() && i < starts.size(); ++i)
	{
		if (attrs[i].IsFootnote())
			continue;
		const TextIndex paraStart = static_cast<TextIndex>(starts[i]);
		// the leading ones stand BEFORE the paragraph's start, nearest last
		for (int32 k = attrs[i].fLeadingUncounted; k >= 1; --k)
		{
			if (CharAt(model, paraStart - k) == kTextChar_FootnoteMarker)
				out.push_back(paraStart - k);
		}
		for (size_t k = 0; k < attrs[i].fUncountedAt.size(); ++k)
		{
			const TextIndex m = paraStart + attrs[i].fUncountedAt[k] + static_cast<int32>(k);
			if (CharAt(model, m) == kTextChar_FootnoteMarker)
				out.push_back(m);
		}
	}
}

/*	PourNoteWords
	The words of a note just made, put in place of the ones it was born with.

	★**REPLACE, NOT APPEND** - see KCMInsertNoteAt: the note is born holding a separator of its
	  own and the file's first paragraph carries one too, so appending would print both.
	⚠**ONE PARAGRAPH AT A TIME, IN ORDER**: a note with several paragraphs is written as one text
	 with returns between, which is what a paragraph break is in a story.
*/
bool16 PourNoteWords(ITextModel* model, TextIndex from, TextIndex to,
					 const std::vector<KCMStoryShape::Para>& paras, PMString& whyNot)
{
	// ★THE SAME ONE COMMAND THE REST OF THE POUR USES (KCMCreateWordsWriteCmd), which is replace,
	//   insert and delete in one - so a note's words go in the way every other word does.
	std::string text;
	for (size_t p = 0; p < paras.size(); ++p)
	{
		if (p > 0)
			text += '\r';
		text += paras[p].fText;
	}
	PMString asString;
	asString.SetUTF8String(text);
	const WideString words(asString);

	InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, from, (to > from) ? (to - from) : 0, words));
	// ★**nil MEANS "NOTHING TO WRITE", NOT "COULD NOT WRITE"** (KCMStoryRestore.h says so): no
	//   characters coming out and none going in. An empty note Word made, into a note born empty,
	//   is exactly that - and calling it a failure would name a refusal nobody can act on.
	if (write == nil)
		return kTrue;
	if (CmdUtils::ProcessCommand(write) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = "the new footnote's words could not be put in";
		whyNot.SetTranslatable(kFalse);
		return kFalse;
	}
	return kTrue;
}

/** Puts new paragraphs in - "\rNEW" right before a return, or "NEW\r" at a paragraph's start - and gives
	the ones after a return the next style (KCMApplyNextStyleAfter).
	@return how many writes went in. */
int32 InsertParagraphs(ITextModel* model, TextIndex at, bool16 afterReturn,
					   const std::vector<const KCMStoryShape::Para*>& news, PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadSpan));
	const bool16 inPlace = (thread != nil && at >= threadStart && at < threadStart + threadSpan
							&& (!afterReturn || CharAt(model, at) == kTextChar_CR)) ? kTrue : kFalse;
	if (!inPlace || news.empty())
	{
		whyNot = "the copy does not hold the paragraphs where they were read, so no paragraph was added "
				 "(please report this - it is a fault of the plug-in, not of the file)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	std::string text;
	for (size_t k = 0; k < news.size(); ++k)
	{
		if (afterReturn)
			text += '\r';
		text += news[k]->fText;
		if (!afterReturn)
			text += '\r';
	}
	PMString asString;
	asString.SetUTF8String(text);
	const WideString words(asString);

	InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, at, 0, words));
	if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = "adding a paragraph failed (a locked story or layer?)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}
	if (afterReturn)
		KCMApplyNextStyleAfter(model, at, at + 1, words.CharCount() - 1);	// a style that cannot be applied leaves the inherited one
	return 1;
}

/** Takes paragraphs out: [from, to), checked first to be whole paragraphs of one thread holding nothing
	InDesign hangs an object on.
	@return how many writes went in. */
int32 DeleteParagraphs(ITextModel* model, TextIndex from, TextIndex to, PMString& whyNot, bool16& outRefused)
{
	outRefused = kFalse;
	TextIndex threadStart = 0;
	int32 threadSpan = 0;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(from, &threadStart, &threadSpan));
	bool16 inPlace = (thread != nil && from >= threadStart && to > from
					  && to <= threadStart + threadSpan - 1			// never the thread's own last return
					  && CharAt(model, to - 1) != -1) ? kTrue : kFalse;
	// Either [a return, the next return) or [a paragraph's start, just past its return).
	if (inPlace && !(CharAt(model, from) == kTextChar_CR && CharAt(model, to) == kTextChar_CR)
		&& !(CharAt(model, to - 1) == kTextChar_CR && (from == threadStart || CharAt(model, from - 1) == kTextChar_CR)))
		inPlace = kFalse;
	if (inPlace)
	{
		WideString standing;
		TextIterator iter(model, from);
		iter.AppendToStringAndIncrement(&standing, to - from);
		for (int32 k = 0; inPlace && k < standing.CharCount(); ++k)
		{
			if (KCMParaText::IsObjectCharacter(static_cast<int32>(standing.GetChar(k).GetValue())))
				inPlace = kFalse;
		}
	}
	if (!inPlace)
	{
		whyNot = "the copy does not hold the paragraphs where they were read, so no paragraph was removed "
				 "(please report this - it is a fault of the plug-in, not of the file)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}

	InterfacePtr<ICommand> write(KCMCreateWordsWriteCmd(model, from, to - from, WideString()));
	if (write == nil || CmdUtils::ProcessCommand(write) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		whyNot = "removing a paragraph failed (a locked story or layer?)";
		whyNot.SetTranslatable(kFalse);
		outRefused = kTrue;
		return 0;
	}
	return 1;
}


// ---- what the plan speaks in, turned into the document's paragraphs ------------------------------

/** The document's paragraphs of one place, as indices into the flat reading (KCMTextRead's attrs).
	★A CELL IS NAMED BY ITS PLACE IN THE ROW (KCMStorySync::Where), the document's paragraphs by their
	  grid column - ColumnsOfRow turns one into the other. */
std::vector<size_t> DocParasOf(const std::vector<KCMParaAttrs>& attrs, const KCMStorySync::Where& w)
{
	std::vector<size_t> out;
	int32 column = -1;
	if (w.fKind == KCMStorySync::Where::kCell)
	{
		std::vector<int32> cols;
		ColumnsOfRow(attrs, w.fTable, w.fRow, cols);
		if (w.fCell < 0 || static_cast<size_t>(w.fCell) >= cols.size())
			return out;
		column = cols[static_cast<size_t>(w.fCell)];
	}
	for (size_t i = 0; i < attrs.size(); ++i)
	{
		bool16 here = kFalse;
		if (w.fKind == KCMStorySync::Where::kBody)
			here = (!attrs[i].IsCell() && !attrs[i].IsFootnote()) ? kTrue : kFalse;
		else if (w.fKind == KCMStorySync::Where::kNote)
			here = (attrs[i].IsFootnote() && attrs[i].fFootnoteOrdinal == w.fNote) ? kTrue : kFalse;
		else
			here = (attrs[i].IsCell() && attrs[i].fTableOrdinal == w.fTable && attrs[i].fCellRow == w.fRow
					&& attrs[i].fCellCol == column) ? kTrue : kFalse;
		if (here)
			out.push_back(i);
	}
	return out;
}

/** Every place of a story's shape: the body, every cell, every note - in that order. */
std::vector<KCMStorySync::Where> PlacesOf(const KCMStoryShape::Story& s)
{
	std::vector<KCMStorySync::Where> out;
	out.push_back(KCMStorySync::Where::Body());
	for (size_t t = 0; t < s.fTables.size(); ++t)
		for (size_t r = 0; r < s.fTables[t].fRows.size(); ++r)
			for (size_t c = 0; c < s.fTables[t].fRows[r].fCells.size(); ++c)
				out.push_back(KCMStorySync::Where::Cell(static_cast<int32>(t), static_cast<int32>(r), static_cast<int32>(c)));
	for (size_t n = 0; n < s.fNotes.size(); ++n)
		out.push_back(KCMStorySync::Where::Note(static_cast<int32>(n)));
	return out;
}

const std::vector<KCMStoryShape::Para>* ParasOfShape(const KCMStoryShape::Story& s, const KCMStorySync::Where& w)
{
	if (w.fKind == KCMStorySync::Where::kBody)
		return &s.fBody;
	if (w.fKind == KCMStorySync::Where::kNote)
		return (w.fNote >= 0 && static_cast<size_t>(w.fNote) < s.fNotes.size()) ? &s.fNotes[static_cast<size_t>(w.fNote)] : nil;
	if (w.fTable < 0 || static_cast<size_t>(w.fTable) >= s.fTables.size())
		return nil;
	const KCMStoryShape::Table& t = s.fTables[static_cast<size_t>(w.fTable)];
	if (w.fRow < 0 || static_cast<size_t>(w.fRow) >= t.fRows.size()
		|| w.fCell < 0 || static_cast<size_t>(w.fCell) >= t.fRows[static_cast<size_t>(w.fRow)].fCells.size())
		return nil;
	return &t.fRows[static_cast<size_t>(w.fRow)].fCells[static_cast<size_t>(w.fCell)].fParas;
}

void Say(KCMSyncResult& out, const char* kind, const std::string& why, bool16 heldBack = kFalse, bool16 whole = kFalse)
{
	KCMSyncNote n;
	n.fKind = kind;
	n.fWhy.SetUTF8String(why);
	n.fWhy.SetTranslatable(kFalse);
	n.fHeldBack = heldBack;
	n.fWholeStory = whole;
	out.fNotes.push_back(n);
}

void Say(KCMSyncResult& out, const char* kind, const PMString& why)
{
	KCMSyncNote n;
	n.fKind = kind;
	n.fWhy = why;
	n.fWhy.SetTranslatable(kFalse);
	out.fNotes.push_back(n);
}

}	// anonymous namespace

void KCMSyncColumnsOfRow(const std::vector<KCMParaAttrs>& attrs, int32 table, int32 row, std::vector<int32>& outCols)
{
	ColumnsOfRow(attrs, table, row, outCols);
}

void KCMApplySyncPlan(const UIDRef& storyRef, const KCMStoryShape::Story& now,
					  const KCMStorySync::Plan& plan, KCMSyncResult& out)
{
	out = KCMSyncResult();
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
	{
		Say(out, "Story", std::string("the story cannot be edited"), kFalse, kTrue);
		return;
	}

	// ---- 0. what the plan itself holds: named, never written ------------------------------------
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = plan.fSteps[i];
		if (s.fKind != KCMStorySync::Step::kHeld)
			continue;
		++out.fHeld;
		// ★A TATE-CHU-YOKO KEPT UNDER A WARICHU IS HELD BACK ON PURPOSE (Word cannot carry it), not refused
		const bool16 kept = (s.fWhat == "Tcy") ? kTrue : kFalse;
		const char* kind = kept ? "Word" : ((s.fWhat == "Table") ? "Table" : "Para");
		Say(out, kind, s.fWhere.Say() + ": " + s.fWhy, kept);
	}

	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;

	// ---- 1. the footnotes Word took away: first, so that no word write meets their markers ------
	std::vector<int32> gone;		// N's note numbers, ascending
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
		if (plan.fSteps[i].fKind == KCMStorySync::Step::kDeleteNote)
			gone.push_back(plan.fSteps[i].fNote);
	std::sort(gone.begin(), gone.end());
	gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
	if (!gone.empty())
	{
		if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
		{
			Say(out, "Note", std::string("the story could not be read, so no footnote was taken away"));
			gone.clear();
		}
		else
		{
			std::vector<TextIndex> markers;
			NoteMarkersOfStory(model, attrs, starts, markers);
			// ⚠THE MARKERS AND THE NOTES MUST COUNT THE SAME, or a deletion would take away the wrong note
			//  (a footnote inside another has a number of its own and a marker this walk steps over).
			if (markers.size() != now.fNotes.size())
			{
				Say(out, "Note", std::string("this story's footnote markers cannot be told apart one by one "
											 "(a footnote inside another?), so none was taken away"));
				gone.clear();
			}
			for (size_t d = gone.size(); d > 0; --d)
			{
				const int32 n = gone[d - 1];
				PMString why;
				if (n >= 0 && static_cast<size_t>(n) < markers.size()
					&& KCMDeleteNoteAt(model, markers[static_cast<size_t>(n)], why) == kSuccess)
				{
					++out.fNoteEdits;
					continue;
				}
				++out.fRefused;
				Say(out, "Note", why.IsEmpty() ? PMString("a footnote could not be taken away") : why);
			}
		}
	}

	// ---- 2. the words, from the back of the story to the front ----------------------------------
	if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
	{
		Say(out, "Story", std::string("the story could not be read"), kFalse, kTrue);
		return;
	}
	std::vector<Job> jobs;
	std::vector< std::vector<int32> > tableOffsets(plan.fSteps.size());
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = plan.fSteps[i];
		if (s.fKind != KCMStorySync::Step::kSetPara && s.fKind != KCMStorySync::Step::kInsertParas
			&& s.fKind != KCMStorySync::Step::kDeleteParas)
			continue;
		KCMStorySync::Where w = s.fWhere;
		if (w.fKind == KCMStorySync::Where::kNote)
		{
			// a note Word took away has gone, and the ones after it moved up a number
			if (std::binary_search(gone.begin(), gone.end(), w.fNote))
				continue;
			w.fNote -= static_cast<int32>(std::lower_bound(gone.begin(), gone.end(), w.fNote) - gone.begin());
		}
		const std::vector<size_t> doc = DocParasOf(attrs, w);
		if (doc.empty())
		{
			++out.fRefused;
			Say(out, "Place", s.fWhere.Say() + ": the document's paragraphs here could not be found");
			continue;
		}
		Job job;
		if (s.fKind == KCMStorySync::Step::kSetPara)
		{
			if (s.fPara < 0 || static_cast<size_t>(s.fPara) >= doc.size() || s.fParas.empty())
				continue;
			const size_t k = doc[static_cast<size_t>(s.fPara)];
			job.fKind = Job::kParagraph;
			job.fPara = k;
			job.fFile = &s.fParas[0];
			job.fKey = 2 * static_cast<int64>(starts[k]);
			for (size_t t = 0; t < s.fTables.size(); ++t)
				tableOffsets[i].push_back(s.fTables[t].second);
			std::sort(tableOffsets[i].begin(), tableOffsets[i].end());
		}
		else if (s.fKind == KCMStorySync::Step::kInsertParas)
		{
			job.fKind = Job::kInsert;
			for (size_t p = 0; p < s.fParas.size(); ++p)
				job.fNew.push_back(&s.fParas[p]);
			if (s.fPara >= 0 && static_cast<size_t>(s.fPara) < doc.size())
			{
				const size_t before = doc[static_cast<size_t>(s.fPara)];
				job.fAt = ReturnOfParagraph(starts[before], attrs[before], paras[before]);
				job.fAfterReturn = kTrue;
				job.fKey = 2 * static_cast<int64>(job.fAt) + 1;
			}
			else
			{
				const size_t first = doc[0];
				job.fAt = static_cast<TextIndex>(starts[first] - attrs[first].fLeadingUncounted);
				job.fAfterReturn = kFalse;
				job.fKey = 2 * static_cast<int64>(job.fAt) - 1;
			}
		}
		else
		{
			if (s.fPara < 0 || s.fCount <= 0 || static_cast<size_t>(s.fPara + s.fCount) > doc.size())
				continue;
			const size_t last = doc[static_cast<size_t>(s.fPara + s.fCount - 1)];
			job.fKind = Job::kDelete;
			if (s.fPara > 0)
			{
				const size_t before = doc[static_cast<size_t>(s.fPara - 1)];
				job.fAt = ReturnOfParagraph(starts[before], attrs[before], paras[before]);
				job.fTo = ReturnOfParagraph(starts[last], attrs[last], paras[last]);
				job.fKey = 2 * static_cast<int64>(job.fAt) + 1;
			}
			else
			{
				const size_t first = doc[static_cast<size_t>(s.fPara)];
				job.fAt = static_cast<TextIndex>(starts[first] - attrs[first].fLeadingUncounted);
				job.fTo = ReturnOfParagraph(starts[last], attrs[last], paras[last]) + 1;
				job.fKey = 2 * static_cast<int64>(job.fAt) - 1;
			}
		}
		job.fStep = i;
		jobs.push_back(job);
	}
	// ★BACK TO FRONT OVER THE WHOLE STORY - a write moves only what stands after it (the pour's own rule)
	std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.fKey > b.fKey; });
	for (size_t j = 0; j < jobs.size(); ++j)
	{
		const Job& job = jobs[j];
		PMString whyNot;
		bool16 refused = kFalse;
		int32 n = 0;
		if (job.fKind == Job::kInsert)
			n = InsertParagraphs(model, job.fAt, job.fAfterReturn, job.fNew, whyNot, refused);
		else if (job.fKind == Job::kDelete)
			n = DeleteParagraphs(model, job.fAt, job.fTo, whyNot, refused);
		else
			n = ApplyParagraph(model, static_cast<TextIndex>(starts[job.fPara]), attrs[job.fPara],
							   paras[job.fPara], job.fFile->fText, tableOffsets[job.fStep], whyNot, refused);
		// ⚠BOTH ANSWERS ARE READ: a write that failed half way leaves what went in ahead of it
		if (refused)
		{
			++out.fRefused;
			Say(out, "Para", whyNot);
		}
		out.fWrites += (n > 0) ? n : 0;
	}

	// ---- 3. the footnotes Word added: a marker in the finished paragraph, then its words ---------
	{
		std::vector< std::pair<TextIndex, size_t> > pending;
		bool16 read = kFalse;
		for (size_t i = 0; i < plan.fSteps.size(); ++i)
		{
			const KCMStorySync::Step& s = plan.fSteps[i];
			if (s.fKind != KCMStorySync::Step::kAddNote)
				continue;
			if (!read)
			{
				if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
				{
					Say(out, "Note", std::string("the story could not be read again, so no footnote was added"));
					break;
				}
				read = kTrue;
			}
			const std::vector<size_t> doc = DocParasOf(attrs, s.fWhere);
			if (s.fPara < 0 || static_cast<size_t>(s.fPara) >= doc.size())
			{
				++out.fRefused;
				Say(out, "Note", s.fWhere.Say() + ": a footnote added in Word has no paragraph to stand in");
				continue;
			}
			const size_t k = doc[static_cast<size_t>(s.fPara)];
			pending.push_back(std::make_pair(static_cast<TextIndex>(starts[k])
											 + KCMParaText::ModelOffsetInParagraph(attrs[k], s.fAt), i));
		}
		// ★HIGHEST POSITION FIRST, for the reason the word pass is
		std::stable_sort(pending.begin(), pending.end());
		for (size_t p = pending.size(); p > 0; --p)
		{
			const KCMStorySync::Step& s = plan.fSteps[pending[p - 1].second];
			PMString why;
			TextIndex from = 0;
			TextIndex to = 0;
			if (KCMInsertNoteAt(model, pending[p - 1].first, from, to, why) != kSuccess)
			{
				++out.fRefused;
				Say(out, "Note", why);
				continue;
			}
			++out.fNoteEdits;		// the note is in the document from here on, whatever becomes of its words
			if (!PourNoteWords(model, from, to, s.fParas, why))
			{
				++out.fRefused;
				Say(out, "Note", why);
			}
		}
	}

	// ---- 4. the ruby, the kenten and the rest, over the words that went in ----------------------
	// ★AGAINST THE FINISHED SHAPE - ApplyToShape of the document AS READ, not the normalized one, so a
	//   paragraph the plan left alone is compared with itself and nothing is written into it - with its
	//   notes numbered the way the document numbers them (RenumberNotesByThread).
	KCMStoryShape::Story finished = KCMStorySync::ApplyToShape(now, plan);
	KCMStorySync::RenumberNotesByThread(finished);
	if (!KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
	{
		Say(out, "Attr", std::string("the story could not be read again, so its ruby and kenten were left alone"));
		return;
	}
	const std::vector<KCMStorySync::Where> places = PlacesOf(finished);
	for (size_t p = 0; p < places.size(); ++p)
	{
		const std::vector<KCMStoryShape::Para>* want = ParasOfShape(finished, places[p]);
		const std::vector<size_t> doc = DocParasOf(attrs, places[p]);
		if (want == nil || doc.size() != want->size())
			continue;		// a place a refusal above has already named
		for (size_t q = 0; q < doc.size(); ++q)
		{
			const size_t i = doc[q];
			PMString whyNot;
			PMString kept;
			bool16 refused = kFalse;
			const int32 n = KCMPourParagraphAttributes(model, static_cast<TextIndex>(starts[i]), attrs[i],
													   paras[i], (*want)[q], whyNot, refused, kept);
			if (refused)
			{
				++out.fRefused;
				Say(out, "Attr", whyNot);
			}
			if (!kept.IsEmpty())
			{
				KCMSyncNote note;
				note.fKind = "Word";
				note.fWhy = kept;
				note.fHeldBack = kTrue;
				out.fNotes.push_back(note);
			}
			out.fAttrWrites += (n > 0) ? n : 0;
		}
	}
}

namespace
{

/** How wide each column of a table put in at `pos` should be: the frame's width there, shared evenly
	(design section 10-1). ★THROUGH THE PARCEL, NOT IFrameList::QueryFrameContaining: that one composes up
	to the position and crashed once inside a table (KCMID.h, the fix of 2026-09-12). A place not yet
	composed into a frame (overset) gets a plain default. */
PMReal ColumnWidthAt(ITextModel* model, TextIndex pos, int32 cols)
{
	PMReal total(360.0);
	InterfacePtr<ITextParcelList> tpl(model != nil ? model->QueryTextParcelList(pos) : nil);
	if (tpl != nil)
	{
		const ParcelKey key = tpl->GetParcelContaining(pos);
		InterfacePtr<IParcelList> pl(static_cast<IParcelList*>(tpl->QueryInterface(IParcelList::kDefaultIID)));
		if (key.IsValid() && pl != nil)
		{
			const PMRect bounds = pl->GetParcelBounds(key);
			if (bounds.Width() > 0.0)
				total = bounds.Width();
		}
	}
	return total / static_cast<PMReal>(cols > 0 ? cols : 1);
}

/** Stage 0's tables put in (S3a, design section 10-2): every position read before the first goes in, then
	written from the back of the story - two at one place in Word's order. */
void InsertTables(const UIDRef& storyRef, const KCMStorySync::Plan& plan, KCMSyncResult& out)
{
	std::vector<const KCMStorySync::Step*> wanted;
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
		if (plan.fSteps[i].fKind == KCMStorySync::Step::kInsertTable)
			wanted.push_back(&plan.fSteps[i]);
	if (wanted.empty())
		return;

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	std::vector<std::string> paras;
	std::vector<KCMParaAttrs> attrs;
	std::vector<int32> starts;
	Utils<ITableUtils> tableUtils;
	if (model == nil || !tableUtils || !KCMTextRead::ReadStory(storyRef, paras, attrs, starts))
	{
		out.fRefused += static_cast<int32>(wanted.size());
		Say(out, "Table", std::string("the story could not be read, so no table Word added was put in"));
		return;
	}
	const std::vector<size_t> body = DocParasOf(attrs, KCMStorySync::Where::Body());
	std::vector< std::pair< std::pair<TextIndex, int32>, const KCMStorySync::Step*> > at;
	for (size_t i = 0; i < wanted.size(); ++i)
	{
		const KCMStorySync::Step& s = *wanted[i];
		TextIndex pos = 0;
		if (s.fPara >= 0 && static_cast<size_t>(s.fPara) < body.size())
		{
			const size_t k = body[static_cast<size_t>(s.fPara)];
			pos = ReturnOfParagraph(starts[k], attrs[k], paras[k]);		// before its return (spike M6)
		}
		else if (!body.empty())
			pos = static_cast<TextIndex>(starts[body[0]] - attrs[body[0]].fLeadingUncounted);
		at.push_back(std::make_pair(std::make_pair(pos, s.fNote), &s));
	}
	std::sort(at.begin(), at.end(), [](const std::pair< std::pair<TextIndex, int32>, const KCMStorySync::Step*>& a,
										const std::pair< std::pair<TextIndex, int32>, const KCMStorySync::Step*>& b)
									 { return a.first > b.first; });
	for (size_t i = 0; i < at.size(); ++i)
	{
		const KCMStorySync::Step& s = *at[i].second;
		// ★A PARAGRAPH OF ITS OWN (2026-09-24, S3b design 12-3-5 - the user's rule): a return first, at the paragraph's
		//   end (before its own return), so the table goes into the new empty paragraph between the two returns.
		//   At the head of the body the return goes at the body's start and the table in front of it. (Until that
		//   day the table went at the end of the paragraph itself - "A[T]", one paragraph.) Back to front as before,
		//   so two tables after one paragraph still come out in Word's order.
		const TextIndex where = at[i].first.first;
		const bool16 atHead = (s.fPara < 0 || static_cast<size_t>(s.fPara) >= body.size()) ? kTrue : kFalse;	// the test that chose `pos`
		{
			PMString asString;								// the way InsertParagraphs builds its words
			asString.SetUTF8String(std::string("\r"));
			const WideString aReturn(asString);
			InterfacePtr<ICommand> newPara(KCMCreateWordsWriteCmd(model, where, 0, aReturn));
			if (newPara == nil || CmdUtils::ProcessCommand(newPara) != kSuccess)
			{
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				++out.fRefused;
				Say(out, "Table", std::string("a table Word added could not be given a paragraph of its own"));
				continue;
			}
		}
		const TextIndex tableAt = atHead ? where : where + 1;
		InterfacePtr<ITableModelList> list(model, UseDefaultIID());
		const int32 before = (list != nil) ? list->GetModelCount() : -1;
		// ★THE WAY KCMReportTable PUTS ITS TABLE IN (codesnippets/SnpCreateTable.cpp): no header or footer
		//   rows, row height 0 = grows with its content, no selection left behind
		tableUtils->InsertTable(model, tableAt, 0, s.fCount, s.fAt, 0, 0, PMReal(0.0),
								ColumnWidthAt(model, tableAt, s.fAt), kTextContentType, ITableUtils::eNoSelection);
		const int32 after = (list != nil) ? list->GetModelCount() : -1;
		if (before < 0 || after != before + 1)
		{
			++out.fRefused;
			Say(out, "Table", std::string("a table Word added could not be put in"));
			continue;
		}
		++out.fTableEdits;
	}
}

}	// anonymous namespace

void KCMApplyTableShape(const UIDRef& storyRef, const KCMStorySync::Plan& plan, KCMSyncResult& out)
{
	// ★EVERY TABLE HELD BY ITS UIDRef FIRST: the steps name a table by its ordinal in the reading from
	//  before any change, and a table put in (below) or taken away would move every ordinal after it
	//  (re-check 2026-09-24: these two were the other way round, harmless only because a round of tables
	//  put in holds no other step)
	std::vector<UIDRef> tables;
	if (!KCMTableRefsOfStory(storyRef, tables))
	{
		++out.fRefused;
		Say(out, "Table", std::string("the story's tables could not be found, so no table's shape was changed"));
		return;
	}

	// ★TABLES PUT IN NEXT, by the text positions of the same reading, from the back of the story
	InsertTables(storyRef, plan, out);
	for (size_t i = 0; i < plan.fSteps.size(); ++i)
	{
		const KCMStorySync::Step& s = plan.fSteps[i];
		if (!s.IsShape() || s.fKind == KCMStorySync::Step::kInsertTable)
			continue;
		const int32 t = s.fWhere.fTable;
		if (t < 0 || static_cast<size_t>(t) >= tables.size())
		{
			++out.fRefused;
			Say(out, "Table", s.fWhere.Say() + ": the table could not be found");
			continue;
		}
		InterfacePtr<ITableModel> table(tables[static_cast<size_t>(t)], UseDefaultIID());
		// ★THE OFFICIAL SHAPE: ITableCommands is queried from the table model (tablebasics/
		//   TblBscSuiteTextCSB.cpp, codesnippets/SnpSortTable.cpp; KCMReportTable does the same)
		InterfacePtr<ITableCommands> cmds(table, UseDefaultIID());
		if (table == nil || cmds == nil)
		{
			++out.fRefused;
			Say(out, "Table", s.fWhere.Say() + ": the table cannot be edited");
			continue;
		}
		ErrorCode err = kSuccess;
		const char* what = "";
		if (s.fKind == KCMStorySync::Step::kResizeRows)
		{
			const RowRange rows = table->GetTotalRows();
			what = "its rows could not be made as many as Word's";
			if (s.fCount <= 0 || s.fCount == rows.count)
				continue;
			if (s.fCount > rows.count)
				// ★AFTER THE LAST ROW - which puts them in the footer when the table has one (the user's rule,
				//   design section 1-7; measured in the spike, M1a). Height 0: the last row's is taken over.
				err = cmds->InsertRows(RowRange(rows.start + rows.count - 1, s.fCount - rows.count), Tables::eAfter, 0.0);
			else
				// ★FROM THE BOTTOM: a footnote or an anchored object in these rows goes with them (the user's
				//   rule, design section 1-6 - InDesign says nothing, spike M2b)
				err = cmds->DeleteRows(RowRange(rows.start + s.fCount, rows.count - s.fCount));
		}
		else if (s.fKind == KCMStorySync::Step::kResizeCols)
		{
			const ColRange cols = table->GetTotalCols();
			what = "its columns could not be made as many as Word's";
			if (s.fCount <= 0 || s.fCount == cols.count)
				continue;
			if (s.fCount > cols.count)
				// ★AFTER THE LAST COLUMN (the rows' rule, design section 9-1). Width 0: the last column's.
				err = cmds->InsertColumns(ColRange(cols.start + cols.count - 1, s.fCount - cols.count), Tables::eAfter, 0.0);
			else
				err = cmds->DeleteColumns(ColRange(cols.start + s.fCount, cols.count - s.fCount));
		}
		else if (s.fKind == KCMStorySync::Step::kUnmerge)
		{
			what = "a merged cell could not be taken apart";
			err = cmds->UnmergeCell(GridAddress(s.fGridRow, s.fGridCol));
		}
		else if (s.fKind == KCMStorySync::Step::kDeleteTable)
		{
			// ★THE TABLE AND ALL IT HOLDS - its footnotes and anchored objects too (the user's rule, design
			//   section 10-1). ITableCommands' own command for it.
			what = "a table Word took away could not be taken away";
			InterfacePtr<ICommand> del(cmds->QueryDeleteTableCmd(tables[static_cast<size_t>(t)]));
			err = (del != nil) ? CmdUtils::ProcessCommand(del) : kFailure;
		}
		else if (s.fKind == KCMStorySync::Step::kMerge)
		{
			// ★GridArea's bottom and right are PAST the last row and column (TableTypes.h: Height() is
			//   bottomRow - topRow), the way KCMReportTable merges its heading cells
			what = "cells could not be merged as Word's are";
			err = cmds->MergeCells(GridArea(s.fGridRow, s.fGridCol, s.fGridRow + s.fGridRowSpan, s.fGridCol + s.fGridColSpan));
		}
		if (err != kSuccess)
		{
			++out.fRefused;
			Say(out, "Table", s.fWhere.Say() + ": " + what);
			continue;
		}
		++out.fTableEdits;
	}
}

// End, KCMStorySyncApply.cpp.
