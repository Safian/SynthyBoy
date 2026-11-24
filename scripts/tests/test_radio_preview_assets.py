# SynthyBoy - Test verified factory MPC radio selector comparison assets.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later

from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
KOMP_DIR = REPO_ROOT / "docs" / "ui-components" / "komponensek"
DOC_MD = REPO_ROOT / "docs" / "ui-components" / "radio-kizaro-valaszto-preview.md"
DOC_HTML = REPO_ROOT / "docs" / "ui-components" / "radio-preview.html"


def test_radio_preview_assets_exist():
    expected_assets = [
        "radio-gyari-manual-p41.png",
        "radio-gyari-padperform-p80.png",
        "radio-sdk-seg3.png",
        "radio-sajat-hz-bpm-key-ledlist.png",
        "radio-gyari-vs-sajat-osszehasonlitas.png",
    ]
    for filename in expected_assets:
        file_path = KOMP_DIR / filename
        assert file_path.exists(), f"Missing expected asset: {filename}"
        im = Image.open(file_path)
        assert im.width > 200, f"Asset {filename} width too small: {im.width}"
        assert im.height > 40, f"Asset {filename} height too small: {im.height}"


def test_documentation_and_preview_files_exist():
    assert DOC_MD.exists(), "Missing radio-kizaro-valaszto-preview.md"
    assert DOC_HTML.exists(), "Missing radio-preview.html"

    md_content = DOC_MD.read_text(encoding="utf-8")
    assert "MPC Standalone OS - User Guide - v3.10.1.pdf" in md_content
    assert "41. oldal" in md_content
    assert "Selectors show all available options" in md_content
    assert "HZ / BPM / KEY" in md_content
    assert "SDK skin NEM firmware" in md_content or "NEM gyári firmware" in md_content

    html_content = DOC_HTML.read_text(encoding="utf-8")
    assert "radio-gyari-manual-p41.png" in html_content
    assert "kepes-katalogus.html" in html_content
    assert "radio-gyari-vs-sajat-osszehasonlitas.png" in html_content
    assert "SDK skin" in html_content
