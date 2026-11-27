# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The 'old UI screens' section of the component page: every archived image is listed and rendered."""
import html
import importlib.util
import json
import unittest
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parent.parent.parent
OLD = ROOT / "docs/screens/old"
PAGE = (ROOT / "docs/ui-components/kepes-katalogus.html").read_text(encoding="utf-8")
LISTED = json.loads((ROOT / "scripts/old-ui-screens.json").read_text(encoding="utf-8"))["images"]
SAFETY = [f"{s['name']}_old.jpg" for s in
          json.loads((ROOT / "scripts/screen-catalog.json").read_text(encoding="utf-8"))["screens"]]


class OldScreensSectionTests(unittest.TestCase):
    def test_every_listed_file_exists_and_is_rendered(self):
        for item in LISTED:
            self.assertTrue((OLD / item["file"]).is_file(), item["file"])
            self.assertIn(f'href="../screens/old/{html.escape(quote(item["file"]))}"', PAGE, item["file"])

    def test_every_old_jpeg_is_listed_or_a_safety_copy(self):
        listed = {item["file"] for item in LISTED}
        self.assertFalse(listed & set(SAFETY))
        for path in OLD.glob("*_old.jpg"):
            self.assertIn(path.name, listed | set(SAFETY), path.name)
        for name in SAFETY:
            self.assertTrue((OLD / name).is_file(), name)
            self.assertIn(f'href="../screens/old/{name}"', PAGE, name)
        self.assertIn("Biztonsági másolatok (a korábbi aktuális képek)", PAGE)

    def test_page_markup(self):
        self.assertEqual(PAGE.count('class="old-shot"'), len(LISTED) + len(SAFETY))
        self.assertIn('loading="lazy" decoding="async"', PAGE)
        self.assertNotIn("data:image", PAGE)
        self.assertIn('id="regi-szuro"', PAGE)
        self.assertGreater(PAGE.index('id="regi-ui-kepernyok"'), PAGE.rindex("</article>"))

    def test_generator_fails_on_missing_file(self):
        spec = importlib.util.spec_from_file_location("gen_catalog", ROOT / "scripts/gen-native-component-catalog.py")
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        gen.OLD_DIRECTORY = ROOT / "docs/screens/does-not-exist"
        with self.assertRaisesRegex(ValueError, "missing"):
            gen.old_screen_groups()


if __name__ == "__main__":
    unittest.main()
