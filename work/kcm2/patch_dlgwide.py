import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('ui/KCMUI.fr', [
 (T*3 + 'Frame(0, 0, 300, 24)' + T + '// Frame\n' + T*3 + 'kTrue,' + T*5 + '// Visible\n' + T*3 + 'kTrue,' + T*5 + '// Enabled\n' + T*3 + 'kEVEAlignLeft | kEVERegularSpaceAfter | kEVEArrangeChildrenInRow,',
  T*3 + 'Frame(0, 0, 440, 80)' + T + '// Frame\n' + T*3 + 'kTrue,' + T*5 + '// Visible\n' + T*3 + 'kTrue,' + T*5 + '// Enabled\n' + T*3 + 'kEVEAlignLeft | kEVERegularSpaceAfter | kEVEArrangeChildrenInRow,'),

 (T + 'Frame(0, 0, 320, 100),' + T*3 + '// Frame (l,t,r,b) - EVE follows the children, not this',
  T + 'Frame(0, 0, 460, 160),' + T*3 + '// Frame (l,t,r,b) - EVE follows the children, not this'),

 (T*5 + 'kKCMPawWordLabelKey,' + T*2 + '// Text ("Note:")\n' + T*5 + 'kKCMPawWordEditWidgetID' + T + '// Associated control for shortcut focus\n\n' + T*5 + 'kEVEAlignLeft | kEVERegularSpaceAfter,',
  T*5 + 'kKCMPawWordLabelKey,' + T*2 + '// Text ("Note:")\n' + T*5 + 'kKCMPawWordEditWidgetID' + T + '// Associated control for shortcut focus\n\n' + T*5 + '// ★Top, not left-centred: beside a four-line box a vertically centred label floats.\n' + T*5 + 'kEVEAlignTop | kEVERegularSpaceAfter,'),
])
