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
//  ★SHARED BY BOTH PLUG-INS AS INLINE FUNCTIONS, like KCMResourceShortValue.h and for the same
//  reason: the panel's rows, the panel's band and the PDF report must show one value one way.
//  It reads the document through SDK interfaces only - no plug-in state, nothing to link.
//
//========================================================================================
#ifndef __KCMResourceUnits_h__
#define __KCMResourceUnits_h__

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
	if (name == "PointSize" || KCMNameEndsWith(name, "FontSize"))
		return kKCMUnitTextSize;
	if (KCMNameEndsWith(name, "Weight"))				// StrokeWeight, RuleAboveLineWeight, UnderlineWeight ...
		return kKCMUnitLine;
	if (name == "Leading" || name == "BaselineShift" || name == "SpaceBefore" || name == "SpaceAfter"
		|| name == "SpaceBetweenSameParagraphStyle"
		|| KCMNameEndsWith(name, "Indent") || KCMNameEndsWith(name, "Offset"))
		return kKCMUnitText;
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

/** `value` (a Resources attribute's value, in points, as the panel shows it) with the same
    measure in the document's own unit appended in brackets - "8.503937007874015 (24 Q)" -
    when the attribute is a length the table knows, the value is a plain number, and the
    document's unit for it is not points. Otherwise `value` unchanged. */
inline PMString KCMResourceValueWithUnit(const PMString& attrName, const PMString& value, IDataBase* db)
{
	const KCMUnitKind kind = KCMUnitKindForAttribute(attrName.GetUTF8String());
	if (kind == kKCMUnitNone || db == nil)
		return value;

	// A plain number, wholly consumed - "8.5039" yes, "Auto" and "0 100 100 0" no.
	const std::string text = value.GetUTF8String();
	if (text.empty())
		return value;
	char* end = nil;
	const double points = std::strtod(text.c_str(), &end);
	if (end == nil || *end != '\0')
		return value;

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
	PMString out(value);
	out.Append(" (");
	out.Append(formatted);
	out.Append(")");
	out.SetTranslatable(kFalse);
	return out;
}

#endif // __KCMResourceUnits_h__

// End, KCMResourceUnits.h.
