import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
apply('source/KCMDocResponder.cpp', [
 ('#include "KCMPageMarksDoc.h"\t\t// KCMMarksRestoreFromDocument -- the marks the document carries',
  '#include "KCMMarksObserver.h"\t\t// KCMMarksEnsureObserver -- so later writes reach the store\n'
  '#include "KCMPageMarksDoc.h"\t\t// KCMMarksSyncFromDocument -- the marks the document carries'),

 ('\tint32 checks = 0, paws = 0;\n'
  '\tif (KCMMarksRestoreFromDocument(db, &checks, &paws) <= 0)\n'
  '\t\treturn;\t\t\t\t// the document carries none of ours: say nothing at all\n'
  '\n'
  '\t// The marks are drawn from the stores, so the views have to be asked to draw again.\n'
  '\tKCMInvalidateDB(db);\n',

  '\t// The observer goes on for EVERY document that opens, whether or not it carries marks today:\n'
  '\t// the reader may put the first one on a moment from now, and the write has to find a listener\n'
  '\t// already there. It is idempotent, so the command asking again later costs nothing.\n'
  '\tKCMMarksEnsureObserver(db);\n'
  '\n'
  '\tint32 checks = 0, paws = 0;\n'
  '\tif (KCMMarksSyncFromDocument(db, &checks, &paws) <= 0)\n'
  '\t\treturn;\t\t\t\t// the document carries none of ours: say nothing at all\n'
  '\n'
  '\t// Redrawing is NOT done here any more: KCMMarksSyncFromDocument invalidates the views and\n'
  '\t// notifies the changed pages itself, because it is the only one that knows which pages moved.\n'),
])
