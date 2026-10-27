//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) - an object placed in text, as the Story mode compares it (2026-10-27)
//
//  WHAT THIS IS FOR. The Story mode compares a story's words and the marks over them, and an object placed in the text
//  stands in the words as one anchor character (U+FFFC) - the same character whatever the object holds. So a math
//  object's MathML, an inline picture's fill, a QR code made anew, or a group's colours could change and the Story mode
//  listed nothing (measured 2026-10-27, docs/ai-notes/kcm-mathml-story-live-2026-10-27.md; only the Pixel mode saw it).
//  The author: "can you show it? into 2.0 if you can" (2026-10-27 evening). The design is
//  docs/superpowers/specs/2026-10-27-kcm-story-object-changes-design.md.
//
//  THE FINGERPRINT IS KFC 1.4.0's (model/KFCObjectSearch.cpp, Fingerprint - memory page-item-version-by-saveall): the
//    object's own persistent bytes and its children's (IPMPersist::SaveAll into memory, clearDirty kFalse), folded into
//    one 64-bit FNV-1a hash, plus the anchored object's SETTINGS (IAnchoredObjectData). Left out on purpose, as KFC
//    leaves them out: a text frame's column objects (ITextFrameColumn - they hold the composition, and typing moves their
//    bytes while the frame stays as it was) and the kInlineBoss's own bytes (they hold where the anchor was composed).
//    Unlike KFC, the words of a text frame placed in text are NOT folded in: that story is compared as a story row of
//    its own.
//  WHERE IT STOPS. SaveAll writes a child as its UID (IPMStream::XferObject), so two prints agree only between two
//    versions of ONE document - a Task Start, a copy saved from it - where the UIDs are the same. A story paired by the
//    place of its first frame (its objects made anew with new UIDs) is therefore not given prints at all (the caller's
//    rule - KCMStoryDiffRun.cpp).
//
//========================================================================================
#ifndef __KCMObjectPrint_h__
#define __KCMObjectPrint_h__

#include "BaseType.h"

#include <string>

class IDataBase;
class ITextModel;

/** The value an object placed in text carries in the Story mode's comparison: "<what it is>\t<16 hex digits>" - what
	it is for a reader ("SVG / MathML", "QR: <its content>", "Group", "Graphic", "Text frame", "Frame", "Object") and
	its fingerprint. Two values that differ anywhere are a change; the reader is shown the part before the tab.
	@param inlineUID the owned item standing in the text (kInlineBoss - ITextUtils::CollectOwnedItems' fUID).
	@return kFalse when the object cannot be read (no child, or its bytes could not be written): no value, no row. */
bool16 KCMObjectPrintOf(IDataBase* db, UID inlineUID, std::string& outValue);

/** The part of a value a reader is shown - before the tab. */
std::string KCMObjectPrintDescription(const std::string& value);

/** Every object placed in `model`'s story, its prints folded into one number - 0 when the story holds none. What lets
	the Story mode's list see a story whose WORDS did not change while an object in it did (KCMStoryStamp): the story's
	own change counters do not move for an edit to the object itself. */
uint64 KCMObjectsPrintOfStory(ITextModel* model);

#endif // __KCMObjectPrint_h__

// End, KCMObjectPrint.h.
