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
//  ★IT LIVES IN source/ AND IS COMPILED INTO BOTH PLUG-INS (moved from ui/ on 2026-09-13, when
//  the PDF report - model side - needed KCMDecodePercentEscapes for the Resources section). The
//  model and the UI are separate plug-ins, so a UI file cannot call a model function - the
//  boundary is the Facade; but a file that uses no SDK type and no plug-in state can be built
//  into each of them from ONE source, which is not a second copy of the rules. Both vcxprojs
//  (real and mirror) list this .cpp; the offline harness work/kcm-attrdiff-test builds it too.
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

/** Turns `%3a` back into `:` - the exporter's percent-escapes, for a reader.

	★★★MEASURED 2026-09-09, and it is NOT a mojibake although it reads as one. A style inside a
	group has the group in its name, and the `Self` attribute the key is built from writes the
	separator escaped:

	    Name="スタイルグループ 1:段落スタイル 1"        <- the XML's own Name, a plain colon
	    Self="ParagraphStyle/スタイルグループ 1%3a段落スタイル 1"   <- what the panel was showing

	The user reported "the `:` part is garbled in the panel". Nothing was corrupted: the escape was
	being shown raw. ⇒ ★**Before calling something an encoding fault, read what the exporter
	actually wrote.**

	⚠**A DISPLAY RULE ONLY.** The escaped form is the definition's identity - it is what pairs the
	two documents - so the model keeps it. This shortens nothing and changes no comparison.
	⚠**Bytes, then UTF-8.** `%E3%81%82` is one character in three escapes, so the decoding happens
	on the byte string and the result is handed back as UTF-8 - which is what the caller turns into
	a PMString with SetUTF8String.
	⚠**A `%` that is not followed by two hex digits is left exactly as it is** - a style really can
	be called "50% grey", and eating that would be the corruption this function exists to undo.
*/
std::string KCMDecodePercentEscapes(const std::string& text);

#endif // __KCMXmlPretty_h__

// End, KCMXmlPretty.h.
