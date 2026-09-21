#!/usr/bin/env python3
"""Exact substring replacement, checked for count, keeping the file's BOM and line endings.

Usage: py -X utf8 text_edit.py <edits.json>

edits.json: [{"file": "...", "find": "...", "replace": "...", "count": 1}, ...]
  Every `find` must appear exactly `count` times (default 1), or the file is left alone.
"""
import json
import sys


def read(path):
    with open(path, 'rb') as f:
        raw = f.read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    if bom:
        raw = raw[3:]
    text = raw.decode('utf-8')
    crlf = text.count('\r\n') > text.count('\n') // 2
    # Matching is done on \n alone, so a find string may be written with plain newlines.
    return text.replace('\r\n', '\n'), bom, crlf


def write(path, text, bom, crlf):
    if crlf:
        text = text.replace('\n', '\r\n')
    raw = text.encode('utf-8')
    if bom:
        raw = b'\xef\xbb\xbf' + raw
    with open(path, 'wb') as f:
        f.write(raw)


def main():
    with open(sys.argv[1], 'r', encoding='utf-8') as f:
        edits = json.load(f)

    byfile = {}
    for e in edits:
        byfile.setdefault(e['file'], []).append(e)

    for path, group in byfile.items():
        text, bom, crlf = read(path)
        for e in group:
            want = int(e.get('count', 1))
            got = text.count(e['find'])
            if got != want:
                print('FAIL %s: %r found %d times, wanted %d' % (path, e['find'][:60], got, want))
                return 1
        for e in group:
            text = text.replace(e['find'], e['replace'])
        write(path, text, bom, crlf)
        print('ok %s (%d edits)' % (path, len(group)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
