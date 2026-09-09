//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM)
//
//  Laying one definition's XML out so a person can read it (2026-09-09, the user: "for the XML
//  display from the menu, show it neatly formatted").
//
//  The exporter writes everything on one line - `<ParagraphStyle Self=".."><Properties><BasedOn
//  type="string">$ID/[No paragraph style]</BasedOn>...` - which is unreadable in a dialog. This
//  puts one element on each line and indents it by its depth.
//
//  ★NO SDK TYPES ON PURPOSE, the same decision KCMResourceAttrDiff.h states and for the same
//  reason: laying out text is the one part of this feature that can be built and run OUTSIDE
//  InDesign, so it is kept free of them and the offline harness (work/kcm-attrdiff-test) tests it.
//
//  ★IT LIVES IN THE UI HALF even though it uses no UI at all. The model and the UI are separate
//  plug-ins, so a UI file cannot call a model function - the boundary is the Facade. A copy in the
//  model would be a second copy of the same rules.
//
//  ⚠**UTF-8 IN, UTF-8 OUT.** Every character it inserts or tests for is ASCII (`<`, `>`, `/`,
//  spaces, newlines), and no ASCII byte can occur inside a multi-byte UTF-8 sequence - so a
//  Japanese style name passes through untouched. The caller converts with PMString::GetUTF8String
//  and PMString::SetUTF8String, which is a lossless round trip; GetPlatformString is NOT (it drops
//  whatever the system code page has no room for).
//
//========================================================================================

#ifndef __KCMXmlPretty_h__
#define __KCMXmlPretty_h__

#include <string>

/** One element per line, indented by depth.

	    <ParagraphStyle Self="ParagraphStyle/Body" Name="Body">
	      <Properties>
	        <BasedOn type="string">$ID/[No paragraph style]</BasedOn>
	      </Properties>
	    </ParagraphStyle>

	★**AN ELEMENT WHOSE CONTENT IS TEXT STAYS ON ONE LINE.** `<BasedOn ...>value</BasedOn>` is a
	property and its value; splitting it over three lines would treat the punctuation as though it
	were the structure.

	⚠**THE INPUT IS NOT PROMISED TO BE WELL-FORMED.** What the comparison keeps is an element and
	its children, and it may be cut short of its own closing tag. Nothing here may throw or drop
	text on that account: unbalanced input is laid out as far as it goes, and a tag that never
	closes is emitted whole as the last line.
	⚠**NOTHING IS REMOVED EXCEPT WHITESPACE BETWEEN TAGS.** Every other byte of the input appears in
	the output - this is a reading aid, not an editor.
*/
std::string KCMPrettyXml(const std::string& xml);

#endif // __KCMXmlPretty_h__

// End, KCMXmlPretty.h.
