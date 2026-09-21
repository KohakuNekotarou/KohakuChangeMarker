import sys
targets = [
 'build/win/prj/KohakuExtendScriptChangeMarker.vcxproj',
 'source/sdksamples/KCM/buildproj/KohakuExtendScriptChangeMarker.vcxproj',
]
# No backslash literals anywhere: the anchor line is COPIED and only the file name swapped,
# so the path separators never pass through this script as text.
adds = [('ClCompile', 'KCMPageMarksDoc.cpp', ['KCMPageMarksCmd.cpp', 'KCMMarksObserver.cpp']),
        ('ClInclude', 'KCMPageMarksDoc.h',   ['KCMPageMarksCmd.h',   'KCMMarksObserver.h'])]
for p in targets:
    raw = open(p, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    t = raw.decode('utf-8-sig').replace('\r\n', '\n')
    if 'KCMPageMarksCmd.cpp' in t:
        print('already done:', p); continue
    out_lines = []
    hits = 0
    for ln in t.split('\n'):
        out_lines.append(ln)
        for tag, anchor, news in adds:
            if tag in ln and anchor in ln:
                hits += 1
                for n in news:
                    out_lines.append(ln.replace(anchor, n))
    if hits != 2:
        print('FAIL anchors in %s: hits=%d' % (p, hits)); sys.exit(1)
    out = '\n'.join(out_lines).replace('\n', '\r\n').encode('utf-8')
    if bom: out = b'\xef\xbb\xbf' + out
    open(p, 'wb').write(out)
    print('patched %s (+4 entries)' % p)
