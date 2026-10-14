//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - the documents KCM makes from PDFs, for the PDF comparison (2026-10-14).
//
//  WHAT ONE IS: a new document with one page per page of the PDF, each the size of that PDF page's trim box (a quarter
//  turn swaps the sides), the PDF page placed on it cropped to the trim box at the page's origin. Named
//  "<file>.pdf (KCM)", shown in a window, and left unmodified so that closing it asks nothing
//  (docs/superpowers/specs/2026-10-14-kcm-pdf-compare-design.md 3.3-3.6).
//
//  ★THE ONE PLACE that answers "is this database a PDF document" and "what identity does its page answer to"
//   ([[one-question-one-place]]): the page pairing, the three rasterisations (full resolution), the refusals and the
//   report all ask here.
//
//  THE REGISTER: the PDF's file, its write time and size when the document was made, the database, each page's key, and
//   how the PDF's pages carry ids. A database is compared, never dereferenced, unless the document list says it is open.
//   An entry goes when its document closes (the close sweep), when the PDF changed and a new document replaces it, and at
//   shutdown.
//
//  MAIN THREAD ONLY: every caller is a menu, a comparison, a draw on the main thread, or the close sweep.
//
//========================================================================================
#ifndef __KCMPdfDoc_h__
#define __KCMPdfDoc_h__

#include "BaseType.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

class IDataBase;
class IDocumentList;
struct KCMPairEnd;

/** A PDF end of the pair as a document: the PDF document made from `pdf` earlier while it is still open and the PDF has
	not changed since; otherwise a new one (the old one closed when nobody changed it). KCMRealisePairEnd calls this for a
	file end whose first bytes are a PDF's.
	@param why the status line's words when this answers kFalse ("The PDF could not be read: ...", "Cancelled.").
	@return kFalse with nothing left open when the PDF could not be read or made, or the reader cancelled. */
bool16	KCMRealisePdfEnd(const IDFile& pdf, IDataBase*& outDB, PMString& why);

/** Whether the file's first bytes hold "%PDF-" (KCMPdf::LooksLikePdf) - never its name. Reads 1 KB. */
bool16	KCMFileIsPdf(const IDFile& file);

/** Whether this end of the pair is a PDF: a file that is one, or a PDF document KCM made. Opens nothing. */
bool16	KCMPairEndIsPdf(const KCMPairEnd& end);

/** Whether `db` is a document KCM made from a PDF, still in the register. nil answers kFalse. */
bool16	KCMIsPdfDoc(IDataBase* db);

/** Whether the pages of these two may pair by identity: kTrue when neither is a PDF document, or both are and each PDF's
	pages all carry the InDesign page uids of one document. kFalse = pair them in order (design D2). */
bool16	KCMPdfDocsPairByIds(IDataBase* targetDB, IDataBase* sourceDB);

/** The identity each page of a PDF document answers to: the first InDesign page uid its PDF page carries, kInvalidUID when
	none. `pages` are the document's page uids; outKeys is parallel to them. */
void	KCMPdfDocPageKeys(IDataBase* db, const std::vector<UID>& pages, std::vector<UID>& outKeys);

/** What the status line's second line starts with when a PDF pair was compared: how the pages were paired, and the pages
	that did not come out at their page size. Empty when neither is a PDF document. */
void	KCMPdfPairingNote(IDataBase* targetDB, IDataBase* sourceDB, PMString& out);

/** The two menu items' work (facade ChoosePdfEnd): `pdf` becomes the Target / the Source as a file choice. kFalse, with the
	reason and nothing changed, when the file is not a PDF. outMessage says what to do next either way. */
bool16	KCMChoosePdfEnd(bool16 asTarget, const IDFile& pdf, PMString& outMessage);

/** The close sweep's half (KCMHandleDocsClosed): forget every PDF document no longer in `docList`. Memory only. */
void	KCMPdfDocSweep(IDocumentList* docList);

/** The model's Shutdown: forget them all. Assignment only (the documents close as every document does). */
void	KCMPdfDocShutdown();

#endif // __KCMPdfDoc_h__

// End, KCMPdfDoc.h.
