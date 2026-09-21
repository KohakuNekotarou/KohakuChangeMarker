import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
apply('ui/KCMPawTracker.cpp', [
 ('''//  ★FOUR GESTURES (2026-09-04, the user's choices while using the builds):
//      plain press          place a PINK paw
//      Alt + press          place a CYAN one
//      Shift + Alt + press  place a GREEN one
//      Shift + press        lift the paw under the point
//    ⚠★★Alt CHANGED THE SIZE for about an hour (1.6x, then 10x, then 5x) before the user replaced
//      the idea with colour: a bigger paw is the same mark drawn larger, a different colour is a
//      different KIND of mark. Every paw is the ordinary size now.
//    ⚠★The lift is the gesture that has to test BOTH keys -- Shift alone lifts, Shift with Alt
//      places -- so `if (shift)` on its own would eat the green paw.''',

  '''//  ★FOUR GESTURES (re-cut 2026-09-07; the first cut was 2026-09-04):
//      plain press          place a paw in the colour the tool is HOLDING
//      Shift + Alt + press  SWAP that colour (red <-> blue) and place in the new one
//      Alt + press          ask for a word, then place the paw carrying it
//      Shift + press        lift the paw under the point
//    ★★**THE TOOL HOLDS A COLOUR NOW** (sPawColour below), where it used to read one straight off
//      the keys. Two colours cannot be reached by three place-gestures, and the user asked for the
//      keys to SWAP rather than to select: red is the default and Shift+Alt turns it blue and back.
//      ⚠The colours are Kohaku InDesign MCP's own two -- red is what Claude marks with, blue is
//        the person's pencil -- and that pairing is the point (KCMConstants.h says why).
//    ⚠★★Alt CHANGED THE SIZE for about an hour (1.6x, then 10x, then 5x) before the user replaced
//      the idea with colour: a bigger paw is the same mark drawn larger, a different colour is a
//      different KIND of mark. Every paw is the ordinary size now.
//    ⚠★The lift is the gesture that has to test BOTH keys -- Shift alone lifts, Shift with Alt
//      places -- so `if (shift)` on its own would eat the swap.'''),

 ('''#include "KCMConstants.h"			// KCMPawColour -- what the modifier keys choose between''',
  '''#include "KCMConstants.h"			// KCMPawColour -- what the tool holds and Shift+Alt swaps
#include "KCMPawWordDialog.h"		// the Alt gesture: ask for a word, then place -- NOT from in here

// The colour the tool is holding. Session state, main thread only, and deliberately NOT per
// document: it is a property of the TOOL in the reader's hand, like a pen they have picked up, so
// carrying it from one document to the next is what a person expects.
// ★Red to begin with (the user, 2026-09-07).
static int32 sPawColour = kKCMPawColourRed;'''),
])
