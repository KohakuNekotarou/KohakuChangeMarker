//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  A Resources value in the DOCUMENT'S OWN UNITS beside its points (2026-09-13, the user's ask:
//  "the numbers are all in points now - next to the points, show the value in the unit that is
//  set, 1 mm say; not only in the PDF, in the panel too"). The export writes every length as a
//  bare number of points, and a reader who set the document to Q and millimetres does not think
//  in points.
//
//  ★WHICH UNIT IS A MATTER OF WHAT THE ATTRIBUTE IS, and the document keeps several (the user's
//  own observation: "a text size is in Q, a stroke weight in the document's stroke setting"):
//  IUnitOfMeasureSettings on the document's workspace holds a unit for text sizes (Q on this
//  machine), one for text measures such as leading and indents (a J-feature-set unit, 歯), one
//  each for the horizontal and the vertical rulers (mm), and one for stroke weights. The
//  attribute's NAME says which of them applies - a table below, by name and by suffix.
//  ⚠A name the table does not know is left in points, on purpose: a wrong unit beside a number
//    is worse than none.
//
//  ★★AND THE OTHER WAY A NUMBER IS NOT WHAT THE EXPORT WROTE: A PERCENTAGE (2026-09-13, the user
//  reading the Advanced Character Formats page). HorizontalScale and VerticalScale are written as
//  the dialog shows them and only want the sign; Tsume is written in em and wants multiplying.
//  ⚠It is NOT a unit and does not go through IUnitOfMeasure - see KCMValueShownAsForAttribute.
//
//  ★SHARED BY BOTH PLUG-INS AS INLINE FUNCTIONS, like KCMResourceShortValue.h and for the same
//  reason: the panel's rows, the panel's band and the PDF report must show one value one way.
//  It reads the document through SDK interfaces only - no plug-in state, nothing to link.
//
//========================================================================================
#ifndef __KCMResourceUnits_h__
#define __KCMResourceUnits_h__

#include <cstdio>
#include <cstdlib>
#include <string>

#include "IDataBase.h"
#include "IDocument.h"
#include "IMeasurementSystem.h"
#include "ISession.h"
#include "IUnitOfMeasure.h"
#include "IUnitOfMeasureSettings.h"
#include "MeasurementSystemID.h"	// kPointsBoss
#include "PMString.h"

/** Which of the document's units a Resources attribute is measured in. */
enum KCMUnitKind
{
	kKCMUnitNone = 0,	// not a length, or a name the table does not know: leave the points alone
	kKCMUnitTextSize,	// PointSize and the like - the text size unit (Q / pt)
	kKCMUnitText,		// leading, indents, offsets - the text unit (歯 on a J feature set; the X unit elsewhere)
	kKCMUnitX,			// horizontal lengths - the horizontal ruler's unit
	kKCMUnitY,			// vertical lengths - the vertical ruler's unit
	kKCMUnitLine		// stroke and rule weights - the line unit
};

