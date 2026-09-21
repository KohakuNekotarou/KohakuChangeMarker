import sys, io, os
# install.py <src-lf-file> <dest> [--bom|--nobom]
src, dest = sys.argv[1], sys.argv[2]
bom = '--nobom' not in sys.argv[3:]
data = open(src, 'rb').read()
data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
if bom and not data.startswith(b'\xef\xbb\xbf'):
    data = b'\xef\xbb\xbf' + data
if not bom and data.startswith(b'\xef\xbb\xbf'):
    data = data[3:]
open(dest, 'wb').write(data)
print('wrote %s (%d bytes, bom=%s)' % (dest, len(data), bom))
