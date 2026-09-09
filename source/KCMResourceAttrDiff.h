//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - Resources comparison mode
//
//  WHICH ATTRIBUTES of one definition are not the same in the two documents.
//
//  The store answers "this paragraph style changed" and hands back each side's body as it came out
//  of the export. That is a wall of XML; what a reader wants is the one line that moved. This turns
//  the two bodies into a short list of "name, what it was, what it is".
//
//  ★★★NO SDK TYPES, ON PURPOSE. Everything here is std::string, and that is what lets the differ
//  be built and run OUTSIDE InDesign (work/kcm-attrdiff-test). It is the one piece of the Resources
//  mode that is pure text handling, so it is also the one piece that can be tested without the
//  application - and it was kept free of PMString to keep it that way. The boundary converts.
//
//  ⚠VALUES COME BACK VERBATIM. `PointSize="8.503937007874015"` yields exactly that string: the
//  export writes bare numbers and nothing here knows which attributes are lengths, so appending
//  "pt" would be an invention. The design says so (section 6-3 of the Resources design).
//
//========================================================================================
#ifndef __KCMResourceAttrDiff_h__
#define __KCMResourceAttrDiff_h__

#include <string>
#include <vector>

/** One attribute that is not the same on the two sides.

	★A side that does not HAVE the attribute comes back EMPTY rather than the entry being left
	out: an Added definition has no Source at all and a Removed one has no Target, and the panel
	has to be able to say which side is missing rather than show an arrow between two values when
	only one exists.
	⚠An attribute that is present on both sides and EMPTY on both is not a difference and does not
	  appear - the two are told apart by the entry existing, not by the string being empty.
*/
struct KCMAttrChange
{
	std::string	fName;
	std::string	fSource;
	std::string	fTarget;
};

/** Compare two element bodies attribute by attribute and keep only what differs.

	Both arguments are the raw text between an element's name and its closing bracket, as the store
	holds it - `Self="ParagraphStyle/Body" PointSize="8.5"`. A leading element name (`<Layer `) is
	ignored rather than reported as an attribute.

	★THE ORDER IS THE TARGET'S. What the reader is looking at is the newer document, so the newer
	side decides the reading order; attributes that exist only in the Source follow at the end,
	because there is nowhere else for them to go.

	@param source the older document's body. Empty means the definition is not there at all, and
	       every attribute of the target comes back as an addition.
	@param target the newer document's body. Empty likewise.
	@param out    CLEARED first, then filled. Empty means the two bodies say the same things.
*/
void KCMDiffAttributes(const std::string& source, const std::string& target,
					   std::vector<KCMAttrChange>& out);

#endif // __KCMResourceAttrDiff_h__

// End, KCMResourceAttrDiff.h.
