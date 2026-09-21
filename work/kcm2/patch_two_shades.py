import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('source/KCMDrawEventHandler.cpp', [
 (T*2 + 'uint8 cr = kKCMPawRedR, cg = kKCMPawRedG, cb = kKCMPawRedB;' + T*2 + '// the default\n'
  + T*2 + 'if (paws[i].fColour == kKCMPawColourBlue)\n'
  + T*2 + '{\n'
  + T*3 + 'cr = kKCMPawBlueR;  cg = kKCMPawBlueG;  cb = kKCMPawBlueB;\n'
  + T*2 + '}\n'
  + T*2 + 'gPort->setrgbcolor(cr / PMReal(255.0), cg / PMReal(255.0), cb / PMReal(255.0));',

  T*2 + 'uint8 ir = 0, ig = 0, ib = 0, fr = 0, fg = 0, fb = 0;\n'
  + T*2 + 'KCMPawColours(paws[i].fColour, ir, ig, ib, fr, fg, fb);\n'
  + T*2 + '// The SHAPE takes the softened shade; the word below takes the ink one.\n'
  + T*2 + 'gPort->setrgbcolor(fr / PMReal(255.0), fg / PMReal(255.0), fb / PMReal(255.0));'),

 (T*2 + 'if (!paws[i].fText.IsEmpty())\n' + T*3 + 'KCMDrawPawWord(gPort, paws[i].fText, cx, cy, s, cr, cg, cb);',
  T*2 + 'if (!paws[i].fText.IsEmpty())\n' + T*3 + 'KCMDrawPawWord(gPort, paws[i].fText, cx, cy, s, ir, ig, ib);'),

 (T + 'gPort->setrgbcolor(kKCMPawRedR / PMReal(255.0),\n'
  + T + '                   kKCMPawRedG / PMReal(255.0),\n'
  + T + '                   kKCMPawRedB / PMReal(255.0));',

  T + '// The same softened shade the page uses, from the same place.\n'
  + T + 'uint8 ir = 0, ig = 0, ib = 0, fr = 0, fg = 0, fb = 0;\n'
  + T + 'KCMPawColours(kKCMPawColourRed, ir, ig, ib, fr, fg, fb);\n'
  + T + 'gPort->setrgbcolor(fr / PMReal(255.0), fg / PMReal(255.0), fb / PMReal(255.0));'),
])
