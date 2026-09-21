import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)

# --- 1. the gesture table at the top of the tracker, and the one inside it
apply('ui/KCMPawTracker.cpp', [
 ('//  ★FOUR GESTURES (re-cut 2026-09-07; the first cut was 2026-09-04):\n'
  '//      plain press          place a paw in the colour the tool is HOLDING\n'
  '//      Shift + Alt + press  SWAP that colour (red <-> blue) and place in the new one\n'
  '//      Alt + press          ask for a word, then place the paw carrying it\n'
  '//      Shift + press        lift the paw under the point',

  '//  ★FIVE GESTURES (re-cut 2026-09-07; the first cut was 2026-09-04):\n'
  '//      plain press          place a paw in the colour the tool is HOLDING\n'
  '//      Alt + press          ask for a word, then place the paw carrying it\n'
  '//      Shift + Alt + press  SWAP that colour (red <-> blue). ★PLACES NOTHING\n'
  '//      Shift + press        lift the paw under the point\n'
  '//      Shift + DOUBLE       clear every paw on that page'),

 (T*3 + '//       plain press      place, in whichever colour the tool is holding\n'
  + T*3 + '//       Shift + Alt      SWAP the colour (red <-> blue) and place in the new one\n'
  + T*3 + '//       Alt              ask for a word, then place -- the word goes beside the paw\n'
  + T*3 + '//       Shift            lift',

  T*3 + '//       plain press      place, in whichever colour the tool is holding\n'
  + T*3 + '//       Alt              ask for a word, then place -- the word goes beside the paw\n'
  + T*3 + '//       Shift + Alt      SWAP the colour (red <-> blue). ★It places NOTHING\n'
  + T*3 + '//       Shift            lift the paw under the point\n'
  + T*3 + '//       Shift + DOUBLE   clear every paw on that page'),

 # --- 2. the colour message must not carry the paw count
 (T*4 + 'msg = (sPawColour == kKCMPawColourBlue) ? "Cat paw colour: blue ("\n'
  + T*4 + '                                        : "Cat paw colour: red (";\n'
  + T*4 + '// changed stays kFalse: the document was not touched, so there is nothing to undo\n'
  + T*4 + '// and nothing to redraw.\n'
  + T*3 + '}',

  T*4 + '// ⚠**This one says its piece and leaves**, because the count appended to every other\n'
  + T*4 + '//   message below belongs to PAWS. "Cat paw colour: blue (2 on this document)" reads\n'
  + T*4 + '//   as "there are two blue paws", which is not what the number counts.\n'
  + T*4 + 'msg = (sPawColour == kKCMPawColourBlue) ? "Cat paw colour: blue"\n'
  + T*4 + '                                        : "Cat paw colour: red";\n'
  + T*4 + 'msg.SetTranslatable(kFalse);\n'
  + T*4 + 'KCMSetStatus(msg);\n'
  + T*4 + '// Nothing was placed, so there is nothing to undo and nothing to redraw.\n'
  + T*4 + 'return kFalse;\n'
  + T*3 + '}'),

 # --- 3. the pointless (void) cast
 (T*3 + '(void)changed;' + T*2 + '// still read above, to word the status line',
  T*3 + '//   ⚠`changed` is now read for ONE thing only -- how the status line is worded. If it ever\n'
  + T*3 + '//     stops being read, delete it rather than leaving a flag nobody acts on.'),
])

# --- 4. the drawing comment that says the word takes the paw's colour (it does not any more)
apply('source/KCMDrawEventHandler.cpp', [
 (T*2 + '// The word the reader typed when they placed it with Alt (2026-09-07). Same colour as the\n'
  + T*2 + '// paw -- it is part of the same mark, not a second one.',
  T*2 + '// The word the reader typed when they placed it with Alt (2026-09-07).\n'
  + T*2 + '// ⚠**In the INK shade, not the paw\'s.** The two were the same colour for an hour and are\n'
  + T*2 + '//   deliberately not now: the paw is that colour softened with white, the word is the\n'
  + T*2 + '//   colour itself (kKCMPawFillWhiteMix says why). Passing fr/fg/fb here would look like a\n'
  + T*2 + '//   tidy-up and would undo the decision.'),
])
