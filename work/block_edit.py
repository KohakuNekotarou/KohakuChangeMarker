#!/usr/bin/env python3
"""Replace an inclusive range of lines, found by a unique start and end anchor.

Usage: py -X utf8 block_edit.py <edits.json>

edits.json: [{"file": "...", "start": "...", "end": "...", "replacement": "..." or null}, ...]
  start / end  substrings, each matching EXACTLY ONE line; end must be at or after start
  after        optional count of further lines to take, beyond the end anchor
  replacement  the lines that take the range's place; null deletes it

Everything is checked before anything is written, and ranges within one file may not overlap.
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


def only(lines, needle, path):
    hits = [i for i, ln in enumerate(lines) if needle in ln]
    if len(hits) != 1:
        print('FAIL %s: anchor %r matched %d lines' % (path, needle, len(hits)))
        raise SystemExit(1)
    return hits[0]


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
            first = only(lines, e['start'], path) - int(e.get('before', 0))
            last = only(lines, e['end'], path) + int(e.get('after', 0))
            if last < first:
                print('FAIL %s: end before start (%d < %d)' % (path, last, first))
                return 1
            plan.append((first, last, e.get('replacement')))

        plan.sort(reverse=True)
        for i in range(len(plan) - 1):
            if plan[i][0] <= plan[i + 1][1]:
                print('FAIL %s: ranges overlap' % path)
                return 1

        for first, last, replacement in plan:
            lines[first:last + 1] = [] if replacement is None else replacement.split('\n')

        write(path, '\n'.join(lines), bom, crlf)
        print('ok %s (%d blocks)' % (path, len(group)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
