# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The single canonical screenshot location; alternate fixtures are temporary exports."""
import io
import json
import os
from tempfile import NamedTemporaryFile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'docs/screens'
CATALOG = ROOT / 'scripts/screen-catalog.json'


def screen_path(name):
    names = {row['name'] for row in json.loads(CATALOG.read_text())['screens']}
    if name not in names:
        raise ValueError(f'Unknown screen: {name}')
    return DIRECTORY / f'{name}.jpg'


def export_path(path):
    """Inside the repository only the canonical current screen filenames are writable."""
    path = Path(path).resolve()
    if path.is_relative_to(ROOT):
        names = {row['name'] + '.jpg' for row in json.loads(CATALOG.read_text())['screens']}
        if path.parent != DIRECTORY or path.name not in names:
            raise ValueError('Screen exports inside the repository must use docs/screens/<screen>.jpg; '
                             'alternate states and comparisons require an external temporary --out path')
    return path


def save_screen(image, path):
    path = export_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Encode before touching the published file, then replace it atomically.
    encoded = io.BytesIO()
    if path.suffix.lower() in ('.jpg', '.jpeg'):
        image.convert('RGB').save(encoded, format='JPEG', quality=95, subsampling=0, optimize=True)
    else:
        from PIL import Image
        file_format = Image.registered_extensions().get(path.suffix.lower())
        if file_format is None:
            raise ValueError(f'Unsupported screen export format: {path.suffix}')
        image.save(encoded, format=file_format, optimize=True)
    temporary = None
    try:
        with NamedTemporaryFile(prefix='.' + path.stem + '-', suffix='.tmp', dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(encoded.getvalue())
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return path


def require_external_output(path):
    """Fixture galleries, layer breakdowns and comparisons never live in the checkout."""
    path = Path(path).resolve()
    if path.is_relative_to(ROOT):
        raise ValueError('Fixture variants and comparisons require a temporary output directory outside the repository')
    return path
