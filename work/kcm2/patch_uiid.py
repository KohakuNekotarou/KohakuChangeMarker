import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('ui/KCMUIID.h', [
 ('DECLARE_PMID(kClassIDSpace, kKCMBookDialogBoss, kKCMUIPrefix + 21)',
  'DECLARE_PMID(kClassIDSpace, kKCMBookDialogBoss, kKCMUIPrefix + 21)\n'
  'DECLARE_PMID(kClassIDSpace, kKCMPawWordDialogBoss, kKCMUIPrefix + 36)' + T +
  '// kDialogBoss + our controller: the one-line box that asks for the word an Alt press puts beside a cat paw (2026-09-07, KCMPawWordDialog.cpp). ★Modal, and opened a millisecond AFTER the tracker lets go -- a modal raised inside a tracker runs its loop under a captured mouse (KIDMCPUIPencilDialog.h says why, and KCMToolButtonEH.cpp waits on the same timer for the same reason)'),

 ('DECLARE_PMID(kImplementationIDSpace, kKCMBookDialogControllerImpl, kKCMUIPrefix + 29)',
  'DECLARE_PMID(kImplementationIDSpace, kKCMPawWordDialogControllerImpl, kKCMUIPrefix + 50)' + T +
  '// IDialogController (CDialogController subclass) for the paw word box (KCMPawWordDialog.cpp). Its ApplyDialogFields is what OK means; Cancel never reaches it, and that is how the two are told apart\n'
  'DECLARE_PMID(kImplementationIDSpace, kKCMBookDialogControllerImpl, kKCMUIPrefix + 29)'),

 ('DECLARE_PMID(kWidgetIDSpace, kKCMBookDialogWidgetID, kKCMUIPrefix + 57)',
  'DECLARE_PMID(kWidgetIDSpace, kKCMBookDialogWidgetID, kKCMUIPrefix + 57)\n'
  'DECLARE_PMID(kWidgetIDSpace, kKCMPawWordDialogWidgetID, kKCMUIPrefix + 67)' + T + '// the paw word dialog itself\n'
  'DECLARE_PMID(kWidgetIDSpace, kKCMPawWordEditWidgetID, kKCMUIPrefix + 68)' + T + '// its one edit box. ⚠It IS referred to (the controller reads it and the label points at it for keyboard focus), which is why it has an ID at all -- a widget nobody names does not need one'),

 ('#define kKCMBookDialogTitleKey' + T + 'kKCMStringPrefix "kKCMBookDialogTitleKey"' + T + '// the title of the book comparison dialog',
  '#define kKCMBookDialogTitleKey' + T + 'kKCMStringPrefix "kKCMBookDialogTitleKey"' + T + '// the title of the book comparison dialog\n'
  '#define kKCMPawWordDialogTitleKey' + T + 'kKCMStringPrefix "kKCMPawWordDialogTitleKey"' + T + '// the title of the paw word box\n'
  '#define kKCMPawWordLabelKey' + T + 'kKCMStringPrefix "kKCMPawWordLabelKey"' + T + '// the prompt beside its edit box'),

 ('#define kKCMStoryRubyRowRsrcID' + T + '1015',
  '#define kKCMStoryRubyRowRsrcID' + T + '1015\n'
  '#define kKCMPawWordDialogRsrcID' + T + '1016' + T + '// the paw word dialog view resource (2026-09-07). ⚠Next free: 1017'),
])
