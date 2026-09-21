import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
apply('source/KCMDrawEventHandler.cpp', [
 ('\tgPort->rectclip(pr);\n\tgPort->setopacity(opacity, kFalse);\n\tgPort->setrgbcolor(kKCMPawRedR / PMReal(255.0),',
  '\tgPort->rectclip(pr);\n\tgPort->setopacity(PMReal(1.0), kFalse);\n\tgPort->setrgbcolor(kKCMPawRedR / PMReal(255.0),'),
 ('\t\t\tif (!onPage.empty())\n\t\t\t\tKCMDrawPawThumb(gPort, db, puid, SelectedMarkOpacity());',
  '\t\t\tif (!onPage.empty())\n\t\t\t\tKCMDrawPawThumb(gPort, db, puid);'),
])
