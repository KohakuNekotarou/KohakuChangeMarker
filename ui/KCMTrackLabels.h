//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Change Marker (KCM) UI - the Track mode's words, signs, times and colours (2026-10-05).
//  ★ONE PLACE: the row's sign, the message area's heading and the author row all ask here.
//
//========================================================================================

#ifndef __KCMTrackLabels_h__
#define __KCMTrackLabels_h__

#include "PMString.h"
#include "IInterfaceColors.h"	// RealAGMColor

/** The Delta column's sign: "≠" replace, "+" insert, "-" delete, "»" move. */
PMString		KCMTrackKindSign(int32 kind);

/** The message area's word: Replaced / Inserted / Deleted / Moved. */
PMString		KCMTrackKindWord(int32 kind);

/** A record's time as the OS's short date and the local time with seconds (KFC's RunLabel, IDTime). */
PMString		KCMTrackTimeLabel(uint64 t);

/** The author's colour, or the panel's Mark colour when InDesign draws them on white. */
RealAGMColor	KCMTrackColour(bool16 hasColour, uint8 r, uint8 g, uint8 b);

/** An author's name for the list: "(no name)" when the record holds none. */
PMString		KCMTrackAuthorName(const PMString& name);

#endif // __KCMTrackLabels_h__

// End, KCMTrackLabels.h.
