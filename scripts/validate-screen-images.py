#!/usr/bin/env python3
"""Validate screenshot locations in the staged index, without changing any files."""
import io
import json
import re
from pathlib import PurePosixPath
import subprocess
from PIL import Image


def staged(path):
    return subprocess.check_output(['git', 'show', ':' + path])


def validate():
    metadata = json.loads(staged('scripts/screen-catalog.json'))
    names = {row['name'] for row in metadata['screens']}
    references = {row['name'] for row in metadata['screens'] if row.get('show_reference', False)}
    retired = set(json.loads(staged('scripts/screen-image-migration.json')))
    assets = set(json.loads(staged('scripts/screen-assets.json'))['non_screen_assets'])
    files = subprocess.check_output(['git', 'ls-files', '-z']).decode().split('\0')
    errors = []

    def check_jpeg(filename):
        try:
            with Image.open(io.BytesIO(staged(filename))) as im:
                if im.format != 'JPEG':
                    errors.append(f'{filename}: .jpg must contain real JPEG data')
        except Exception as error:
            errors.append(f'{filename}: {error}')

    for filename in filter(None, files):
        path = PurePosixPath(filename)
        if filename.startswith('docs/screens/old/'):
            # Safety archive of earlier screens (owner, 2026-11-27): one flat folder of <name>_old.jpg copies.
            if path.parent != PurePosixPath('docs/screens/old') or path.suffix != '.jpg' \
                    or not path.stem.endswith('_old') or not re.fullmatch(r'[a-z0-9_]+', path.stem):
                errors.append(f'{filename}: docs/screens/old holds only <name>_old.jpg files')
            else:
                check_jpeg(filename)
        elif filename.startswith('docs/screens/'):
            stem = path.stem.removesuffix('_old')
            if path.parent != PurePosixPath('docs/screens') or path.suffix != '.jpg' or stem not in names:
                errors.append(f'{filename}: only <screen>.jpg and <screen>_old.jpg are allowed in the flat image directory')
                continue
            if path.stem.endswith('_old') and stem not in references:
                errors.append(f'{filename}: completed screen no longer keeps an old reference')
                continue
            check_jpeg(filename)
        elif path.suffix.lower() in ('.png', '.jpg', '.jpeg', '.webp'):
            if filename in retired:
                errors.append(f'{filename}: retired screenshot; use docs/screens/<screen>.jpg')
            elif filename not in assets and not filename.startswith(('components/native/', 'scripts/mpc-skin-extras/overlay-backgrounds/')):
                errors.append(f'{filename}: screenshot must be in docs/screens; a genuine non-screen asset must be declared in scripts/screen-assets.json')
            elif '/overlay-backgrounds/preview/' in filename:
                errors.append(f'{filename}: screenshot previews are not operational background assets')
    if errors:
        raise SystemExit('Screen image layout rejected:\n' + '\n'.join(errors))


if __name__ == '__main__':
    validate()
