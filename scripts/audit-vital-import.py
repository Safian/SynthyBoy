#!/usr/bin/env python3
"""Independent audit of the Vital preset importer (docs/ui-design/VITAL-IMPORT-AUDIT-REPORT.md).

    python3 -I scripts/audit-vital-import.py --vital DIR [--vital DIR ...] --imported DIR [--roundtrip FILE.json] [--json OUT.json] [-v]

  --vital       directories searched recursively for .vital files (unzip a .vitalbank first); the originals are never modified
  --imported    output directory of `synthyboy_vital_import` (<name>.sbpreset + <name>.report.json)
  --roundtrip   output of `synthyboy_vital_roundtrip` (what the Vital-derived engine holds after loading each .sbpreset); optional

Python 3 standard library only; nothing is imported from the importer. The audit reads
  - the original .vital JSON (settings, modulations, wavetables, lfos, sample),
  - the importer's report.json and the written .sbpreset,
  - docs/params.json (OUR parameter table: ranges, units, choice lists, modulation source / destination lists),
  - core/vital-import/vital_map.cpp (ONLY the pairing "our key <-> Vital control name"; its conversion columns are not used),
  - core/engine-vital/upstream/src/common/synth_parameters.cpp (Vital's own definition of every control: range, value scale, display
    multiplier / units; this is the independent source of the expected value).

Checks
  1 completeness  every key of `settings` and every modulation slot / wavetable / LFO shape / the sample object ends up in exactly ONE of
                  imported / approximated / dropped / ignored (the implicit classes are named: unused modulation slot, ...); any key
                  accounted nowhere (or twice) is an error.
  2 correctness   the expected value of every imported / approximated row is recomputed from Vital's own value definition
                  (display value = scale(raw) * multiplier + offset, in Vital's display unit) converted into our unit, clamped into our
                  range, snapped to our step, and compared with the .sbpreset. Special rows (frame position, oscillator destination,
                  warp type, sample, filter routing, effect order, bend range, voice amplitude, modulation slots) have their own rule.
                  Keys the importer does not address must still hold their default.
  3 engine        (with --roundtrip) the engine's parameter read-back equals the file for every key, and the value of every Vital
                  control inside the engine equals the number in the original .vital file (up to the documented deviations).
Exit code 0 when every check passes, 1 otherwise.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --------------------------------------------------------------------------------------------------------------------
# sources of truth
# --------------------------------------------------------------------------------------------------------------------
KNOWN_CONST = {
    'kNumOscillatorWaveFrames - 1': 256.0, 'kNumOscillatorWaveFrames / 2': 128.0, '-kNumOscillatorWaveFrames / 2': -128.0,
    'kMaxPolyphony - 1': 32.0, 'Distortion::kMinDrive': -30.0, 'Distortion::kMaxDrive': 30.0,
    'SynthOscillator::kNumDistortionTypes - 1': 12.0, 'SynthOscillator::kNumSpectralMorphTypes - 1': 11.0,
    'SynthOscillator::kNumUnisonStackTypes - 1': 10.0, 'RandomLfo::kNumStyles - 1': 3.0, 'SynthLfo::kNumSyncOptions - 1': 4.0,
    'SynthLfo::kNumSyncTypes - 1': 5.0, 'kNumFilterModels - 1.0': 7.0,
}


def num_or_none(text):
    t = text.strip().rstrip('f')
    if t in KNOWN_CONST:
        return KNOWN_CONST[t]
    try:
        return float(t)
    except ValueError:
        return KNOWN_CONST.get(text.strip())


def load_vital_details():
    """control name -> dict(min, max, default, mult, scale, invert, units, offset) from Vital's parameter definition."""
    src = open(os.path.join(REPO, 'core/engine-vital/upstream/src/common/synth_parameters.cpp'), encoding='utf-8').read()
    lists = {}
    for m in re.finditer(r'ValueDetailsLookup::(\w+)\[\] = \{(.*?)\n  \};', src, re.S):
        lists[m.group(1)] = m.group(2)
    ent = re.compile(r'\{\s*"(\w+)",\s*(0x[0-9A-Fa-f]+),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?),\s*'
                     r'ValueDetails::(\w+),\s*(true|false),\s*"([^"]*)"', re.S)
    table = {}
    for lname, body in lists.items():
        for e in ent.findall(body):
            table[(lname, e[0])] = dict(min=num_or_none(e[2]), max=num_or_none(e[3]), default=num_or_none(e[4]), offset=num_or_none(e[5]) or 0.0,
                                         mult=num_or_none(e[6]) if num_or_none(e[6]) is not None else 360.0, scale=e[7], invert=e[8] == 'true',
                                         units=e[9].strip())
    group = {'osc': 'osc_parameter_list', 'env': 'env_parameter_list', 'lfo': 'lfo_parameter_list', 'random': 'random_lfo_parameter_list',
             'filter': 'filter_parameter_list', 'modulation': 'mod_parameter_list'}

    def lookup(control):
        m = re.match(r'(osc|env|lfo|random|filter|modulation)_(\d+|fx)_(.+)$', control)
        if m and (group[m.group(1)], m.group(3)) in table:
            return table[(group[m.group(1)], m.group(3))]
        return table.get(('parameter_list', control))
    return lookup


def load_pairs():
    """our key <-> Vital control pairing of the importer's table (conversion columns deliberately not read)."""
    txt = open(os.path.join(REPO, 'core/vital-import/vital_map.cpp'), encoding='utf-8').read()
    return [(k, c) for k, c in re.findall(r'\{ "([^"]+)", "([^"]+)", Conv::\w+, [^,]+, [^ ]+ \}', txt)]


