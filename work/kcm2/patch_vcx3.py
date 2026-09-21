import sys
targets = ['build/win/prj/KohakuChangeMarkerUI.vcxproj',
           'source/sdksamples/KCM/buildproj/KohakuChangeMarkerUI.vcxproj']
adds = [('ClCompile', 'KCMBookDialog.cpp', ['KCMPawWordDialog.cpp']),
        ('ClInclude', 'KCMBookDialog.h',   ['KCMPawWordDialog.h'])]
for p in targets:
    raw = open(p,'rb').read(); bom = raw.startswith(b'\xef\xbb\xbf')
    t = raw.decode('utf-8-sig').replace('\r\n','\n')
    if 'KCMPawWordDialog.cpp' in t:
        print('already done:', p); continue
    out, hits = [], 0
    for ln in t.split('\n'):
        out.append(ln)
        for tag, anchor, news in adds:
            if tag in ln and anchor in ln:
                hits += 1
                for n in news:
                    out.append(ln.replace(anchor, n))
    if hits != 2:
        print('FAIL anchors in %s: %d' % (p, hits)); sys.exit(1)
    data = '\n'.join(out).replace('\n','\r\n').encode('utf-8')
    if bom: data = b'\xef\xbb\xbf' + data
    open(p,'wb').write(data)
    print('patched', p)
