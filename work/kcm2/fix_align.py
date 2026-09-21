import sys; sys.path.insert(0,'work/kcm2')
from ed import apply
T = chr(9)
apply('ui/KCMUI.fr', [
 (T*5 + '// ★Top, not left-centred: beside a four-line box a vertically centred label floats.\n' + T*5 + 'kEVEAlignTop | kEVERegularSpaceAfter,',
  T*5 + '// ⚠kEVEAlignTop DOES NOT EXIST. EveInfo.fh:33-36 has Left / Right / Center / Fill and\n' + T*5 + '//   nothing else, so a tall box beside a one-line label leaves the label centred on it.\n' + T*5 + '//   Cosmetic, and the alternative would be a hand-placed frame.\n' + T*5 + 'kEVEAlignLeft | kEVERegularSpaceAfter,'),
])
