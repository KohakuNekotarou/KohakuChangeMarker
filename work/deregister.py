#!/usr/bin/env python3
"""Take a file out of both .vcxproj copies and both .filters copies.

Usage: py -X utf8 deregister.py <basename> [<basename> ...]

A .filters entry spans several lines (<ClCompile ...>\n  <Filter>..</Filter>\n</ClCompile>), so
an opening tag that does not close itself takes the lines up to its closing tag with it.
"""
import sys

ROOT = r"C:\Users\user\Desktop\plugin_sdk_21.0.0.192"
PATHS = [
    ROOT + r"\source\sdksamples\KCM\buildproj\KohakuExtendScriptChangeMarker.vcxproj",
    ROOT + r"\source\sdksamples\KCM\buildproj\KohakuExtendScriptChangeMarker.vcxproj.filters",
    ROOT + r"\build\win\prj\KohakuExtendScriptChangeMarker.vcxproj",
    ROOT + r"\build\win\prj\KohakuExtendScriptChangeMarker.vcxproj.filters",
]


def main():
    names = sys.argv[1:]
    if not names:
        print('usage: deregister.py <basename> ...')
        return 1

    for path in PATHS:
        try:
            with open(path, 'rb') as f:
                raw = f.read()
        except IOError:
            print('skip (not there) %s' % path)
            continue
        bom = raw.startswith(b'\xef\xbb\xbf')
        text = (raw[3:] if bom else raw).decode('utf-8')
        lines = text.split('\n')

        keep, dropped, inside = [], 0, False
        for line in lines:
            if inside:
                dropped += 1
                if '</Cl' in line:
                    inside = False
                continue
            if any(n in line for n in names):
                dropped += 1
                stripped = line.rstrip()
                inside = stripped.endswith('>') and not stripped.endswith('/>')
                continue
            keep.append(line)

        with open(path, 'wb') as f:
            f.write((b'\xef\xbb\xbf' if bom else b'') + '\n'.join(keep).encode('utf-8'))
        print('%-92s %d line(s)' % (path, dropped))
    return 0


if __name__ == '__main__':
    sys.exit(main())
