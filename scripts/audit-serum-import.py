#!/usr/bin/env python3
"""Independent audit of the Serum 2 importer (docs/ui-design/WP21-REPORT.md).

    python3 -I scripts/audit-serum-import.py --serum DIR --imported DIR [--content-dir DIR] [--json OUT.json] [--selftest] [-v]

  --serum        the Serum content folder (the real files; read only): every .SerumPreset below Presets/ is audited
  --imported     output directory of `synthyboy_serum_import --in <serum>/Presets --out DIR` (<name>.sbpreset + <name>.report.json, same folder layout)
  --content-dir  the user content directory the importer wrote to (default IMPORTED/content): the stored tables are compared with the embedded data
  --selftest     mutate one imported preset in memory in several ways and require the audit to find every mutation (the audit itself is tested)

Python 3 standard library only. Nothing is imported from the importer and no C++ conversion table is read. The audit decodes the original files with the
REFERENCE zstd decoder (`zstd -d`, a subprocess) and its own CBOR reader, and takes ranges, choice lists, the modulation source and destination lists and the
effect pool layout from docs/params.json (our parameter table). The conversion of every parameter is stated here again from docs/ui-design/13-new-keys.md 4.26.

Checks
  1 completeness  leaves of the decoded tree == report.leaves; imported + approximated + dropped + ignored == leaves (unaccounted 0); no entry claims more leaves
                  than the node it names holds.
  2 correctness   for every report entry with a Serum path and one of our keys: the expected value is recomputed from the decoded Serum value with the table of
                  this script, clamped and snapped like our table, and compared with the value in the .sbpreset (the effect parameters through the normalisation
                  of the pool slot).
  3 stray writes  every key of the .sbpreset that differs from its default is explained by an entry, by a Serum default rule, or by a named family.
  4 content       the stored wavetable of an embedded table equals the embedded floats (16 bit); the stored sample and impulse exist and hash to their reference.
  5 matrix        source and destination choices of the imported slots equal the lists of docs/params.json.
Exit code 0 when every check passes, 1 otherwise.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------------------------------------------------------------------------------
# the decoder (reference zstd + an own CBOR reader)
# ---------------------------------------------------------------------------------------------------------------------------------------
def load_serum(path):
    d = open(path, 'rb').read()
    assert d[:9] == b'XferJson\0'
    n = struct.unpack('<Q', d[9:17])[0]
    header = json.loads(d[17:17 + n].rstrip(b'\0'))
    rest = d[17 + n:]
    usz, fmt = struct.unpack('<II', rest[:8])
    assert fmt == 2
    body = subprocess.run(['zstd', '-d', '-c', '-q'], input=rest[8:], capture_output=True).stdout
    assert len(body) == usz, 'zstd: %d bytes, header says %d' % (len(body), usz)
    value, end = cbor(body, 0)
    assert end == len(body)
    return header, value


def cbor(b, i):
    ib = b[i]
    i += 1
    mt, ai = ib >> 5, ib & 31

    def arg():
        nonlocal i
        if ai < 24:
            return ai
        size = {24: 1, 25: 2, 26: 4, 27: 8}[ai]
        v = int.from_bytes(b[i:i + size], 'big')
        i += size
        return v
    if mt == 0:
        return arg(), i
    if mt == 1:
        return -1 - arg(), i
    if mt in (2, 3):
        n = arg()
        raw = bytes(b[i:i + n])
        return (raw if mt == 2 else raw.decode()), i + n
    if mt == 4:
        n = arg()
        out = []
        for _ in range(n):
            v, i = cbor(b, i)
            out.append(v)
        return out, i
    if mt == 5:
        n = arg()
        out = {}
        for _ in range(n):
            k, i = cbor(b, i)
            v, i = cbor(b, i)
            out[k] = v
        return out, i
    if mt == 6:
        arg()
        return cbor(b, i)
    if ai == 20:
        return False, i
    if ai == 21:
        return True, i
    if ai in (22, 23):
        return None, i
    if ai == 25:
        return struct.unpack('>e', b[i:i + 2])[0], i + 2
    if ai == 26:
        return struct.unpack('>f', b[i:i + 4])[0], i + 4
    if ai == 27:
        return struct.unpack('>d', b[i:i + 8])[0], i + 8
    raise ValueError('cbor %d/%d' % (mt, ai))


def leaves(x):
    if isinstance(x, dict):
        return sum(leaves(y) for y in x.values())
    if isinstance(x, list):
        return sum(leaves(y) for y in x)
    return 1


def resolve(tree, path):
    cur = tree
    for part in path.split('/'):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
            cur = cur[int(part)]
        else:
            return None, False
    return cur, True


# ---------------------------------------------------------------------------------------------------------------------------------------
# our parameter table
# ---------------------------------------------------------------------------------------------------------------------------------------
class Table:
    def __init__(self):
        d = json.load(open(os.path.join(REPO, 'docs/params.json')))
        self.params = d['params']
        self.by_key = {p['key']: p for p in self.params}
        self.mod_sources = [m['key'] if isinstance(m, dict) else m for m in d['modSources']]
        # the destination choices (core/include/synthyboy/modulation.h): the modulatable keys in table order, but the 64 mod amounts, master.portamento.time and
        # master.polyphony sit behind the last key of block B20 (master.voice_override), in that order; the keys of later blocks continue after them
        special = ['mod%02d.amount' % (s + 1) for s in range(64)] + ['master.portamento.time', 'master.polyphony']
        self.mod_dest = []
        for p in self.params:
            if p.get('modulatable') and p['key'] not in special:
                self.mod_dest.append(p['key'])
            if p['key'] == 'master.voice_override':
                self.mod_dest.extend(special)

    def clamp(self, key, v):
        p = self.by_key[key]
        v = min(p['max'], max(p['min'], v))
        iv = p.get('interval', 0)
        if iv:
            v = p['min'] + round((v - p['min']) / iv) * iv
            v = min(p['max'], max(p['min'], v))
        return v

    def normalise(self, key, v):
        p = self.by_key[key]
        prop = min(1.0, max(0.0, (v - p['min']) / (p['max'] - p['min'])))
        if p['scale'] == 'exp_skew' and p.get('skew', 1) != 1:
            return prop ** p['skew']
        return prop

    def choice_index(self, key, name):
        return self.by_key[key]['choices'].index(name)

    def pool_own_keys(self, prefix):
        keys = [p['key'] for p in self.params if p['key'].startswith(prefix + '.') and not p['key'].endswith(('.on', '.rack', '.force_enable'))]
        return keys[:24]


T = Table()
POOL_PREFIX = {'FXChorus': 'chorus', 'FXComp': 'compressor', 'FXDelay': 'delay', 'FXDistortion': 'distortion', 'FXEQ': 'eq', 'FXFilter': 'fxfilter', 'FXFlanger': 'flanger',
               'FXPhaser': 'phaser', 'FXReverb': 'reverb', 'FXHyperD': 'hyperdim', 'FXUtils': 'utility', 'FXBode': 'bode', 'FXConv': 'convolve'}


def note_hz(x):
    return 440.0 * 2.0 ** ((135.0 * x - 69.0) / 12.0)


def db(x):
    return -120.0 if x <= 1e-6 else 20.0 * math.log10(x)


def level_out(x):
    """A507: the level knob of a filter or an effect (kParamLevelOut) is the gain (2 v) ^ 2 in Serum 2.1.5: default 0.5 = unity, 1 = +12.04 dB."""
    return -120.0 if x <= 1e-6 else 40.0 * math.log10(2.0 * x)


# The laws measured against the real Serum 2.1.5 (tools/ref-compare, docs/ui-design/13-new-keys.md 4.46, A488 .. A496), stated here again from that table:
def interp(xs, ys, v):
    if v <= xs[0]:
        return ys[0]
    if v >= xs[-1]:
        return ys[-1]
    i = 0
    while i + 2 < len(xs) and v > xs[i + 1]:
        i += 1
    return ys[i] + (ys[i + 1] - ys[i]) * (v - xs[i]) / (xs[i + 1] - xs[i])


_FILTER_LAWS = None


def filter_reso_level(typ, freq, reso):
    """A501: the level difference (Serum - ours, dB) of a filter type by cutoff and resonance, bilinear on the measured grid (docs/ui-design/refcompare-filter-laws.json)."""
    global _FILTER_LAWS
    if _FILTER_LAWS is None:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'ui-design', 'refcompare-filter-laws.json')) as f:
            _FILTER_LAWS = json.load(f)
    grid = _FILTER_LAWS['types'].get(typ)
    if grid is None:
        return 0.0
    fs, rs = _FILTER_LAWS['freqs'], _FILTER_LAWS['resonances']
    edge = 1.0 if freq >= fs[0] else max(0.0, (freq - 0.2) / (fs[0] - 0.2))   # below the grid the correction fades out to 0 at the cutoff 0.2
    f = min(fs[-1], max(fs[0], freq))
    r = min(rs[-1], max(rs[0], reso))
    fi = 0
    while fi < len(fs) - 2 and f > fs[fi + 1]:
        fi += 1
    ri = 0
    while ri < len(rs) - 2 and r > rs[ri + 1]:
        ri += 1
    tf, tr = (f - fs[fi]) / (fs[fi + 1] - fs[fi]), (r - rs[ri]) / (rs[ri + 1] - rs[ri])
    a = grid[fi][ri] + (grid[fi][ri + 1] - grid[fi][ri]) * tr
    b = grid[fi + 1][ri] + (grid[fi + 1][ri + 1] - grid[fi + 1][ri]) * tr
    return (a + (b - a) * tf) * edge


def filter_drive_level(typ, drive, reso):
    """A501: the level the drive adds on top (docs/ui-design/refcompare-filter-laws.json, 'drive'), bilinear."""
    filter_reso_level(typ, 0.5, 0.0)   # loads the table
    d = _FILTER_LAWS.get('drive', {})
    grid = d.get('types', {}).get(typ)
    if grid is None:
        return 0.0
    ds, rs = d['drives'], d['resonances']
    x = min(ds[-1], max(ds[0], drive))
    r = min(rs[-1], max(rs[0], reso))
    di = 0
    while di < len(ds) - 2 and x > ds[di + 1]:
        di += 1
    ri = 0
    while ri < len(rs) - 2 and r > rs[ri + 1]:
        ri += 1
    td, tr = (x - ds[di]) / (ds[di + 1] - ds[di]), (r - rs[ri]) / (rs[ri + 1] - rs[ri])
    a = grid[di][ri] + (grid[di][ri + 1] - grid[di][ri]) * tr
    b = grid[di + 1][ri] + (grid[di + 1][ri + 1] - grid[di + 1][ri]) * tr
    return a + (b - a) * td


def note_hz_voice_filter(x):
    return 440.0 * 2.0 ** ((135.0 * x - 69.0 + 2.0) / 12.0)


_CURVE_V = [0, 10, 30, 50, 66.6, 80, 95, 100]
_CURVES = {(True, False): [-7.0, -5.0, -1.5, 2.5, 7.8, 12.0, 14.0, 14.5], (False, False): [13.0, 12.0, 8.5, 2.5, -1.0, -3.0, -8.0, -9.5],
           (True, True): [-11.0, -9.0, -4.5, 0.5, 4.0, 8.0, 12.0, 13.0], (False, True): [12.0, 9.0, 5.0, 0.0, -3.5, -9.5, -15.0, -17.0]}


def env_curve(attack):
    def f(v, n=1):
        return interp(_CURVE_V, _CURVES[(attack, n > 1)], v)
    f.n_aware = True
    return f


def filter_drive(v):
    return interp([0, 10, 25, 50, 75, 100], [0.0, 0.0, 0.0, 2.0, 6.0, 12.0], v)


def filter_drive_makeup(v):
    return interp([0, 10, 25, 50, 75, 100], [0.0, 3.3, 7.7, 10.7, 11.0, 10.2], v)


COMP_T = [0.275, 0.3, 0.325, 0.35, 0.375, 0.4, 0.425, 0.45, 0.475, 0.5, 0.525, 0.55, 0.575, 0.6, 0.625, 0.65, 0.675, 0.7, 0.725, 0.75, 0.775, 0.8, 0.825, 0.85, 0.875, 0.9, 0.9125,
          0.925, 0.9375, 0.95, 0.96, 0.9625, 0.97, 0.975, 0.98, 0.9875, 0.99, 0.995]
COMP_DB = [5.31, 4.4, 3.45, 2.47, 1.45, 0.39, -0.72, -1.88, -3.09, -4.36, -5.7, -7.11, -8.59, -10.17, -11.86, -13.65, -15.58, -17.67, -19.94, -22.42, -25.17, -28.24, -31.71, -35.73,
           -40.48, -46.29, -49.76, -53.77, -58.51, -64.29, -70.04, -71.69, -77.35, -81.88, -87.21, -96.89, -100.28, -105.27]


def comp_threshold(v):
    """A514: the peak ceiling in dB of the Serum compressor threshold (compthr2.py: the output peak of the limiter of a saw with the master volume 1.0, whose +2.98 dB are taken off)."""
    if v >= 1.0:
        return -120.0
    if v <= COMP_T[0]:
        return min(12.0, 10.60 + 59.73 * math.log10(max(1e-6, 1.0 - max(0.0, v))))
    if v >= COMP_T[-1]:
        return max(-120.0, COMP_DB[-1] - 2.98 + (-120.0 - (COMP_DB[-1] - 2.98)) * (v - COMP_T[-1]) / (1.0 - COMP_T[-1]))
    return interp(COMP_T, [x - 2.98 for x in COMP_DB], v)


# ---------------------------------------------------------------------------------------------------------------------------------------
# the conversions (13-new-keys.md 4.26), one table per scope: parameter -> (key pattern, function or enumeration)
# ---------------------------------------------------------------------------------------------------------------------------------------
def lin(a, b=0.0):
    return lambda v: v * a + b


def boolean(v):
    return 1.0 if v >= 0.5 else 0.0


WARP = {'kNoWarp': 0, 'kSync': 1, 'kQuantize': 3, 'kPWM': 6, 'kBendPos': 12, 'kBendNeg': 13, 'kBendPosNeg': 14, 'kASYMPos': 15, 'kASYMNeg': 16, 'kASYMPosNeg': 17, 'kFlip': 18,
        'kDLM': 19, 'kRemap_1': 20, 'kRemap_2': 21, 'kRemap_3': 22, 'kRemap_4': 23, 'kEvenOdd': 24, 'kFilterLPF': 25, 'kFilterHPF': 26, 'kDistTube': 27, 'kDistSoftClip': 28,
        'kDistHardClip': 29, 'kDistDiode1': 30, 'kDistDiode2': 31, 'kDistLinFold': 32, 'kDistSinFold': 33, 'kDistZeroSquare': 34, 'kDistAsym': 35, 'kDistRectify': 36,
        'kDistSineShaper': 37, 'kDistStompBox': 38, 'kDistTapeSat': 39, 'kDistSoftSat': 40, 'kFM_OSC': 41, 'kFM_OSC2': 42, 'kFMX_OSC': 43, 'kFMX_OSC2': 44, 'kFMP_OSC': 45,
        'kFMP_OSC2': 46, 'kPD_OSC': 47, 'kPD_OSC2': 48, 'kSelfPD': 49, 'kAM_OSC': 50, 'kAM_OSC2': 51, 'kRM_OSC': 9, 'kRM_OSC2': 10}
WARP.update({'kSpectralComb': 76, 'kAddharmonics': 77, 'kAddsubharmonics': 78, 'kDetune': 79, 'kShepardNarrow': 80, 'kShepardFilter': 81, 'kPeakOctaveDown': 82, 'kPeakOctaveUp': 83,
             'kPeakHarmDown': 84, 'kPeakHarmUp': 85, 'kGate': 86, 'kSpectralPitchShift': 87, 'kSpectralPhaseTwist': 88, 'kSpread': 89, 'kSpectralShift': 90, 'kSmear': 91, 'kMirror': 92,
             'kMask_OSC': 93, 'kMask_OSC2': 94, 'kMask_NOISE': 95, 'kVocode_OSC': 96, 'kVocode_OSC2': 97, 'kVocode_NOISE': 98})   # GROUPC WP-12: the spectral warps
for _i, _fam in enumerate(('kFM', 'kFMX', 'kFMP', 'kPD', 'kAM', 'kRM')):   # GROUPB WP-06: the modes from the noise, sub and filters, entries 52 .. 75
    for _j, _src in enumerate(('NOISE', 'SUB', 'FILT1', 'FILT2')):
        WARP['%s_%s' % (_fam, _src)] = 52 + 4 * _i + _j
STACK = {'kNoUnisonStack': 0, 'kCenter12': 1, 'kCenter24': 2, 'kOctave1': 3, 'kOctave2': 4, 'kOctave3': 4, 'kOctaveFifth1': 5, 'kOctaveFifth2': 6, 'kOctaveFifth3': 6}
DETUNE = {'kDetuneLinear': 0, 'kDetuneSuper': 1, 'kDetuneExp': 2, 'kDetuneInv': 3, 'kDetuneRandom': 4}
LOOP = {'kNone': 0, 'kForward': 1, 'kReverse': 2, 'kPingPong': 3, 'kTailed': 4}
SLICING = {'Off': 0, 'Auto': 1, 'Manual': 2}
PITCHMODE = {'Semitones': 0, 'Harmonics': 1, 'Ratio': 2}
PHASEMEM = {'kAll': 0, 'kContiguous': 1, 'kPerVoice': 2}
SUBSHAPE = {'kSine': 0, 'kRoundRect': 1, 'kTriangle': 2, 'kSaw': 3, 'kSquare': 4, 'kPulse': 5}
DISTORTION = {'kTube': 6, 'kSoftClip': 0, 'kHardClip': 1, 'kDiode1': 7, 'kDiode2': 8, 'kLinFold': 2, 'kSinFold': 3, 'kZeroSquare': 9, 'kDownsample': 5, 'kAsym': 10,
              'kRectify': 11, 'kXShaper': 12, 'kXShaperAsym': 13, 'kSineShaper': 3, 'kStompBox': 6, 'kTapeSat': 0, 'kOverdrive': 0, 'kSoftSat': 0}
# GROUPC WP-11 (A412 .. A418): the granular oscillator (13-new-keys.md 4.37)
GRAIN_WINDOW = {'kWindowHann': 0, 'kWindowWelch': 1, 'kWindowGaussian': 2, 'kWindowBlackmanHarris': 3, 'kWindowSinc': 4, 'kWindowTukey': 5, 'kWindowTriangle': 6, 'kWindowTrapezoid': 7,
                'kWindowExpDec': 8, 'kWindowExpDecRev': 9, 'kWindowExpDecReverse': 9}
SPAWN = {'kTogether': 0, 'kEven': 1, 'kExponential': 2, 'kRandom': 3}
YAXIS = {'kYAxisNone': 0, 'kYAxisOscVolume': 1, 'kYAxisWarp': 2, 'kYAxisWarp2': 3, 'kYAxisDensity': 4, 'kYAxisGrainLength': 5, 'kYAxisWindowAmount': 6, 'kYAxisWindowSkew': 7,
         'kYAxisRandomOffset': 8, 'kYAxisRandomDir': 9, 'kYAxisRandomPitch': 10, 'kYAxisRandomGrainLength': 11, 'kYAxisRandomPan': 12, 'kYAxisRandomGain': 13,
         'kYAxisRandomWindowAmount': 14, 'kYAxisRandomWindowSkew': 15, 'kYAxisRandomWarp': 16, 'kYAxisRandomWarp2': 17}
REVERB = {'kPlate': 1, 'kHall': 2, 'kVintage': 3, 'kSpace': 4, 'kAbyss': 5}

OSC = {
    'kParamVolume': ('osc#.level', lin(100)), 'kParamPan': ('osc#.pan', lin(2)), 'kParamFine': ('osc#.tune', lin(1)), 'kParamCoarsePit': ('osc#.pitch_coarse', lin(1)),
    'kParamUnison': ('osc#.unison.voices', lin(1)), 'kParamDetune': ('osc#.unison.detune', lambda v: 100.0 * max(0.0, v) ** 1.25), 'kParamDetuneWid': ('osc#.unison.blend', lin(1)),
    'kParamUnisonStereo': ('osc#.unison.spread', lin(1)), 'kParamUnisonRange': ('osc#.unison.range', lin(1)), 'kParamUnisonStack': ('osc#.unison.stack', STACK),
    'kParamDetuneMode': ('osc#.unison.mode', DETUNE), 'kParamPitchMode': ('osc#.pitch_mode', PITCHMODE), 'kParamPitchTrack': ('osc#.pitch_track', boolean),
    'kParamPitchBendTrack': ('osc#.pitch_bend_track', boolean), 'kParamKeyZoneMin': ('osc#.map.key_low', lin(1)), 'kParamKeyZoneMax': ('osc#.map.key_high', lin(1)),
    'kParamVelocityZoneMin': ('osc#.map.vel_low', lin(1)), 'kParamVelocityZoneMax': ('osc#.map.vel_high', lin(1)), 'kParamKeyZoneWarp': ('osc#.map.key_warp', boolean),
    'kParamKeyZoneFold': ('osc#.map.key_fold', boolean), 'kParamUnisonWarp': ('osc#.warp.spread', lin(1)), 'kParamUnisonWarp2': ('osc#.warp2.spread', lin(1)),
    'kParamStart': ('osc#.sample.start', lin(1)), 'kParamEnd': ('osc#.sample.end', lin(1)), 'kParamLoopStart': ('osc#.sample.loop_start', lin(1)),
    'kParamLoopEnd': ('osc#.sample.loop_end', lin(1)), 'kParamLoopMode': ('osc#.sample.loop_mode', LOOP), 'kParamReverse': ('osc#.sample.reverse', boolean),
    'kParamLinkLoopLength': ('osc#.sample.loop_link', boolean), 'kParamLoopEndsAtRelease': ('osc#.sample.loop_exit', boolean), 'kParamRandomStart': ('osc#.random_phase', lin(1)),
    'kParamUnisonSpan': ('osc#.sample.uni_span', lin(1)), 'kParamSlicingEnabled': ('osc#.sample.slice_mode', SLICING),
    'kParamAutoSliceThreshold': ('osc#.sample.slice_threshold', lin(100)), 'kParamSlicingRootNote': ('osc#.sample.slice_root', lin(1)),
    'kParamScanTempoLock': ('osc#.sample.scan_lock_tempo', boolean)}
WARPKEYS = {'kParamWarp': ('osc#.warp.amount', lin(100)), 'kParamWarp2': ('osc#.warp2.amount', lin(100)), 'kParamWarpVar': ('osc#.warp.phase', lin(360)),
            'kParamWarpVar2': ('osc#.warp2.phase', lin(360)), 'kParamWarpMenu': ('osc#.warp.type', WARP), 'kParamWarpMenu2': ('osc#.warp2.type', WARP)}
GRAN = {'kParamWindowParam': ('osc#.gran.window_amount', lin(1)), 'kParamWindowSkew': ('osc#.gran.window_skew', lin(1)), 'kParamWindowShape': ('osc#.gran.window_shape', GRAIN_WINDOW),
        'kParamUnisonTrigPattern': ('osc#.gran.spawn_pattern', SPAWN), 'kParamYAxisAssignment': ('osc#.gran.y_axis', YAXIS), 'kParamRandomOffset': ('osc#.gran.rand_offset', lin(1)),
        'kParamRandomDir': ('osc#.gran.rand_dir', lin(1)), 'kParamRandomPitch': ('osc#.gran.rand_pitch', lin(1)), 'kParamRandomGrainLength': ('osc#.gran.rand_length', lin(1)),
        'kParamRandomPan': ('osc#.gran.rand_pan', lin(1)), 'kParamRandomGain': ('osc#.gran.rand_level', lin(1)), 'kParamRandomWindowAmount': ('osc#.gran.rand_window', lin(1)),
        'kParamRandomWindowSkew': ('osc#.gran.rand_skew', lin(1)), 'kParamRandomWarp': ('osc#.gran.rand_warp', lin(1)), 'kParamRandomWarp2': ('osc#.gran.rand_warp2', lin(1))}
SPECTRAL = {'kParamFreqLo': ('osc#.spec.freq_lo', lin(1)), 'kParamFreqHi': ('osc#.spec.freq_hi', lin(1)), 'kParamLoHiIsSmooth': ('osc#.spec.lohi_smooth', boolean),
            'kParamLoHiPostWarp': ('osc#.spec.lohi_post_warp', boolean), 'kParamPhaseLock': ('osc#.spec.phase_lock', boolean), 'kParamTransients': ('osc#.spec.transients', boolean),
            'kParamSpecFltShift': ('osc#.spec.cut', lin(1)), 'kParamSpecFltWetDry': ('osc#.spec.mix', lin(1))}
MULTI = {'kParamRandomPhase': ('osc#.random_phase', lin(1)), 'kParamEnvOverride': ('osc#.multi.override', boolean), 'kParamEnvDelay': ('osc#.multi.delay', lin(1000)),
         'kParamEnvAttack': ('osc#.multi.attack', lin(1000)), 'kParamEnvHold': ('osc#.multi.hold', lin(1000)), 'kParamEnvDecay': ('osc#.multi.decay', lin(1000)),
         'kParamEnvSustain': ('osc#.multi.sustain', lin(100)), 'kParamEnvRelease': ('osc#.multi.release', lin(1000)), 'kParamVelTrack': ('osc#.multi.vel_track', lin(1)),
         'kParamVelTrackOverride': ('osc#.multi.vel_override', boolean), 'kParamTimbreShift': ('osc#.multi.timbre', lin(100))}   # GROUPC WP-13
WT = {'kParamInitialPhase': ('osc#.phase', lin(1)), 'kParamRandomPhase': ('osc#.random_phase', lin(1)), 'kParamUnisonWTPos': ('osc#.frame_spread', lin(1)),
      'kParamPhaseMemory': ('osc#.phase_memory', PHASEMEM)}
NOISE = {'kParamVolume': ('noise.level', lin(105.7)), 'kParamPan': ('noise.pan', lin(2)), 'kParamEnable': ('noise.on', boolean), 'kParamPitch': ('noise.pitch', lin(1))}
NOISEOSC = {'kParamFine': ('noise.fine', lin(100)), 'kParamInitialPhase': ('noise.phase', lin(1)), 'kParamRandomPhase': ('noise.random_phase', boolean),
            'kParamOneShot': ('noise.loop', lin(-1, 1)), 'kParamColor': ('noise.filter', lin(200, -100))}
SUB = {'kParamVolume': ('sub.level', lin(65.7)), 'kParamPan': ('sub.pan', lin(2)), 'kParamEnable': ('sub.on', boolean), 'kParamOctave': ('sub.octave', lin(1))}
SUBOSC = {'kParamShape': ('sub.waveform', SUBSHAPE), 'kParamInitialPhase': ('sub.phase', lin(100.0 / 360.0)), 'kParamContiguousPhase': ('sub.phase_contiguous', boolean)}
FILTER = {'kParamEnable': ('filter#.on', boolean), 'kParamFreq': ('filter#.cutoff', note_hz_voice_filter), 'kParamReso': ('filter#.resonance', lin(1)), 'kParamDrive': ('filter#.drive', filter_drive),
          'kParamVar': ('filter#.var', lin(1)), 'kParamWet': ('filter#.mix', lin(1)), 'kParamKeyTrack': ('filter#.keytrack', lin(100)), 'kParamLevelOut': ('filter#.level', level_out),
          'kParamStereo': ('filter#.pan', lin(2, -100)), 'kParamPad': ('filter#.clean_mode', boolean), 'kParamX': ('filter#.formant.x', lin(100)), 'kParamY': ('filter#.formant.y', lin(100))}
ENV = {'kParamAttack': ('env#.attack', lin(1000)), 'kParamHold': ('env#.hold', lin(1000)), 'kParamDecay': ('env#.decay', lin(1000)), 'kParamSustain': ('env#.sustain', lin(100)),
       'kParamRelease': ('env#.release', lin(1000)), 'kParamCurve1': ('env#.attack_curve', env_curve(True)), 'kParamCurve2': ('env#.decay_curve', env_curve(False)),
       'kParamCurve3': ('env#.release_curve', env_curve(False)), 'kParamLegatoInverted': ('env#.legato_inverted', boolean)}
MACRO = {'kParamValue': ('macro.#', lin(1))}
GLOBAL = {'kParamMasterVolume': ('master.volume', lambda v: db(v) + 2.98), 'kParamVoiceAmp': ('master.volume', db), 'kParamPolyCount': ('master.polyphony', lin(1)), 'kParamBendRangeUp': ('master.bend_range', lin(1)),
          'kParamLegato': ('master.legato', boolean), 'kParamPortamentoTime': ('master.portamento.time', lin(1000)), 'kParamPortamentoCurve': ('master.portamento.slope', lin(0.08)),
          'kParamPortaAlways': ('master.portamento.always', boolean), 'kParamPortaScaled': ('master.portamento.scaled', boolean), 'kParamTranspose': ('master.transpose', lin(1)),
          'kParamGlobalTuning': ('master.tuning.a4', lin(1)), 'kParamDirectVol': ('direct.level', lambda v: db(v) + 6.0206), 'kParamFXBus1Vol': ('bus1.level', lambda v: db(v) + 6.0206), 'kParamFXBus2Vol': ('bus2.level', lambda v: db(v) + 6.0206),
          'kParamFXBus1Dest': ('bus1.destination', lin(1)), 'kParamFXBus2Dest': ('bus2.destination', lin(1))}


def logp(a, b):
    return lambda v: a * (b / a) ** (v / 100.0)


def logpi(a, b):
    return lambda v: b * (a / b) ** (v / 100.0)


def logpct(a, b):
    return lambda v: 0.0 if v <= a else 100.0 * math.log(v / a) / math.log(b / a)


def ratio_pct(v):
    return 0.0 if v <= 1 else 100.0 * (1 - 1 / v)


FX = {
    'FXChorus': {'kParamWet': ('chorus.mix', lin(1)), 'kParamRate': ('chorus.rate', lin(1)), 'kParamDelay': ('chorus.delay1', lin(1)), 'kParamDelay2': ('chorus.delay2', lin(1)),
                 'kParamDepth': ('chorus.depth', lin(4)), 'kParamFeedback': ('chorus.feedback', lin(1)), 'kParamFilt': ('chorus.filter_cutoff', lin(1)), 'kParamLevelOut': ('chorus.level', level_out)},
    'FXComp': {'kParamWet': ('compressor.mix', lin(1)), 'kParamAttack': ('compressor.attack', logpct(0.1, 1000)), 'kParamRelease': ('compressor.release', logpct(0.1, 1000)),
               'kParamMultiband': ('compressor.bands', lin(-3, 3)), 'kParamThresh': ('compressor.mid.upper_threshold', comp_threshold), 'kParamRatio': ('compressor.mid.upper_ratio', ratio_pct),
               'kParamMakeup': ('compressor.mid.gain', db), 'kParamXoverLow': ('compressor.crossover_low', lin(1)), 'kParamXoverHi': ('compressor.crossover_high', lin(1)),
               'kParamLevelOut': ('compressor.level', level_out)},
    'FXDelay': {'kParamWet': ('delay.mix', lin(1)), 'kParamTimeL': ('delay.time', lin(1000)), 'kParamTimeR': ('delay.time2', lin(1000)), 'kParamFeedback': ('delay.feedback', lin(1)),
                'kParamFreq': ('delay.filter_cutoff', lin(1)), 'kParamBW': ('delay.filter_spread', lin(12.12)), 'kParamMode': ('delay.style', lin(1, 1)), 'kParamHQ': ('delay.hq', boolean),
                'kParamOffsetL': ('delay.offset_left', lin(1)), 'kParamOffsetR': ('delay.offset', lin(1)), 'kParamLevelOut': ('delay.level', level_out)},
    'FXDistortion': {'kParamWet': ('distortion.mix', lin(1)), 'kParamDrive': ('distortion.drive', lin(0.3)), 'kParamMode': ('distortion.type', DISTORTION),
                     'kParamFreq': ('distortion.filter_cutoff', note_hz), 'kParamLPHP': ('distortion.filter_blend', lin(1)), 'kParamPrePost': ('distortion.filter_order', lin(1)),
                     'kParamLevelOut': ('distortion.level', level_out)},
    'FXFilter': {'kParamWet': ('fxfilter.mix', lin(1)), 'kParamFreq': ('fxfilter.cutoff', note_hz), 'kParamReso': ('fxfilter.resonance', lin(1)), 'kParamDrive': ('fxfilter.drive', lin(0.2)),
                 'kParamVar': ('fxfilter.var', lin(1)), 'kParamStereo': ('fxfilter.pan', lin(2, -100)), 'kParamLevelOut': ('fxfilter.level', level_out), 'kParamPad': ('fxfilter.clean_mode', boolean),
                 'kParamX': ('fxfilter.formant.x', lin(100)), 'kParamY': ('fxfilter.formant.y', lin(100))},
    'FXFlanger': {'kParamWet': ('flanger.mix', lin(1)), 'kParamRate': ('flanger.rate', lin(1)), 'kParamDepth': ('flanger.depth', lin(1)), 'kParamFeedback': ('flanger.feedback', lin(1)),
                  'kParamWidth': ('flanger.phase_offset', lin(1)), 'kParamLevelOut': ('flanger.level', level_out)},
    'FXPhaser': {'kParamWet': ('phaser.mix', lin(1)), 'kParamRate': ('phaser.rate', lin(1)), 'kParamDepth': ('phaser.depth', lin(0.48)), 'kParamFeedback': ('phaser.feedback', lin(1)),
                 'kParamFreq': ('phaser.center', lin(1)), 'kParamWidth': ('phaser.phase_offset', lin(1)), 'kParamLevelOut': ('phaser.level', level_out)},
    'FXReverb': {'kParamWet': ('reverb.mix', lin(1)), 'kParamType': ('reverb.type', REVERB), 'kParamSize': ('reverb.size', lin(1)), 'kParamPreDelay': ('reverb.predelay', lin(1000)),
                 'kParamDelay': ('reverb.predelay', lin(1)), 'kParamFeedback': ('reverb.decay', logp(20, 30000)), 'kParamFreq': ('reverb.pre_low_cut', logp(20, 12000)),
                 'kParamFreqB': ('reverb.pre_high_cut', logpi(20, 12000)), 'kParamWidth': ('reverb.width', lin(1)), 'kParamLevelOut': ('reverb.level', level_out)},
    'FXHyperD': {'kParamWet': ('hyperdim.mix', lin(1)), 'kParamRate': ('hyperdim.rate', lin(0.1)), 'kParamDetune': ('hyperdim.detune', lin(1)), 'kParamUnison': ('hyperdim.unison', lin(1)),
                 'kParamRetrig': ('hyperdim.retrig', boolean), 'kParamDimESize': ('hyperdim.dim_size', lin(1)), 'kParamDimEWet': ('hyperdim.dim_mix', lin(1)), 'kParamLevelOut': ('hyperdim.level', level_out)},
    'FXUtils': {'kParamWet': ('utility.mix', lin(1)), 'kParamWidth': ('utility.width', lin(1)), 'kParamBalance': ('utility.pan', lin(1)), 'kParamHPF': ('utility.hpf', lin(1)),
                'kParamLPF': ('utility.lpf', lin(1)), 'kParamLFMono': ('utility.mono_bass', boolean), 'kParamLFXover': ('utility.mono_freq', lin(1)), 'kParamPolarityL': ('utility.polarity_l', boolean),
                'kParamPolarityR': ('utility.polarity_r', boolean), 'kParamLevelOut': ('utility.level', level_out)},
    'FXBode': {'kParamWet': ('bode.mix', lin(1)), 'kParamRange': ('bode.range', lin(1)), 'kParamBlur': ('bode.blur', lin(1)), 'kParamFeedback': ('bode.feed', lin(1)),
               'kParamDelayTime': ('bode.delay', lin(1000)), 'kParamDelayBalance': ('bode.balance', lin(1)), 'kParamOutputWidth': ('bode.width', lin(1)), 'kParamRetrig': ('bode.retrig', boolean),
               'kParamMonoInput': ('bode.mono_input', boolean), 'kParamLevelOut': ('bode.level', level_out)},
    'FXEQ': {},
    'FXConv': {'kParamWet': ('convolve.mix', lin(1)), 'kParamSize': ('convolve.size', lin(0.1)), 'kParamTone': ('convolve.tone', lin(1)), 'kParamMinPhase': ('convolve.phi_min', boolean),
               'kParamPredelay': ('convolve.predelay', lin(1000)), 'kParamAttack': ('convolve.attack', lin(1000)), 'kParamDecay': ('convolve.decay', lin(2.5)),
               'kParamDamping': ('convolve.damp', lin(1)), 'kParamIpTrim': ('convolve.ir_gain', lin(1)), 'kParamLevelOut': ('convolve.level', level_out)},
}


POOL_TYPES = ['chorus', 'compressor', 'delay', 'distortion', 'eq', 'fxfilter', 'flanger', 'phaser', 'reverb', 'hyperdim', 'utility', 'bode', 'convolve']  # fxiNN.type 1 ..


def db_keys():
    keys = set()
    tables = [OSC, WARPKEYS, WT, NOISE, NOISEOSC, SUB, SUBOSC, FILTER, ENV, MACRO, GLOBAL] + list(FX.values())
    for t in tables:
        for key, fn in t.values():
            if fn is db or fn is level_out:
                keys.add(key)
    for n in (1, 2):
        keys.add('filter%d.level' % n)
    for band in ('low', 'mid', 'high'):
        keys.add('compressor.%s.upper_threshold' % band)
        keys.add('compressor.%s.gain' % band)
    return keys


DB_KEYS = db_keys()


def source_key(sb, key):
    """a pool parameter fxiNN.pKK -> the key of the table it stands for."""
    m = re.match(r'fxi(\d\d)\.p(\d\d)$', key)
    if not m:
        return key
    t = int(sb['state']['params'].get('fxi%s.type' % m.group(1), 0))
    if t < 1:
        return key
    own = T.pool_own_keys(POOL_TYPES[t - 1])
    return own[int(m.group(2)) - 1] if int(m.group(2)) - 1 < len(own) else key


def expected(fn, v, n=1):
    if isinstance(fn, dict):
        return float(fn[v]) if isinstance(v, str) and v in fn else None
    if isinstance(v, bool):
        v = float(v)
    if not isinstance(v, (int, float)):
        return None
    if getattr(fn, 'n_aware', False):
        return float(fn(float(v), n))
    return float(fn(float(v)))


# ---------------------------------------------------------------------------------------------------------------------------------------
# the effect modules of REFCOMPARE round 3 (A508 .. A513), stated again from docs/ui-design/13-new-keys.md 4.48 and the measured data of docs/ui-design/refcompare-fx-laws.json
# ---------------------------------------------------------------------------------------------------------------------------------------
FX_DEFAULTS = {
    'FXChorus': {'kParamDelay': 5.0, 'kParamDelay2': 0.0, 'kParamDepth': 26.458, 'kParamFeedback': 9.5, 'kParamFilt': 1000.0, 'kParamLevelOut': 0.5, 'kParamRate': 0.078125, 'kParamWet': 50.0},
    'FXComp': {'kParamAttack': 90.13846, 'kParamRelease': 90.08452, 'kParamThresh': 0.5, 'kParamRatio': 4.0, 'kParamMakeup': 1.0, 'kParamWet': 100.0, 'kParamMultiband': 0.0, 'kParamLevelOut': 0.5,
               'kParamXoverLow': 128.3, 'kParamXoverHi': 2500.0},
    'FXDelay': {'kParamBW': 6.74923, 'kParamFeedback': 40.0, 'kParamFreq': 848.52814, 'kParamHQ': 1.0, 'kParamLevelOut': 0.5, 'kParamMode': 0.0, 'kParamOffsetL': 1.0, 'kParamOffsetR': 1.0,
                'kParamTimeL': 0.06236, 'kParamTimeR': 0.07642, 'kParamWet': 30.0},
    'FXDistortion': {'kParamDrive': 25.0, 'kParamPrePost': 0.0, 'kParamWet': 100.0, 'kParamLevelOut': 0.5},
    'FXFilter': {'kParamDrive': 0.0, 'kParamReso': 0.0, 'kParamFreq': 0.5, 'kParamWet': 100.0, 'kParamLevelOut': 0.5, 'kParamPad': 0.0, 'kParamStereo': 50.9},
    'FXFlanger': {'kParamDepth': 100.0, 'kParamFeedback': 50.0, 'kParamLevelOut': 0.5, 'kParamRate': 0.078125, 'kParamWet': 100.0, 'kParamWidth': 180.0},
    'FXPhaser': {'kParamDepth': 50.0, 'kParamFeedback': 80.0, 'kParamFreq': 600.0, 'kParamLevelOut': 0.5, 'kParamRate': 0.078125, 'kParamWet': 100.0, 'kParamWidth': 180.0},
    'FXHyperD': {'kParamDetune': 25.0, 'kParamLevelOut': 0.5, 'kParamRate': 40.0, 'kParamRetrig': 0.0, 'kParamUnison': 4.0, 'kParamWet': 50.0},
    'FXUtils': {'kParamBalance': 0.0, 'kParamLPF': 20000.0, 'kParamLevelOut': 0.5, 'kParamWet': 100.0},
    'FXBode': {'kParamBlur': 0.0, 'kParamFeedback': 0.0, 'kParamLevelOut': 0.5, 'kParamWet': 100.0},
    'FXConv': {'kParamLevelOut': 0.5, 'kParamWet': 20.0},
    'FXReverb': {'kParamSize': 35.0, 'kParamWet': 20.0, 'kParamFeedback': 25.0, 'kParamFreqB': 35.0, 'kParamWidth': 20.0, 'kParamPreDelay': 0.0, 'kParamLevelOut': 0.5},
}
_FXLAWS = None
FX_WET_GAIN = {'FXChorus': -4.8, 'FXDelay': 0.0, 'FXFlanger': 1.5, 'FXPhaser': -1.2, 'FXFilter': 1.3, 'FXBode': 1.5}
FX_MIX_TYPE = ['FXChorus', 'FXComp', 'FXDelay', 'FXDistortion', 'FXEQ', 'FXFilter', 'FXFlanger', 'FXPhaser', 'FXReverb', 'FXHyperD', 'FXUtils', 'FXBode', 'FXConv']


def fx_laws():
    global _FXLAWS
    if _FXLAWS is None:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'ui-design', 'refcompare-fx-laws.json')) as f:
            _FXLAWS = json.load(f)
    return _FXLAWS


def distortion_law(mode, drive):
    rows = fx_laws()['distortion'].get(mode)
    if rows is None:
        return None
    pts = []
    for r in rows:
        ok = r['err'] <= 2.5
        pts.append((r['d'], round(r['k'] if ok else 0.3 * r['d'], 1), round(max(-60.0, min(24.0, r['gain'] if ok else r['id_gain'])), 1)))   # the table of the importer has one decimal
    d = max(0.0, min(100.0, drive))
    i = 0
    while i + 2 < len(pts) and d > pts[i + 1][0]:
        i += 1
    t = (d - pts[i][0]) / (pts[i + 1][0] - pts[i][0])
    return pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t, pts[i][2] + (pts[i + 1][2] - pts[i][2]) * t


def mix_remap(fxname, w, gain_db):
    """the mix knob (0 .. 1) and the extra level (dB) of our module for the Serum Wet knob w and a wet signal gain_db stronger than ours (None: no law of the module)."""
    law = fx_laws().get('mix', {}).get(fxname)
    if law is None:
        return None
    s, o = law['serum'], law['ours']
    x = max(0.0, min(1.0, w)) * 20.0
    i = min(19, int(x))
    f = x - i
    dt = s[i][1] + (s[i + 1][1] - s[i][1]) * f
    et = (s[i][2] + (s[i + 1][2] - s[i][2]) * f) * 10.0 ** (gain_db / 20.0)
    ang = math.atan2(et, dt)
    k = 0
    while k < 19 and math.atan2(o[k + 1][2], o[k + 1][1]) < ang:
        k += 1
    a0, a1 = math.atan2(o[k][2], o[k][1]), math.atan2(o[k + 1][2], o[k + 1][1])
    t = max(0.0, min(1.0, (ang - a0) / (a1 - a0))) if a1 > a0 else 0.0
    d_o, e_o = o[k][1] + (o[k + 1][1] - o[k][1]) * t, o[k][2] + (o[k + 1][2] - o[k][2]) * t
    length = math.hypot(d_o, e_o)
    return (o[k][0] + (o[k + 1][0] - o[k][0]) * t) * 0.01, (20.0 * math.log10(max(1e-6, math.hypot(dt, et) / length)) if length > 1e-6 else 0.0)


def fx_expected(fxname, pp):
    """the plain values of the keys of the pool slot of one effect module (a dict key -> value; keys the audit does not model are missing)."""
    rows = FX[fxname]
    vals = {}
    given = set()

    def has(param):
        return param in pp

    def num(param, default):
        v = pp.get(param, default)
        return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else default

    typ = pp.get('kParamType')
    for param, node in pp.items():
        row = rows.get(param)
        if not row:
            continue
        key, fn = row
        if fxname == 'FXReverb' and param == 'kParamFeedback' and typ in ('kSpace', 'kAbyss'):
            key, fn = 'reverb.feedback', lin(1)
        if fxname == 'FXEQ':
            continue
        ex = expected(fn, node)
        if ex is None:
            continue
        if fxname == 'FXComp' and param in ('kParamThresh', 'kParamRatio', 'kParamMakeup'):
            for band in ('low', 'mid', 'high'):
                vals[key.replace('.mid.', '.%s.' % band)] = ex
        else:
            vals[key] = ex
        given.add(key)
    # the Serum defaults of the parameters the file leaves out (a key that another parameter of the file gives is not overwritten)
    for param, dv in FX_DEFAULTS.get(fxname, {}).items():
        if has(param):
            continue
        row = rows.get(param)
        if not row:
            continue
        key, fn = row
        if fxname == 'FXReverb' and param == 'kParamFeedback' and typ in ('kSpace', 'kAbyss'):
            key, fn = 'reverb.feedback', lin(1)
            ex = dv
        else:
            ex = expected(fn, dv)
        if ex is None or key in given:
            continue
        if fxname == 'FXComp' and param in ('kParamThresh', 'kParamRatio', 'kParamMakeup'):
            for band in ('low', 'mid', 'high'):
                vals[key.replace('.mid.', '.%s.' % band)] = ex
        else:
            vals[key] = ex
    # ---- the compressor
    if fxname == 'FXComp':
        mb = num('kParamMultiband', 0.0) >= 0.5
        ratio = num('kParamRatio', 4.0)
        limit = ratio >= 1.0e5
        vals['compressor.limit'] = 1.0 if (limit and not mb) else 2.0
        mk = num('kParamMakeup', 1.0)
        if limit and not mb:
            for band in ('low', 'mid', 'high'):
                vals['compressor.%s.gain' % band] = 36.0 * math.log(max(1.0, 2.0 * mk - 1.0)) / math.log(61.0)
        if mb:
            knee_ud = [-51.0, -33.6, -20.3, -7.9, 4.5]
            ud_def = [72.533, 100.0, 100.0]
            t_at = [0.0, 0.3, 0.42, 0.5, 0.6, 0.7, 0.8, 1.0]
            shift_db = [30.0, 22.0, 9.1, 0.0, -15.1, -27.8, -41.0, -70.0]
            thresh = max(0.0, min(1.0, num('kParamThresh', 0.5)))
            shift = interp(t_at, shift_db, thresh)
            gr = max(1.0, ratio)
            strength = 0.78 if gr >= 1.0e5 else min(0.8, 1.0 - 1.0 / gr)
            for b, band in enumerate(('low', 'mid', 'high')):
                ud = max(0.0, min(200.0, num('kParamThreshUD%d' % b, ud_def[b])))
                knee = interp([0.0, 50.0, 100.0, 150.0, 200.0], knee_ud, ud) + shift
                r = max(0.0, min(1.0, num('kParamRatio%d' % b, 1.0)))
                vals['compressor.%s.upper_threshold' % band] = max(-120.0, knee)
                vals['compressor.%s.upper_ratio' % band] = 100.0 * strength * r ** 0.9
                vals['compressor.%s.gain' % band] = 20.0 * math.log10(max(1.0, mk)) + num('kParamGain%d' % b, 0.0)
    # ---- the equalizer: low = shelf | high pass, mid = peak, mid 2 = second peak, high = shelf | low pass; the Serum law
    if fxname == 'FXEQ':
        t = [int(num('kParamType1', 0.0)), int(num('kParamType2', 0.0))]
        groups, peaks = [], 0
        for b in range(2):
            if t[b] == 1:
                groups.append('mid' if peaks == 0 else 'mid2')
                peaks += 1
            else:
                groups.append('low' if b == 0 else 'high')
        vals['eq.law'] = 1.0
        for b in range(2):
            g = groups[b]
            n = str(b + 1)
            vals['eq.%s.cutoff' % g] = num('kParamFreq' + n, 209.6 if b == 0 else 2040.7)
            vals['eq.%s.resonance' % g] = num('kParamReso' + n, 60.0)
            vals['eq.%s.gain' % g] = num('kParamGain' + n, 0.0)
            vals['eq.%s.mode' % g] = 1.0 if g == 'mid2' else 0.0 if g == 'mid' else (1.0 if t[b] == 2 else 0.0)
    # ---- the delay
    if fxname == 'FXDelay':
        sync = num('kParamBeatSync', 1.0) >= 0.5
        if not sync:
            vals['delay.sync'] = 0.0
        else:
            steps = [(0.0023, 0.2, -1), (0.0062, 15.5, -1), (0.015, 57.6, -1), (0.0322, 125.0, 6), (0.062, 250.0, 5), (0.110, 500.0, 4), (0.179, 1000.0, 3), (0.2775, 2000.0, 2), (1e9, 4000.0, 1)]
            sl = next(s for s in steps if num('kParamTimeL', 0.06236) < s[0])
            sr = next(s for s in steps if num('kParamTimeR', 0.07642) < s[0])
            if sl[2] >= 0 and sr[2] >= 0:
                vals['delay.sync'] = 1.0
                vals['delay.division'] = float(sl[2])
                vals['delay.division2'] = float(sr[2])
            else:
                vals['delay.sync'] = 0.0
                vals['delay.time'] = sl[1]
                vals['delay.time2'] = sr[1]
    # ---- the reverb decay
    if fxname == 'FXReverb':
        tn = typ if isinstance(typ, str) else 'kPlate'
        sec = None
        if tn == 'kPlate':
            sec = interp([0, 8, 10, 15, 18, 20, 25, 28, 30, 35, 40, 50, 60, 70, 80], [0.36, 0.30, 0.48, 0.46, 0.77, 1.0, 1.6, 2.1, 2.2, 3.5, 4.6, 8.4, 10.6, 11.1, 12.0], num('kParamSize', 35.0))
        elif tn == 'kHall':
            sec = 3.2
        elif tn == 'kVintage':
            sec = interp([0, 50, 100], [0.93, 2.04, 3.9], num('kParamVintageScale', 0.0))
        if sec is not None:
            vals['reverb.decay'] = sec * 1000.0
    # ---- the wet signal: the mix and the level of our module so that the dry and the wet gain of the Serum law come out
    gain = 0.0
    if fxname == 'FXDistortion':
        mode = typ if False else pp.get('kParamMode', 'kTube')
        law = distortion_law(mode if isinstance(mode, str) else 'kTube', num('kParamDrive', 25.0))
        if law is not None:
            vals['distortion.drive'] = law[0]
            gain = law[1]
    elif fxname == 'FXHyperD':
        u = max(0.0, min(7.0, num('kParamUnison', 4.0)))
        hg = [float(x) for x in fx_laws()['hyper'].values()] if False else [round(fx_laws()['hyper'][str(n)], 1) for n in range(8)]
        i = min(6, int(u))
        gain = hg[i] + (hg[i + 1] - hg[i]) * (u - i)
        if u < 0.5:
            vals['hyperdim.mix'] = 0.0
    elif fxname == 'FXReverb':
        gain = {'kHall': -6.7, 'kVintage': -5.0, 'kSpace': -13.0, 'kAbyss': 4.5}.get(typ if isinstance(typ, str) else 'kPlate', 0.8)
    else:
        gain = FX_WET_GAIN.get(fxname, 0.0)
    prefix = POOL_PREFIX[fxname]
    mixkey, levelkey = prefix + '.mix', prefix + '.level'
    wetp = FX[fxname].get('kParamWet')
    levp = FX[fxname].get('kParamLevelOut')
    if wetp and levp and not (fxname == 'FXHyperD' and num('kParamUnison', 4.0) < 0.5):
        w = max(0.0, min(1.0, num('kParamWet', FX_DEFAULTS.get(fxname, {}).get('kParamWet', 100.0)) * 0.01))
        r = mix_remap(fxname, w, gain)
        if r is not None or abs(gain) > 0.05:
            if r is None:
                rr = 10.0 ** (gain / 20.0)
                big = (1.0 - w) + w * rr
                r = (w * rr / big, 20.0 * math.log10(big))
            vals[mixkey] = 100.0 * r[0]
            vals[levelkey] = level_out(num('kParamLevelOut', 0.5)) + r[1]
    return vals


# ---------------------------------------------------------------------------------------------------------------------------------------
class Audit:
    def __init__(self, verbose):
        self.verbose = verbose
        self.errors = []
        self.stats = collections.Counter()
        self.fx_done = set()

    def err(self, name, msg):
        self.errors.append('%s: %s' % (name, msg))

    def classify(self, path):
        """path of a leaf -> (rule table, key pattern index n, FX info) or None."""
        parts = path.split('/')
        m = re.match(r'(Oscillator|VoiceFilter|Env|Macro|Global|LFO|RoutingSlot|FXRack)(\d*)$', parts[0])
        if not m:
            return None
        mod, idx = m.group(1), int(m.group(2) or 0)
        if mod == 'Oscillator':
            if len(parts) == 3 and parts[1] == 'plainParams':
                if idx <= 2:
                    return OSC, idx + 1, parts[2]
                return (NOISE if idx == 3 else SUB), 1, parts[2]
            if len(parts) == 4 and parts[2] == 'plainParams':
                blk = parts[1]
                if idx <= 2 and blk.startswith(('WTOsc', 'SampleOsc', 'MultiSampleOsc', 'GranularOsc', 'SpectralOsc')):
                    table = dict(WARPKEYS)
                    if blk.startswith('WTOsc'):
                        table.update(WT)
                    if blk.startswith('GranularOsc'):
                        table.update(GRAN)
                    if blk.startswith('SpectralOsc'):
                        table.update(SPECTRAL)
                    if blk.startswith('MultiSampleOsc'):
                        table.update(MULTI)
                    return table, idx + 1, parts[3]
                if blk.startswith('NoiseOsc') and idx == 3:
                    return NOISEOSC, 1, parts[3]
                if blk.startswith('SubOsc') and idx == 4:
                    return SUBOSC, 1, parts[3]
            return None
        if len(parts) == 3 and parts[1] == 'plainParams':
            return {'VoiceFilter': FILTER, 'Env': ENV, 'Macro': MACRO, 'Global': GLOBAL}.get(mod), (1 if mod == 'Global' else idx + 1), parts[2]
        return None

    def check_value(self, name, p, sb, key, exp, how):
        if key not in T.by_key:
            self.err(name, 'unknown key %s (%s)' % (key, how))
            return
        want = T.clamp(key, exp)
        got = sb['state']['params'].get(key)
        if got is None:
            self.err(name, '%s missing in the preset' % key)
            return
        self.stats['values checked'] += 1
        if abs(got - want) > 2e-4 * max(1.0, abs(want)) + 1e-6:
            self.err(name, '%s: expected %.6g (from %s) got %.6g' % (key, want, how, got))

    def fx_slot_value(self, name, sb, slot, number, key, plain):
        want = T.normalise(key, T.clamp(key, plain))
        got = sb['state']['params'].get('fxi%02d.p%02d' % (slot, number + 1))
        self.stats['fx values checked'] += 1
        if got is None or abs(got - want) > 2e-4:
            self.err(name, 'fxi%02d.p%02d (%s): expected %.6g got %s' % (slot, number + 1, key, want, got))

    def audit_file(self, name, serum_path, report, sb, content_dir):
        header, tree = load_serum(serum_path)
        total = leaves(tree)
        # 1 completeness
        if report['leaves'] != total:
            self.err(name, 'leaves: tree %d, report %d' % (total, report['leaves']))
        c = report['counts']
        if c['imported'] + c['approximated'] + c['dropped'] + c['ignored'] != total:
            self.err(name, 'classes add up to %d, tree has %d' % (c['imported'] + c['approximated'] + c['dropped'] + c['ignored'], total))
        if report['unaccounted'] != 0:
            self.err(name, 'unaccounted %d' % report['unaccounted'])
        if report['truncated']:
            self.err(name, 'report truncated')
        explained = set()
        fx_slot = 0
        fx_slots = {}
        conv_instance0 = False
        for r in range(3):
            lst = tree.get('FXRack%d' % r, {}).get('FX', [])
            for j, e in enumerate(lst):
                fxname = next((k for k, v in e.items() if k.startswith('FX') and isinstance(v, dict)), None)
                if fxname in ('FXSplit', 'FXSplit3', 'FXSplitMS') or fxname not in POOL_PREFIX:
                    continue
                if fxname == 'FXConv' and not conv_instance0 and ('embeddedIR' in e[fxname] or 'relativePathToIR' in e[fxname]):
                    conv_instance0 = True
                    continue
                fx_slot += 1
                fx_slots[(r, j)] = fx_slot
        # 2 correctness
        combos = collections.defaultdict(list)
        for e in report['entries']:
            self.stats['entries'] += 1
            if e['count'] > 0 and e['path']:
                node, ok = resolve(tree, e['path'])
                if ok and e['count'] > leaves(node):
                    self.err(name, 'entry %s claims %d leaves, node holds %d' % (e['path'], e['count'], leaves(node)))
            if e['kind'] not in ('imported', 'approximated') or not e['key'] or not e['path']:
                continue
            explained.add(e['key'])
            if e['key'].endswith('.model') and e['key'].startswith('filter'):
                explained.add(e['key'][:-6] + '.style')
            node, ok = resolve(tree, e['path'])
            if not ok:
                continue
            if e['path'].startswith('FXRack') and '/plainParams/' in e['path']:
                self.audit_fx_entry(name, e, node, tree, sb, fx_slots)
                continue
            info = self.classify(e['path'])
            if not info or info[0] is None:
                continue
            table, n, param = info
            row = table.get(param)
            if not row:
                continue
            key = row[0].replace('#', str(n))
            if key != e['key']:
                if param in ('kParamPitch', 'kParamOctave'):
                    pass
                else:
                    self.err(name, '%s: importer wrote %s, expected %s' % (e['path'], e['key'], key))
                continue
            exp = expected(row[1], node, n)
            if exp is None:
                self.err(name, '%s: no expected value for %r' % (e['path'], node))
                continue
            if key in combos:
                continue
            exp = self.override(tree, key, param, e['path'], exp)
            if exp is None:
                continue
            combos[key].append(e['path'])
            self.check_value(name, e['path'], sb, key, exp, e['path'])
        for e in report['entries']:
            m = re.match(r'ModSlot(\d+)/plainParams/(kParam\w+)$', e['path'])
            if m and e['kind'] in ('imported', 'approximated') and e['key']:
                k = int(m.group(1)) + 1
                node, ok = resolve(tree, e['path'])
                spec = {'kParamAmount': ('amount', lin(1)), 'kParamBipolar': ('bipolar', lin(1)), 'kParamBypass': ('bypass', lin(1)), 'kParamAuxInverted': ('aux_inv', lin(1)),
                        'kParamCurveIn': ('power', lin(0.1)), 'kParamAuxCurve': ('aux_power', lin(0.1)), 'kParamOut': ('aux_output', lin(1))}.get(m.group(2))
                if ok and spec:
                    key = 'mod%02d.%s' % (k, spec[0])
                    if key != e['key']:
                        self.err(name, '%s: importer wrote %s, expected %s' % (e['path'], e['key'], key))
                    else:
                        want = spec[1](float(node))
                        if spec[0] == 'amount':
                            # a destination whose Serum value is a linear gain (a dB value here) gets its depth limited to 24 dB down / 12 dB up
                            dk = next((x['ours'] for x in report['entries'] if x['path'] == 'ModSlot%d/destModule*' % (k - 1)), '')
                            sk = source_key(sb, dk)
                            if sk in DB_KEYS and sk in T.by_key:
                                rng = T.by_key[sk]['max'] - T.by_key[sk]['min']
                                want = want * min(1.0, (24.0 if want < 0 else 12.0) / rng)
                        self.check_value(name, e['path'], sb, key, want, e['path'])
        # transposition: octave * 12 + semitones
        for i in range(3):
            o = tree.get('Oscillator%d' % i, {}).get('plainParams')
            if isinstance(o, dict) and ('kParamOctave' in o or 'kParamPitch' in o):
                self.check_value(name, 'Oscillator%d' % i, sb, 'osc%d.transpose' % (i + 1), o.get('kParamOctave', 0) * 12 + o.get('kParamPitch', 0), 'octave * 12 + pitch')
                explained.add('osc%d.transpose' % (i + 1))
        # 5 matrix
        for e in report['entries']:
            if e['path'].endswith('/source') and e['key'].startswith('mod') and e['kind'] in ('imported', 'approximated'):
                slot = int(e['key'][3:5])
                src, ok = resolve(tree, e['path'])
                self.check_source(name, sb, slot, src, e['key'] + '', aux=False)
            if e['key'].endswith('.destination') and e['key'].startswith('mod') and e['path'].endswith('/destModule*'):
                slot = int(e['key'][3:5])
                dkey = e['ours']
                if dkey not in T.mod_dest:
                    self.err(name, 'mod%02d: destination %s is not modulatable' % (slot, dkey))
                else:
                    want = 1 + T.mod_dest.index(dkey)
                    got = sb['state']['params'].get('mod%02d.destination' % slot)
                    self.stats['mod destinations'] += 1
                    if got != want:
                        self.err(name, 'mod%02d.destination: expected %d (%s) got %s' % (slot, want, dkey, got))
        # 3 stray writes
        params = sb['state']['params']
        fx_prefixes = tuple('fxi%02d.' % s for s in fx_slots.values())
        mod_prefixes = tuple('mod%02d.' % (int(e['key'][3:5])) for e in report['entries'] if e['key'].startswith('mod') and len(e['key']) > 5)
        strays = []
        for k, v in params.items():
            if k not in T.by_key:
                continue
            if abs(v - T.by_key[k]['default']) < 1e-9:
                continue
            if k in explained or k.startswith(fx_prefixes) or k.startswith(mod_prefixes):
                continue
            if re.match(r'(fx\.order\.\d+|splitter_(lh|lmh|ms)\.(on|rack|freq|freq_low|freq_high|low_bypass|mid_bypass|high_bypass|high_bypass)|osc\d\.sample\.(root|scan_range|scan)|osc\d\.gran\.(density_mode|density|density_sync|density_grains|length_mode|length|length_sync|length_percent|manual|position)|osc\d\.spec\.(manual|position|freq_shift|mix|cut)|osc\d\.tune|master\.polyphony|osc\d\.pitch_coarse)$', k):
                continue
            if self.default_rule(k, tree, v):
                continue
            strays.append(k)
        for k in strays:
            self.err(name, 'unexplained non-default key %s = %s' % (k, params[k]))
        self.stats['strays'] += len(strays)
        self.audit_content(name, tree, sb, content_dir)
        self.audit_noise(name, tree, sb)
        self.audit_osc_extras(name, tree, sb)
        self.audit_granular(name, tree, sb)
        self.audit_spectral(name, tree, sb)
        self.audit_multisample(name, tree, sb, content_dir)
        self.audit_mod_slew(name, tree, sb)
        self.audit_remainder_item4(name, tree, sb, report)
        self.audit_voice_panel(name, tree, sb)
        self.audit_midi_map(name, tree, report)

    VOICE_ROWS = {'Pan': 'pan', 'Detune': 'detune', 'FilterCutoff': 'cutoff', 'EnvTime': 'env', 'Mod1': 'mod1', 'Mod2': 'mod2'}
    VOICE_OTHER = {'kParamVoiceCount': 'voicectl.length', 'kParamGlobalRandomOscPan': 'voicectl.rand.pan', 'kParamGlobalRandomOscDetune': 'voicectl.rand.detune',
                   'kParamGlobalRandomOscDetune10x': 'voicectl.rand.detune10x', 'kParamGlobalRandomFilterCutoff': 'voicectl.rand.cutoff',
                   'kParamGlobalRandomEnvTime': 'voicectl.rand.env', 'kParamGlobalScalingEnvTime': 'voicectl.scale.env', 'kParamGlobalScalingLfoTime': 'voicectl.scale.lfo',
                   'kParamGlobalScalingLfoTimeSnap': 'voicectl.scale.lfo_mode', 'kParamOscA': 'voicectl.osc.a', 'kParamOscB': 'voicectl.osc.b', 'kParamOscC': 'voicectl.osc.c',
                   'kParamOscN': 'voicectl.osc.noise', 'kParamOscS': 'voicectl.osc.sub'}

    def audit_voice_panel(self, name, tree, sb):
        """GROUPA2 (WP-07, A379): VoicePanel0/plainParams -> voicectl.*; what the panel does not mention is the default (all steps zero, 8 steps, scaling 100 %, every target)."""
        params = sb['state']['params']
        vp = tree.get('VoicePanel0')
        pp = vp.get('plainParams') if isinstance(vp, dict) else None
        if not isinstance(pp, dict):
            pp = {}
        want = {}
        for pname, v in pp.items():
            if not isinstance(v, (int, float)) or isinstance(v, bool):
                continue
            m = re.match(r'kParamVoice([1-8])(Pan|Detune|FilterCutoff|EnvTime|Mod1|Mod2)$', pname)
            if m:
                want['voicectl.s%s.%s' % (m.group(1), self.VOICE_ROWS[m.group(2)])] = float(v)
            elif pname in self.VOICE_OTHER:
                key = self.VOICE_OTHER[pname]
                if key.startswith('voicectl.osc.') or key in ('voicectl.rand.detune10x', 'voicectl.scale.lfo_mode'):
                    v = 1.0 if v >= 0.5 else 0.0
                want[key] = float(v)
            else:
                self.err(name, 'VoicePanel0 parameter %s has no row of the audit' % pname)
        if want:
            self.stats['voice panels checked'] += 1
        for key, value in want.items():
            self.check_value(name, 'VoicePanel0/plainParams', sb, key, value, 'VoicePanel0')
        for key, info in T.by_key.items():
            if key.startswith('voicectl.') and key not in want:
                got = params.get(key)
                if got is not None and abs(got - T.clamp(key, info['default'])) > 1e-6:
                    self.err(name, '%s is %s, the panel does not mention it (default %s)' % (key, got, info['default']))

    def audit_midi_map(self, name, tree, report):
        """GROUPA2 (WP-02, A386): the CC map of a preset: an entry whose first parameter id is a macro (module 7, index 0..7) and whose CC can be learned is imported as
        macro.N, every other one is dropped; nothing is left unreported."""
        mm = tree.get('midiMap')
        if not isinstance(mm, list):
            return
        by_path = {e['path']: e for e in report['entries'] if e['path'].startswith('midiMap/')}
        for k, item in enumerate(mm):
            ids = item.get('paramIDs') or []
            cc = item.get('ccNum')
            want = None
            if ids and isinstance(ids[0], (int, float)):
                pid = int(ids[0])
                module, index, parameter = pid // 1000000, (pid // 1000) % 1000, pid % 1000
                if module == 7 and 0 <= index < 8 and parameter == 0 and 1 <= cc <= 119 and cc not in (32, 64, 6, 38) and not 96 <= cc <= 101:
                    want = 'macro.%d' % (index + 1)
            e = by_path.get('midiMap/%d' % k)
            self.stats['midi map entries checked'] += 1
            if e is None:
                self.err(name, 'midiMap/%d is not in the report' % k)
            elif want is not None and (e['kind'] != 'imported' or e['key'] != want):
                self.err(name, 'midiMap/%d: expected imported as %s, got %s %s' % (k, want, e['kind'], e['key']))
            elif want is None and e['kind'] != 'dropped':
                self.err(name, 'midiMap/%d: expected dropped, got %s' % (k, e['kind']))

    SERUM_ROOT = ''

    def audit_noise(self, name, tree, sb):
        """GROUPA1 (WP-04, A348): the noise source plays the embedded data, else the file of the Noises folder, unless the preset names a colour; a colour's
        kParamColor is the FILTER knob; a sample noise has no filter."""
        o = tree.get('Oscillator3', {})
        no = o.get('NoiseOsc3') if isinstance(o, dict) else None
        params = sb['state']['params']
        stored = [x for x in sb['state'].get('content', {}).get('samples', []) if x['osc'] == 4]
        if not isinstance(no, dict):
            if params.get('noise.type', 0) != 0 or stored:
                self.err(name, 'noise.type / noise sample without a NoiseOsc3 block')
            return
        pp = no.get('plainParams') if isinstance(no.get('plainParams'), dict) else {}
        colour = pp.get('kParamNoiseType') in ('White', 'Pink', 'Brown', 'Geiger')
        rel = no.get('relativePathToNoiseSample')
        emb = no.get('embeddedNoiseData')
        have = None
        if not colour:
            if isinstance(emb, list) and emb and isinstance(emb[0], list) and emb[0]:
                have = 'embedded'
            elif isinstance(rel, str) and os.path.isfile(os.path.join(self.SERUM_ROOT, 'Samples', 'Factory Non-Tonal', 'Noises', rel.replace('\\', '/').lstrip('/'))):
                have = 'file'
        typ = params.get('noise.type', 0)
        self.stats['noise sources checked'] += 1
        if have:
            if typ != 1 or len(stored) != 1:
                self.err(name, 'noise sample (%s): noise.type %s, %d stored samples' % (have, typ, len(stored)))
            elif have == 'embedded' and stored[0]['frames'] != len(emb[0]):
                self.err(name, 'embedded noise sample: %d frames stored, %d in the preset' % (stored[0]['frames'], len(emb[0])))
            elif have == 'embedded':
                self.stats['embedded noise samples'] += 1
        else:
            if typ != 0 or stored:
                self.err(name, 'no noise sample can be read, yet noise.type %s with %d stored samples' % (typ, len(stored)))
        if colour and isinstance(pp.get('kParamColor'), (int, float)):
            want = max(-100.0, min(100.0, (pp['kParamColor'] - 0.5) * 200.0))
            if abs(params.get('noise.filter', 0.0) - want) > 2e-4 * 100:
                self.err(name, 'noise.filter: expected %.4g (kParamColor %s) got %s' % (want, pp['kParamColor'], params.get('noise.filter')))
        elif not colour and params.get('noise.filter', 0.0) != 0.0:
            self.err(name, 'noise.filter set on a sample noise')

    RATIO_SOURCE = {0: 1, 1: 2, 2: 3, 3: 0, 4: 4}

    def audit_mod_slew(self, name, tree, sb):
        """GROUPB (WP-08, A408) / REMAINDER item 1 (A449, A450): a connected slot's kParamSmoothRise / Fall (0 .. 100) are the keys modNN.rise / fall in ms = 2000 x (v / 100)^2, a
        missing one follows the other while kParamSmoothLink (absent: on) is on; kParamDelayOffset (0 .. 1) is modNN.delay_beats = 4 v with modNN.delay_sync on when kParamDelayBeatSync,
        otherwise modNN.delay = 4000 v ms. A slot that was not connected has all of them at their defaults, and no record of content.mod_curves carries a time."""
        params = sb['state']['params']
        for m in sb['state'].get('content', {}).get('mod_curves', []):
            if m.get('rise') or m.get('fall'):
                self.err(name, 'mod%02d: the old rise / fall fields of content.mod_curves were written' % m.get('slot', 0))
        for k in range(64):
            slot = tree.get('ModSlot%d' % k)
            pp = slot.get('plainParams') if isinstance(slot, dict) and isinstance(slot.get('plainParams'), dict) else {}
            rise, fall, link = pp.get('kParamSmoothRise'), pp.get('kParamSmoothFall'), pp.get('kParamSmoothLink')
            delay, sync = pp.get('kParamDelayOffset'), pp.get('kParamDelayBeatSync')
            connected = params.get('mod%02d.destination' % (k + 1), 0) != 0 and params.get('mod%02d.source' % (k + 1), 0) != 0
            key = lambda tail: 'mod%02d.%s' % (k + 1, tail)
            got = lambda tail: params.get(key(tail), T.by_key[key(tail)]['default'])
            if not connected or not isinstance(slot, dict):
                for tail in ('rise', 'fall', 'delay', 'delay_beats', 'delay_sync'):
                    if abs(got(tail)) > 0:
                        self.err(name, 'mod%02d: %s set for a slot that is not connected' % (k + 1, tail))
                continue
            if rise is not None or fall is not None:
                linked = True if link is None else link >= 0.5
                r = rise if rise is not None else 0.0
                f = fall if fall is not None else 0.0
                if linked and rise is not None and fall is None:
                    f = r
                if linked and fall is not None and rise is None:
                    r = f
                want = tuple(min(5000.0, 2000.0 * (max(0.0, min(100.0, v)) / 100.0) ** 2) for v in (r, f))
                have = (got('rise'), got('fall'))
                self.stats['slew keys'] += 1
                if abs(have[0] - want[0]) > 1e-3 * max(1.0, want[0]) or abs(have[1] - want[1]) > 1e-3 * max(1.0, want[1]):
                    self.err(name, 'mod%02d: RISE / FALL expected %.4g / %.4g ms, got %.4g / %.4g' % (k + 1, want[0], want[1], have[0], have[1]))
            elif abs(got('rise')) > 0 or abs(got('fall')) > 0:
                self.err(name, 'mod%02d: RISE / FALL set without a Serum value' % (k + 1))
            if delay is not None:
                v = max(0.0, min(1.0, delay))
                synced = sync is not None and sync >= 0.5
                self.stats['slot delays'] += 1
                if synced:
                    if abs(got('delay_beats') - 4.0 * v) > 1e-3 or got('delay_sync') != 1.0 or got('delay') != 0.0:
                        self.err(name, 'mod%02d: DELAY expected %.4g beats synced, got %.4g beats (sync %s, %.4g ms)' % (k + 1, 4.0 * v, got('delay_beats'), got('delay_sync'), got('delay')))
                elif abs(got('delay') - 4000.0 * v) > 1e-2 or got('delay_sync') != 0.0 or got('delay_beats') != 0.0:
                    self.err(name, 'mod%02d: DELAY expected %.4g ms, got %.4g ms (sync %s)' % (k + 1, 4000.0 * v, got('delay'), got('delay_sync')))
            elif abs(got('delay')) > 0 or abs(got('delay_beats')) > 0 or got('delay_sync') != 0:
                self.err(name, 'mod%02d: DELAY set without a Serum value' % (k + 1))

    # ---- REMAINDER item 4: PZ SVF response curves (S4), LFO swing flag / point curve modulation (S13), sample trim (S12) --------------------------------------------------------------
    @staticmethod
    def pz_db(sets, weights, x):
        """The response in dB of the blended sets at the position x (0 .. 1) of the log frequency axis 20 Hz .. 20 kHz, restated here from 13-new-keys.md 4.43 (A452 .. A455)."""
        def freq(v):
            return 20.0 * 1000.0 ** min(1.0, max(0.0, v))

        def quality(q):
            return 0.5 * 100.0 ** min(1.0, max(0.0, q))
        f = freq(x)
        total = 0.0
        for st, w in zip(sets, weights):
            db = (st['gain'] - 0.5) * 24.0
            sv = complex(0.0, f)
            for i in range(8):
                wp, wz, qp, qz = freq(st['fp'][i]), freq(st['fz'][i]), quality(st['qp'][i]), quality(st['qz'][i])
                pole = sv * sv + sv * (wp / qp) + wp * wp
                t = int(round(st['types'][i]))
                h = 1.0
                if t == 0: h = sv * sv / pole
                elif t == 1: h = wp * wp / pole
                elif t == 3: h = pole.conjugate() / pole
                elif t == 4: h = sv * (wp / qp) / pole
                elif t == 5: h = (sv * sv + wz * wz) / pole * (wp * wp / (wz * wz))
                elif t == 6: h = (sv * sv + sv * (wz / qz) + wz * wz) / pole
                db += 20.0 * math.log10(max(1e-9, abs(h)))
            total += w * db
        return min(60.0, max(-60.0, total))

    def audit_remainder_item4(self, name, tree, sb, report):
        params = sb['state']['params']
        content = sb['state'].get('content', {})
        # S4: the response curve of every VoiceFilter / FXFilter of type PZ_SVF equals the response computed here from the poles and zeros (within the reduction to 16 points)
        curves = {c['filter']: c for c in content.get('filter_curves', [])}
        expect = {}
        for key, filt in (('filter1', tree.get('VoiceFilter0')), ('filter2', tree.get('VoiceFilter1'))):
            if isinstance(filt, dict) and isinstance(filt.get('plainParams'), dict) and filt['plainParams'].get('kParamType') == 'PZ_SVF' and isinstance(filt.get('PZs'), list):
                expect[key] = (filt['PZs'], filt['plainParams'])
        for rack in range(3):
            fx = tree.get('FXRack%d' % rack)
            if isinstance(fx, dict) and isinstance(fx.get('FX'), list):
                pass   # a pooled FX filter has no content entry; the main FX filter is covered by the report entries below
        for key, (sets_raw, pp) in expect.items():
            sets = [st for st in sets_raw if isinstance(st, dict) and all(k in st for k in ('fp', 'fz', 'qp', 'qz', 'types', 'gain'))][:4]
            c = curves.get(key)
            if c is None:
                self.err(name, '%s: PZ SVF poles and zeros without a response curve' % key)
                continue
            xx, yy = min(1.0, max(0.0, pp.get('kParamX', 0.5))), min(1.0, max(0.0, pp.get('kParamY', 0.5)))
            weights = [(1 - xx) * (1 - yy), xx * (1 - yy), (1 - xx) * yy, xx * yy] if len(sets) == 4 else [1.0 / len(sets)] * len(sets)
            pts = c['points']
            if not (2 <= len(pts) <= 16):
                self.err(name, '%s: PZ curve has %d points' % (key, len(pts)))
                continue
            worst = 0.0
            for k in range(129):
                x = k / 128.0
                got = None
                for a, b in zip(pts, pts[1:]):
                    if a['x'] <= x <= b['x']:
                        t = (x - a['x']) / (b['x'] - a['x']) if b['x'] > a['x'] else 0.0
                        got = a['y'] + t * (b['y'] - a['y'])
                        break
                if got is None:
                    got = pts[0]['y'] if x < pts[0]['x'] else pts[-1]['y']
                worst = max(worst, abs(got - self.pz_db(sets, weights, x)))
            self.stats['pz curves'] += 1
            if worst > 6.0:
                self.err(name, '%s: PZ response curve deviates by %.2f dB from the poles and zeros' % (key, worst))
        for key in curves:
            if key not in expect and key != 'fxfilter':
                self.err(name, '%s: a PZ response curve for a filter that is not a PZ SVF' % key)
        # S13: the LFO swing flag takes the global swing: (global - 50) * 4, 0 .. 100 %
        glob = tree.get('Global0')
        gpp = glob.get('plainParams') if isinstance(glob, dict) and isinstance(glob.get('plainParams'), dict) else {}
        gswing = gpp.get('kParamSwing', 50.0) if isinstance(gpp.get('kParamSwing', 50.0), (int, float)) else 50.0
        rlfo_slots = set()
        for i in range(10):
            lfo = tree.get('LFO%d' % i)
            pp = lfo.get('plainParams') if isinstance(lfo, dict) and isinstance(lfo.get('plainParams'), dict) else {}
            if pp.get('kParamType') in ('RandomSH', 'Lorenz', 'Rossler'):
                continue
            key = 'lfo%d.swing' % (i + 1)
            want = min(100.0, max(0.0, (gswing - 50.0) * 4.0)) if pp.get('kParamSwing', 0) and pp.get('kParamSwing') >= 0.5 else 0.0
            got = params.get(key, 0.0)
            if abs(got - want) > 1e-3:
                self.err(name, '%s expected %.4g from the swing flag, got %.4g' % (key, want, got))
            elif want > 0:
                self.stats['lfo swings'] += 1
        # S13: point modulation assignments with the curve target end up as mod_curve on a point
        shapes = {s['lfo']: s for s in content.get('lfo_shapes', [])}
        wanted = sum(1 for e in report['entries'] if e.get('detail') == 'point_curve_modulation')
        present = sum(1 for s in shapes.values() for pt in s['points'] if 'mod_curve' in pt)
        if wanted != present:
            self.err(name, 'point curve modulation: %d entries, %d points carry mod_curve' % (wanted, present))
        else:
            self.stats['point curve modulations'] += present
        # S12: trimHead / trimTail fold into START / END of the oscillator
        for i in range(3):
            o = tree.get('Oscillator%d' % i)
            if not isinstance(o, dict):
                continue
            for b in ('SampleOsc', 'GranularOsc', 'SpectralOsc'):
                blk = o.get('%s%d' % (b, i))
                if not (isinstance(blk, dict) and ('trimHead' in blk or 'trimTail' in blk) and blk.get('numFrames')):
                    continue
                opp = o.get('plainParams') if isinstance(o.get('plainParams'), dict) else {}
                if opp.get('kParamType') not in ('kOsc_Sample', 'kOsc_Granular', 'kOsc_Spectral') or not any(e['key'] == 'osc%d.sample.start' % (i + 1) for e in report['entries']):
                    continue
                total = float(blk['numFrames'])
                lo = min(1.0, max(0.0, blk.get('trimHead', 0) / total)) if 'trimHead' in blk else 0.0
                hi = min(1.0, max(0.0, blk.get('trimTail', total) / total)) if 'trimTail' in blk else 1.0
                if hi <= lo:
                    continue
                start = opp.get('kParamStart', 0.0) * 0.01 if isinstance(opp.get('kParamStart', 0.0), (int, float)) else 0.0
                end = opp.get('kParamEnd', 100.0) * 0.01 if isinstance(opp.get('kParamEnd', 100.0), (int, float)) else 1.0
                for tail, want in (('start', 100.0 * (lo + start * (hi - lo))), ('end', 100.0 * (lo + end * (hi - lo)))):
                    got = params.get('osc%d.sample.%s' % (i + 1, tail), 0.0 if tail == 'start' else 100.0)
                    if abs(got - want) > 1e-2 * max(1.0, abs(want)) * 0.01 + 1e-3:
                        self.err(name, 'osc%d.sample.%s expected %.5g from trimHead / trimTail, got %.5g' % (i + 1, tail, want, got))
                self.stats['sample trims'] += 1

    def audit_osc_extras(self, name, tree, sb):
        """GROUPB (WP-05, A398 / A399): kParamPitchSource (0 .. 2 = Osc A .. C, 3 = Noise, 4 = Sub) is osc#.ratio_src when the pitch mode is Ratio (an oscillator that names
        itself, the noise and every other mode play the note of the voice); the WT block's kParamXfadeMode is osc#.smooth_interp (1 = on)."""
        params = sb['state']['params']
        for i in range(3):
            o = tree.get('Oscillator%d' % i)
            pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
            want = 0
            if pp.get('kParamPitchMode') == 'Ratio' and isinstance(pp.get('kParamPitchSource'), (int, float)):
                want = self.RATIO_SOURCE.get(int(round(pp['kParamPitchSource'])), 0)
                if want == i + 1:
                    want = 0
                self.stats['ratio sources'] += 1
            got = params.get('osc%d.ratio_src' % (i + 1), 0)
            if got != want:
                self.err(name, 'osc%d.ratio_src: expected %d got %s' % (i + 1, want, got))
            wt = o.get('WTOsc%d' % i) if isinstance(o, dict) else None
            wp = wt.get('plainParams') if isinstance(wt, dict) and isinstance(wt.get('plainParams'), dict) else {}
            smooth = 0
            if pp.get('kParamType', 'kOsc_WT') == 'kOsc_WT' and isinstance(wp.get('kParamXfadeMode'), (int, float)):
                smooth = 1 if wp['kParamXfadeMode'] >= 0.5 else 0
                self.stats['smooth interpolation flags'] += 1
            got = params.get('osc%d.smooth_interp' % (i + 1), 0)
            if got != smooth:
                self.err(name, 'osc%d.smooth_interp: expected %d got %s' % (i + 1, smooth, got))

    @staticmethod
    def nearest_division(beats):
        """the entry of oscN.gran.density_sync / length_sync (3 per division 1/1 .. 1/128: straight, triplet, dotted) nearest to `beats` on a log scale."""
        best, err = 12, 1e9
        for i in range(24):
            b = 4.0 / (1 << (i // 3)) * (1.0, 2.0 / 3.0, 1.5)[i % 3]
            e = abs(math.log2(max(1e-6, beats) / b))
            if e < err:
                best, err = i, e
        return best

    def audit_granular(self, name, tree, sb):
        """GROUPC WP-11 (A412 .. A418): type Granular (2); the block rows are in GRAN; here the value + mode pairs: density (Free = Hz, BPM = beats -> nearest division, Grains = count) and
        grain length (Free = seconds -> ms, BPM = beats -> nearest division, Percent = a multiple of the density period -> percent; no mode in the file = Percent); the play head of
        the common block: kParamManualPositionMode and kParamPosition."""
        params = sb['state']['params']
        for i in range(3):
            n = i + 1
            o = tree.get('Oscillator%d' % i)
            pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
            if pp.get('kParamType') != 'kOsc_Granular':
                if params.get('osc%d.type' % n) == 2:
                    self.err(name, 'osc%d.type is Granular but the file says %s' % (n, pp.get('kParamType', 'kOsc_WT')))
                continue
            self.stats['granular oscillators'] += 1
            if params.get('osc%d.type' % n) != 2:
                self.err(name, 'osc%d.type: expected 2 (Granular) got %s' % (n, params.get('osc%d.type' % n)))
            blk = o.get('GranularOsc%d' % i)
            bp = blk.get('plainParams') if isinstance(blk, dict) and isinstance(blk.get('plainParams'), dict) else {}
            md, dv = bp.get('kParamDensityMode'), bp.get('kParamDensity')
            dm = 1 if isinstance(md, str) and 'BPM' in md else (2 if isinstance(md, str) and 'Grain' in md else 0)
            if md is not None or dv is not None:
                self.check_value(name, None, sb, 'osc%d.gran.density_mode' % n, dm, 'kParamDensityMode')
            if isinstance(dv, (int, float)):
                key, val = (('density', dv), ('density_sync', self.nearest_division(dv)), ('density_grains', round(dv)))[dm]
                self.check_value(name, None, sb, 'osc%d.gran.%s' % (n, key), val, 'kParamDensity')
            ml, lv = bp.get('kParamLengthMode'), bp.get('kParamGrainLength')
            lm = 1 if isinstance(ml, str) and 'BPM' in ml else (3 if ml is None or (isinstance(ml, str) and 'Percent' in ml) else 0)
            if ml is not None or lv is not None:
                self.check_value(name, None, sb, 'osc%d.gran.length_mode' % n, {0: 0, 1: 1, 3: 2}[lm], 'kParamLengthMode')
            if isinstance(lv, (int, float)):
                key, val = {0: ('length', lv * 1000.0), 1: ('length_sync', self.nearest_division(lv)), 3: ('length_percent', lv * 100.0)}[lm]
                self.check_value(name, None, sb, 'osc%d.gran.%s' % (n, key), val, 'kParamGrainLength')
            if isinstance(pp.get('kParamManualPositionMode'), (int, float)):
                self.check_value(name, None, sb, 'osc%d.gran.manual' % n, 1.0 if pp['kParamManualPositionMode'] >= 0.5 else 0.0, 'kParamManualPositionMode')
            if isinstance(pp.get('kParamPosition'), (int, float)):
                self.check_value(name, None, sb, 'osc%d.gran.position' % n, pp['kParamPosition'], 'kParamPosition')

    def audit_spectral(self, name, tree, sb):
        """GROUPC WP-12 (A419 .. A426): type Spectral (3); the block rows are in SPECTRAL; here the common block (Manual, Position, Hz offset = the frequency shift) and the flex curve
        (the drawn filter mask, content.spectral_filters: not stored when flat, y = 0.5 everywhere)."""
        params = sb['state']['params']
        stored = {e['osc']: e for e in sb['state'].get('content', {}).get('spectral_filters', [])}
        for i in range(3):
            n = i + 1
            o = tree.get('Oscillator%d' % i)
            pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
            if pp.get('kParamType') != 'kOsc_Spectral':
                if params.get('osc%d.type' % n) == 3:
                    self.err(name, 'osc%d.type is Spectral but the file says %s' % (n, pp.get('kParamType', 'kOsc_WT')))
                if n in stored:
                    self.err(name, 'osc%d has a spectral filter but is not a spectral oscillator' % n)
                continue
            self.stats['spectral oscillators'] += 1
            if params.get('osc%d.type' % n) != 3:
                self.err(name, 'osc%d.type: expected 3 (Spectral) got %s' % (n, params.get('osc%d.type' % n)))
            if isinstance(pp.get('kParamManualPositionMode'), (int, float)):
                self.check_value(name, None, sb, 'osc%d.spec.manual' % n, 1.0 if pp['kParamManualPositionMode'] >= 0.5 else 0.0, 'kParamManualPositionMode')
            if isinstance(pp.get('kParamPosition'), (int, float)):
                self.check_value(name, None, sb, 'osc%d.spec.position' % n, pp['kParamPosition'], 'kParamPosition')
            if isinstance(pp.get('kParamHzOffset'), (int, float)):
                self.check_value(name, None, sb, 'osc%d.spec.freq_shift' % n, pp['kParamHzOffset'], 'kParamHzOffset')
            blk = o.get('SpectralOsc%d' % i)
            flex = blk.get('flex') if isinstance(blk, dict) else None
            ys = flex.get('yVals') if isinstance(flex, dict) else None
            drawn = isinstance(ys, list) and len(ys) > 0 and any(abs(y - 0.5) > 1e-4 for y in ys)
            if drawn != (n in stored):
                self.err(name, 'osc%d: the flex curve is %s but the preset %s a spectral filter' % (n, 'drawn' if drawn else 'flat', 'has' if n in stored else 'has no'))
            elif drawn:
                self.stats['spectral filters'] += 1
                pts = stored[n]['points']
                if len(pts) < 2 or pts[0]['x'] > 1e-3 or pts[-1]['x'] < 1 - 1e-3:
                    self.err(name, 'osc%d: the spectral filter does not span the axis' % n)

    def audit_multisample(self, name, tree, sb, content_dir):
        """GROUPC WP-13 (A427 .. A434): type Multisample (4); the block rows are in MULTI; here the instrument: content.instruments holds the SFZ of the preset, the stored SFZ has as many
        regions as the original and names the same sample files (as .wav files that exist below its folder, except the ones the Serum content did not hold)."""
        params = sb['state']['params']
        stored = {e['osc']: e for e in sb['state'].get('content', {}).get('instruments', [])}
        for i in range(3):
            n = i + 1
            o = tree.get('Oscillator%d' % i)
            pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
            if pp.get('kParamType') != 'kOsc_MultiSample':
                if params.get('osc%d.type' % n) == 4:
                    self.err(name, 'osc%d.type is Multisample but the file says %s' % (n, pp.get('kParamType', 'kOsc_WT')))
                if n in stored:
                    self.err(name, 'osc%d has an instrument but is not a multisample oscillator' % n)
                continue
            self.stats['multisample oscillators'] += 1
            blk = o.get('MultiSampleOsc%d' % i)
            text = blk.get('embedded_sfz') if isinstance(blk, dict) else None
            rel = blk.get('sfzPathRelative') if isinstance(blk, dict) else None
            source = text if isinstance(text, str) else None
            if source is None and isinstance(rel, str):
                path = os.path.join(self.SERUM_ROOT, 'Multisamples', rel.replace('\\', '/'))
                if os.path.isfile(path):
                    source = open(path, errors='replace').read()
            if not source or '<region>' not in source:
                continue
            want_regions = source.count('<region>')
            if params.get('osc%d.type' % n) != 4 or n not in stored:
                self.err(name, 'osc%d: a multisample instrument with %d regions was not imported (type %s)' % (n, want_regions, params.get('osc%d.type' % n)))
                continue
            ref = stored[n]
            path = os.path.join(content_dir, 'instruments', ref['file'])
            if not os.path.isfile(path):
                self.err(name, 'osc%d: the stored SFZ is missing: %s' % (n, ref['file']))
                continue
            data = open(path, 'rb').read()
            if hashlib.sha256(data).hexdigest() != ref['sha256']:
                self.err(name, 'osc%d: the stored SFZ differs from its hash' % n)
            got = data.decode('utf-8', 'replace')
            if got.count('<region>') != want_regions or ref.get('zones') != want_regions:
                self.err(name, 'osc%d: the stored SFZ has %d regions, the original %d (zones field %s)' % (n, got.count('<region>'), want_regions, ref.get('zones')))
            self.stats['instrument regions'] += want_regions
            base = os.path.dirname(path)
            missing = 0
            for sample in re.findall(r'^sample=(.*)$', got, re.M):
                if not os.path.isfile(os.path.join(base, sample)):
                    missing += 1
            self.stats['instrument samples missing'] += missing
            if missing > 0 and missing > want_regions // 2 and self.SERUM_ROOT:
                self.err(name, 'osc%d: %d of %d sample files of the instrument are not stored' % (n, missing, want_regions))

    SERUM_DEFAULTS = [(r'osc(\d)\.on$', lambda m: 1.0 if m.group(1) == '1' else 0.0), (r'(noise|sub)\.on$', lambda m: 0.0), (r'filter\d\.on$', lambda m: 0.0),
                      (r'osc\d\.unison\.voices$', lambda m: 1.0), (r'osc\d\.unison\.blend$', lambda m: 75.0), (r'osc\d\.unison\.law$', lambda m: 1.0), (r'osc\d\.unison\.detune_curve$', lambda m: 0.0), (r'osc\d\.unison\.detune$', lambda m: 100.0 * 0.25 ** 1.25), (r'osc\d\.unison\.law$', lambda m: 1.0), (r'osc\d\.unison\.detune_curve$', lambda m: 0.0), (r'osc\d\.unison\.detune$', lambda m: 100.0 * 0.25 ** 1.25), (r'osc\d\.unison\.spread$', lambda m: 100.0),
                      (r'osc(\d)\.destination$', lambda m: 5.0 if m.group(1) == '1' else 3.0), (r'(noise|sub)\.destination$', lambda m: 3.0),
                      (r'filter\d\.destination$', lambda m: 0.0), (r'filter\d\.model$', lambda m: 22.0), (r'filter\d\.style$', lambda m: 0.0),
                      (r'osc\d\.type$', lambda m: 0.0), (r'master\.bend_range$', lambda m: 2.0), (r'master\.bend_down_range$', lambda m: 2.0),
                      (r'osc\d\.sample\.root$', lambda m: 60.0), (r'noise\.loop$', lambda m: 1.0), (r'osc\d\.env1$|(noise|sub|filter\d)\.env1$', lambda m: 1.0),
                      (r'distortion\.type$', lambda m: 6.0), (r'reverb\.type$', lambda m: 1.0)]

    def default_rule(self, key, tree, value=None):
        if re.match(r'filter\d\.model$', key) and value is not None and abs(value - 9.0) < 1e-6:
            return True   # a filter module that is not in the file keeps the model of our engine (the Serum default MG Low 12 is written for a module that is)
        for pat, fn in self.SERUM_DEFAULTS:
            m = re.match(pat, key)
            if m and value is not None:
                return abs(value - fn(m)) < 1e-6

        """keys the importer sets to the Serum default when the file does not say (and the families it always sets)."""
        pats = [r'master\.velocity_track$', r'osc\d\.level$', r'(noise|sub)\.level$', r'env\d\.(attack|decay|release)$', r'osc[123]\.(on|destination|filter_blend|bus[12]_send|env1|type)$', r'(noise|sub)\.(on|destination|filter_blend|bus[12]_send|env1)$', r'filter[12]\.(on|model|style|destination|bus[12]_send|env1)$',
                r'filter\.routing$', r'lfo\d+\.(shape|smooth|host|sync|division|sync_type|direction)$', r'rlfo\d\.', r'master\.(bend_range|bend_down_range|bend_down_link|polyphony|voice\.same_note)$',
                r'quality\.', r'osc\d\.frame$', r'osc\d\.wavetable$', r'filter\d\.(mix|keytrack|level|pan|cutoff|resonance|drive|var)$', r'env\d\.(attack_curve|decay_curve|release_curve|sustain|hold)$',
                r'osc\d\.(unison\.(blend|spread|voices|range)|map\.(key_high|vel_high|vel_low)|sample\.(end|loop_end|slice_threshold)|random_phase)$',
                r'osc\d\.remap\.slot$', r'convolve\.', r'distortion\.type$', r'master\.(mpe|tuning|scale|volume)', r'master\.voice', r'direct\.level$|bus\d\.(level|destination)$']
        return any(re.match(p, key) for p in pats)

    def override(self, tree, key, param, path, exp):
        """The documented deviations of 13-new-keys.md 4.26: a mono toggle forces one voice, a ratio mode carries the ratio in the coarse slot, a multisample zone adds its tune."""
        if key == 'master.polyphony' and tree.get('Global0', {}).get('plainParams', {}).get('kParamMonoToggle', 0) >= 0.5 if isinstance(tree.get('Global0', {}).get('plainParams'), dict) else False:
            return 1.0
        m = re.match(r'osc(\d)\.', key)
        if m:
            o = tree.get('Oscillator%d' % (int(m.group(1)) - 1), {})
            pp = o.get('plainParams') if isinstance(o.get('plainParams'), dict) else {}
            if key.endswith('.pitch_coarse') and pp.get('kParamPitchMode') in ('Ratio', 'Harmonics') and 'kParamPitchRatio' in pp:
                return pp['kParamPitchRatio']
            if key.endswith('.tune') and pp.get('kParamType') == 'kOsc_MultiSample':
                return None
            if key.endswith('.level') and pp.get('kParamType') in ('kOsc_Sample', 'kOsc_Granular'):
                # A517: a sample / granular oscillator of ours plays quieter than the Serum one (3.0 / 4.9 dB, controlled experiments): the level goes up as far as its range
                return min(100.0, exp * 10.0 ** ((3.0 if pp.get('kParamType') == 'kOsc_Sample' else 4.9) / 20.0))
        m = re.match(r'filter(\d)\.level$', key)
        if m:   # A492: the louder output of the Serum drive stage goes into the level of the filter
            f = tree.get('VoiceFilter%d' % (int(m.group(1)) - 1), {})
            fp = f.get('plainParams') if isinstance(f.get('plainParams'), dict) else {}
            if exp > -59.99:
                # A492 / A501: the louder output of the drive stage and the level the resonance takes or adds (per filter type, cutoff, resonance) go into the level of the filter
                clamp = lambda x: min(12.0, max(-60.0, x))   # the importer stores in three steps (the rule, the drive makeup, the laws of the type), each clamped to the range of filterN.level
                lv = clamp(exp)
                if isinstance(fp.get('kParamDrive'), (int, float)):
                    mk = filter_drive_makeup(float(fp['kParamDrive']))
                    if mk > 0.0 and lv > -59.99:
                        lv = clamp(lv + mk)
                typ = fp.get('kParamType') if isinstance(fp.get('kParamType'), str) else 'MgL12'
                fr = float(fp['kParamFreq']) if isinstance(fp.get('kParamFreq'), (int, float)) else 0.5
                rs = float(fp['kParamReso']) if isinstance(fp.get('kParamReso'), (int, float)) else 0.0
                law = filter_reso_level(typ, fr, rs) + filter_drive_level(typ, float(fp['kParamDrive']) if isinstance(fp.get('kParamDrive'), (int, float)) else 0.0, rs)
                return clamp(lv + law) if law != 0.0 and lv > -59.99 else lv
        return exp

    def check_source(self, name, sb, slot, src, key, aux):
        if not isinstance(src, list) or not src:
            return
        got = sb['state']['params'].get('mod%02d.source' % slot)
        self.stats['mod sources'] += 1
        base = {1: 'mod_wheel', 2: 'env1', 3: 'env2', 4: 'env3', 5: 'env4', 16: 'velocity', 17: 'note', 18: 'aftertouch', 19: 'poly_aftertouch', 20: 'noise', 21: 'random',
                22: 'note_on_rand2', 23: 'note_on_alt1', 24: 'note_on_alt2', 33: 'pitch_wheel', 34: 'expression_x', 35: 'slide', 36: 'poly_aftertouch', 37: 'release_velocity',
                38: 'fixed', 49: 'osc_a', 50: 'osc_b', 51: 'osc_c', 52: 'sub', 53: 'filter1', 54: 'filter2', 55: 'active_voices', 56: 'voice_mod1', 57: 'voice_mod2',
                58: 'voice_index', 59: 'note_on_rand_discrete'}
        sid = src[0]
        if 6 <= sid <= 13:
            want_key = 'lfo%d' % (sid - 5)
        elif sid == 14:
            want_key = 'lfo9'
        elif sid == 15:
            want_key = 'lfo10'
        elif 25 <= sid <= 32:
            want_key = 'macro%d' % (sid - 24)
        else:
            want_key = base.get(sid)
        if want_key is None:
            if got not in (0, None):
                self.err(name, 'mod%02d.source is %s but Serum source %d has no equivalent' % (slot, got, sid))
            return
        if want_key.startswith('lfo') and got is not None and got > 0 and T.mod_sources[int(got) - 1].startswith('rlfo'):
            return  # a random LFO took its place
        want = T.mod_sources.index(want_key) + 1
        if got != want:
            self.err(name, 'mod%02d.source: expected %d (%s) got %s' % (slot, want, want_key, got))

    def audit_fx_entry(self, name, e, node, tree, sb, fx_slots):
        """REFCOMPARE round 3 (13-new-keys.md 4.48): the effect module is audited as a whole: the keys of its pool slot are recomputed from the file values, the Serum defaults of the absent
        parameters and the measured laws (stated here again in fx_expected), once per module."""
        parts = e['path'].split('/')
        r = int(parts[0][6:])
        j = int(parts[2])
        fxname = parts[3]
        if (name, r, j) in self.fx_done or fxname not in FX:
            return
        self.fx_done.add((name, r, j))
        pp = tree.get('FXRack%d' % r, {}).get('FX', [{}] * (j + 1))[j].get(fxname, {}).get('plainParams', {})
        if not isinstance(pp, dict):
            pp = {}
        slot = fx_slots.get((r, j))
        want = fx_expected(fxname, pp)
        if slot is None:
            return
        own = T.pool_own_keys(POOL_PREFIX[fxname])
        for key, plain in want.items():
            if key not in own:
                continue
            if plain is None:
                continue
            self.fx_slot_value(name, sb, slot, own.index(key), key, plain)

    def audit_content(self, name, tree, sb, content_dir):
        content = sb.get('state', {}).get('content', {})
        for w in content.get('wavetables', []):
            pack = {'user.wavetables': 'wavetables'}.get(w['pack'])
            path = os.path.join(content_dir, pack, w['file']) if pack else None
            if not path or not os.path.isfile(path):
                self.err(name, 'stored wavetable missing: %s/%s' % (w['pack'], w['file']))
                continue
            data = open(path, 'rb').read()
            if hashlib.sha256(data).hexdigest() != w['sha256']:
                self.err(name, 'stored wavetable hash differs: %s' % w['file'])
            self.stats['tables stored'] += 1
            osc = w['osc'] - 1
            emb = tree.get('Oscillator%d' % osc, {}).get('WTOsc%d' % osc, {}).get('embeddedWTData')
            if emb and len(emb) % 2048 == 0 and len(emb) // 2048 <= 256:
                # RIFF: find the data chunk, compare the 16 bit samples with the floats
                i = data.index(b'data')
                n = struct.unpack('<I', data[i + 4:i + 8])[0]
                samples = struct.unpack('<%dh' % (n // 2), data[i + 8:i + 8 + n])
                if len(samples) != len(emb):
                    self.err(name, 'stored table has %d samples, embedded data %d' % (len(samples), len(emb)))
                else:
                    worst = max(abs(s - round(max(-1.0, min(1.0, x)) * 32767.0)) for s, x in zip(samples, emb))
                    self.stats['table samples compared'] += len(emb)
                    if worst > 1:
                        self.err(name, 'stored table differs from the embedded data by %d LSB' % worst)
        for s in content.get('samples', []):
            pack = {'user.samples': 'samples'}.get(s['pack'])
            path = os.path.join(content_dir, pack, s['file']) if pack else None
            if not path or not os.path.isfile(path) or hashlib.sha256(open(path, 'rb').read()).hexdigest() != s['sha256']:
                self.err(name, 'stored sample missing or changed: %s/%s' % (s['pack'], s['file']))
            self.stats['samples stored'] += 1
        for s in content.get('impulses', []):
            pack = {'user.impulses': 'impulses'}.get(s['pack'])
            path = os.path.join(content_dir, pack, s['file']) if pack else None
            if not path or not os.path.isfile(path) or hashlib.sha256(open(path, 'rb').read()).hexdigest() != s['sha256']:
                self.err(name, 'stored impulse missing or changed: %s/%s' % (s['pack'], s['file']))
            self.stats['impulses stored'] += 1


def find_pairs(serum, imported):
    root = os.path.join(serum, 'Presets')
    pairs = []
    for dp, dn, fn in os.walk(root):
        for f in sorted(fn):
            if f.endswith('.SerumPreset'):
                rel = os.path.relpath(os.path.join(dp, f), root)
                base = os.path.join(imported, rel[:-len('.SerumPreset')])
                pairs.append((rel, os.path.join(dp, f), base + '.report.json', base + '.sbpreset'))
    return sorted(pairs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--serum', required=True)
    ap.add_argument('--imported', required=True)
    ap.add_argument('--content-dir')
    ap.add_argument('--json')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    content_dir = a.content_dir or os.path.join(a.imported, 'content')
    audit = Audit(a.v)
    Audit.SERUM_ROOT = a.serum
    pairs = find_pairs(a.serum, a.imported)
    first = None
    for rel, src, rep, sb in pairs:
        if not (os.path.isfile(rep) and os.path.isfile(sb)):
            audit.err(rel, 'no imported result')
            continue
        report = json.load(open(rep))
        preset = json.load(open(sb))
        if first is None and report['counts']['imported'] > 100:
            first = (rel, src, report, preset)
        audit.audit_file(rel, src, report, preset['state'] and preset, content_dir) if False else audit.audit_file(rel, src, report, {'state': preset['state']}, content_dir)
        audit.stats['files'] += 1
    mutation_ok = True
    if a.selftest and first:
        rel, src, report, preset = first
        muts = [('osc1.level', 3.0), ('filter1.cutoff', 77.0), ('env1.attack', 999.0), ('master.volume', 5.0), ('lfo1.rate', 17.0), ('mod01.amount', -77.0), ('osc1.unison.voices', 11.0), ('voicectl.s1.pan', 33.0), ('osc1.smooth_interp', None), ('osc1.ratio_src', None)]
        for key, val in muts:
            if key not in preset['state']['params']:
                continue
            if val is None:
                val = 1.0 - preset['state']['params'][key]  # a flag / small choice: the other value
            bad = json.loads(json.dumps({'state': preset['state']}))
            bad['state']['params'][key] = val
            t = Audit(False)
            t.audit_file(rel, src, report, bad, content_dir)
            found = any(key in e for e in t.errors)
            print('selftest: mutation of %-18s %s' % (key, 'found' if found else 'NOT FOUND'))
            if not found:
                mutation_ok = False
    if a.selftest:
        # GROUPC: the granular keys are found as well (a preset with a granular oscillator that has a density, a grain length and a window)
        for rel, src, rep, sb in pairs:
            _h, tree = load_serum(src)
            for i in range(3):
                o = tree.get('Oscillator%d' % i)
                pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
                blk = o.get('GranularOsc%d' % i) if isinstance(o, dict) else None
                bp = blk.get('plainParams') if isinstance(blk, dict) and isinstance(blk.get('plainParams'), dict) else {}
                if pp.get('kParamType') == 'kOsc_Granular' and 'kParamDensity' in bp and 'kParamGrainLength' in bp:
                    report = json.load(open(rep))
                    preset = json.load(open(sb))
                    for key in ('osc%d.type' % (i + 1), 'osc%d.gran.density' % (i + 1), 'osc%d.gran.length_percent' % (i + 1), 'osc%d.gran.window_amount' % (i + 1)):
                        bad = json.loads(json.dumps({'state': preset['state']}))
                        bad['state']['params'][key] = bad['state']['params'].get(key, 0.0) + 1.5
                        t = Audit(False)
                        t.audit_file(rel, src, report, bad, content_dir)
                        found = any(key in e for e in t.errors)
                        print('selftest: mutation of %-22s %s' % (key, 'found' if found else 'NOT FOUND'))
                        if not found:
                            mutation_ok = False
                    break
            else:
                continue
            break
        # the spectral keys as well (a preset with a spectral oscillator that has a frequency band and a filter mix)
        for rel, src, rep, sb in pairs:
            _h, tree = load_serum(src)
            found_spectral = None
            for i in range(3):
                o = tree.get('Oscillator%d' % i)
                pp = o.get('plainParams') if isinstance(o, dict) and isinstance(o.get('plainParams'), dict) else {}
                blk = o.get('SpectralOsc%d' % i) if isinstance(o, dict) else None
                bp = blk.get('plainParams') if isinstance(blk, dict) and isinstance(blk.get('plainParams'), dict) else {}
                if pp.get('kParamType') == 'kOsc_Spectral' and 'kParamFreqLo' in bp:
                    found_spectral = i
                    break
            if found_spectral is None:
                continue
            report = json.load(open(rep))
            preset = json.load(open(sb))
            for key in ('osc%d.type' % (found_spectral + 1), 'osc%d.spec.freq_lo' % (found_spectral + 1)):
                bad = json.loads(json.dumps({'state': preset['state']}))
                bad['state']['params'][key] = bad['state']['params'].get(key, 0.0) + 1.5
                t = Audit(False)
                t.audit_file(rel, src, report, bad, content_dir)
                found = any(key in e for e in t.errors)
                print('selftest: mutation of %-22s %s' % (key, 'found' if found else 'NOT FOUND'))
                if not found:
                    mutation_ok = False
            break
        # REMAINDER item 1: the RISE / FALL and DELAY keys of a slot are found as well (a connected slot with kParamSmoothRise / kParamDelayOffset)
        for what, param, tail in (('RISE', 'kParamSmoothRise', 'rise'), ('DELAY', 'kParamDelayOffset', 'delay_beats')):
            done = False
            for rel, src, rep, sb in pairs:
                _h, tree = load_serum(src)
                preset = json.load(open(sb))
                for k in range(64):
                    slot = tree.get('ModSlot%d' % k)
                    pp = slot.get('plainParams') if isinstance(slot, dict) and isinstance(slot.get('plainParams'), dict) else {}
                    key = 'mod%02d.%s' % (k + 1, 'delay' if param == 'kParamDelayOffset' and not pp.get('kParamDelayBeatSync') else tail)
                    if param in pp and preset['state']['params'].get('mod%02d.destination' % (k + 1), 0) != 0:
                        report = json.load(open(rep))
                        bad = json.loads(json.dumps({'state': preset['state']}))
                        bad['state']['params'][key] = bad['state']['params'].get(key, 0.0) + 1.5
                        t = Audit(False)
                        t.audit_file(rel, src, report, bad, content_dir)
                        found = any(('mod%02d:' % (k + 1)) in e for e in t.errors)
                        print('selftest: mutation of %-22s %s' % (key, 'found' if found else 'NOT FOUND'))
                        if not found:
                            mutation_ok = False
                        done = True
                        break
                if done:
                    break
    print('audited %d files; %s' % (audit.stats['files'], ', '.join('%s %d' % (k, v) for k, v in sorted(audit.stats.items()) if k != 'files')))
    for e in audit.errors[:60]:
        print('ERROR', e)
    print('%d errors' % len(audit.errors))
    if a.json:
        json.dump({'stats': dict(audit.stats), 'errors': audit.errors}, open(a.json, 'w'), indent=1)
    return 0 if not audit.errors and mutation_ok else 1


if __name__ == '__main__':
    sys.exit(main())
