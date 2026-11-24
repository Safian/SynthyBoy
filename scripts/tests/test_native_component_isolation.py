# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Native components load outside the repository with their native-family dependencies."""
import argparse
import ast
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


LIBRARY = Path(__file__).resolve().parents[2] / "components/native"


def copy_native_family(folder, destination, *, symlinks=False):
    family = destination / "components"
    family.mkdir()
    for name in ("style.py", "colors.json", "sizes.json"):
        shutil.copy2(LIBRARY.parent / name, family / name)
    native = family / "native"
    native.mkdir()
    # Component-to-component imports are supported: the fixture carries the native family.
    for owner in LIBRARY.iterdir():
        if owner.is_dir() and not owner.is_symlink() and (owner / (owner.name + ".py")).is_file():
            shutil.copytree(owner, native / owner.name, symlinks=symlinks)
    return native / folder.name


class ComponentIsolationTest(unittest.TestCase):
    selected_component = None

    def test_individual_directories_work_outside_the_repository(self):
        if self.selected_component is None:
            folders = sorted(path for path in LIBRARY.iterdir() if path.is_dir() and not path.is_symlink() and (path / (path.name + ".py")).is_file())
            self.assertIn(LIBRARY / "level", folders)
        else:
            folders = [LIBRARY / self.selected_component]
        for folder in folders:
            with self.subTest(component=folder.name), tempfile.TemporaryDirectory() as tmp:
                component = copy_native_family(folder, Path(tmp), symlinks=True)
                source = component / (folder.name + ".py")
                self.assertEqual(list(component.rglob("*.py")), [source])
                self.assertTrue((component / "img").is_dir())
                for link in component.rglob("*"):
                    if link.is_symlink():
                        self.assertTrue(link.resolve(strict=True).is_relative_to(component))
                for node in ast.walk(ast.parse(source.read_text())):
                    if isinstance(node, ast.ImportFrom):
                        self.assertFalse(node.level, "Use absolute native-family imports")
                        names = [node.module or ""]
                    elif isinstance(node, ast.Import):
                        names = [alias.name for alias in node.names]
                    else:
                        continue
                    for name in names:
                        self.assertFalse((name.split(".")[0] in {"scripts", "components"} and not (name == "components.style" or name.startswith("components.native."))) or
                                         name.startswith(("native_", "render_")), name)
                subprocess.run([sys.executable, "-E", "-c",
                                "import runpy, sys; runpy.run_path(sys.argv[1])", str(source)],
                               cwd=tmp, check=True)

    def test_dial_and_macro_can_each_supply_the_complete_level_control(self):
        names = ("level", "macro_card")
        if self.selected_component is not None:
            if self.selected_component not in names:
                self.skipTest("This component does not provide a LEVEL control")
            names = (self.selected_component,)
        for name in names:
            with self.subTest(component=name), tempfile.TemporaryDirectory() as tmp:
                folder = copy_native_family(LIBRARY / name, Path(tmp))
                verify = """
from pathlib import Path
import runpy
import sys
module = runpy.run_path(sys.argv[1])
if "level_definition" in module:
    definition = module["level_definition"](skins_dir=Path.cwd())
else:
    definitions = module["build_macro_card_definitions"](Path.cwd())
    level_type = definitions["macroCard"]["componentsData"][0]["componentData"]["type"]
    definition = definitions[level_type]
children = {item["componentData"]["name"]: item["componentData"] for item in definition["componentsData"]}
assert {"knob", "name", "value"} <= children.keys()
knob = children["knob"]["data"]
assert knob["numFrames"] == 127 and Path(knob["filmStrip"]).is_file()
"""
                subprocess.run([sys.executable, "-E", "-c", verify, str(folder / (name + ".py"))],
                               cwd=tmp, check=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check one native component outside the repository.")
    parser.add_argument("component", choices=sorted(p.name for p in LIBRARY.iterdir() if (p / (p.name + ".py")).is_file()))
    ComponentIsolationTest.selected_component = parser.parse_args().component
    unittest.main(argv=[sys.argv[0]])
