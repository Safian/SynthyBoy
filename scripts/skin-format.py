#!/usr/bin/env python3
"""Rewrite skin/TUI.json with one JSON element per line (lossless: only whitespace changes, duplicate keys and key order are kept).

Why: with one element per line git merges edits of different screens (tabs) without conflicts. Run it after every hand edit of skin/TUI.json:
    python3 scripts/skin-format.py [path]            # default skin/TUI.json
"""
import json
import sys
from pathlib import Path


class Obj(list):
    """A JSON object as a list of (key, value) pairs: keeps duplicate keys and order."""


def dump(o, ind, out):
    pad = ' ' * ind
    if isinstance(o, Obj):
        if not o:
            out.append('{}')
            return
        out.append('{\n')
        for i, (k, v) in enumerate(o):
            out.append(pad + ' ' + json.dumps(k, ensure_ascii=False) + ': ')
            dump(v, ind + 1, out)
            out.append(',\n' if i < len(o) - 1 else '\n')
        out.append(pad + '}')
    elif isinstance(o, list):
        if not o:
            out.append('[]')
            return
        out.append('[\n')
        for i, v in enumerate(o):
            out.append(pad + ' ')
            dump(v, ind + 1, out)
            out.append(',\n' if i < len(o) - 1 else '\n')
        out.append(pad + ']')
    else:
        out.append(json.dumps(o, ensure_ascii=False))


def strip(text):
    res, in_string, escaped = [], False, False
    for ch in text:
        if in_string:
            res.append(ch)
            if escaped:
                escaped = False
            elif ch == '\\':
                escaped = True
            elif ch == '"':
                in_string = False
        elif ch == '"':
            in_string = True
            res.append(ch)
        elif ch not in ' \n\t\r':
            res.append(ch)
    return ''.join(res)


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'skin' / 'TUI.json'
    raw = path.read_text(encoding='utf-8')
    data = json.loads(raw, object_pairs_hook=lambda p: Obj(tuple(x) for x in p))
    out = []
    dump(data, 0, out)
    text = ''.join(out) + '\n'
    if strip(raw) != strip(text):
        raise SystemExit('skin-format: not lossless, nothing written')
    if text != raw:
        path.write_text(text, encoding='utf-8')
        print('skin-format: rewritten', path)


if __name__ == '__main__':
    main()