def load_params():
    d = json.load(open(os.path.join(REPO, 'docs/params.json'), encoding='utf-8'))
    return {p['key']: p for p in d['params']}, d['modSources'], d['modDestinations']


# --------------------------------------------------------------------------------------------------------------------
# Vital value definition -> our unit
# --------------------------------------------------------------------------------------------------------------------
def scaled(d, raw):
    s = d['scale']
    if s in ('kIndexed', 'kLinear'):
        v = raw
    elif s == 'kQuadratic':
        v = raw * raw
    elif s == 'kCubic':
        v = raw ** 3
    elif s == 'kQuartic':
        v = raw ** 4
    elif s == 'kSquareRoot':
        v = math.sqrt(max(raw, 0.0))
    elif s == 'kExponential':
        v = 2.0 ** raw
    else:
        raise ValueError(s)
    return v


def display(d, raw):
    v = scaled(d, raw)
    if d['invert']:
        v = 1.0 / v
    return v * d['mult'] + d['offset']


def note_to_hz(n):
    return 440.0 * 2.0 ** ((n - 69.0) / 12.0)


def round_half_up(x):
    return math.floor(x + 0.5)


# choice lists that got entries appended after the Vital layout (docs/ui-design/13-new-keys.md section 3): last index a Vital file can mean
def legacy_max(key):
    if re.match(r'(filter\d|fxfilter)\.style$', key): return 5
    if re.match(r'(filter\d|fxfilter)\.model$', key): return 7
    if re.match(r'lfo\d+\.shape$', key): return 11
    if re.match(r'rlfo\d\.style$', key): return 3
    if re.match(r'osc\d\.wavetable$', key): return 12
    if re.match(r'(osc\d|noise)\.destination$', key): return 4
    return None


def expected_generic(d, p, raw):
    """Expected plain value of our parameter p for the Vital value raw, from Vital's value definition. Returns (value, how)."""
    unit = p['unit']
    pmin, pmax = float(p['min']), float(p['max'])
    if d['scale'] == 'kIndexed':
        v = round_half_up(raw) - d['min'] + pmin if p['scale'] == 'stepped' else raw
        return v, 'index'
    if unit == 'percent':
        # a percent of the Vital range (after Vital's own value scaling): vmin -> our min, vmax -> our max
        f0, f1 = scaled(d, d['min']), scaled(d, d['max'])
        if d['invert']:
            raise ValueError('inverted percent')
        t = (scaled(d, raw) - f0) / (f1 - f0)
        return pmin + t * (pmax - pmin), 'percent of the Vital range'
    if unit == 'ms':
        disp = display(d, raw)
        if 'secs' in d['units'] or d['units'] == '':
            return disp * 1000.0, 'secs -> ms'
        return disp, 'ms'
    if unit == 'Hz':
        if d['units'] in ('', 'semitones'):
            return note_to_hz(raw), 'note -> Hz'
        if d['invert'] and 'secs' in d['units']:
            return 1.0 / display(d, raw), '1/period -> Hz'
        return display(d, raw), 'Hz'
    if unit == 'degrees':
        return raw * 360.0, 'cycle fraction -> degrees'
    if unit == 'cents':
        return raw * 100.0, 'tune -> cents'
    if unit in ('dB', 'semitone', 'voices', 'none'):
        return display(d, raw) if d['scale'] != 'kLinear' else raw * d['mult'], unit
    raise ValueError('no rule for unit ' + unit)


def snap(p, v):
    iv = float(p.get('interval', 0) or 0)
    if iv > 0:
        v = math.floor(v / iv + 0.5) * iv
    return v


# --------------------------------------------------------------------------------------------------------------------
# effect order: decode of effect_chain_order (Vital's permutation index)
# --------------------------------------------------------------------------------------------------------------------
def decode_effect_order(code):
    """order[position] = effect id (0..8); code is a Lehmer-like index (Vital: utils.cpp, stated here independently)."""
    work = list(range(9))
    code = int(code)
    for i in range(9):
        r = 9 - i
        inv = code % r
        code //= r
        idx = r - 1 - inv
        taken = work.pop(idx)
        work.insert(r - 1, taken)
    return work[:9]


def decode_by_enumeration(code):
    """The same permutation by brute force definition check: placing positions from the back (cross-check of the decoder above)."""
    order = list(range(9))
    c = int(code)
    out = [0] * 9
    pool = list(order)
    # position r-1 receives the element at index (r-1-inv) of the remaining pool (r = pool size)
    for r in range(9, 0, -1):
        inv = c % r
        c //= r
        out[r - 1] = pool.pop(r - 1 - inv)
    return out


# --------------------------------------------------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------------------------------------------------
MOD_KEY = re.compile(r'^modulation_(\d+)_(amount|power|bipolar|stereo|bypass)$')
STRUCTURED = {'modulations', 'wavetables', 'lfos', 'sample', 'tuning'}
SAMPLE_CONSUMED = {'sample_random_phase', 'sample_transpose', 'sample_tune', 'sample_keytrack', 'sample_loop', 'sample_bounce'}
NORMAL_FIRST = {'osc_%d_%s' % (n, t) for n in (1, 2, 3) for t in ('detune_range', 'midi_track', 'spectral_unison', 'transpose_quantize', 'smooth_interpolation', 'view_2d')}


def safe_stem(name):
    s = os.path.splitext(name)[0]
    s = ''.join(c if (c.isalnum() and c.isascii()) or c in '-_.' else '_' for c in s)
    return s or 'preset'


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        h.update(f.read())
    return h.hexdigest()


