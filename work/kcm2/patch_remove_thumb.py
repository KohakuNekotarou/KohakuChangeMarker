import sys; sys.path.insert(0,'work/kcm2')
from ed import load, save
T = chr(9)

# ---------- 1. the drawing: the whole thumbnail-paw function and its call ----------
p = 'source/KCMDrawEventHandler.cpp'
t, bom = load(p)

def cut(start_mark, end_mark, label, replacement=''):
    global t
    s = t.find(start_mark)
    if s < 0:
        print('FAIL %s: start not found' % label); sys.exit(1)
    e = t.find(end_mark, s)
    if e < 0:
        print('FAIL %s: end not found' % label); sys.exit(1)
    t = t[:s] + replacement + t[e + len(end_mark):]
    print('  cut:', label)

cut('//========================================================================================\n// ONE paw at the centre of a Pages panel thumbnail',
    'static void KCMDrawPawThumb(IGraphicsPort* gPort, IDataBase* db, UID pageUID)\n{',
    'function head')
cut('\tif (gPort == nil || db == nil)\n\t\treturn;\n\n\tPMRect pr;\n\tif (!KCMQueryPageRect(db, pageUID, pr))',
    '\tgPort->fill();\n}\n\n',
    'function body',
    '//========================================================================================\n'
    '// (KCMDrawPawThumb stood here on 2026-09-07 and was REMOVED the same day, at the user\'s word:\n'
    '//  "let us stop drawing the cat paw illustration on the Pages panel".)\n'
    '//  It drew ONE red paw at the centre of a page\'s thumbnail, under the tick. What it cost was\n'
    '//  four passes over its size in one afternoon -- 0.72 of the page\'s short side, then the paw\'s\n'
    '//  own size, then five times that, then ten -- because a thumbnail is a tenth of the page\'s\n'
    '//  width and nothing sized for the page survives there. ★The lesson worth keeping is the one\n'
    '//  the tick already knew: a thumbnail mark is a DIFFERENT PICTURE of the same idea, not the\n'
    '//  same picture at another zoom. ⚠If a paw is ever wanted there again, that is the starting\n'
    '//  point -- not the paw outline scaled down.\n'
    '//========================================================================================\n\n')

# the call site
cut('\t// ★★**THE PAW GOES FIRST AND THE TICK OVER IT**',
    '\t\t\t\tKCMDrawPawThumb(gPort, db, puid);\n\t\t}\n\t}\n\n',
    'call site')

# the gate line added only so the thumbnail could be reached
cut('\t\t\t// ★★The cat paws, for exactly the reason the ticks are here',
    '\t\t\tKCMPawStampHasAny(::GetDataBase(ded->changedBy)) ||\n',
    'anyMarkableContent gate',
    '\t\t\t// (The cat paws were added to this gate on 2026-09-07, so a page carrying ONLY paws\n'
    '\t\t\t//  would reach the thumbnail drawing, and taken out again when that drawing was\n'
    '\t\t\t//  dropped. ⚠They belong here ONLY if something draws them into a thumbnail: the\n'
    '\t\t\t//  layout and print routes reach the port through wantPaws, which is tested on its own\n'
    '\t\t\t//  in the early-out below.)\n')
save(p, t, bom)

# ---------- 2. the constants ----------
p = 'source/KCMConstants.h'
t, bom = load(p)
s = t.find('// The paw drawn into a Pages panel thumbnail')
e = t.find('static const PMReal kKCMPawThumbDropRatio = 0.20;')
if s < 0 or e < 0:
    print('FAIL: thumb constants not found'); sys.exit(1)
e = t.find('\n', e) + 1
t = t[:s] + ('// (The Pages panel thumbnail carried a paw for one afternoon on 2026-09-07 --\n'
             '//  kKCMPawThumbSizeMul and kKCMPawThumbDropRatio lived here -- and the user dropped the\n'
             '//  idea. KCMDrawEventHandler.cpp keeps the note on what it cost and what it taught.)\n') + t[e:]
save(p, t, bom)
print('  cut: thumb constants')
