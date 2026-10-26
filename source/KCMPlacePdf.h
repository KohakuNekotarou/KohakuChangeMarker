//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuChangeMarker (KCM) - placing one page of a PDF, and giving the reader's PDF place settings back.
//
//  ★THE BEFORE/AFTER REPORT'S (KCMReport.cpp, where these lived until 2026-10-14): it places its two side PDFs page by
//   page, picking the page through the session's place preferences - the only door the place command reads - and must
//   hand the reader's settings back however it ends. (A file of its own since the PDF comparison placed pages too; that
//   second caller went with the comparison on 2026-10-27.)
//
//  ⚠THE PAGE NUMBER IS 1-BASED HERE (IPDFPlacePrefs::SetPage); the same interface's SetPageRange list is 0-based
//   (docs/ai-notes/hdr-reread-b04-2026-10-02.md 2-3).
//
//========================================================================================
#ifndef __KCMPlacePdf_h__
#define __KCMPlacePdf_h__

#include "BaseType.h"
#include "ICommand.h"
#include "InterfacePtr.h"
#include "PMString.h"

class IPDFPlacePrefs;

/** The session's PDF place preferences (the workspace's IPDFPlacePrefs), AddRef'ed - hold it in an InterfacePtr.
	nil during the shutdown sequence. */
IPDFPlacePrefs*	KCMQuerySessionPdfPlacePrefs();

/** The next PlaceFileInFrame takes page `page1` (1-based) of a PDF, cropped to `cropTo` (IPDFPlacePrefs::kCropTo...), on an
	opaque background, without the options dialog - through kSetPDFPlacePrefsCmdBoss, starting from `current` so nothing
	else the reader set is changed. kFalse with `why` (a lower-case phrase) when the command could not be made or failed. */
bool16	KCMSetPdfPlacePage(int32 page1, int32 cropTo, IPDFPlacePrefs* current, PMString& why);

/** Holds the PDF place preferences as they were, in a command made on construction, and processes it on destruction -
	so the reader's setting comes back whichever way the caller leaves. Make it BEFORE the first KCMSetPdfPlacePage.
	⚠With no settings to copy (nil) it holds nothing and gives nothing back: a command of defaults would CHANGE them. */
class KCMPdfPlacePrefsRestorer
{
public:
	explicit KCMPdfPlacePrefsRestorer(IPDFPlacePrefs* current);
	~KCMPdfPlacePrefsRestorer();
private:
	InterfacePtr<ICommand> fRestore;
	KCMPdfPlacePrefsRestorer(const KCMPdfPlacePrefsRestorer&);
	KCMPdfPlacePrefsRestorer& operator=(const KCMPdfPlacePrefsRestorer&);
};

#endif // __KCMPlacePdf_h__

// End, KCMPlacePdf.h.
