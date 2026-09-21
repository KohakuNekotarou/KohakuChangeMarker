import sys
targets = ['build/win/prj/KohakuExtendScriptChangeMarker.vcxproj',
           'source/sdksamples/KCM/buildproj/KohakuExtendScriptChangeMarker.vcxproj']
for p in targets:
    raw = open(p,'rb').read(); bom = raw.startswith(b'\xef\xbb\xbf')
    t = raw.decode('utf-8-sig').replace('\r\n','\n')
    if 'KCMJsonText.h' in t:
        print('already done:', p); continue
    out, hits = [], 0
    for ln in t.split('\n'):
        out.append(ln)
        if 'ClInclude' in ln and 'KCMPageMarksCmd.h' in ln:
            hits += 1
            out.append(ln.replace('KCMPageMarksCmd.h', 'KCMJsonText.h'))
    if hits != 1:
        print('FAIL anchor in %s: %d' % (p, hits)); sys.exit(1)
    data = '\n'.join(out).replace('\n','\r\n').encode('utf-8')
    if bom: data = b'\xef\xbb\xbf' + data
    open(p,'wb').write(data)
    print('patched', p)
