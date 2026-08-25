//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  The panel's MESSAGE AREA, drawn by hand so that it can show more than one colour: when a change
//  row is clicked, this box shows the OTHER side of that edit, with the characters that differ at
//  the theme's text colour and the words around them faded (user's request, 2026-08-20 - the same
//  treatment the change ROW already had).
//
//  ★WHAT A STOCK MULTI-LINE STATIC TEXT GAVE US, AND WHAT IT COST. It held one string, wrapped
//  it, and drew the lot in ONE colour. The wrapping was the part worth keeping -- messages are
//  raised from many places through KCMSetStatus and several of them put a full save path in here
//  -- so it is written out below rather than lost. The one colour was the part that had to go.
//
//  ★AN ORDINARY MESSAGE IS NOT A SPECIAL CASE HERE: it arrives as the middle piece alone and
//  comes out as one run at the theme’s text colour, which is what the stock widget drew. That is
//  why **not one of those callers needed changing** (IKCMStatusTextData.h).
//  ⚠**Do not write how many callers there are.** "72" stood in these two paragraphs while there
//    were 42, and the same number had been copied into IKCMStatusTextData.h as well.
//
//  ★★HOW MANY LINES: as many as the box holds, worked out at draw time. The resource this replaced
//  declared 4, and in a Japanese UI 4 is still what fits - measured 2026-08-21 with a diagnostic
//  build: the font answers ascent 12.7 + descent 5.3 + leading 0.0 = 18.0px, and the box is 74px.
//  An English UI's palette font is about 12px a line (memory
//  palette-font-line-height-by-ui-language), which makes the same box hold 6 - room the stock
//  widget's hardcoded 4 was leaving empty. ⚠UNMEASURED: this machine has only the Japanese UI.
//  A hand-drawn box has no line-count field to disagree with its own height, so the height and the
//  font are the whole answer.
//
//  ★★WHEN IT DOES NOT FIT, THE CONTEXT GIVES WAY - the same rule the change row follows
//  (KCMStoryCellView.cpp), so that the two answer that question the same way. The change itself
//  is cut only when it alone overflows the box, and then an ellipsis says so.
//  ⚠An ordinary message has no context, so a long one takes the second branch and ends in an
//    ellipsis. That is a CHANGE from the stock widget, which cut silently at the last line it had
//    room for. Truncation that shows is better than truncation that does not - but the ellipsis is
//    not a licence to write long messages: a number cut in half still reads as a different number
//    (memory ellipsis-in-status-line-breaks-numbers), so the rule "keep the line short" stands.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IInterfaceFonts.h"	// the palette window font

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString
#include "DVPublicUtilities.h"	// dv_utils::FontInfoGetDVAFontMetrics - the font's own ascent/descent
#include "ISession.h"			// GetExecutionContextSession
#include "IWidgetUtils.h"		// GetViewYPosition - only for the fallback when the metrics are refused
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId
#include "Utils.h"				// Utils<IWidgetUtils>()
#include "WidgetDefs.h"			// EllipsizeStyle (kEllipsizeEnd) - only the reading is ever ellipsized here

// Project includes:
#include "IKCMStatusTextData.h"
#include "KCMUIID.h"
#include "KCMPanelTextDraw.h"	// kKCMContextTextWeight, KCMBlendColor - shared with the row cell

// Std includes:
#include <vector>

namespace
{

/* One piece of the message as it arrives: a string, and whether it is drawn faded.
   ★Only two colours exist in this box, and the faded one means one thing only: CONTEXT - words
   carried along so the reader can place the change. Everything the message itself says (the
   heading, and the characters that differ) is drawn at the theme's text colour. */
struct KCMRun
{
	PMString	fText;
	bool16		fFaded;

	/* Is this run the CHANGED CHARACTERS - the ones a reading would go over (2026-08-22)?

	   ★NOT THE SAME QUESTION AS "not faded", which is why it needs a field of its own. The HEADING
	   is drawn unfaded too (user's call, 2026-08-21: it is not context, it is the box saying which
	   document these words are from), so a search for the unfaded run would find the heading first
	   and hang the reading over "Source Text:". */
	bool16		fIsChange;

