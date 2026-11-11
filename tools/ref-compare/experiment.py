"""SynthyBoy - ref-compare: controlled single-parameter experiments against the real Serum 2.

A preset is crafted from the plug-in's own init state (its processor state, read through tools/serum-reference --probe) with edits of module parameters, written as a
.SerumPreset container, rendered by the real Serum 2 (synthyboy_serum_reference) and by us (synthyboy_serum_import -> synthyboy_ref_render), and the two are measured.
Nothing here is committed with audio: the crafted presets and renders live in the experiment directory (tests/serum-reference/experiments, gitignored).

  from experiment import Experiment
  e = Experiment(root, build)                    # root = repository, build = CMake build directory
  p = e.preset('t1', {'Oscillator0.plainParams.kParamVolume': 0.5})
  ref, ours = e.render_both(p)                   # float arrays (frames, 2) at 48 kHz
"""
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))

_spec = importlib.util.spec_from_file_location('audit_serum_import', os.path.join(HERE, '..', '..', 'scripts', 'audit-serum-import.py'))
_audit = importlib.util.module_from_spec(_spec)
_argv = sys.argv
sys.argv = ['x']
try:
    _spec.loader.exec_module(_audit)
except SystemExit:
    pass
sys.argv = _argv


def cbor_encode(v):
    def head(mt, n):
        if n < 24:
            return bytes([mt << 5 | n])
        if n < 256:
            return bytes([mt << 5 | 24, n])
        if n < 65536:
            return bytes([mt << 5 | 25]) + n.to_bytes(2, 'big')
        if n < 2 ** 32:
            return bytes([mt << 5 | 26]) + n.to_bytes(4, 'big')
        return bytes([mt << 5 | 27]) + n.to_bytes(8, 'big')
    if v is None:
        return b'\xf6'
    if v is True:
        return b'\xf5'
    if v is False:
        return b'\xf4'
    if isinstance(v, int):
        return head(0, v) if v >= 0 else head(1, -1 - v)
    if isinstance(v, float):
        return b'\xfb' + struct.pack('>d', v)
    if isinstance(v, bytes):
        return head(2, len(v)) + v
    if isinstance(v, str):
        b = v.encode()
        return head(3, len(b)) + b
    if isinstance(v, list):
        return head(4, len(v)) + b''.join(cbor_encode(x) for x in v)
    if isinstance(v, dict):
        return head(5, len(v)) + b''.join(cbor_encode(k) + cbor_encode(x) for k, x in v.items())
    raise TypeError(type(v))


def make_container(header, value):
    body = cbor_encode(value)
    comp = subprocess.run(['zstd', '-c', '-q', '-19'], input=body, capture_output=True, check=True).stdout
    hdr = dict(header)
    hdr['hash'] = hashlib.md5(comp).hexdigest()
    hj = json.dumps(hdr, separators=(',', ':'), sort_keys=True).encode()
    return b'XferJson\0' + len(hj).to_bytes(8, 'little') + hj + len(body).to_bytes(4, 'little') + (2).to_bytes(4, 'little') + comp


def juce_b64_decode(s):
    alphabet = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
    n, _, enc = s.partition('.')
    out, val, pos = bytearray(), 0, 0
    for ch in enc:
        val |= alphabet.index(ch) << pos
        pos += 6
        while pos >= 8:
            out.append(val & 255)
            val >>= 8
            pos -= 8
    return bytes(out[:int(n)])


