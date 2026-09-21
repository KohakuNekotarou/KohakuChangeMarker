import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('source/KCMConstants.h', [
 ('static const PMReal kKCMPawTextBoldStroke = 0.04;' + T + '// line width as a fraction of the size',
  'static const PMReal kKCMPawTextBoldStroke = 0.0;' + T + '// line width as a fraction of the size; 0 = no faux bold\n'
  '// ⚠★**0 ON PURPOSE** (the user, 2026-09-07: "try taking the weight off the text once"). It was\n'
  '//   0.04 -- Kohaku InDesign MCP\'s own faux bold -- and the note came out heavier than the page it\n'
  '//   sits on. Putting the weight back is this one number; the drawing skips the stroke pass while\n'
  '//   it is zero, so nothing else has to change either way.'),
])
apply('source/KCMDrawEventHandler.cpp', [
 ('\tgPort->selectfont(font, kKCMPawTextPt);\n\tgPort->setlinewidth(kKCMPawTextPt * kKCMPawTextBoldStroke);\n',
  '\tgPort->selectfont(font, kKCMPawTextPt);\n'
  '\t// The faux bold, when it is asked for. ⚠A stroke of width 0 is not "no stroke" to a port -- it\n'
  '\t//   is the thinnest line it can draw -- so the pass is skipped rather than drawn at zero.\n'
  '\tconst bool16 bold = (kKCMPawTextBoldStroke > PMReal(0.0)) ? kTrue : kFalse;\n'
  '\tif (bold)\n'
  '\t\tgPort->setlinewidth(kKCMPawTextPt * kKCMPawTextBoldStroke);\n'),
 ('\t\tgPort->show(x, ly, (uint32)n16, buf16, IGraphicsPort::kFillText);\n\t\tgPort->show(x, ly, (uint32)n16, buf16, IGraphicsPort::kStrokeText);',
  '\t\tgPort->show(x, ly, (uint32)n16, buf16, IGraphicsPort::kFillText);\n'
  '\t\tif (bold)\n'
  '\t\t\tgPort->show(x, ly, (uint32)n16, buf16, IGraphicsPort::kStrokeText);'),
])