	KCMRun() : fFaded(kTrue), fIsChange(kFalse) {}
	KCMRun(const PMString& text, bool16 faded, bool16 isChange = kFalse)
		: fText(text), fFaded(faded), fIsChange(isChange) {}
};

/* One piece of the message as it will be drawn: a run, or the part of a run that fell on one line.
   ★A run that crosses a line boundary becomes two fragments of the SAME colour - which is the whole
   reason the colour travels on the fragment rather than on the line. */
struct KCMFrag
{
	PMString	fText;
	bool16		fFaded;
	bool16		fIsChange;	// carried through from the run - see KCMRun
	int32		fLine;
	PMReal		fX;

	KCMFrag() : fFaded(kTrue), fIsChange(kFalse), fLine(0), fX(0.0) {}
	KCMFrag(const PMString& text, bool16 faded, bool16 isChange, int32 line, const PMReal& x)
		: fText(text), fFaded(faded), fIsChange(isChange), fLine(line), fX(x) {}
};

// Spelled out rather than passed as bare kFalse, and spelled out on EVERY call: the defaults in
// DrawStringUtils.h disagree with each other (the draw calls default to kFalse, the measure calls to
// kTrue), so taking the defaults would measure a string differently from how it is drawn.
// ⚠'&' has to survive verbatim - this box shows save paths, and a folder called "Q&A資料" lost its
//   ampersand until the resource this replaced set the same flag to kFalse (KCMUI.fr).
const bool16 kKCMDontConvertAmpersand = kFalse;
const bool16 kKCMNoUnderline = kFalse;

/* KCMHead / KCMTail
   The first n characters, and everything from the nth on. ★Written with Truncate/Remove rather than
   PMString::Substring because those work in the same unit as CharCount and hand back a value -
   Substring returns a string the caller has to delete (PMString.h:357-363). */
PMString KCMHead(const PMString& s, int32 n)
{
	PMString out(s);
	const int32 total = out.CharCount();
	if (n < 0)
		n = 0;
	if (n < total)
		out.Truncate(total - n);
	return out;
}

PMString KCMTail(const PMString& s, int32 n)
{
	PMString out(s);
	if (n > 0)
		out.Remove(0, (n < out.CharCount()) ? n : out.CharCount());
	return out;
}

/* KCMSafeCut
   Move a cut position off the middle of a surrogate pair, so that neither side of the cut ends up
   holding half a character.

   ★It cuts BEFORE the pair, whichever side is being kept: KCMHead keeps [0, pos) and KCMTail
   keeps [pos, end), so one position serves both.
   ⚠It may well never fire. PMString's character count is documented as characters rather than
     bytes, and TextIndex - which counts the same way - counts a surrogate pair as one
     (memory textindex-counts-code-points). If that holds here, GetChar never hands back half a
     pair and this is a no-op. It is three lines for a doubt that would otherwise have to be
     carried around, and it is correct either way. */
int32 KCMSafeCut(const PMString& s, int32 pos)
{
	const int32 total = s.CharCount();
	if (pos <= 0 || pos >= total)
		return pos;
	const uchar16 c = s.GetChar(pos).GetValue();
	if (c >= 0xDC00 && c <= 0xDFFF)		// the second half of a pair: cut in front of the first half
		return pos - 1;
	return pos;
}

/** U+2026 HORIZONTAL ELLIPSIS, the same character the model puts on a cut excerpt
	(KCMStoryDiffRun.cpp writes it as the UTF-8 bytes \xE2\x80\xA6).
	⚠Written as an escape rather than as the glyph: a narrow literal would be converted to the
	  system code page by the compiler, and the glyph itself would depend on this file keeping its
	  UTF-8 BOM - which files in this feature have lost three times (memory cpp-japanese-needs-bom). */
PMString KCMEllipsis()
{
	const char16_t ellipsis[] = u"…";
	PMString s;
	s.SetXString(reinterpret_cast<const UTF16TextChar*>(ellipsis), 1);
	s.SetTranslatable(kFalse);
	return s;
}

PMReal KCMWidth(IGraphicsContext* gc, const PMString& s, const InterfaceFontInfo& font)
{
	if (s.IsEmpty())
		return PMReal(0.0);
	return StringUtils::PMMeasureString(gc, s, font, kKCMDontConvertAmpersand).X();
}

/* KCMFitCount
   How many characters from the front of s fit in `room`.

   ★MEASURED AS WHOLE PREFIXES, NOT SUMMED PER CHARACTER. Adding up the width of each character in
   turn would be one measurement cheaper but would ignore the spacing a font puts BETWEEN glyphs,
   and the error accumulates along the line. A binary search over prefixes asks the same question
   the drawing will ask, about six times per line. */
int32 KCMFitCount(IGraphicsContext* gc, const InterfaceFontInfo& font,
					const PMString& s, const PMReal& room)
{
	const int32 total = s.CharCount();
	if (total <= 0 || room <= PMReal(0.0))
		return 0;
	if (KCMWidth(gc, s, font) <= room)
		return total;

	int32 lo = 0, hi = total;
	while (lo < hi)
	{
		const int32 mid = (lo + hi + 1) / 2;
		if (KCMWidth(gc, KCMHead(s, mid), font) <= room)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

/** Is there any text after this point that the layout has not placed? */
bool16 KCMAnythingLeft(const std::vector<KCMRun>& runs, size_t atRun, const PMString& rest)
{
	if (!rest.IsEmpty())
		return kTrue;
	for (size_t i = atRun + 1; i < runs.size(); ++i)
		if (!runs[i].fText.IsEmpty())
			return kTrue;
	return kFalse;
}

/* KCMLayoutRuns
   Wrap the runs into lines and record where each fragment goes.

   @param availWidth the width of one line.
   @param maxLines how many lines the box holds.
   @param out [out] every fragment that FITS, in drawing order. ★Filled even when the answer is
	  kFalse, so that a caller with nothing better to fall back on can still draw what fits.
   @return kTrue when everything was placed; kFalse when the box ran out of lines first.

   Breaking rules, in order:
	 1. a line break in the text is always honoured;
	 2. a line is broken at a SPACE when there is one to break at (English, and paths that have
		one) - otherwise between characters, which is the only thing Japanese offers and the only
		thing a long path offers;
	 3. a wrapped line never starts with the space it was broken at. (A line the TEXT broke keeps
		its spaces: those are the author's.) */
bool16 KCMLayoutRuns(IGraphicsContext* gc, const InterfaceFontInfo& font,
					   const std::vector<KCMRun>& runs, const PMReal& availWidth,
					   int32 maxLines, std::vector<KCMFrag>& out)
{
	out.clear();
	if (maxLines <= 0 || availWidth <= PMReal(0.0))
		return runs.empty();

	int32 line = 0;
	PMReal x(0.0);
	bool16 justWrapped = kFalse;

	for (size_t r = 0; r < runs.size(); ++r)
	{
		PMString rest = runs[r].fText;
		const bool16 faded = runs[r].fFaded;
		const bool16 isChange = runs[r].fIsChange;

		while (!rest.IsEmpty())
		{
			// (1) A break the text asked for.
			if (rest.GetChar(0).IsLineBreakChar())
			{
				const bool16 wasCR = rest.GetChar(0).IsChar('\r');
				rest.Remove(0, 1);
				if (wasCR && !rest.IsEmpty() && rest.GetChar(0).IsChar('\n'))
					rest.Remove(0, 1);		// CRLF is one break, not two
				++line;
				x = PMReal(0.0);
				justWrapped = kFalse;		// the author's spaces on the new line are the author's
				if (line >= maxLines)
					return !KCMAnythingLeft(runs, r, rest);
				continue;
			}

			// (3) The space a wrap broke at does not start the next line.
			if (justWrapped && rest.GetChar(0).IsSpace())
			{
				rest.Remove(0, 1);
				continue;
			}

			// As much of this run as may go on one line: up to the next break it asks for.
			int32 breakAt = -1;
			const int32 restLen = rest.CharCount();
			for (int32 i = 0; i < restLen; ++i)
			{
				if (rest.GetChar(i).IsLineBreakChar())
				{
					breakAt = i;
					break;
				}
			}
			const PMString chunk = (breakAt < 0) ? rest : KCMHead(rest, breakAt);

			const PMReal room = availWidth - x;
			int32 fit = KCMFitCount(gc, font, chunk, room);

			if (fit >= chunk.CharCount())
			{
				// All of it goes on this line.
				if (!chunk.IsEmpty())
				{
					out.push_back(KCMFrag(chunk, faded, isChange, line, x));
					x += KCMWidth(gc, chunk, font);
				}
				rest = (breakAt < 0) ? PMString() : KCMTail(rest, breakAt);	// leave the break itself
				justWrapped = kFalse;
				continue;
			}

			if (fit <= 0)
			{
				// Nothing fits in what is left of this line. Start a fresh one and try again -
				// unless this line was already fresh, in which case one character has to go down
				// anyway or the loop would never move.
				if (x > PMReal(0.0))
				{
					++line;
					x = PMReal(0.0);
					justWrapped = kTrue;
					if (line >= maxLines)
						return !KCMAnythingLeft(runs, r, rest);
					continue;
				}
				fit = 1;
			}
			else
			{
				// (2) Break at a space if this chunk offers one.
				int32 space = -1;
				for (int32 i = fit; i > 0; --i)
				{
					if (chunk.GetChar(i - 1).IsSpace())
					{
						space = i;
						break;
					}
				}
				fit = (space > 0) ? space : KCMSafeCut(chunk, fit);
			}

			const PMString head = KCMHead(chunk, fit);
			if (!head.IsEmpty())
				out.push_back(KCMFrag(head, faded, isChange, line, x));
			rest = KCMTail(rest, fit);
			++line;
			x = PMReal(0.0);
			justWrapped = kTrue;
			if (line >= maxLines)
				return !KCMAnythingLeft(runs, r, rest);
		}
	}
	return kTrue;
}

/** The four pieces as runs, with the heading on a line of its own. */
std::vector<KCMRun> KCMMakeRuns(const PMString& label, const PMString& pre,
									const PMString& mid, const PMString& post)
{
	std::vector<KCMRun> runs;
	if (!label.IsEmpty())
	{
		PMString heading(label);
		heading.Append("\n");		// ★the heading owns its break: nothing may share its line
		// ★NOT FADED (user's call, 2026-08-21). Faded means context - and the heading is not
		//   context: it is the box saying WHICH document's words these are, which is the one thing
		//   the reader cannot work out from the words themselves. It was faded only because it was
		//   grouped with everything that is not the change; being neither is what it is.
		runs.push_back(KCMRun(heading, kFalse));
	}
	if (!pre.IsEmpty())
		runs.push_back(KCMRun(pre, kTrue));
	if (!mid.IsEmpty())
		runs.push_back(KCMRun(mid, kFalse, kTrue /*these are the changed characters*/));
	if (!post.IsEmpty())
		runs.push_back(KCMRun(post, kTrue));
	return runs;
}

/** The leading context cut down to its last `keep` characters, with an ellipsis for what went.
	★It loses its HEAD - the end facing AWAY from the change. Whatever ellipsis the model put there
	goes with it, and exactly one is put back. */
PMString KCMTrimLeadingContext(const PMString& pre, int32 keep)
{
	const int32 total = pre.CharCount();
	if (pre.IsEmpty() || keep >= total)
		return pre;
	PMString out = KCMEllipsis();
	out.Append(KCMTail(pre, KCMSafeCut(pre, total - keep)));
	out.SetTranslatable(kFalse);
	return out;
}

/** The trailing context cut down to its first `keep` characters. It loses its TAIL, on the same
	terms as above. */
PMString KCMTrimTrailingContext(const PMString& post, int32 keep)
{
	const int32 total = post.CharCount();
	if (post.IsEmpty() || keep >= total)
		return post;
	PMString out = KCMHead(post, KCMSafeCut(post, keep));
	out.Append(KCMEllipsis());
	out.SetTranslatable(kFalse);
	return out;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KCMStatusTextData - the four strings the message area draws
//----------------------------------------------------------------------------------------

/** Non-persistent holder for the current message, aggregated on the widget's boss beside the view.
	Written by KCMSetStatus / KCMSetStatusSegments, read by Draw. */
class KCMStatusTextData : public CPMUnknown<IKCMStatusTextData>
{
public:
	KCMStatusTextData(IPMUnknown* boss) : CPMUnknown<IKCMStatusTextData>(boss) {}
	virtual ~KCMStatusTextData() {}

	virtual void SetSegments(const PMString& label, const PMString& pre,
							 const PMString& mid, const PMString& post,
							 const PMString& ruby)
	{
		// ★Not translation keys. Messages are assembled sentences and document text, and a short
		//   common word left translatable can be looked up in the string tables and come back as
		//   something else entirely (memory menu-string-translation-traps). The callers say so too;
		//   saying it here as well means the box is safe whoever writes to it.
		fLabel = label; fLabel.SetTranslatable(kFalse);
		fPre   = pre;   fPre.SetTranslatable(kFalse);
		fMid   = mid;   fMid.SetTranslatable(kFalse);
		fPost  = post;  fPost.SetTranslatable(kFalse);
		fRuby  = ruby;  fRuby.SetTranslatable(kFalse);
	}

	virtual void GetSegments(PMString& outLabel, PMString& outPre,
							 PMString& outMid, PMString& outPost, PMString& outRuby) const
	{
		outLabel = fLabel;
		outPre   = fPre;
		outMid   = fMid;
		outPost  = fPost;
		outRuby  = fRuby;
	}

private:
	PMString fLabel;
	PMString fPre;
	PMString fMid;
	PMString fPost;
	PMString fRuby;
};

CREATE_PMINTERFACE(KCMStatusTextData, kKCMStatusTextDataImpl)

//----------------------------------------------------------------------------------------
// KCMStatusTextView - the self-drawing message area
//----------------------------------------------------------------------------------------

/** Implements IControlView: wraps the message into the box and draws it in up to two colours. */
class KCMStatusTextView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KCMStatusTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KCMStatusTextView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KCMStatusTextView, kKCMStatusTextViewImpl)

void KCMStatusTextView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKCMStatusTextData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString label, pre, mid, post, ruby;
	data->GetSegments(label, pre, mid, post, ruby);

	// ★NOTHING IS PAINTED BEHIND THE TEXT. The panel draws its own background; this box adds words
	//   on top of it, exactly as the stock widget it replaced did. An empty message is therefore a
	//   no-op rather than a blank rectangle.
	if (label.IsEmpty() && pre.IsEmpty() && mid.IsEmpty() && post.IsEmpty())
		return;

	// ⚠A reading with nothing under it is not drawn. It belongs OVER the changed characters, and
	//   without them there is no place for it to be - see the layout below, which finds its line by
	//   looking for them.
	if (mid.IsEmpty())
		ruby = PMString();		// (assigned rather than Clear()d - see the kNothing below)

	// The palette window's SYSTEM SCRIPT font - the one the resource this replaced named
	// (kPaletteWindowFontId there; the system-script variant here for the same reason the change
	// row's cell uses it, since this box now shows the document's own text as well as the panel's
	// own sentences). ⚠A hand-drawn widget has no font field to read: its boss is a generic panel,
	//   which carries no IUIFontSpec, so the font is named here in code.
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);

	const PMRect frame = this->GetInnerContentFrame();
	const PMReal availWidth = frame.Width();
	if (availWidth <= PMReal(0.0))
		return;

	// ★★ONE LINE'S ADVANCE COMES FROM THE FONT, NOT FROM A MEASURED STRING (2026-08-21, measured
	//   with a temporary diagnostic build). ascent + descent + leading is what decides how tall a
	//   line of this font is - and in a Japanese UI it answers **18.0** (12.7 + 5.3 + 0.0) where
	//   PMMeasureString("Ag").Y() answers **19.0**. That one pixel of padding costs a whole LINE:
	//   the box is 74px, which holds 4 lines at 18 and only 3 at 19. ⚠And 4 is not a coincidence -
	//   KCMUI.fr grew this box to exactly that height for exactly that reason (2026-08-11, from the
	//   same 17.9px measurement: memory palette-font-line-height-by-ui-language). Measuring a
	//   string answers "how tall is this ink", which is a different question from "how far to the
	//   next line".
	// ★The fallback is the measured string, so a font whose metrics are refused still draws
	//   something readable rather than nothing.
	PMReal lineHeight(0.0);
	PMReal ascent(0.0);
	bool16 haveMetrics = kFalse;
	{
		float fontSize = 0.0f, fontAscent = 0.0f, fontDescent = 0.0f, fontLeading = 0.0f;
		if (dv_utils::FontInfoGetDVAFontMetrics(fontInfo, &fontSize, &fontAscent, &fontDescent, &fontLeading))
		{
			lineHeight = PMReal(fontAscent + fontDescent + fontLeading);
			ascent = PMReal(fontAscent);
			haveMetrics = (lineHeight > PMReal(0.0));
		}
	}
	if (!haveMetrics)
	{
		lineHeight = StringUtils::PMMeasureString(&gc, PMString("Ag"), fontInfo, kKCMDontConvertAmpersand).Y();
		if (lineHeight <= PMReal(0.0))
			return;
		ascent = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, lineHeight);
	}

