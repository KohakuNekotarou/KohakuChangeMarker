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
//  ★**WHAT A PAGE LEAVES OUT IS COUNTED AND NAMED** in the sentence: a frame whose part holds a table, a footnote
//    reference or an endnote mark (a box is text only in this version - Export Story Text carries those), a hidden one
//    (layer or object), an empty one (a thread the text has not reached), one whose part does not survive the format's
//    own round trip. Master pages' items are not the page's (ISpread::GetItemsOnPage does not return them).
//  ★**EACH BOX CHECKS ITSELF BEFORE ITS FILE IS WRITTEN** - the story export's rule: the part goes back into its story
//    exactly as it was cut, and written as a page it reads back as written.
//
//========================================================================================
#ifndef __KCMPageExport_h__
#define __KCMPageExport_h__

#include "BaseType.h"
#include "PMString.h"

class IDataBase;
class IDFile;
class UIDList;

/** Write each of `pages` as one .docx into a new folder "<document> pages YYYY-MM-DD HHMMSS" under `parent`: the file
	"<document> p<page name>.docx", the page's size, a text box per text frame (top to bottom, then left to right).
	@param db         the document. Nothing in it is changed.
	@param pages      pages of `db`. A master page, or a UID that is no page of the document, is counted and passed over.
	@param outMessage what was written and where, and what was left out and why - for the panel's status line.
	@return kFalse when nothing could be written (no folder is made then). */
bool16 KCMExportPagesAsWord(IDataBase* db, const UIDList& pages, const IDFile& parent, PMString& outMessage);

#endif // __KCMPageExport_h__

// End, KCMPageExport.h.