inline bool KCMNameEndsWith(const std::string& s, const char* suffix)
{
	const size_t n = std::string(suffix).size();
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/** The unit an IDML attribute is measured in, from its name. The names are IDML's own (what
    KCMResourceAttrDiff hands out): PointSize, Leading, LeftIndent, SpaceBefore, StrokeWeight,
    RuleAboveOffset, LeftInset, TextColumnGutter ...
    ★Suffixes first where a family shares one: every "...Weight" is a stroke, every
      "...FontSize" a text size, every "...Indent" / "...Offset" a text measure. */
inline KCMUnitKind KCMUnitKindForAttribute(const std::string& name)
{
	// ★★Leading IS A TEXT SIZE, NOT A TEXT MEASURE (2026-09-13, the user reading the list: "the Q
	//   is coming out as H"). It sat with the indents until then, on the reasoning that a leading
	//   is a spacing - which is **reasoning, not measurement**, and it was wrong. The two J units
	//   are the same length (1Q = 1H = 0.25mm), so the NUMBER was right all along and only the
	//   letter beside it was not: the kind of mistake that survives every check except a reader
	//   who knows what the document says.
	// ★★★BaselineShift IS A TEXT SIZE TOO (2026-09-13, the user reading the Advanced Character
	//   Formats page: "BaselineShift - it is the Q one"). It sat on the line below until then, and
	//   the comment on that line had already said this would happen: it was there by the same
	//   reasoning that had put Leading there, and the reasoning was wrong twice. **Both times the
	//   number was right and only the letter beside it was not** (1Q = 1H = 0.25mm), which is why
	//   nothing but a reader who knows what the document says can catch it.
	if (name == "PointSize" || name == "Leading" || name == "BaselineShift"
		|| KCMNameEndsWith(name, "FontSize"))
		return kKCMUnitTextSize;
	if (KCMNameEndsWith(name, "Weight"))				// StrokeWeight, RuleAboveLineWeight, UnderlineWeight ...
		return kKCMUnitLine;
	// ⚠THE REST OF THIS LINE IS STILL UNMEASURED, and Leading and BaselineShift are the reason to
	//   say so out loud: the spaces and the indents are here for the same reasoning that put those
	//   two here, and nobody has read them against the document yet. **Two out of two of the names
	//   that have since been read turned out to be in the wrong place.** If one of them shows the
	//   wrong letter, it is this line and not a new fault.
	if (name == "SpaceBefore" || name == "SpaceAfter"
		|| name == "SpaceBetweenSameParagraphStyle"
		|| KCMNameEndsWith(name, "Indent") || KCMNameEndsWith(name, "Offset"))
		return kKCMUnitText;
	// ⚠★★★A SUFFIX CATCHES STRANGERS, AND ONE IS ALREADY HERE: **`ScaleAffectsLineHeight`** ends in
	//   "Height" and is a BOOLEAN ("false" in every captured export). It reaches this line and is
	//   told it is a vertical length. Nothing goes wrong today, and the reason is worth naming
	//   because it is not luck: **KCMResourceValueWithUnit refuses anything that is not a plain
	//   number**, so "false" comes back exactly as written. That guard is load-bearing, not tidy.
	//   ★Third of the same shape found on 2026-09-13 - `ShataiAdjustTsume` for a "Tsume" suffix and
	//     `ShataiDegreeAngle` for an "Angle" one are written up beside the signs below.
	//   ⇒ **Before adding a suffix here, grep the real export for what else ends that way.**
	if (name == "TopInset" || name == "BottomInset" || KCMNameEndsWith(name, "Height"))
		return kKCMUnitY;
	if (name == "LeftInset" || name == "RightInset" || KCMNameEndsWith(name, "Gutter") || KCMNameEndsWith(name, "Width"))
		return kKCMUnitX;
	return kKCMUnitNone;
}

/** The ClassID of the document's unit for `kind`, or kInvalidClass when the document has none
    for it (the text unit outside a J feature set falls back to the horizontal one). */
inline ClassID KCMDocumentUnitFor(IDataBase* db, KCMUnitKind kind)
{
	if (db == nil || kind == kKCMUnitNone)
		return kInvalidClass;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	if (doc == nil)
		return kInvalidClass;
	InterfacePtr<IUnitOfMeasureSettings> units(doc->GetDocWorkSpace(), UseDefaultIID());
	if (units == nil)
		return kInvalidClass;
	switch (kind)
	{
		case kKCMUnitTextSize:	return units->GetTextSizeUnitOfMeasure();
		case kKCMUnitText:
		{
			const ClassID text = units->GetTextUnitOfMeasure();
			return (text != kInvalidClass) ? text : units->GetXUnitOfMeasure();
		}
		case kKCMUnitX:			return units->GetXUnitOfMeasure();
		case kKCMUnitY:			return units->GetYUnitOfMeasure();
		case kKCMUnitLine:		return units->GetLineUnitOfMeasure();
		default:				return kInvalidClass;
	}
}

//========================================================================================
// Signs: the other way a number is not what the export wrote
//========================================================================================

/** How a Resources value is written down when a UNIT OF MEASURE is not what it wants.

	★One enum and one lookup, not a table per sign: "how is this attribute shown" is a single
	question and it gets a single place to be answered ([[one-question-one-place]]). */
enum KCMValueShownAs
{
	kKCMShownAsWritten = 0,	// nothing to add: a length, a name, a boolean, an enumeration
	kKCMShownPercent,		// the number already IS the percentage: HorizontalScale="100" -> 100%
	kKCMShownPercentOfOne,	// the number is a fraction of one em: Tsume="0.5" -> 50%
	kKCMShownDegrees		// an angle: Skew="15" -> 15 degrees
};

/** Which sign a Resources attribute's value is shown with, if any.

	2026-09-13, the user reading the Advanced Character Formats page: "HorizontalScale - it is a %
	display. VerticalScale - a % display. Tsume - change it to a % display." ⇒ **the first two only
	want the sign; Tsume wants converting.**

	★★WHY Tsume IS MULTIPLIED AND THE SCALES ARE NOT. The scales are written as the dialog shows
	them (measured: `HorizontalScale="100"` for an untouched style, and the dialog says 100%).
	A tsume is not: the SDK says what it is in so many words - "the tsume amount is the difference
	between the glyph width in the font and the width set by JIS X 4051 for that character, **in
	em**" (ITsumeTable.h:34-36) - so the export's 0..1 is a fraction of an em and the dialog's
	0..100% is that fraction shown per hundred.
	⚠**The measurement that is missing is a NON-ZERO tsume**: every tsume in every captured export
	  is "0", and 0 x 100 is 0, so the factor itself has never been seen to be right. Setting a
	  style's 文字ツメ to 50% and reading the row is the one check that tells 50% from 0.5%.

	⚠★★★EXACT NAMES, NEVER A SUFFIX, and there is a live trap behind that: the same export carries
	  **`ShataiAdjustTsume="true"`**, whose name ends in "Tsume" and whose value is a boolean. A
	  KCMNameEndsWith("Tsume") here - the shape the unit table above uses for its families - would
	  have caught it. (It would have survived anyway, because the parse below refuses anything that
	  is not a plain number, which is the second reason that guard is there and not an afterthought.)

	★ANGLES, 2026-09-13 (the user, same page: "Skew - a degree sign goes on it. CharacterRotation -
	  a degree sign goes on it"). The export writes plain degrees (measured: `Skew="0"`,
	  `CharacterRotation="0"`), so these want the sign and nothing else.
	  ⚠**`ShataiDegreeAngle="4500"` is in the same export and is NOT here**: its number is not
	    degrees as written (45 degrees reads 4500), and nobody has asked for it. It is named here
	    only so that the next person to reach for a "...Angle" suffix sees why there is none.
*/
inline KCMValueShownAs KCMValueShownAsForAttribute(const std::string& name)
{
	// ★The tints, 2026-09-13 (the user, on the Character Colour page). IDML writes a tint as the
	//   percentage the dialog shows - and as **-1 when there is none**, which the guard in
	//   KCMResourceValueWithUnit is there for (measured: every tint in every captured export is -1).
	if (name == "HorizontalScale" || name == "VerticalScale"
		|| name == "FillTint" || name == "StrokeTint")
		return kKCMShownPercent;
	if (name == "Tsume")
		return kKCMShownPercentOfOne;
	if (name == "Skew" || name == "CharacterRotation")
		return kKCMShownDegrees;
	return kKCMShownAsWritten;
}

/** `number` with `suffix`, and no trailing zeros - "12.5%", "0%", never "100.0000%".

	★The export writes bare doubles and a conversion can leave a tail of zeros on one, so the
	digits are cut rather than shown: a reader comparing two values wants to see which moved, and
	four zeros on both sides of a column is noise in the way of that. Four decimals is where it
	rounds, which is finer than anything the dialogs can set. */
inline PMString KCMFormatNumberWithSuffix(double number, const char* suffix)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.4f", number);

	std::string s(buf);
	const size_t dot = s.find('.');
	if (dot != std::string::npos)						// always true for %f, but say so anyway
	{
		const size_t last = s.find_last_not_of('0');
		// "100.0000" -> last lands ON the point, so the point goes too. ⚠`erase(dot)` and not
		// `erase(dot - 1 + 1)`: written the second way, a value whose point is at index 0 would
		// keep the point and read ".%". `%.4f` never writes such a value - it always emits a digit
		// first - so this is a branch that cannot be reached, which is exactly why it was wrong
		// and nothing said so.
		s.erase((last == dot) ? dot : last + 1);
	}
	s += suffix;

	PMString out;
	out.SetUTF8String(s);
	out.SetTranslatable(kFalse);
	return out;
}

