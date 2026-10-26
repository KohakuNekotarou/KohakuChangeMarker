//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - "Export Pages to Word...": the chosen pages, each as a Word document of text boxes
//
//  WHAT THIS IS FOR. A catalog page holds many stories in many places, and the author wanted to edit them where they
//  stand (2026-10-25: "can a page go to Word with its text where it stands on the page?"). Each chosen page becomes one
//  .docx the size of the page, each of its text frames a text box at the frame's place holding the frame's part of its
//  story (KCMPageDocx.h - the format; KCMPageSplice.h - the part). "Import Story Text..." takes the file back and puts
//  every box's words into its story at the range it came from (KCMPageImport.h).
//  The design is docs/superpowers/specs/2026-10-25-kcm-word-layout-export-design.md.
//
//  ★**READING ONLY.** The walk composes text, and composing dirties: everything runs inside the document's
//    SaveRestoreModifiedState, the guard the story export keeps.
//  ★**WHAT A PAGE LEAVES OUT IS COUNTED AND NAMED** in the sentence: a threaded or an overset frame (a box is its whole
//    story since 2026-10-26), a frame holding an endnote's marker (the note's words are another story's), a VERTICAL
//    frame whose story holds a table (Word 2007 cannot lay one out in a vertical text box - measured 2026-10-26), a
//    hidden one (layer or object), an empty one (a thread the text has not reached), one whose box does not survive the
//    format's own round trip. ★A table and a footnote go in since 2026-10-26 (KCMPageDocx.h says how they are spelt).
//    Master pages' items are not the page's (ISpread::GetItemsOnPage does not return them).
//  ★**EACH BOX CHECKS ITSELF BEFORE ITS FILE IS WRITTEN** - the story export's rule: written as a page, it reads back as
//    written - its tables, its footnotes and where their references stand included.
//
//========================================================================================
#ifndef __KCMPageExport_h__
#define __KCMPageExport_h__

#include "BaseType.h"
#include "PMString.h"
#include "UIDRef.h"

#include <string>
#include <vector>

#include "KCMParaText.h"		// KCMParaAttrs
#include "KCMStoryShape.h"

class IDataBase;
class IDFile;
class ITextModel;
class UIDList;

/** One story as the page format reads it - for the export, and for the import that puts the boxes back (KCMPageImport.h):
	its BODY paragraphs as KCMTextRead::ReadStory gives them (neither a cell's nor a note's - index p is fShape.fBody[p]),
	and its shape as the story export reads it (KCMStoryFromDocument). ★ONE READING FOR BOTH SIDES, so a part cut at the
	export is cut the same way coming back. */
struct KCMPageStoryFacts
{
	bool16						fReadable;	// kFalse: not read, or its footnote references could not be placed
	std::vector<std::string>	fParas;
	std::vector<KCMParaAttrs>	fAttrs;
	std::vector<int32>			fStarts;
	KCMStoryShape::Story		fShape;

	KCMPageStoryFacts() : fReadable(kFalse) {}
};

/** `story` read the way the page format reads it. @return fReadable. */
bool16 KCMReadPageStoryFacts(const UIDRef& story, KCMPageStoryFacts& out);

/** Whether [start, start + span) of `model` holds what a PART of a story cannot carry (a box of 2026-10-25 - the import's
	rule for such a box): a table's anchor, a footnote's reference or an endnote's marker. ★Asked of the MODEL, not of
	the shape: at the edge of a part a text offset cannot tell a table just inside from one just outside. */
bool16 KCMPagePartHoldsTableOrNote(ITextModel* model, TextIndex start, int32 span);

/** Whether [start, start + span) of `model` holds an endnote's reference - what a WHOLE box cannot carry (2026-10-26: its
	tables and footnotes go with it, and an endnote's words are another story's). ★Told by the owned item standing there
	(kEndnoteAnchorBoss), not by the character: in the text it is the same U+0004 as a footnote's reference. */
bool16 KCMPagePartHoldsEndnote(ITextModel* model, TextIndex start, int32 span);

/** The document's xmpMM:DocumentID and OriginalDocumentID as UTF-8 (IAdobeMediaMgmtMetaData on kDocBoss - the road
	IDataBase.h names "FOR EXTERNAL USE"; the PDF comparison read the same two). Empty when they cannot be read.
	★What a page file names its document by (2026-10-26 - KCMPageDocx::Page::fDocumentId): a name can change after the export. */
void KCMDocumentIds(IDataBase* db, std::string& outId, std::string& outOriginal);

/** Whether a file's two ids are this document's: the same DocumentID, or the same OriginalDocumentID (a copy saved from the
	same original) - the rule the PDF comparison measured on 2026-10-14. Two empty ids (a file that names none) match. */
bool16 KCMDocumentIdsMatch(IDataBase* db, const std::string& id, const std::string& original);

/** Write each of `pages` as one .docx into a new folder "Pages YYYY-MM-DD HHMMSS" under `parent`: the file "p<page
	name>.docx" (no document name since 2026-10-26 - the tag names the document by its ids), the page's size, a text box per
	text frame that is its story's only one and shows all of it (top to bottom, then left to right).
	@param db         the document. Nothing in it is changed.
	@param pages      pages of `db`. A master page, or a UID that is no page of the document, is counted and passed over.
	@param outMessage what was written and where, and what was left out and why - for the panel's status line.
	@return kFalse when nothing could be written (no folder is made then). */
bool16 KCMExportPagesAsWord(IDataBase* db, const UIDList& pages, const IDFile& parent, PMString& outMessage);

#endif // __KCMPageExport_h__

// End, KCMPageExport.h.