def normalise_class(detail, name):
    """Group the vital keys of one detail class: osc_1_xyz -> osc_N_xyz, modulation_17_amount -> modulation_N_amount."""
    n = re.sub(r'(?<=_)\d+(?=_)', 'N', name)
    n = re.sub(r'\[\d+\]', '[i]', n)
    return n


class Audit:
    def __init__(self, args):
        self.args = args
        self.details = load_vital_details()
        self.pairs = load_pairs()
        self.by_control = {c: k for k, c in self.pairs}
        self.params, self.mod_sources, self.mod_dests = load_params()
        self.src_choice = {s['key']: s['choice'] for s in self.mod_sources}
        self.dst_choice = {s['key']: s['choice'] for s in self.mod_dests}
        self.errors = []          # (preset, area, text)
        self.rows = collections.Counter()
        self.per_file = []
        self.class_totals = collections.Counter()
        self.ignored_classes = collections.defaultdict(lambda: collections.Counter())
        self.dropped_items = []
        self.expect_dev = collections.Counter()
        self.rt_ok = 0
        self.rt_expl = collections.Counter()

    def err(self, preset, area, text):
        self.errors.append((preset, area, text))

    # ---------------------------------------------------------------------------------------------------------------
    def run(self):
        vitals = []
        for d in self.args.vital:
            for root, _, files in os.walk(d):
                for f in files:
                    if f.endswith('.vital'):
                        vitals.append(os.path.join(root, f))
        vitals.sort()
        imported = {}
        for f in sorted(os.listdir(self.args.imported)):
            if f.endswith('.sbpreset'):
                path = os.path.join(self.args.imported, f)
                doc = json.load(open(path, encoding='utf-8'))
                imported[doc.get('import', {}).get('source', {}).get('sha256')] = (f[:-len('.sbpreset')], doc)
        rt = {}
        if self.args.roundtrip:
            for e in json.load(open(self.args.roundtrip, encoding='utf-8'))['presets']:
                rt[e['file'][:-len('.sbpreset')]] = e
        for v in vitals:
            sha = sha256_file(v)
            if sha not in imported:
                self.err(os.path.basename(v), 'file', 'no imported .sbpreset with the SHA-256 of this file')
                continue
            stem, doc = imported[sha]
            report = json.load(open(os.path.join(self.args.imported, stem + '.report.json'), encoding='utf-8'))
            self.audit_one(os.path.basename(v), json.load(open(v, encoding='utf-8')), report, doc, rt.get(stem), stem)
        return not self.errors

    # ---------------------------------------------------------------------------------------------------------------
    def audit_one(self, name, vdoc, report, sb, rt, stem):
        S = vdoc['settings']
        state = sb['state']['params']
        content = sb['state'].get('content', {})
        items = report['items']
        P = self.params
        defaults = {k: p['default'] for k, p in P.items()}
        cnt = collections.Counter()

        # ---- structure of the file ----------------------------------------------------------------------------------
        mods = S.get('modulations', [])
        used = [i for i, m in enumerate(mods) if m.get('source') or m.get('destination')]

        # item index: by the Vital name(s) they refer to
        by_name = collections.defaultdict(list)
        slot_items = collections.defaultdict(list)
        wt_items = collections.defaultdict(list)
        lfo_items = collections.defaultdict(list)
        for it in items:
            vn = it['vital_name']
            m = re.match(r'modulations\[(\d+)\]', vn)
            if m:
                slot_items[int(m.group(1))].append(it)
                continue
            m = re.match(r'wavetables\[(\d+)\]', vn)
            if m:
                wt_items[int(m.group(1))].append(it)
                continue
            m = re.match(r'lfos\[(\d+)\]', vn)
            if m:
                lfo_items[int(m.group(1))].append(it)
                continue
            for part in vn.split(','):
                by_name[part].append(it)

        # ---- 1 completeness: every settings key -------------------------------------------------------------------
        slot_dropped = {i for i, its in slot_items.items() if any(x['kind'] == 'dropped' for x in its)}
        active_slots = [i for i in used if i not in slot_dropped]
        cls = {}
        for key in S:
            if key in STRUCTURED:
                continue
            kinds = []
            m = MOD_KEY.match(key)
            if m:
                i = int(m.group(1)) - 1
                if i in active_slots:
                    kinds.append('imported')
                else:
                    kinds.append('ignored:unused_modulation_slot' if i not in slot_dropped else 'dropped:with_its_modulation_connection')
            elif key in self.by_control:
                kinds.append('imported')
            elif key in ('pitch_bend_range', 'effect_chain_order', 'filter_1_filter_input', 'filter_2_filter_input', 'mpe_enabled',
                         'stereo_routing', 'voice_override',
                         'osc_1_transpose_quantize', 'osc_2_transpose_quantize', 'osc_3_transpose_quantize'):
                kinds.append('imported')
            elif key in SAMPLE_CONSUMED and content.get('samples'):
                kinds.append('imported')  # carried by the oscillator the sample was mapped to
            for it in by_name.get(key, []):
                kinds.append(it['kind'] + ':' + it['detail'])
            # a table row that has an item is that item's class, not "imported" as well
            if len(kinds) > 1 and kinds[0] == 'imported' and any(k.split(':')[0] in ('approximated', 'dropped', 'ignored') for k in kinds[1:]):
                kinds = kinds[1:]
            if key in ('filter_1_filter_input', 'filter_2_filter_input') and len(kinds) == 2 and kinds[0] == 'imported' \
                    and any(k.startswith('approximated') for k in kinds[1:]):
                kinds = kinds[1:]
            if not kinds:
                cls[key] = 'UNACCOUNTED'
            else:
                cls[key] = kinds[0]
                if len(kinds) > 1:
                    self.err(name, 'completeness', '%s accounted %d times: %s' % (key, len(kinds), kinds))
            c0 = cls[key].split(':')[0]
            cnt[c0] += 1
            if c0 == 'UNACCOUNTED':
                self.err(name, 'completeness', '%s (= %r) is in settings but in no class (not imported, approximated, dropped or ignored)' % (key, S[key]))
            if c0 in ('ignored', 'dropped'):
                self.ignored_classes[c0][(normalise_class(cls[key], key), cls[key].split(':', 1)[1] if ':' in cls[key] else '')] += 1
        # structured members
        if 'modulations' in S:
            for i, m in enumerate(mods):
                if not (m.get('source') or m.get('destination')):
                    cnt['ignored(empty modulation slot)'] += 1
                elif i in slot_dropped:
                    cnt['dropped(modulation connection)'] += 1
                    for it in slot_items[i]:
                        if it['kind'] == 'dropped':
                            self.dropped_items.append((name, it))
                else:
                    cnt['imported(modulation connection)'] += 1
                    if 'line_mapping' in m:
                        cnt['imported(modulation line_mapping)'] += 1
                    for it in slot_items.get(i, []):
                        cnt['approximated(modulation line_mapping)'] += 1
        for sk in ('wavetables', 'lfos'):
            if sk in S:
                cnt['imported(%s)' % sk] += len(S[sk]) - sum(1 for i in (wt_items if sk == 'wavetables' else lfo_items) if any(x['kind'] == 'dropped' for x in (wt_items if sk == 'wavetables' else lfo_items)[i]))
        if 'sample' in S:
            si = by_name.get('sample', [])
            cnt[(si[0]['kind'] if si else 'imported') + '(sample object)'] += 1
        # everything that is in `items` must refer to something that exists in the file
        known = set(S) | {'meta'}
        for it in items:
            vn = it['vital_name']
            base = [p for p in vn.split(',')]
            for b in base:
                if re.match(r'(modulations|wavetables|lfos)\[', b) or b in ('sample', 'tuning', 'synth_version', 'preset_name', 'author', 'comments', 'preset_style'):
                    continue
                if b not in S:
                    self.err(name, 'completeness', 'report item for %r, which is not in the file' % b)

        # ---- 2 correctness -------------------------------------------------------------------------------------------
        touched = set()
        frames = {}
        for w in content.get('wavetables', []):
            frames[w['osc']] = w['frames']
        wt_is_custom = {n: state.get('osc%d.wavetable' % n) == 13 for n in (1, 2, 3)}
        sample_target = 0
        for smp in content.get('samples', []):
            sample_target = smp['osc']
        sample_obj = S.get('sample', {})
        custom_sample = bool(sample_obj) and sample_obj.get('name') not in (None, '', 'White Noise')

        def check(key, want, how, raw=None, ctl=None):
            touched.add(key)
            got = state.get(key)
            if got is None:
                self.err(name, 'value', '%s missing in the .sbpreset' % key)
                return
            p = P[key]
            tol = 2e-4 * max(1.0, abs(want)) + 1e-4 * (float(p['max']) - float(p['min']))
            self.rows['checked'] += 1
            if abs(got - want) > tol:
                self.rows['mismatch'] += 1
                self.err(name, 'value', '%s: file %s (%r) -> expected %.9g (%s), written %.9g' % (key, ctl, raw, want, how, got))

        for ctl, key in self.by_control.items():
            if ctl not in S:
                continue
            c0 = cls.get(ctl, '').split(':')[0]
            p = P.get(key)
            if p is None:
                self.err(name, 'table', 'our key %s of the table is not in docs/params.json' % key)
                continue
            raw = float(S[ctl])
            mm = MOD_KEY.match(ctl)
            if mm and int(mm.group(1)) - 1 not in active_slots:
                continue  # unused slot: stays at the default (checked below with the untouched keys)
            d = self.details(ctl)
            if d is None:
                self.err(name, 'table', 'no Vital definition found for %s' % ctl)
                continue
            if c0 not in ('imported', 'approximated'):
                continue
            if sample_target and re.match(r'osc_%d_(on|level|pan|transpose|tune|random_phase|destination)$' % sample_target, ctl):
                continue  # the oscillator slot now plays the sample (checked with the sample rows below)
            want, how = self.expect(name, ctl, key, p, d, raw, S, state, frames, wt_is_custom, custom_sample)
            if want is None:
                continue
            check(key, want, how, raw, ctl)

        # specials
        self.audit_specials(name, S, state, content, sb, items, mods, active_slots, slot_dropped, slot_items, check, touched, cls, by_name, sample_target)

        # keys not addressed by the importer keep their default
        for key, p in P.items():
            if key in touched:
                continue
            got = state.get(key)
            if got is None:
                self.err(name, 'value', '%s missing in the .sbpreset' % key)
                continue
            if abs(got - defaults[key]) > 1e-6 * max(1.0, abs(defaults[key])):
                self.err(name, 'untouched', '%s = %.9g differs from its default %.9g although the importer should not touch it' % (key, got, defaults[key]))

        # ---- metadata, macro names (doc 11, 1.2.1) ---------------------------------------------------------------------
        self.audit_meta(name, vdoc, sb, stem)

        # ---- 3 engine -------------------------------------------------------------------------------------------------
        if rt is not None:
            self.audit_engine(name, S, rt, items, cls, by_name, len(active_slots), state)
        self.per_file.append(dict(file=name, counts=dict(cnt), report=report['counts'], fidelity=report['fidelity'], keys=len(S)))
        for k, v in cnt.items():
            self.class_totals[k] += v

    # ---------------------------------------------------------------------------------------------------------------
    def audit_meta(self, name, vdoc, sb, stem):
        meta = sb['meta']
        src = sb['import']['source']
        want_name = vdoc.get('preset_name') or os.path.splitext(name)[0]
        if meta.get('name') != want_name[:255]:
            self.err(name, 'meta', 'name %r, expected %r' % (meta.get('name'), want_name))
        if meta.get('author', '') != vdoc.get('author', ''):
            self.err(name, 'meta', 'author %r, expected %r' % (meta.get('author'), vdoc.get('author')))
        comments = vdoc.get('comments', '').replace('\r\n', ' ').replace('\n', ' ').replace('\r', ' ')
        got = meta.get('description', '')
        if not (got == comments or (got.endswith('\u2026') and comments.encode('utf-8').startswith(got[:-1].encode('utf-8')))):
            self.err(name, 'meta', 'description %r does not match the comments %r' % (got[:60], comments[:60]))
        style = vdoc.get('preset_style', '')
        cat = {'Bass': 'Bass', 'Lead': 'Lead', 'Pad': 'Pad', 'Keys': 'Keys', 'Percussion': 'Pluck', 'Sequence': 'Lead', 'Experimental': 'FX', 'SFX': 'FX'}.get(style, 'Lead')
        if meta.get('category') != cat:
            self.err(name, 'meta', 'category %r for the style %r, expected %r' % (meta.get('category'), style, cat))
        tags = meta.get('tags', '').split(',')
        sha = src.get('sha256', '')
        if 'imported' not in tags or 'vital' not in tags or ('src:' + sha[:12]) not in tags:
            self.err(name, 'meta', 'tags %r miss imported / vital / src:<sha>' % meta.get('tags'))
        macro_names = src.get('macro_names', [])
        for i in range(4):
            want = vdoc.get('macro%d' % (i + 1), '')
            if i >= len(macro_names) or macro_names[i] != want[:32]:
                self.err(name, 'meta', 'macro name %d %r, expected %r' % (i + 1, macro_names[i] if i < len(macro_names) else None, want))
        if src.get('version') != vdoc.get('synth_version'):
            self.err(name, 'meta', 'vital version %r, expected %r' % (src.get('version'), vdoc.get('synth_version')))

    def expect(self, name, ctl, key, p, d, raw, S, state, frames, wt_is_custom, custom_sample):
        # frame position and spread: a fraction of the frames of that oscillator's table (257 for a built-in)
        m = re.match(r'osc_(\d)_(wave_frame|frame_spread)$', ctl)
        if m:
            n = int(m.group(1))
            f = frames.get(n, 257) if wt_is_custom[n] else 257
            want = raw / max(1.0, f - 1.0) * 100.0
            return min(max(want, float(p['min'])), float(p['max'])), 'frame / (%d - 1) * 100' % f
        m = re.match(r'(osc_\d|sample)_destination$', ctl)
        if m:
            e = round_half_up(raw)
            want = 3.0 if 4 < e <= 14 else e
            return min(want, legacy_max(key) if legacy_max(key) is not None else want), 'destination (5..14 = send to one effect -> Effects only)'
        if ctl == 'sample_on':
            return (0.0 if custom_sample else round_half_up(raw)), 'noise on (a custom sample is not noise)'
        if re.match(r'osc_\d_distortion_type$', ctl):
            e = round_half_up(raw)
            mp = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 10: 9, 11: 10}  # Vital kFmSample (9) and kRmSample (12) have no counterpart
            return float(mp.get(e, 0)), 'warp type map'
        if ctl == 'portamento_time':
            return (0.0 if raw <= -9.9999 else min(2.0 ** raw * 1000.0, float(p['max']))), 'portamento (-10 = off)'
        try:
            want, how = expected_generic(d, p, raw)
        except ValueError as e:
            self.err(name, 'rule', '%s: %s' % (ctl, e))
            return None, ''
        lm = legacy_max(key)
        if lm is not None and want > lm:
            want = lm
            self.expect_dev['legacy_capped'] += 1
        want = min(max(want, float(p['min'])), float(p['max']))
        if key == 'master.volume':
            want = max(want, -60.0)
        return snap(p, want), how

    # ---------------------------------------------------------------------------------------------------------------
    def audit_specials(self, name, S, state, content, sb, items, mods, active, dropped, slot_items, check, touched, cls, by_name, sample_target):
        P = self.params
        # ---- pitch bend range, voice amplitude, filter routing, effect order -------------------------------------
        if 'pitch_bend_range' in S:
            check('master.bend_range', min(24.0, round_half_up(S['pitch_bend_range'])), 'bend range, our max 24', S['pitch_bend_range'], 'pitch_bend_range')
        else:
            touched.add('master.bend_range')
        if 'mpe_enabled' in S:
            check('master.mpe.on', 1.0 if S['mpe_enabled'] else 0.0, 'mpe_enabled', S['mpe_enabled'], 'mpe_enabled')
        else:
            touched.add('master.mpe.on')
        if 'stereo_routing' in S:
            check('master.stereo_routing', round(S['stereo_routing'] * 100.0, 3), 'stereo_routing', S['stereo_routing'], 'stereo_routing')
        else:
            touched.add('master.stereo_routing')
        if 'voice_override' in S:
            check('master.voice_override', round_half_up(S['voice_override']), 'voice_override', S['voice_override'], 'voice_override')
        else:
            touched.add('master.voice_override')
        for n in (1, 2, 3):
            k = 'osc_%d_transpose_quantize' % n
            pk = 'osc%d.transpose_snap' % n
            if k in S:
                check(pk, round_half_up(S[k]), k, S[k], k)
            else:
                touched.add(pk)
        a, b = S.get('filter_1_filter_input'), S.get('filter_2_filter_input')
        if a is not None or b is not None:
            f1, f2 = bool(a), bool(b)
            want = 0.0 if (f1 and f2) else (1.0 if f2 else (2.0 if f1 else 0.0))
            check('filter.routing', want, 'filter_N_filter_input (0,0)=parallel (0,1)=1->2 (1,0)=2->1', (a, b), 'filter_*_filter_input')
        else:
            touched.add('filter.routing')
        if 'effect_chain_order' in S:
            code = S['effect_chain_order']
            if float(code) != int(code) or not 0 <= code < math.factorial(9):
                want = list(range(9))
            else:
                want = decode_effect_order(code)
                alt = decode_by_enumeration(code)
                if want != alt:
                    self.err(name, 'order', 'the two independent decoders disagree for %r' % code)
            for i in range(9):
                check('fx.order.%d' % (i + 1), float(want[i]), 'effect_chain_order %r -> %s' % (code, want), code, 'effect_chain_order')
            # the 16 slots must stay a permutation (positions 10..16 keep 9..15)
            vals = sorted(state['fx.order.%d' % (i + 1)] for i in range(16))
            if vals != list(range(16)):
                self.err(name, 'order', 'fx.order.1..16 is not a permutation: %s' % [state['fx.order.%d' % (i + 1)] for i in range(16)])
        for i in range(1, 17):
            touched.add('fx.order.%d' % i) if i > 9 or 'effect_chain_order' in S else None
        if 'voice_amplitude' in S and abs(S['voice_amplitude'] - 1.0) > 1e-6:
            vol = state['master.volume']
            base = self.expect_volume(S)
            want = max(base + 20.0 * math.log10(max(S['voice_amplitude'], 1e-6)), -60.0)
            touched.add('master.volume')
            if abs(vol - want) > 0.01:
                self.err(name, 'value', 'master.volume with voice_amplitude %r: expected %.4g, written %.4g' % (S['voice_amplitude'], want, vol))

        # ---- oscillator wavetables and the sample --------------------------------------------------------------------
        wts = S.get('wavetables', [])
        for n in (1, 2, 3):
            key = 'osc%d.wavetable' % n
            touched.add(key)
            v = state[key]
            cw = [w for w in content.get('wavetables', []) if w['osc'] == n]
            if n - 1 < len(wts):
                if v == 13 and not cw:
                    self.err(name, 'content', '%s = Preset table but no content wavetable is stored' % key)
                if v != 13 and cw:
                    self.err(name, 'content', '%s is built-in %d but a content wavetable is stored' % (key, v))
                if v == 13 and cw:
                    want_frames = self.table_frames(wts[n - 1])
                    if want_frames is not None and cw[0]['frames'] != want_frames:
                        self.err(name, 'content', '%s: stored %d frames, the keyframes of the file give %d' % (key, cw[0]['frames'], want_frames))
            elif v != P[key]['default']:
                self.err(name, 'content', '%s changed without a table in the file' % key)
        lfos = S.get('lfos', [])
        for n in range(1, 11):
            key = 'lfo%d.shape' % n
            touched.add(key)
            v = state[key]
            cl = [s for s in content.get('lfo_shapes', []) if s['lfo'] == n]
            if n <= len(lfos):
                if v == 12 and not cl:
                    self.err(name, 'content', '%s = Custom but no content shape is stored' % key)
                if v != 12 and cl:
                    self.err(name, 'content', '%s is built-in %d but a custom shape is stored' % (key, v))
                if v == 12:
                    L = lfos[n - 1]
                    pts = L['points']
                    # Vital stores the line upside down relative to the LFO value (y = 1 - value)
                    want_pts = [(round(pts[2 * k], 4), round(1.0 - pts[2 * k + 1], 4), round(L['powers'][k], 3)) for k in range(min(L['num_points'], len(L['powers'])))]
                    got_pts = [(round(p['x'], 4), round(p['y'], 4), round(p['power'], 3)) for p in cl[0]['points']]
                    if want_pts != got_pts:
                        self.err(name, 'content', '%s points differ: file(1-y)=%s stored=%s' % (key, want_pts[:4], got_pts[:4]))
                    if bool(L.get('smooth')) != bool(cl[0].get('smooth')):
                        self.err(name, 'content', '%s smooth flag differs' % key)
            elif v != P[key]['default']:
                self.err(name, 'content', '%s changed without a shape in the file' % key)

        # ---- the sample oscillator -------------------------------------------------------------------------------
        so = [s for s in content.get('samples', [])]
        if so:
            n = so[0]['osc']
            touched.update(['osc%d.type' % n, 'osc%d.sample.keytrack' % n, 'osc%d.sample.loop_mode' % n])
            if state['osc%d.type' % n] != 1:
                self.err(name, 'sample', 'osc%d.type is not Sample although a sample is mapped to it' % n)
            if state['noise.on'] != 0:
                self.err(name, 'sample', 'noise.on stays on with a mapped sample')
            on = S.get('sample_on', 0)
            lvl = S.get('sample_level', 0.7071)
            for k, want in (('osc%d.on' % n, round_half_up(on)), ('osc%d.level' % n, lvl * lvl * 100.0), ('osc%d.pan' % n, S.get('sample_pan', 0.0) * 100.0),
                            ('osc%d.transpose' % n, round_half_up(S.get('sample_transpose', 0.0))), ('osc%d.tune' % n, S.get('sample_tune', 0.0) * 100.0),
                            ('osc%d.random_phase' % n, 100.0 if S.get('sample_random_phase', 0.0) else 0.0)):
                if not self.args.skip_sample_osc:
                    check(k, float(want), 'sample oscillator mapped to osc%d' % n, None, 'sample_*')
            dd = round_half_up(S.get('sample_destination', 3.0))
            check('osc%d.destination' % n, 3.0 if dd > 4 else float(dd), 'sample destination', dd, 'sample_destination')
            check('osc%d.sample.keytrack' % n, 1.0 if S.get('sample_keytrack', 1.0) else 0.0, 'sample keytrack', None, 'sample_keytrack')
            loop = S.get('sample_loop', 0.0)
            bounce = S.get('sample_bounce', 0.0)
            check('osc%d.sample.loop_mode' % n, 0.0 if not loop else (3.0 if bounce else 1.0), 'sample loop / bounce (0 one-shot, 1 forward, 3 ping-pong)', None, 'sample_loop')
            for k in ('osc%d.on' % n, 'osc%d.level' % n, 'osc%d.pan' % n, 'osc%d.transpose' % n, 'osc%d.tune' % n, 'osc%d.random_phase' % n, 'osc%d.destination' % n):
                touched.add(k)
        else:
            for n in (1, 2, 3):
                for k in ('osc%d.type' % n, 'osc%d.sample.keytrack' % n, 'osc%d.sample.loop_mode' % n):
                    if state[k] != P[k]['default']:
                        self.err(name, 'sample', '%s differs from its default without a sample' % k)
                    touched.add(k)

        # ---- the modulation matrix --------------------------------------------------------------------------------
        for i in range(64):
            nn = '%02d' % (i + 1)
            ks, kd = 'mod%s.source' % nn, 'mod%s.destination' % nn
            touched.update([ks, kd])
            if i in active:
                m = mods[i]
                src = self.mod_source_key(m['source'])
                dst = self.mod_dest_key(m['destination'], S, sample_target)
                if self.src_choice.get(src) is None:
                    self.err(name, 'mod', 'slot %d: source %r has no choice in our list but was imported' % (i + 1, m['source']))
                    continue
                if self.dst_choice.get(dst) is None:
                    self.err(name, 'mod', 'slot %d: destination %r (%s) has no choice in our list but was imported' % (i + 1, m['destination'], dst))
                    continue
                if state[ks] != self.src_choice[src]:
                    self.err(name, 'mod', 'slot %d source %r: expected choice %d (%s), written %d' % (i + 1, m['source'], self.src_choice[src], src, state[ks]))
                if state[kd] != self.dst_choice[dst]:
                    self.err(name, 'mod', 'slot %d destination %r: expected choice %d (%s), written %d' % (i + 1, m['destination'], self.dst_choice[dst], dst, state[kd]))
            else:
                if state[ks] != 0 or state[kd] != 0:
                    self.err(name, 'mod', 'slot %d is unused / dropped but source=%s destination=%s are set' % (i + 1, state[ks], state[kd]))
        # a dropped connection must have a reason that is true
        for i in dropped:
            m = mods[i]
            for it in slot_items[i]:
                if it['kind'] != 'dropped':
                    continue
                if it['detail'] == 'destination_not_modulatable':
                    dst = self.mod_dest_key(m['destination'], S)
                    if self.dst_choice.get(dst) is not None:
                        self.err(name, 'mod', 'slot %d dropped as not modulatable, but %r (%s) is in our destination list' % (i + 1, m['destination'], dst))
                if it['detail'] == 'source_not_exposed':
                    if self.src_choice.get(self.mod_source_key(m['source'])) is not None:
                        self.err(name, 'mod', 'slot %d dropped as unsupported source, but %r is in our source list' % (i + 1, m['source']))
        # the files' mod flags: every flag of an imported slot was checked as a table row; here: bypass semantics and stereo / bipolar exist
        for i in active:
            nn = i + 1
            for fld in ('amount', 'power', 'bipolar', 'stereo', 'bypass'):
                if 'modulation_%d_%s' % (nn, fld) not in S:
                    self.err(name, 'mod', 'slot %d has no %s in settings (default taken)' % (nn, fld))
        mod_curves = {mc['slot']: mc for mc in content.get('mod_curves', [])}
        for i in active:
            m = mods[i]
            if 'line_mapping' in m:
                mc = mod_curves.get(i + 1)
                if not mc:
                    self.err(name, 'content', 'slot %d has line_mapping in file but no mod_curves in preset' % (i + 1))
                else:
                    lm = m['line_mapping']
                    pts = lm['points']
                    want_pts = [(round(pts[2 * k], 4), round(1.0 - pts[2 * k + 1], 4), round(lm['powers'][k], 3)) for k in range(min(lm['num_points'], len(lm['powers'])))]
                    got_pts = [(round(p['x'], 4), round(p['y'], 4), round(p['power'], 3)) for p in mc['points']]
                    if want_pts != got_pts:
                        self.err(name, 'content', 'slot %d line_mapping points differ: file(1-y)=%s stored=%s' % (i + 1, want_pts[:4], got_pts[:4]))

    def expect_volume(self, S):
        raw = S.get('volume', 5473.0404)
        return max(min(math.sqrt(max(raw, 0.0)) - 80.0, 6.0), -60.0)

    def mod_source_key(self, s):
        m = re.match(r'(env|lfo)_(\d+)$', s)
        if m: return m.group(1) + m.group(2)
        m = re.match(r'random_(\d+)$', s)
        if m: return 'rlfo' + m.group(1)
        m = re.match(r'macro_control_(\d+)$', s)
        if m: return 'macro' + m.group(1)
        return s

    def mod_dest_key(self, dst, S, sample_target=0):
        if sample_target and dst in ('sample_level', 'sample_pan', 'sample_transpose', 'sample_tune'):
            dst = 'osc_%d_%s' % (sample_target, dst[len('sample_'):])
        if dst == 'stereo_routing':
            return 'master.stereo_routing'
        if dst in self.by_control:
            return self.by_control[dst]
        return dst

    def table_frames(self, wt):
        """Frames of a table = last keyframe position + 1 over all groups (Vital's render model); None if undetermined."""
        last = -1
        for g in wt.get('groups', []):
            for c in g.get('components', []):
                for k in c.get('keyframes', []):
                    last = max(last, int(round(k.get('position', 0))))
        return last + 1 if last >= 0 else None

    # ---------------------------------------------------------------------------------------------------------------
    def audit_engine(self, name, S, rt, items, cls, by_name, n_active, state):
        for mm in rt['param_mismatches']:
            self.err(name, 'engine', 'state round trip: %s set %s read back %s' % (mm['key'], mm['set'], mm['got']))
        if rt['connected_slots'] != n_active:
            self.err(name, 'engine', 'the engine wired %d modulation slots, the file has %d importable connections' % (rt['connected_slots'], n_active))
        approx_names = {it['vital_name'] for it in items if it['kind'] == 'approximated'}
        for ctl, got in rt['controls'].items():
            if ctl not in S:
                continue
            raw = float(S[ctl])
            if got is None:
                self.err(name, 'engine', 'control %s does not exist in the engine' % ctl)
                continue
            d = self.details(ctl)
            rng = (d['max'] - d['min']) if d and d['max'] is not None and d['min'] is not None else 1.0
            mm = MOD_KEY.match(ctl)
            if mm and (cls.get(ctl, '').startswith('ignored') or cls.get(ctl, '').startswith('dropped')):
                continue  # the engine has no connection in that slot
            if ctl == 'pitch_bend_range':
                continue  # the engine takes the bend range from master.bend_range (its own control stays 0, see vital_engine.cpp)
            tol = 1e-3 * max(1.0, abs(raw)) + 2e-3 * rng
            if abs(got - raw) <= tol:
                self.rt_ok += 1
                continue
            if ctl in approx_names:
                self.rt_expl['approximated by the importer (clamped / remapped)'] += 1
                continue
            if re.match(r'osc_\d_(wave_frame|frame_spread)$', ctl):
                self.err(name, 'engine', 'control %s: file %.6g, engine %.6g' % (ctl, raw, got))
                continue
            if ctl in ('effect_chain_order',):
                self.rt_expl['effect_chain_order (the engine keeps the control for the import only; the chain takes its order from fx.order.1..16, checked above)'] += 1
                continue
            if ctl == 'sample_on':
                self.rt_expl['sample_on with a custom sample (noise switched off, sample mapped to an oscillator)'] += 1
                continue
            if re.match(r'(sample|osc_\d)_(on|level|pan|transpose|tune|random_phase|destination)$', ctl) and any(
                    s for s in (state.get('osc1.type'), state.get('osc2.type'), state.get('osc3.type'))):
                self.rt_expl['sample controls moved onto the oscillator that plays the sample'] += 1
                continue
            self.err(name, 'engine', 'control %s: file %.9g, engine %.9g' % (ctl, raw, got))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--vital', action='append', required=True)
    ap.add_argument('--imported', required=True)
    ap.add_argument('--roundtrip')
    ap.add_argument('--json')
    ap.add_argument('--skip-sample-osc', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()
    a = Audit(args)
    ok = a.run()

    print('files audited: %d' % len(a.per_file))
    tot = collections.Counter()
    for f in a.per_file:
        print('%-46s keys %4d  %s' % (f['file'], f['keys'], ' '.join('%s=%d' % (k, v) for k, v in sorted(f['counts'].items()))))
    print('\nclass totals over all files:')
    for k, v in sorted(a.class_totals.items()):
        print('  %-48s %6d' % (k, v))
    for c0 in ('dropped', 'ignored'):
        print('\n%s by key class (normalised), reason:' % c0)
        for (kname, detail), n in sorted(a.ignored_classes[c0].items(), key=lambda x: (-x[1], x[0])):
            print('  %5d  %-48s %s' % (n, kname, detail))
    print('\nvalue rows checked: %d, mismatches: %d' % (a.rows['checked'], a.rows['mismatch']))
    print('expected deviations noted: %s' % dict(a.expect_dev))
    if args.roundtrip:
        print('engine control read-back equal to the file: %d; explained deviations: %s' % (a.rt_ok, dict(a.rt_expl)))
    byarea = collections.Counter(e[1] for e in a.errors)
    print('\nERRORS: %d  %s' % (len(a.errors), dict(byarea)))
    seen = collections.Counter()
    for p, area, text in a.errors:
        key = (area, re.sub(r'\d+(\.\d+)?', '#', text)[:90])
        seen[key] += 1
        if args.verbose or seen[key] <= 3:
            print('  [%s] %s: %s' % (p, area, text))
    if args.json:
        json.dump(dict(per_file=a.per_file, totals=a.class_totals, errors=a.errors, rows=a.rows), open(args.json, 'w'), indent=1, default=lambda x: dict(x))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
