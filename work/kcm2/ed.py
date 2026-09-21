import sys
# ed.py <file> ; edits are supplied by importing module `edits` -- see callers.
def load(p):
    raw = open(p,'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    return raw.decode('utf-8-sig').replace('\r\n','\n'), bom
def save(p, t, bom):
    out = t.replace('\n','\r\n').encode('utf-8')
    if bom: out = b'\xef\xbb\xbf' + out
    open(p,'wb').write(out)
def apply(p, pairs):
    t, bom = load(p)
    for old, new in pairs:
        n = t.count(old)
        if n != 1:
            print('FAIL %s: count=%d for %r' % (p, n, old[:90])); sys.exit(1)
        t = t.replace(old, new)
    save(p, t, bom)
    print('patched %s (%d edits)' % (p, len(pairs)))
