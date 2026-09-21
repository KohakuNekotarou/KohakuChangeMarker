import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('ui/KCMUIFactoryList.h', [
 ('REGISTER_PMINTERFACE(KCMBookDialogController, kKCMBookDialogControllerImpl)',
  'REGISTER_PMINTERFACE(KCMPawWordDialogController, kKCMPawWordDialogControllerImpl)' + T +
  '// the one-line box that asks for the word beside a cat paw (CDialogController subclass; KCMPawWordDialog.cpp)\n'
  'REGISTER_PMINTERFACE(KCMBookDialogController, kKCMBookDialogControllerImpl)'),
])
apply('ui/KCMUIStartup.cpp', [
 ('#include "KCMBookDialog.h"' + T + T + '// KCMBookDialogShutdown (the book comparison result: rows, two paths, summary)',
  '#include "KCMBookDialog.h"' + T + T + '// KCMBookDialogShutdown (the book comparison result: rows, two paths, summary)\n'
  '#include "KCMPawWordDialog.h"' + T + T + '// KCMPawWordDialog::Shutdown (the paw word box\'s one-shot timer)'),

 ('\tKCMBookDialogShutdown();',
  '\tKCMBookDialogShutdown();\n'
  '\t// ⚠**The timer, not the dialog.** ICallbackTimer holds a raw function pointer into this\n'
  '\t//   plug-in, and one left armed while the plug-in unloads is a crash (ICallbackTimer.h). The\n'
  '\t//   window between arming and firing is a millisecond, so this almost never has anything to\n'
  '\t//   do -- which is exactly why leaving it out would never be noticed until it was.\n'
  '\tKCMPawWordDialog::Shutdown();'),
])
