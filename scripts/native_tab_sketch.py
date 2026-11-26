"""Common tab drawing and handle resolution for generated native skin previews."""

from __future__ import annotations

from PIL import Image, ImageFont

from components import style


class NativeTabSketch:
    def _open_image(self, filename: str) -> Image.Image | None:
        if not filename:
            return None
        if filename not in self._img_cache:
            path = self._skins / filename
            if not path.is_file():
                return None
            self._img_cache[filename] = Image.open(path).convert("RGBA")
        return self._img_cache[filename]

    def _font(self, size: int, style_name: str = "SemiBold") -> ImageFont.ImageFont:
        return self._base_mod.font(style_name, max(8, int(round(size))))

    @staticmethod
    def _strip_prefix(handle: str) -> tuple[str | None, str]:
        if handle.startswith("WithName[") and "]/" in handle:
            head, rest = handle.split("]/", 1)
            return head[len("WithName["):], rest
        if handle.startswith("FromGesture/"):
            return None, handle[len("FromGesture/"):]
        return None, handle

    def _resolve_handle(self, handle: str | None, remaps: dict[str, str]) -> tuple[str | None, str | None]:
        if not handle:
            return None, None
        seen = set()
        with_name = None
        cur = handle
        while cur and cur not in seen:
            seen.add(cur)
            wn, stripped = self._strip_prefix(cur)
            if wn:
                with_name = wn
            if stripped in remaps:
                cur = remaps[stripped]
                continue
            cur = stripped
            break
        return with_name, cur

    def render_tab(self, tab_name: str) -> Image.Image:
        tabs_by_name = {t.get("tabName"): t for t in self._sketch.tui["pageData"]["tabs"]}
        # the finished skin names the first sub-tab of a key by its label (99_zzz_labels.py): Osc A, Env 1, LFO 1, Filter 1; the older short names still find it
        for short, label in (("Osc", "Osc A"), ("Env", "Env 1"), ("LFO", "LFO 1"), ("Filter", "Filter 1")):
            if tab_name == short and short not in tabs_by_name and label in tabs_by_name:
                tab_name = label
            elif tab_name == label and label not in tabs_by_name and short in tabs_by_name:
                tab_name = short
        if tab_name not in tabs_by_name:
            raise KeyError(f"Tab '{tab_name}' not found in TUI tabs: {list(tabs_by_name.keys())}")
        tab = tabs_by_name[tab_name]["componentDefinition"]
        bg = tab.get("backgroundData", {}).get("focussed", {})
        bg_name = bg.get("image", "")
        im = self._open_image(bg_name)
        if im is not None:
            canvas = im.resize((1280, 628), Image.Resampling.LANCZOS).copy()
        else:
            canvas = Image.new("RGBA", (1280, 628), style.color("surface.panel"))
        for child in tab.get("componentsData", []):
            self._draw_entry(canvas, child, (0, 0), (1280, 628), {})
        return canvas