class Experiment:
    def __init__(self, root, build, workdir=None, plugin_args=()):
        self.root = root
        self.host = os.path.join(build, 'tools', 'serum-reference', 'synthyboy_serum_reference')
        self.importer = os.path.join(build, 'tools', 'serum-import', 'synthyboy_serum_import')
        self.ours_bin = os.path.join(build, 'tools', 'ref-compare', 'synthyboy_ref_render')
        self.work = workdir or os.path.join(root, 'tests', 'serum-reference', 'experiments')
        os.makedirs(self.work, exist_ok=True)
        self.plugin_args = list(plugin_args)
        self._init = None

    # ---- the init state of the plug-in ----
    def init_body(self):
        if self._init is None:
            dump = os.path.join(self.work, 'init-state')
            subprocess.run([self.host, '--probe', '--dump-state', dump] + self.plugin_args, capture_output=True, check=True)
            raw = open(dump + '.initial', 'rb').read()
            xml = raw[8:-1].decode()
            comp = xml.split('<IComponent>')[1].split('</IComponent>')[0]
            blob = juce_b64_decode(comp)
            path = os.path.join(self.work, 'init-processor.XferJson')
            open(path, 'wb').write(blob)
            hdr, val = _audit.load_serum(path)
            self._init = (hdr, val)
        return self._init

    def preset(self, name, edits=None, base=None):
        """A .SerumPreset (path) = the init processor body with `edits` ({'Oscillator0.plainParams.kParamVolume': 0.5, ...}) applied; `base` = a loaded body to start from instead."""
        import copy
        hdr, val = self.init_body()
        val = copy.deepcopy(base if base is not None else val)
        for path, v in (edits or {}).items():
            node = val
            parts = path.split('.')
            for p in parts[:-1]:
                if not isinstance(node.get(p), dict):
                    node[p] = {}
                node = node[p]
            node[parts[-1]] = v
        h = {'fileType': 'SerumPreset', 'presetName': name, 'presetAuthor': '', 'presetDescription': '', 'product': 'Serum2', 'productVersion': hdr['productVersion'],
             'tags': [], 'url': 'https://xferrecords.com/', 'vendor': 'Xfer Records', 'version': 9.0}
        d = os.path.join(self.work, 'presets')
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, name + '.SerumPreset')
        open(path, 'wb').write(make_container(h, val))
        return path

    # ---- renders ----
    def render_serum(self, preset, notes='60:100:0:2.5', length=4.0):
        out = os.path.join(self.work, 'wav', os.path.basename(preset) + '.' + hashlib.md5(notes.encode()).hexdigest()[:6] + '.serum.wav')
        r = subprocess.run([self.host, '--preset', preset, '--out', out, '--notes', notes, '--length', str(length), '--id', 'exp', '--allow-unchanged'] + self.plugin_args, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('serum render failed: ' + r.stderr[-300:])
        return read(out)

    def render_ours(self, preset, notes='60:100:0:2.5', length=4.0, overrides=''):
        imp = os.path.join(self.work, 'imported')
        os.makedirs(imp, exist_ok=True)
        subprocess.run([self.importer, '--in', preset, '--out', imp, '--serum-root', os.path.join(self.root, 'tests', 'serum-presets'), '--content-dir', os.path.join(imp, 'content')],
                       capture_output=True, check=True)
        sb = os.path.join(imp, os.path.splitext(os.path.basename(preset))[0] + '.sbpreset')
        out = os.path.join(self.work, 'wav', os.path.basename(preset) + '.' + hashlib.md5(notes.encode()).hexdigest()[:6] + '.ours.wav')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        mf = tempfile.NamedTemporaryFile('w', suffix='.tsv', delete=False)
        mf.write('\t'.join(['exp', sb, notes, str(length), out] + ([overrides] if overrides else [])) + '\n')
        mf.close()
        r = subprocess.run([self.ours_bin, '--manifest', mf.name, '--user-dir', imp], capture_output=True, text=True)
        os.unlink(mf.name)
        if 'FAIL' in r.stdout:
            raise RuntimeError('ours failed: ' + r.stdout)
        return read(out)

    def render_both(self, preset, notes='60:100:0:2.5', length=4.0):
        return self.render_serum(preset, notes, length), self.render_ours(preset, notes, length)


def read(path):
    sr, d = wavfile.read(path)
    return d.astype(np.float32)


def rms_db(x, lo=0.0, hi=2.5, sr=48000):
    seg = x[int(lo * sr):int(hi * sr)].astype(np.float64)
    return 10.0 * np.log10(max(float(np.mean(seg ** 2)), 1e-20))


def spectrum_peaks(x, lo=0.3, hi=2.0, sr=48000, n=6):
    """The strongest partials (Hz, dB) of the mono sustain, for a quick look at a tone."""
    seg = x[int(lo * sr):int(hi * sr)].mean(axis=1).astype(np.float64)
    w = np.hanning(len(seg))
    m = np.abs(np.fft.rfft(seg * w)) * 2 / w.sum()
    f = np.fft.rfftfreq(len(seg), 1 / sr)
    idx = [i for i in range(2, len(m) - 1) if m[i] > m[i - 1] and m[i] >= m[i + 1]]
    idx.sort(key=lambda i: -m[i])
    return [(round(float(f[i]), 1), round(float(20 * np.log10(m[i] + 1e-12)), 1)) for i in idx[:n]]


def harmonics_db(x, f0=261.63, n=24, lo=0.4, hi=2.0, sr=48000):
    """Level (dB) of the first n harmonics of f0 in the mono sustain: the quickest look at a filter or a waveshaper."""
    seg = x[int(lo * sr):int(hi * sr)].mean(axis=1).astype(np.float64)
    w = np.hanning(len(seg))
    m = np.abs(np.fft.rfft(seg * w)) * 2 / w.sum()
    out = []
    for k in range(1, n + 1):
        c = int(round(f0 * k * len(seg) / sr))
        out.append(20 * np.log10(max(float(m[max(0, c - 3):c + 4].max()), 1e-9)))
    return np.array(out)


def fit_override(e, preset, key, candidates, notes='60:100:0:2.5', length=4.0, target=None, dist=None):
    """Renders ours with `key` overridden to each candidate and returns [(distance to the Serum render, candidate)] sorted. `target` = the Serum audio."""
    if target is None:
        target = e.render_serum(preset, notes, length)
    dist = dist or (lambda a, b: float(np.mean(np.abs(harmonics_db(a) - harmonics_db(b)))))
    res = []
    for c in candidates:
        o = e.render_ours(preset, notes, length, overrides='%s=%g' % (key, c))
        res.append((dist(target, o), c))
    return sorted(res)
