//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - the Resources mode's QR codes (2026-10-27)
//
//  WHAT THIS IS FOR. A QR code's content - the link, the words, the business card - is drawn as a pattern nobody can
//  read off the page, and a script cannot read it either (the DOM offers only the five create...QRCode methods -
//  measured the same evening by the session working on KFC). The document's INX writes it out in full, one element per
//  code under the EPS that draws it (docs/ai-notes/kcm-qr-resources-measure-2026-10-27.md):
//      <Rectangle Self="ufd" ...> ... <EPS Self="ufe" ...> <HyperlinkQRCode Self="u106" UrlLink="..." /> </EPS>
//  so the Resources mode, which reads that INX, takes it from there. The author: "I want QR code changes found too -
//  into 2.0", "make the Resources mode find them" (2026-10-27 evening).
//
//  PAIRED BY ITS FRAME, NOT BY ITSELF. Making a QR code anew - the only way to change one - gives the EPS and the QR
//    element new UIDs, while the frame keeps its own (measured: EPS 254 -> 270, frame 253 kept). The frame's name or
//    script label pairs the code when the frame has one; otherwise the frame's UID, which means something only between
//    two versions of one document (a Task Start, a copy saved from it) - the premise the Story mode rests on as well.
//    The key itself is KCMResourceKeyOf's (route [D]); this file gives the reader's spelling of it.
//  NOT A QR CODE ANCHORED IN TEXT: its INX stands inside <Story>, which this mode does not read. The Story mode's
//    inline objects are its place (docs/superpowers/specs/2026-10-27-kcm-story-object-changes-design.md).
//  NOT ITS COLOUR: the child <QrCodeSwatch> is what the page shows, and what the page shows is the Pixel mode's.
//
//========================================================================================
#ifndef __KCMResourceQRCode_h__
#define __KCMResourceQRCode_h__

#include "PMString.h"

#include "KCMResourceParse.h"	// KCMResourceItem / KCMResourceList

class IDataBase;

/** kTrue for the five elements InDesign writes a QR code's content into - HyperlinkQRCode (UrlLink), PlainTextQRCode
	(PlainText), EmailQRCode (EmailAddress, Subject, Body), TextMessageQRCode (TextMessage, CellNumber) and VCardQRCode
	(thirteen fields). Measured 2026-10-27 (work/note-scripts/2026-10-27-kcm-qr-change/kinds.ps1). */
bool16 KCMIsQRCodeElement(const PMString& elementName);

/** The UID a bare Self names ("u10e" -> 0x10e), kInvalidUID for anything else - a name, or a UID with a suffix. */
UID KCMUIDOfSelf(const PMString& self);

/** For every item with a fFrameSelf (a QR code), where its frame stands, as the Pages panel names the page - "p.3" -
	or "pasteboard", into fWhere. Nothing else is touched.
	@param db the document the items were read from. A Task Start in memory reads its items from the LIVE document at
		that moment (KCMTaskStartClone.cpp), so its pages are that moment's - which is what its rows mean. */
void KCMResolveQRCodePlaces(IDataBase* db, KCMResourceList& items);

/** What the list shows as a QR code row's key: "p.1, frame 253" - or "p.1, Label=qr-a" / "p.1, <the frame's name>"
	when the frame has one; "pasteboard, ..." off the pages. For a reader only: the pairing is KCMResourceKeyOf's. */
PMString KCMQRCodeDisplayKey(const KCMResourceItem& item);

#endif // __KCMResourceQRCode_h__

// End, KCMResourceQRCode.h.