/** The degree sign, AS BYTES AND NOT AS A LITERAL.

	⚠★★★**This file has no UTF-8 BOM** (checked 2026-09-13), so a `°` typed into it would be read
	by the compiler in the system code page and reach the reader as mojibake - the failure this
	repository has already met and written down ([[cpp-japanese-needs-bom]]). U+00B0 is in CP932,
	which is the dangerous half of that note: it would COMPILE and only go wrong on screen.
	★Writing the two UTF-8 bytes keeps the source pure ASCII, and the string is handed over with
	  SetUTF8String, which names the encoding at the other end too. Nothing has to agree by luck.
	⇒ **Adding a BOM to this file would work as well; this way nothing depends on the BOM staying.** */
const char* const kKCMDegreeSign = "\xC2\xB0";

/** A Resources attribute's value IN THE DOCUMENT'S OWN UNIT - "24 Q" - when the attribute is a
    length the table knows, the value is a plain number, and the document's unit for it is not
    points. Otherwise `value` unchanged.

    ★★★THE POINTS ARE NOT SHOWN BESIDE IT (2026-09-13, the user's call: "just the 7 Q"). It read
    "17.00787401574803 (24 Q)" for a day - the export's own number with the readable one in
    brackets - and the export's number is the one nobody asked for: it is a conversion artefact
    fifteen digits long, and the reader set the document to Q precisely so as not to see it.
    ⚠**A value the table does not know is still shown exactly as the export wrote it**, points and
      all, because a length in unknown units is better read raw than labelled wrongly. So a column
      of these is not uniformly in the document's unit, and that is deliberate. */
