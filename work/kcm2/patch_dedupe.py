import sys; sys.path.insert(0,'work/kcm2')
from ed import load, save
p = 'source/KCMPageCheck.cpp'
t, bom = load(p)
start = t.index('// Escape a UTF-8 string for a JSON string literal')
end   = t.index('// Read the whole file into a std::string.')
old = t[start:end]
if 'KCMJsonReadString' not in old or len(old) > 3000:
    print('FAIL: block looks wrong (%d bytes)' % len(old)); sys.exit(1)
new = ('// The two JSON string helpers that used to stand here moved to KCMJsonText.h on 2026-09-07:\n'
       '// the cat paws inside a page script label now carry the reader\'s own words too, so the same\n'
       '// escaping had to serve two files. Two copies of an escape rule are two rules the day one is\n'
       '// fixed, and the failure is silent -- a document that saves and reads back as something else.\n\n')
t = t[:start] + new + t[end:]
anchor = '#include "KCMDocUidSet.h"'
if t.count(anchor) != 1:
    print('FAIL: include anchor count=%d' % t.count(anchor)); sys.exit(1)
t = t.replace(anchor, '#include "KCMJsonText.h"\t\t// KCMJsonEscape / KCMJsonReadString -- shared with KCMPageMarksDoc.cpp\n' + anchor)
save(p, t, bom)
print('removed the duplicated helpers (%d bytes) and added the include' % len(old))