	// ★★A READING COSTS ONE LINE OF THE BOX, AND IT IS TAKEN OUT HERE (2026-08-22) - before the
	//   wrapping, so that everything downstream (the two binary searches that give the context away,
	//   the ellipsis rule) works against the room that is really left. The reading is drawn on a
	//   line of its own above the changed characters, and there is only ever ONE such line however
	//   the text wraps: the changed characters are one run, and the reading goes over the first
	//   piece of it (see the drawing below). In a Japanese UI that is 4 lines becoming 3.
	// ⚠Not "a line per fragment". A run split across a wrap becomes several fragments, and hanging a
	//   reading over each would be claiming the word was read that way in the document.
	const int32 rubyLines = ruby.IsEmpty() ? 0 : 1;

	int32 maxLines = static_cast<int32>(ToDouble(frame.Height() / lineHeight)) - rubyLines;
	if (maxLines < 1)
		maxLines = 1;		// a box too short for even one line still shows the beginning of it

	// Colours, entirely from the current theme, so the panel keeps working in a light UI and a dark
	// one without a hardcoded value. ⚠No hilite pair here, unlike the change row's cell: a message
	//   area is never selected, so the panel's fill is always what stands behind it.
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);		// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(kInterfaceTextColor, fg);
	}
	const RealAGMColor kChangeColor = fg;
	const RealAGMColor kContextColor = KCMBlendColor(bg, fg, PMReal(kKCMContextTextWeight));

	// ---- lay the message out, giving the context away first if it does not fit ----------------

	std::vector<KCMFrag> frags;
	if (!KCMLayoutRuns(&gc, fontInfo, KCMMakeRuns(label, pre, mid, post), availWidth, maxLines, frags))
	{
		// ★frags now holds as much of the whole message as the box could take. It is kept as the
		//   last resort below, so nothing here has to succeed for something to be drawn.
		const int32 preLen = pre.CharCount();
		const int32 postLen = post.CharCount();
		const int32 maxKeep = (preLen > postLen) ? preLen : postLen;

		std::vector<KCMFrag> best;
		bool16 found = kFalse;

		// The largest amount of context that still fits, the same amount on each side.
		if (maxKeep > 0)
		{
			int32 lo = 0, hi = maxKeep;
			while (lo <= hi)
			{
				const int32 keep = lo + (hi - lo) / 2;
				std::vector<KCMFrag> trial;
				const bool16 fits = KCMLayoutRuns(&gc, fontInfo,
					KCMMakeRuns(label, KCMTrimLeadingContext(pre, keep), mid,
								  KCMTrimTrailingContext(post, keep)),
					availWidth, maxLines, trial);
				if (fits)
				{
					best.swap(trial);
					found = kTrue;
					lo = keep + 1;
				}
				else
					hi = keep - 1;
			}
		}

		// Still no room: the change alone overflows the box. Cut its tail and say so.
		// ★This is also the branch an ordinary long message takes - it is all "change" and has no
		//   context to give away.
		if (!found)
		{
			const PMString kNothing;
			int32 lo = 0, hi = mid.CharCount();
			while (lo <= hi)
			{
				const int32 keep = lo + (hi - lo) / 2;
				PMString midCut;
				if (keep >= mid.CharCount())
					midCut = mid;
				else
				{
					midCut = KCMHead(mid, KCMSafeCut(mid, keep));
					midCut.Append(KCMEllipsis());
					midCut.SetTranslatable(kFalse);
				}
				std::vector<KCMFrag> trial;
				const bool16 fits = KCMLayoutRuns(&gc, fontInfo,
					KCMMakeRuns(label, kNothing, midCut, kNothing), availWidth, maxLines, trial);
				if (fits)
				{
					best.swap(trial);
					found = kTrue;
					lo = keep + 1;
				}
				else
					hi = keep - 1;
			}
		}

		if (found)
			frags.swap(best);
	}

	// ---- draw --------------------------------------------------------------------------------

	// ★Each fragment is drawn at an explicit BASELINE rather than centred in a line-high box: the
	//   line advance (18) is a pixel less than the ink the renderer reports (19), so centring would
	//   let the last line's descenders drift below the box. Putting the first baseline one ascent
	//   below the top and stepping by the advance is what the advance is for.
	const PMReal baseline0 = frame.Top() + ascent;

	// ★★WHICH LINE THE READING GOES ON, and where on it (2026-08-22). The first fragment of the
	//   changed characters is the answer to both: the reading stands over the characters it belongs
	//   to, and the wrapping has already decided where those landed.
	// ⚠THE FIRST fragment, not the widest or the last. A word broken across a wrap is still one
	//   word, and the reading belongs to its beginning - which is also where the eye looks for it.
	int32 rubyLine = -1;
	PMReal rubyBaseX(0.0), rubyBaseW(0.0);
	if (!ruby.IsEmpty())
	{
		for (size_t i = 0; i < frags.size(); ++i)
		{
			if (!frags[i].fIsChange)
				continue;
			rubyLine  = frags[i].fLine;
			rubyBaseX = frags[i].fX;
			rubyBaseW = KCMWidth(&gc, frags[i].fText, fontInfo);
			break;
		}
	}

	// ★★"TWO LINES SHOWN AS ONE" IS EXPRESSED AS A SHIFT, NOT AS A TABLE OF LINE HEIGHTS (user's
	//   words: "use two lines and make them look like one"). Every line from the reading's own line
	//   downwards moves one line further down, and the reading is drawn in the gap that opens up.
	//   Lines ABOVE it - the heading, and any context that wrapped before the change - do not move.
	//   ⇒ One integer decides the whole layout, and when there is no reading it is never consulted.
	auto lineOf = [&](int32 line) -> PMReal
	{
		const int32 shifted = (rubyLine >= 0 && line >= rubyLine) ? (line + 1) : line;
		return baseline0 + lineHeight * PMReal(shifted);
	};

	for (size_t i = 0; i < frags.size(); ++i)
	{
		const KCMFrag& f = frags[i];
		const PMPoint at(frame.Left() + f.fX, lineOf(f.fLine));
		StringUtils::PMDrawStringRGB(&gc, at, f.fText, fontInfo,
									 f.fFaded ? kContextColor : kChangeColor,
									 kKCMDontConvertAmpersand, kKCMNoUnderline);
	}

	if (rubyLine >= 0)
	{
		// ★The same centring rule the change row's cell uses (KCMPanelTextDraw.h).
		const PMReal rubyW = KCMWidth(&gc, ruby, fontInfo);
		const PMReal rubyX = KCMRubyX(frame.Left() + rubyBaseX, rubyBaseW, rubyW, frame.Left());

		// ⚠The box's right edge is the one thing the reading may not overhang. It is cut at its
		//   TAIL - a reading is identified by its head - and only when it really does not fit, so
		//   an ordinary reading over an ordinary word is never touched.
		PMString shown = ruby;
		const PMReal room = frame.Right() - rubyX;
		if (rubyW > room && room > PMReal(0.0))
			shown = StringUtils::PMEllipsizeString(&gc, room, ruby, fontInfo, kEllipsizeEnd, nil,
												   kKCMDontConvertAmpersand);

		if (room > PMReal(0.0))
		{
			// Full strength: on these messages the reading IS what changed. Only pre/post are faded.
			StringUtils::PMDrawStringRGB(&gc, PMPoint(rubyX, baseline0 + lineHeight * PMReal(rubyLine)),
										 shown, fontInfo, kChangeColor,
										 kKCMDontConvertAmpersand, kKCMNoUnderline);
		}
	}
}

// End, KCMStatusTextView.cpp.
