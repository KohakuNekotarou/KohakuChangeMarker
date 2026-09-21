#!/usr/bin/env python3
"""Replace whole lines identified by a unique anchor, keeping the file's BOM and line endings.

Usage: py -X utf8 line_edit.py <edits.json>

edits.json: [{"file": "...", "anchor": "...", "replacement": "line\nline" or null}, ...]
  anchor      a substring that must appear on EXACTLY ONE line of the file
  replacement the lines that take its place; null deletes the line

Every edit is checked before anything is written: one bad anchor and the file is left alone.
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
    return text, bom, crlf


def write(path, text, bom, crlf):
    if crlf:
        text = text.replace('\r\n', '\n').replace('\n', '\r\n')
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
        lines = text.replace('\r\n', '\n').split('\n')

        plan = []
        for e in group:
            hits = [i for i, ln in enumerate(lines) if e['anchor'] in ln]
            if len(hits) != 1:
                print('FAIL %s: anchor %r matched %d lines' % (path, e['anchor'], len(hits)))
                return 1
            plan.append((hits[0], e.get('replacement')))

        for index, replacement in sorted(plan, reverse=True):
            if replacement is None:
                del lines[index]
            else:
                lines[index:index + 1] = replacement.split('\n')

        write(path, '\n'.join(lines), bom, crlf)
        print('ok %s (%d edits)' % (path, len(group)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
