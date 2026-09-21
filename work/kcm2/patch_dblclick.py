import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('ui/KCMPawTracker.cpp', [
 (T + 'const bool16 leftPress =\n'
  + T*2 + '(evType == IEvent::kLButtonDn) ||\n'
  + T*2 + '(evType == IEvent::kDoubleClick && theEvent->LButtonDn());\n'
  + T + 'if (!leftPress)\n'
  + T*2 + 'return kFalse;',

  T + 'const bool16 leftPress =\n'
  + T*2 + '(evType == IEvent::kLButtonDn) ||\n'
  + T*2 + '(evType == IEvent::kDoubleClick && theEvent->LButtonDn());\n'
  + T + 'if (!leftPress)\n'
  + T*2 + 'return kFalse;\n'
  + T + '// ★The SECOND press of a pair, which Shift turns into "clear this page" below.\n'
  + T + 'const bool16 isDouble = (evType == IEvent::kDoubleClick) ? kTrue : kFalse;'),

 (T*4 + 'changed = flags->PawStampLiftAt(db, pageUID, x, y, half);\n'
  + T*4 + 'msg = changed ? "Paw lifted (" : "Paw: none under that point (";',

  T*4 + 'if (isDouble)\n'
  + T*4 + '{\n'
  + T*5 + '// ★★**SHIFT + DOUBLE CLICK CLEARS THE PAGE** (2026-09-07, the user asked for it).\n'
  + T*5 + '//   ⚠**The single press has ALREADY happened** and lifted the paw under the point:\n'
  + T*5 + '//     Windows delivers kLButtonDn first and kDoubleClick second, and nothing here\n'
  + T*5 + '//     can know a second click is coming. So the reader gets what they asked for --\n'
  + T*5 + '//     an empty page -- in TWO undo steps rather than one, and Ctrl+Z twice puts it\n'
  + T*5 + '//     all back. That is the honest cost of the gesture, not a defect to hunt.\n'
  + T*5 + 'const int32 gone = flags->PawStampClearPage(db, pageUID);\n'
  + T*5 + 'changed = (gone > 0) ? kTrue : kFalse;\n'
  + T*5 + 'msg = changed ? "Cat paws cleared from this page (" : "Paw: none left on this page (";\n'
  + T*4 + '}\n'
  + T*4 + 'else\n'
  + T*4 + '{\n'
  + T*5 + 'changed = flags->PawStampLiftAt(db, pageUID, x, y, half);\n'
  + T*5 + 'msg = changed ? "Paw lifted (" : "Paw: none under that point (";\n'
  + T*4 + '}'),
])
