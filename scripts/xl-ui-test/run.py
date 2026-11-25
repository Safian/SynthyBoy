#!/usr/bin/env python3
"""Run an installed SynthyBoy XL editor tour or hardware cases using its packaged TUI."""
import argparse
import importlib
import hashlib
import json
import math
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

from PIL import ImageStat

from device import XL, Unavailable, navigate, recognise, recognise_word
from shard import MODELS, add_shard, selected
from inventory import function_key, inventory, locate


def write(path, value):
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")
    tmp.replace(path)


def safe(name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:100]


def differences(before, after):
    if before.keys() != after.keys():
        raise AssertionError("parameter set changed across snapshots")
    return {k: [before[k], v] for k, v in after.items() if before[k] != v}


class Runner:
    def __init__(self, args, tabs, controls, device):
        self.args, self.tabs, self.controls, self.dev = args, tabs, controls, device
        self.out = args.out
        self.results = []
        self.passed_controls = set()
        self.pid = None

    def event(self, event, **data):
        with (self.out / "events.jsonl").open("a") as f:
            f.write(json.dumps(dict(local_unix_s=time.time(), event=event, **data), ensure_ascii=False) + "\n")

    def capture(self, tag):
        tag = safe(tag)
        image = self.dev.image()
        image.save(self.out / (tag + ".png"))
        if self.args.capture_overlay:
            try:
                overlay = self.dev.overlay()
                self.dev.compose(image, overlay).save(self.out / (tag + ".composite.png"))
                overlay.getchannel("A").save(self.out / (tag + ".alpha.png"))
            except (Unavailable, RuntimeError, ValueError, OSError) as error:
                self.event("overlay_unavailable", tag=tag, reason=str(error))
                raise Unavailable(f"DRM capture incomplete for {tag}: {error}") from error
        status = self.dev.status()
        write(self.out / (tag + ".status.json"), status)
        return image, status

    def boot(self):
        remote = self.dev.remote("sha256sum " + shlex.quote(self.args.remote_tui)).decode().split()[0]
        expected = hashlib.sha256(self.args.tui.read_bytes()).hexdigest()
        if expected != remote:
            raise ValueError("installed TUI hash differs from the local inventory")
        self.pid = self.dev.remote("pidof MPC").decode().strip()
        if not self.pid:
            raise Unavailable("MPC application is not running")
        self.dev.status()
        self.event("begin", host=self.args.host, tui_sha256=remote, pid=self.pid)

    def alive(self):
        if self.dev.remote("pidof MPC").decode().strip() != self.pid:
            raise AssertionError("MPC application restarted during the test")

    def enter(self, view):
        return navigate(self.dev, self.tabs, view)

    def root(self, view):
        state = self.enter(view)
        expected = "0" if view == "play1" else "1"
        if state.get("ui_depth") != expected:
            self.enter("env1" if view == "play1" else "play1")
            state = self.enter(view)
        if state.get("state") != "visible" or state.get("ui_depth") != expected:
            raise Unavailable(f"{view} root unavailable: {state.get('state')}, depth={state.get('ui_depth')}")
        return state

    def disabled_osc_readouts(self, view, image):
        from disabled import OSC, candidates, gate_state, native_fields, read_value, verify_art
        state = self.dev.snapshot(display=True)
        if state["engine"].get(OSC[view][1]) != 0:
            raise AssertionError("active oscillator editor has no readable native text")
        values, frames = {}, []
        for row in candidates(self.args.tui):
            if row["view"] != view or row["rect"][1] < 210 or not row["values"]:
                continue
            if gate_state(row["gates"], state["vst"]) is not True:
                continue
            for box, expected in native_fields(row, state):
                try:
                    values[tuple(box)] = read_value(image, box, expected)
                except AssertionError:
                    continue
            for art in row["art"]:
                if art["frames"] <= 1 or gate_state(art["gates"], state["vst"]) is not True:
                    continue
                try:
                    frames.append(verify_art(image, art, self.args.tui.parent, state["vst"]))
                except (AssertionError, Unavailable):
                    continue
            if len(values) >= 2 and frames:
                return list(values.values())
        raise AssertionError(f"disabled oscillator body lacks two verified native readouts and a dial frame: {values}, {frames}")

    def quick_case(self, view):
        name = "quick." + view
        state = self.root(view)
        image, confirmed = self.capture(name)
        if state["view"] != view or confirmed["view"] != view:
            raise AssertionError(f"expected {view}, observed {confirmed['view']}")
        # Exclude both the host footer and top menu. A plain host colour must fail.
        crop = image.crop((0, 142, 1280, 722))
        words = [r["text"].strip() for r in recognise(crop) if len(r["text"].strip()) >= 2 and float(r["conf"]) >= 45]
        if view == "macros":
            # TARGETS is visible even when all the native Macro dials are missing.
            for index in range(1, 9):
                prefix = f"macros/Macro {index} Card/"
                dials = [c for c in self.controls if c["type"] == "Knob" and c["id"].startswith(prefix)]
                if len(dials) != 1:
                    raise AssertionError(f"Macro {index}: expected one native dial, found {len(dials)}")
                x, y, w, h = dials[0]["rect"]
                contrast = ImageStat.Stat(image.crop((x, y, x+w, y+h)).convert("L")).stddev[0]
                if contrast < 1:
                    raise AssertionError(f"Macro {index}: empty native dial at {dials[0]['rect']}")
        if view in ("osca", "oscb", "oscc", "oscsub", "oscnoise"):
            # The selector and POWER lamp survive a broken OFF layout. Test four body strips without the header.
            contrast = [ImageStat.Stat(image.crop((x, 230, x + 305, 726)).convert("L")).stddev[0]
                        for x in (16, 332, 648, 964)]
            if sum(s > 1 for s in contrast) < 2:
                raise AssertionError(f"empty oscillator editor body (native luminance deviations {contrast})")
            crop = image.crop((0, 210, 1280, 726)).resize((2560, 1032))
            words = [r["text"].strip() for r in recognise(crop) if r["text"].strip() and float(r["conf"]) >= 45]
            if not words:
                words = self.disabled_osc_readouts(view, image)
        if not words:
            raise AssertionError("no readable native editor text in primary framebuffer")
        self.alive()
        return {"ocr": words, "view": confirmed["view"], "primary_image": name + ".png"}

    def expect(self, spec, snapshots, result):
        if not any(k in spec for k in ("target", "status", "ocr", "vst", "overlay_clear", "overlay_visible")):
            raise ValueError("expect requires an observable")
        deadline = time.monotonic() + float(spec.get("timeout", 5))
        expected_status = spec.get("status", {})
        while expected_status:
            state = self.dev.status()
            bad = {k: state.get(k) for k, v in expected_status.items() if state.get(k) != str(v)}
            if not bad:
                break
            if time.monotonic() >= deadline:
                raise AssertionError(f"expected status {expected_status}, observed {bad}")
            time.sleep(.2)
        if "ocr" in spec:
            image = self.dev.image()
            if "rect" in spec:
                x, y, w, h = spec["rect"]
                image = image.crop((x, y, x + w, y + h))
            text = " ".join(r["text"].upper() for r in recognise(image) if r["text"].strip())
            if ("rect" in spec and len(spec["ocr"]) == 1 and image.width <= 180 and image.height <= 80
                    and spec["ocr"][0].upper() not in text):
                text += " " + recognise_word(image, spec.get("alphabet")).upper()
            for word in spec["ocr"]:
                if word.upper() not in text:
                    raise AssertionError(f"OCR missing {word!r}: {text}")
            result["ocr"] = text
        for prop in ("overlay_clear", "overlay_visible"):
            if prop in spec:
                alpha = self.dev.overlay().getchannel("A")
                x, y, w, h = spec[prop]
                changed = alpha.crop((x, y, x + w, y + h)).getbbox() is not None
                result[prop] = changed
                if changed != (prop == "overlay_visible"):
                    raise AssertionError(f"{prop} failed in {spec[prop]}")
        if "vst" in spec:
            actual = self.dev.snapshot(vst=True)["vst"]
            result["vst"] = {}
            if not spec["vst"]:
                raise ValueError("VST expectation requires at least one index")
            for key, expected in spec["vst"].items():
                index = int(key)
                if index not in actual:
                    raise Unavailable(f"normalized VST value unavailable for index {index}")
                result["vst"][key] = actual[index]
                if not math.isclose(actual[index], float(expected), abs_tol=1e-6, rel_tol=0):
                    raise AssertionError(f"VST {index}: expected {expected}, observed {actual[index]}")
        if "target" in spec:
            target, actual = spec["target"], self.dev.params()
            if target != "*" and target not in actual:
                raise Unavailable("engine parameter unavailable: " + target)
            if "value" in spec and actual[target] != float(spec["value"]):
                raise AssertionError(f"{target}: expected {spec['value']}, got {actual[target]}")
            if "change" in spec:
                reference = snapshots[spec.get("from", "baseline")]
                diff = differences(reference, actual)
                mode = spec["change"]
                result["engine_changes"] = diff
                if mode == "only":
                    ok = set(diff) == {target}
                elif mode == "changed":
                    ok = target in diff
                elif mode in ("unchanged", "restored"):
                    ok = not diff if target == "*" else target not in diff
                else:
                    raise ValueError("unknown change check " + mode)
                if not ok:
                    raise AssertionError(f"{target}: expected {mode}; observed {diff}")
            elif "value" not in spec:
                raise ValueError("parameter expectation requires value or change")

    def step(self, spec, case, snapshots, result):
        op = spec["op"]
        if "if_changed" in spec:
            guard = spec["if_changed"]
            base, key = snapshots[guard.get("from", "baseline")], guard["target"]
            if self.dev.params()[key] == base[key]:
                result["already_restored"] = True
                return
        if op == "snapshot":
            name = spec.get("name", "baseline")
            snapshots[name] = self.dev.params()
            write(self.out / f"{safe(case)}.{safe(name)}.params.json", snapshots[name])
        elif op == "navigate":
            self.root(spec["view"])
        elif op == "expect":
            self.expect(spec, snapshots, result)
        elif op == "capture":
            self.capture(case + "." + spec.get("name", "capture"))
        elif op in ("drag", "tap"):
            p = (spec["x"], spec["y"]) if "x" in spec and "y" in spec else None
            if "control" in spec:
                control = locate(self.controls, self.dev.status()["view"], spec["control"], p)
                result["control"] = control["id"]
                if not p:
                    x, y, w, h = control["rect"]
                    p = (x + w // 2, y + h // 2)
            if not p:
                raise ValueError("gesture requires coordinates or an unambiguous packaged control")
            result["point"] = p
            if op == "tap":
                self.dev.gesture(*p)
            else:
                self.dev.gesture(*p, spec.get("dx", 0), spec.get("dy", 0), spec.get("duration", 350))
        elif op in ("qlink", "wheel", "save_reload"):
            module = "cases_project" if op == "save_reload" else "cases_q"
            try:
                adapter = importlib.import_module(module)
            except ModuleNotFoundError as error:
                if error.name != module:
                    raise
                raise Unavailable(f"{op} requires a verified XL adapter") from error
            result["adapter_result"] = adapter.run(self.dev, spec, lambda step: self.step(step, case, snapshots, {}), self.out)
        else:
            raise ValueError("unknown operation " + op)
        self.alive()

    def run_case(self, case, next_view=None):
        case_id = case["id"]
        record = {"id": case_id, "view": case["view"], "status": "NOT_RUN", "steps": [], "restore": []}
        self.results.append(record)
        snaps, verified = {}, set()
        def phase(key):
            pending = set()
            for spec in case.get(key, []):
                result = {"spec": spec, "local_unix_s": time.time()}
                record[key].append(result)
                self.event("step", case=case_id, phase=key, spec=spec)
                self.step(spec, case_id, snaps, result)
                if result.get("control"):
                    pending = {result["control"]}
                elif spec["op"] in ("navigate", "tap", "drag"):
                    pending.clear()
                if spec["op"] == "expect" and key == "steps":
                    verified.update(pending)
                    pending.clear()
                result["status"] = "PASS"
        try:
            self.root(case["view"])
            phase("steps")
            if not any(s["spec"]["op"] == "expect" for s in record["steps"]):
                raise Unavailable("case has no independent functional oracle")
            record["status"] = "PASS"
        except Exception as e:
            record.update(status="NOT_RUN" if isinstance(e, Unavailable) else "FAIL", error=str(e))
        finally:
            try:
                self.capture(case_id + ".after")
            except Exception as e:
                record.update(status="FAIL", capture_error=str(e))
            try:
                phase("restore")
                state = self.dev.status()
                same_root = (record["status"] == "PASS" and next_view == case["view"]
                             and state.get("view") == next_view and state.get("state") == "visible"
                             and state.get("ui_depth") == ("0" if next_view == "play1" else "1")
                             and state.get("page_top") == "0")
                if not same_root:
                    self.root("play1")
                self.alive()
            except Exception as e:
                record.update(status="FAIL", recovery_error=str(e))
            if record["status"] == "PASS":
                self.passed_controls.update(verified)
            self.save()

    def save(self):
        ids = {c["id"] for c in self.controls}
        functions = {function_key(c) for c in self.controls}
        reached = {function_key(c) for c in self.controls if c["id"] in self.passed_controls}
        write(self.out / "results.json", self.results)
        write(self.out / "coverage.json", {"packaged_controls": len(ids), "verified": sorted(self.passed_controls),
              "unverified": sorted(ids - self.passed_controls),
              "distinct_host_commands_by_tab": len(functions), "verified_commands": sorted(reached),
              "unverified_commands": sorted(functions - reached),
              "semantic_cases": {r["id"]: r["status"] for r in self.results}})


def validate_cases(cases, tabs, controls):
    errors, seen = [], set()
    for case in cases:
        case_id = case.get("id")
        if not case_id or case_id in seen or case.get("view") not in tabs:
            errors.append(f"{case_id}: invalid identity or initial view")
            continue
        seen.add(case_id)
        view = case["view"]
        for step in case["steps"] + case.get("restore", []):
            try:
                op = step["op"]
                if op == "navigate":
                    view = step["view"]
                    if view not in tabs: raise ValueError(f"unknown view {view}")
                elif op == "expect" and "view" in step.get("status", {}):
                    view = step["status"]["view"]
                elif op in ("drag", "tap") and "control" in step:
                    point = (step["x"], step["y"]) if "x" in step and "y" in step else None
                    locate(controls, view, step["control"], point)
                elif op not in ("drag", "tap", "expect", "capture", "snapshot", "qlink", "wheel", "save_reload"):
                    raise ValueError(f"unknown op {op}")
            except (KeyError, ValueError) as error:
                errors.append(f"{case_id}: {error}")
    return errors


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("inventory", "quick", "full"), required=True)
    parser.add_argument("--tui", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--host")
    parser.add_argument("--capture-overlay", action="store_true", help="also save DRM alpha and the reconstructed display")
    parser.add_argument("--tool-dir", default=None)
    parser.add_argument("--remote-tui", default=None)
    parser.add_argument("--cases", type=Path, help="replace the bundled case catalogs")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--case-id", action="append", help="run only these configured cases")
    add_shard(parser)
    args = parser.parse_args()
    model = MODELS[args.device]
    views = selected(args, parser)
    args.host = args.host or model.host
    args.tool_dir = args.tool_dir or model.helper_dir
    args.remote_tui = args.remote_tui or model.tui
    args.out.mkdir(parents=True, exist_ok=True)
    if any((args.out / p).exists() for p in ("inventory.json", "results.json", "events.jsonl")):
        parser.error("use a new output directory to preserve prior evidence")
    tabs, controls = inventory(args.tui)
    controls = [control for control in controls if control["view"] in views]
    write(args.out / "inventory.json", {"tui_sha256": hashlib.sha256(args.tui.read_bytes()).hexdigest(),
                                         "tabs": tabs, "device":args.device, "assigned_views":views, "controls": controls})
    cases = []
    if args.suite == "full":
        core = Path(__file__).with_name("cases.json")
        catalogs = [args.cases or core]
        noise = core.with_name("cases_noise.json")
        if not args.cases and noise.is_file():
            catalogs.append(noise)
        for catalog in catalogs:
            cases.extend(json.loads(catalog.read_text()))
    cases = [case for case in cases if case["view"] in views]
    if args.case_id:
        unknown = set(args.case_id) - {c["id"] for c in cases}
        if unknown:
            parser.error(f"undefined case IDs {unknown}")
        cases = [case for case in cases if case["id"] in args.case_id]
    failures = validate_cases(cases, tabs, controls)
    if args.suite == "inventory" or args.dry_run:
        write(args.out / "plan.json", {"suite": args.suite, "device":args.device, "views":views, "case_ids": [c["id"] for c in cases],
              "distinct_host_commands_by_tab": len({function_key(c) for c in controls}), "validation_errors": failures,
              "hardware_status": "NOT_RUN"})
        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1
        print(f"{len(tabs)} packaged views, {len(controls)} interactive entries, {len(cases)} configured cases; hardware not run")
        return 0
    if not args.host:
        parser.error("hardware mode requires --host")
    runner = Runner(args, tabs, controls, model.adapter(args.host, args.tool_dir))
    runner.boot()
    try:
        if args.suite == "quick":
            for view in views:
                record = {"id": "quick." + view}
                runner.results.append(record)
                try:
                    record.update(runner.quick_case(view), status="PASS")
                except Exception as e:
                    record.update(status="NOT_RUN" if isinstance(e, Unavailable) else "FAIL", error=str(e))
                    try:
                        runner.capture("failed." + view)
                    except Exception as capture:
                        record["capture_error"] = str(capture)
                runner.event("quick", **record)
                runner.save()
                print(view, record["status"], flush=True)
        else:
            for i, case in enumerate(cases):
                errors = [error for error in failures if error.partition(":")[0] == case["id"]]
                if errors:
                    record = {"id": case["id"], "status": "NOT_RUN", "error": "; ".join(errors)}
                    runner.results.append(record)
                    runner.event("invalid_case", **record)
                    runner.save()
                else:
                    next_view = cases[i + 1]["view"] if i + 1 < len(cases) else None
                    runner.run_case(case, next_view)
                print(case["id"], runner.results[-1]["status"], flush=True)
    finally:
        try:
            runner.root("play1")
            runner.alive()
        except Exception as e:
            runner.results.append({"id": "return.play1", "status": "FAIL", "error": str(e)})
        runner.save()
        runner.event("end")
    if any(r["status"] != "PASS" for r in runner.results) or not runner.results:
        return 1
    if args.suite == "full" and len(runner.passed_controls) != len(controls):
        print("Full packaged-control coverage incomplete", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(cli())
