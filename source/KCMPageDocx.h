//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - one page of a document as a Word document: each text frame a text box
//
//  WHAT THIS IS FOR. "Export Pages to Word..." (2026-10-25 - the author: a catalog has many stories in many places;
//  can a page go to Word with its text where it stands on the page?). Each page the reader chose becomes one .docx the
//  size of the page, and each text frame on it a text box of the frame's size at the frame's place, holding the words
//  the frame shows - its part of its story. The reader edits the boxes in Word and hands the file to "Import Story
//  Text...", which puts each box's words back into its story, at the range the box came from (KCMPageImport.h).
//  The design is docs/superpowers/specs/2026-10-25-kcm-word-layout-export-design.md.
//
//  *** A PURE FUNCTION. No SDK type, no document, no file. *** Built and tested outside InDesign with
//  work/kcm-storydocx-test/build-page.cmd (page_test.cpp), the way KCMStoryDocx is.
//
//  HOW A PAGE IS SPELT (measured in Word 2007 - docs/ai-notes/kcm-page-docx-word-2026-10-25.md):
//    the page      the section's w:pgSz, every margin 0, and ONE paragraph that anchors every box
//    a box         a VML text box (w:pict / v:shape / v:textbox / w:txbxContent) placed against the page in points,
//                  growing with its words (mso-fit-shape-to-text), the frame's insets as its own
//    its words     KCMStoryDocx::WriteBlocks - the story round trip's own spelling, so ruby, kenten, tate-chu-yoko,
//                  warichu and the invisible characters travel exactly as they do there - each paragraph in a style
//                  carrying the frame's first character's size and font
//    which box     ★A LOCKED CONTENT CONTROL AROUND THE WORDS, tagged "kcm-box-<n>". Word keeps it with the words
//                  whatever it makes of the shape: Word 2010 and later save a VML box twice, as DrawingML and as VML
//                  (mc:AlternateContent), and the reader counts it once.
//    which story   customXml/item1.xml - the page tag, format 2: per box its frame, its story, the range it came from
//                  and a fingerprint of what stood there then. ★NOT A WORD OF THE STORY (the author's rule of
//                  2026-09-19 for the story tag: a file is handed on, and words nobody can see must not go with it).
//
//========================================================================================
#ifndef __KCMPageDocx_h__
#define __KCMPageDocx_h__

#include "BaseType.h"
#include "KCMStoryShape.h"	// Story - a box's words
#include "KCMZipStore.h"	// Entry - a part of the package

#include <string>
#include <vector>

namespace KCMPageDocx
{

/** The page tag's namespace, "urn:kohaku:kcm:page:1". ⚠Not the story tag's: a page file is not a story file, and the
	story reader takes a tag it does not know for a stranger's - which is what keeps the two readers apart. */
extern const char* const kPageTagNamespace;

/** One text frame of the page, as its box. Places and sizes in points, the page's top left the origin, y down. */
struct Box
{
	int32		fN;				// 1.. - the box's number, its content control's tag ("kcm-box-<n>")
	int32		fFrame;			// the text frame's UID
	int32		fStory;			// its story's UID
	int32		fStart;			// where the frame's text began in the story when exported (a TextIndex)
	int32		fEnd;			// and where it ended - exclusive, a paragraph break at the end left out
	int32		fPrintBytes;	// the fingerprint of that range when exported (KCMPageSplice::Fingerprint):
	uint32		fPrintCrc;		//   its byte count and its CRC-32
	double		fX, fY, fW, fH;
	double		fInsetL, fInsetT, fInsetR, fInsetB;
	bool16		fVertical;
	double		fPointSize;		// the first character's size; 0 = none written
	std::string	fFontName;		// UTF-8, the name Word knows the font by; empty = none written
	KCMStoryShape::Story fContent;	// the body only - no tables, no notes

	Box() : fN(0), fFrame(0), fStory(0), fStart(0), fEnd(0), fPrintBytes(0), fPrintCrc(0),
			fX(0), fY(0), fW(0), fH(0), fInsetL(0), fInsetT(0), fInsetR(0), fInsetB(0),
			fVertical(kFalse), fPointSize(0) {}
};

/** One page: its UID and name, its size, and its boxes in the order they are written (top to bottom, then left to
	right - the caller sorts them). */
struct Page
{
	int32				fPageUid;
	std::string			fPageName;		// UTF-8, as the Pages panel spells it
	double				fWidth, fHeight;
	std::vector<Box>	fBoxes;

	Page() : fPageUid(0), fWidth(0), fHeight(0) {}
};

/** Every part of the page's package, in the order they are zipped - the story format's names, footnotes.xml never.
	★The same page is the same bytes (the story format's rule; the export's self-check and the tests rely on it).
	@return kFalse with a reason: a box's words cannot be written (KCMStoryDocx::WriteBlocks' refusals), or two boxes
	  share a number. */
bool16 WriteParts(const Page& page, std::vector<KCMZipStore::Entry>& outParts, std::string& whyNot);

/** One box as the file holds it: the tag's facts in fBox - its place and size are not read back, the import needs
	none - and in fBox.fContent the words Word holds. ★fWhyNot non-empty: THIS box cannot go in, and says why (gone
	from the file, there twice, words this reader refuses); the page's other boxes still can. */
struct ReadBox
{
	Box			fBox;
	std::string	fWhyNot;
};

struct ReadResult
{
	bool16					fIsPage;			// the package carries a page tag
	int32					fPageUid;
	std::string				fPageName;
	std::vector<ReadBox>	fBoxes;				// in the tag's order
	bool16					fTextOutsideBoxes;	// words typed outside every box - they are not read, and the caller says so

	ReadResult() : fIsPage(kFalse), fPageUid(0), fTextOutsideBoxes(kFalse) {}
};

/** The parts of one package -> the page's boxes.
	@return kTrue with fIsPage kFalse when the package carries no page tag - a story file, or a stranger's .docx; the
	  caller reads it as a story. kFalse with a reason when it IS a page and its tag or its document cannot be read. */
bool16 ReadParts(const std::vector<KCMZipStore::Entry>& parts, ReadResult& out, std::string& whyNot);

}	// namespace KCMPageDocx

#endif // __KCMPageDocx_h__

// End, KCMPageDocx.h.