inline PMString KCMResourceValueWithUnit(const PMString& attrName, const PMString& value, IDataBase* db)
{
	const std::string name = attrName.GetUTF8String();

	// A plain number, wholly consumed - "8.5039" yes, "Auto", "true" and "0 100 100 0" no.
	// ★Parsed ONCE, before either rule, because both need exactly this test and a value that is
	//   not a number is left alone by both.
	const std::string text = value.GetUTF8String();
	if (text.empty())
		return value;
	char* end = nil;
	const double number = std::strtod(text.c_str(), &end);
	const bool isPlainNumber = (end != nil && *end == '\0');

	// ★★THE SIGNS FIRST, AND THEY NEED NO DOCUMENT. A percentage and an angle are not lengths:
	//   nothing is converted through a unit of measure, there is no ruler setting to ask about,
	//   and the answer is the same whichever document the row came from - which is why this runs
	//   before the `db == nil` guard below rather than after it.
	const KCMValueShownAs shownAs = KCMValueShownAsForAttribute(name);
	if (shownAs != kKCMShownAsWritten)
	{
		if (!isPlainNumber)
			return value;

		// ⚠★★★A NEGATIVE PERCENTAGE IS NOT A PERCENTAGE - IT IS "THERE ISN'T ONE" (2026-09-13).
		//   IDML writes `FillTint="-1"` for a run that takes its tint from the swatch, and every
		//   tint in every captured export is exactly that. Dressed as "-1%" it reads as a real
		//   setting that somebody typed, which is worse than the raw number: the reader would go
		//   looking in the dialog for a field that is empty. **Shown as written, it is at least
		//   obviously not a percentage.** ★None of the others can go negative (a scale and a tsume
		//   are both ranges from zero), so one rule covers all three kinds and costs nothing.
		//   ⚠An angle CAN be negative, which is why the guard is inside the percentage arms below
		//     rather than here.
		switch (shownAs)
		{
			case kKCMShownPercent:		return (number < 0) ? value
													: KCMFormatNumberWithSuffix(number, "%");
			case kKCMShownPercentOfOne:	return (number < 0) ? value
													: KCMFormatNumberWithSuffix(number * 100.0, "%");
			case kKCMShownDegrees:		return KCMFormatNumberWithSuffix(number, kKCMDegreeSign);
			default:					return value;
		}
	}

	const KCMUnitKind kind = KCMUnitKindForAttribute(name);
	if (kind == kKCMUnitNone || db == nil || !isPlainNumber)
		return value;
	const double points = number;

	const ClassID unitClass = KCMDocumentUnitFor(db, kind);
	if (unitClass == kInvalidClass || unitClass == kPointsBoss)
		return value;
	InterfacePtr<IMeasurementSystem> ms(GetExecutionContextSession(), UseDefaultIID());
	if (ms == nil)
		return value;
	const int32 index = ms->Location(unitClass);
	if (index < 0)
		return value;
	InterfacePtr<IUnitOfMeasure> unit(ms->QueryUnitOfMeasure(index));
	if (unit == nil)
		return value;

	// Format writes the number WITH the unit's own abbreviation - "12 Q", "3 mm" (measured
	// 2026-09-13; appending GetName as well read "12 Q 級").
	PMString formatted;
	unit->Format(unit->PointsToUnits(PMReal(points)), formatted);
	formatted.SetTranslatable(kFalse);
	return formatted;
}

#endif // __KCMResourceUnits_h__

// End, KCMResourceUnits.h.
