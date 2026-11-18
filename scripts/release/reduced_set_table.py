
import json
import glob
import os
import re

# Paths
PRESET_DIR = "/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-g84-reduced-set-proposal/content/presets"
LOUDNESS_JSON = "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/round11/loud-xl.json"
PERF_TABLE_K61 = "/Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/round12/k61-r12-tables.md"
PRESET_TODO = "/Users/szabi/Documents/Projects/Safian/SynthyBoyVST/docs/release/preset-todo.md"

def get_presets():
    presets = []
    for filepath in glob.glob(f"{PRESET_DIR}/**/*.sbpreset", recursive=True):
        with open(filepath, 'r') as f:
            try:
                data = json.load(f)
                meta = data.get("meta", {})
                state = data.get("state", {})
                params = state.get("params", {})
                
                # Osc types:
                osc_types = []
                for i in range(1, 4):
                    osc_on = params.get(f"osc{i}.on", 0)
                    if osc_on:
                        osc_type = params.get(f"osc{i}.wavetable", 0) # simplified
                        osc_types.append(str(osc_type))
                
                presets.append({
                    "name": meta.get("name"),
                    "group": meta.get("category"),
                    "folder": os.path.basename(os.path.dirname(filepath)),
                    "osc": osc_types,
                    "effects": "Convolve" if params.get("convolve.on", 0) else "None"
                })
            except:
                continue
    return presets

def get_loudness():
    loudness = {}
    with open(LOUDNESS_JSON, 'r') as f:
        data = json.load(f)
        for name, info in data.items():
            loudness[name] = info.get("volume")
    return loudness

def get_perf_data():
    perf = {}
    with open(PERF_TABLE_K61, 'r') as f:
        lines = f.readlines()
        for line in lines:
            if "|" in line:
                parts = line.split("|")
                if len(parts) > 2:
                    name = parts[1].strip()
                    if name and name != "preset":
                        perf[name] = parts[2].strip()
    return perf

def get_todo_presets():
    todo = []
    with open(PRESET_TODO, 'r') as f:
        for line in f:
            match = re.search(r"\[.*\] (.*):", line)
            if match:
                todo.append(match.group(1).strip().replace(" *", ""))
    return todo

def main():
    presets = get_presets()
    loudness = get_loudness()
    perf = get_perf_data()
    todo = get_todo_presets()

    print("| Name | Group | Folder | Oscillators | Effects | Loudness (LU) | CPU (p99) | Status |")
    print("|---|---|---|---|---|---|---|---|")
    for p in presets:
        name = p['name']
        status = "TODO" if name in todo else "PASSED"
        print(f"| {name} | {p['group']} | {p['folder']} | {', '.join(p['osc'])} | {p['effects']} | {loudness.get(name, 'N/A')} | {perf.get(name, 'N/A')} | {status} |")

if __name__ == "__main__":
    main()
