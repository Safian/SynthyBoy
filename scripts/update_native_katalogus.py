#!/usr/bin/env python3
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the screen catalogue from the one flat screenshot library."""
import hashlib
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from components.style import color, size

OUTPUT = ROOT / 'docs/ui-components/native-katalogus.html'
CATALOG = ROOT / 'scripts/screen-catalog.json'


def generate():
    screens = json.loads(CATALOG.read_text(encoding='utf-8'))['screens']
    cards = []
    def figure(name, caption):
        path = ROOT / 'docs/screens' / f'{name}.jpg'
        if not path.is_file():
            return ''
        version = hashlib.sha256(path.read_bytes()).hexdigest()[:12]
        url = f'../screens/{name}.jpg?v={version}'
        return f'<figure><a href="{url}"><img src="{url}" alt="{html.escape(caption)}"></a><figcaption>{html.escape(caption)}</figcaption></figure>'
    for row in screens:
        name = row['name']
        title = html.escape(row['title'])
        current = figure(name, row['title'] + ' · aktuális előnézet')
        old = figure(name + '_old', row['title'] + ' · rögzített régi referencia') if row.get('show_reference', False) else ''
        cards.append(f'<section id="{name}"><h2>{title}</h2><p>{html.escape(row["status"])}</p>'
                     f'<div class="pair">{old}{current or "<p>Még nincs aktuális kép.</p>"}</div></section>')
    output = '''<!doctype html>
<html lang="hu"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SynthyBoy képernyők</title>
<style>body{margin:auto;padding:{{SIZE:catalog.page_padding}}vw;max-width:{{SIZE:catalog.max_width}}px;background:{{COLOR:catalog.background}};color:{{COLOR:catalog.text}};font:{{SIZE:font.body}}px system-ui}a{color:{{COLOR:catalog.link}}}section{padding:{{SIZE:catalog.card.padding}}px 0;border-top:{{SIZE:stroke.hair}}px solid {{COLOR:catalog.divider}}}.pair{display:flex;gap:{{SIZE:catalog.card.gap}}px;flex-wrap:wrap}figure{margin:0;flex:1 1 {{SIZE:catalog.card.min_width}}px}img{display:block;width:100%;height:auto}figcaption{padding:{{SIZE:catalog.card.padding}}px 0;color:{{COLOR:catalog.footer}}}</style>
<a href="kepes-katalogus.html">← Komponensek</a><h1>SynthyBoy képernyők</h1>
<p>Képernyőnként egy aktuális kép. Régi referencia csak a még nyitott átállásoknál szerepel.</p>
''' + '\n'.join(cards) + '\n</html>\n'
    output = re.sub(r"\{\{COLOR:([^{}]+)\}\}", lambda match: color(match[1]), output)
    output = re.sub(r"\{\{SIZE:([^{}]+)\}\}", lambda match: str(size(match[1])), output)
    OUTPUT.write_text(output, encoding='utf-8')
    print(f'Generated {OUTPUT.relative_to(ROOT)} from {len(screens)} screens')


if __name__ == '__main__':
    generate()
