#!/usr/bin/env python3
"""SynthyBoy - ref-compare: writes docs/ui-design/REFCOMPARE-REPORT.md from the results of a before run (the importer as it was), an after run, the Serum-vs-Serum noise floor and the
module catalog. The text is fixed here, the numbers come from the files, so the report can be written again after any run.

  make_doc.py --before RESULTS.json --after RESULTS.json --noise noise.json [--catalog GROUP=cat.json ...] --variants variants.json --out FILE.md [--tip HASH]

No preset names that reveal commercial content: only counts, categories and the metric families; the ids the report lists are the file ids the repository already lists."""
import argparse
import collections
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import report as rp  # noqa: E402


def load(p):
    return json.load(open(p))['records']


def summarize(recs):
    ok = [r for r in recs if r.get('score') is not None]
    counts = collections.OrderedDict((c, sum(1 for r in recs if r['class'] == c)) for c in ('MATCH', 'CLOSE', 'DIFFERENT', 'FAILED'))
    scores = [r['score'] for r in ok]
    med = {k: float(np.median([r[k] for r in ok if r.get(k) is not None])) for k in ('d_lufs', 'd_mel', 'd_stft', 'd_env_db', 'd_centroid', 'd_rolloff', 'd_seg_attack', 'd_seg_sustain',
                                                                                  'd_seg_release', 'd_peak', 'd_f0_cents', 'd_tail', 'd_sidemid')}
    fam = {}
    for f, names in rp.FAMILIES.items():
        v = [r['sub'][n] for r in ok for n in names if n in r.get('sub', {})]
        fam[f] = float(np.mean(v)) if v else None
    cat = collections.defaultdict(list)
    for r in ok:
        cat[rp.category(r['rel'])].append(r['score'])
    return {'counts': counts, 'mean': float(np.mean(scores)), 'median': float(np.median(scores)), 'hist': rp.histogram(scores), 'med': med, 'fam': fam, 'cat': {k: (len(v), float(np.mean(v))) for k, v in cat.items()},
            'n': len(recs), 'nok': len(ok), 'failed': collections.Counter(r['failed'] for r in recs if r.get('failed'))}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--before', required=True)
    ap.add_argument('--after', required=True)
    ap.add_argument('--noise', required=True)
    ap.add_argument('--catalog', action='append', default=[])
    ap.add_argument('--variants', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--tip', default='')
    a = ap.parse_args()
    B, A = summarize(load(a.before)), summarize(load(a.after))
    noise = json.load(open(a.noise))
    var = json.load(open(a.variants))['variants']
    L = []
    w = L.append
    w('# REFCOMPARE - the imported Serum 2 presets against the real Serum 2.1.5\n')
    w('Owner request: "get reference outputs as WAV for the presets, and after loading the preset check that our waveform is the same as in Serum" and "fix them too". Everything here was '
      'measured against the Serum 2.1.5 plug-in installed on the Mac (VST3), offline, 48 kHz, 32 bit float stereo, blocks of 512. No Serum audio and none of the 656 presets is committed '
      '(`tests/serum-reference/` and `tests/serum-presets/` are gitignored); the committed numbers are `docs/ui-design/refcompare-baseline.json` (score and class per render) and '
      '`docs/ui-design/refcompare-variants.json` (the calibration). Entry point: `scripts/run-ref-compare.sh` (usage in its header). Assumptions A488 .. A500 are in '
      '`13-new-keys.md` 4.46, owner questions 117 .. 122 in `14-morning-questions.md`.\n')
    w('## 1. Result\n')
    w('Renders: %d (656 presets with the C3 note, %d with a second pass: C1 for the basses, C5 for leads / plucks / chords / bells / arps). The same scoring (`tools/ref-compare/scoring.json`) '
      'was applied to the importer as it was (before) and as it is now (after), both with the content (tables, samples, instruments) loaded.\n' % (A['n'], A['n'] - 656))
    w('| class | before | after |\n|---|---:|---:|')
    for c in ('MATCH', 'CLOSE', 'DIFFERENT', 'FAILED'):
        w('| %s | %d | %d |' % (c, B['counts'][c], A['counts'][c]))
    w('\nMean score %.1f -> %.1f, median %.1f -> %.1f (0 .. 100).\n' % (B['mean'], A['mean'], B['median'], A['median']))
    w('FAILED renders are the presets whose Serum render is silent with the note script (%s): %s.\n' % (', '.join('%s %d' % kv for kv in A['failed'].items()) or 'none',
                                                                                                    'drums / loops that need other keys or a held pattern'))
    w('| score | before | after |\n|---|---:|---:|')
    for k in A['hist']:
        w('| %s | %d | %d |' % (k, B['hist'][k], A['hist'][k]))
    w('\nMedian distance per metric over all renders (smaller is closer; the unit is in the name):\n')
    w('| metric | before | after |\n|---|---:|---:|')
    names = [('d_lufs', 'loudness difference (LU)'), ('d_peak', 'peak difference (dB)'), ('d_mel', 'log-mel spectrogram (dB)'), ('d_stft', 'multi-resolution STFT loss'),
             ('d_env_db', '10 ms RMS envelope (dB)'), ('d_seg_attack', 'attack level (dB)'), ('d_seg_sustain', 'sustain level (dB)'), ('d_seg_release', 'release level (dB)'),
             ('d_centroid', 'spectral centroid (octaves)'), ('d_rolloff', 'spectral rolloff (octaves)'), ('d_f0_cents', 'fundamental of tonal presets (cents)'), ('d_sidemid', 'side / mid (dB)'),
             ('d_tail', 'tail to -60 dB (s)')]
    for k, n in names:
        w('| %s | %.2f | %.2f |' % (n, B['med'][k], A['med'][k]))
    w('\nMean sub-score of a feature family (0 .. 100):\n')
    w('| family | before | after |\n|---|---:|---:|')
    for f in A['fam']:
        w('| %s | %.1f | %.1f |' % (f, B['fam'][f], A['fam'][f]))
    w('\nMean score per category (the folder below Factory, User = the owner folder):\n')
    w('| category | n | before | after |\n|---|---:|---:|---:|')
    for c in sorted(A['cat']):
        w('| %s | %d | %.1f | %.1f |' % (c, A['cat'][c][0], B['cat'].get(c, (0, 0.0))[1], A['cat'][c][1]))
    w('\n## 2. Loading a preset into Serum 2 (tools/serum-reference)\n')
    w('The plug-in has 128 programs ("Prog 1" ..), 2623 parameters and no file-open API without its editor. What works is the plug-in state: `getStateInformation` returns a JUCE VST3 wrapper '
      '(`VC2!`, a size, an XML text with the base64 of the IComponent and the IEditController state); the IComponent state is an XferJson container (header member `component: processor`) whose '
      'body is the same CBOR map as a preset file. Route (a) with the raw file bytes does nothing; with the file wrapped as a state it is refused while the preset body carries the interface '
      'members (Osc, WTOsc, Filter, SerumGUI, preset name ..). The working route: take the members of the plug-in\'s own processor state, put in the preset\'s value for each (the plug-in\'s '
      'own value where the preset has none), pack the body as a zstd frame of raw blocks, write the MD5 of that into the header and put the container into the wrapper of the state the plug-in '
      'reported. The plug-in loads headless, needs no licence dialog, reports a latency of 0 samples and renders about 2.5 times faster than real time. The host is a JUCE console program '
      '(`synthyboy_serum_reference`, macOS only, not built elsewhere), the batch runner (`render_all.py`) starts one process per render with a timeout, runs at nice 19, skips what has a sidecar '
      'with the same preset hash, and records a failure per render (`.fail.json`) instead of stopping. 956 renders took 22 minutes; 2 failed once through an exit race of the first host '
      'version (fixed, retried).\n')
    w('## 3. Noise floor (Serum against Serum)\n')
    w('All %d renders were made twice. **%d (%.1f %%) are bit identical**; the others differ in the random start phase / random modulation / unison, and that distance is the tolerance floor of '
      'every metric: the 95th percentile of each distance is the floor `scoring.json` subtracts before a distance costs points (and for a preset that has two reference renders, its own distance '
      'when larger, `--ref2-dir`).\n' % (noise['pairs'], noise['bit_identical'], 100.0 * noise['bit_identical'] / noise['pairs']))
    w('| metric | median | 95th percentile (floor) | 99th | max |\n|---|---:|---:|---:|---:|')
    for k in ('d_mel', 'd_stft', 'd_env_corr', 'd_env_db', 'd_lufs', 'd_peak', 'd_centroid', 'd_rolloff', 'd_f0_cents', 'd_sidemid', 'd_tail'):
        s = noise['stats'][k]
        w('| %s | %.3f | %.3f | %.3f | %.2f |' % (k, s['p50'], s['p95'], s['p99'], s['max']))
    w('\n## 4. The score\n')
    w('Per render, after the time alignment (the first transient, cross-correlation of the 1 ms envelopes, reported; most renders have offset 0): (1) 10 ms RMS envelope correlation and per '
      'segment level (attack 0 .. 0.25 s, sustain, release); (2) log-mel (80 bands, 2048 / 512) and multi-resolution STFT (512, 1024, 2048: spectral convergence + log magnitude); (3) spectral '
      'centroid and 85 % rolloff trajectories; (4) fundamental in cents for tonal presets (an octave counts as 600 cents); (5) L/R balance, side/mid, inter-channel correlation; (6) integrated '
      'loudness (BS.1770-4 LUFS) and peak; (7) the quietest 50 ms of the release; (8) tail to -60 dB. Each distance becomes a sub-score `100 * clip(1 - max(0, d - floor) / span)`, the score is '
      'the weighted mean (weights in `scoring.json`: spectrum 40, envelope and segments 31, loudness 15, fundamental 16, stereo 10, others; applicable features only). MATCH >= 85, '
      'CLOSE >= 65.\n')
    w('**Calibration.** The spans were set so that deliberately broken versions of our own render score clearly lower: 5 crafted presets whose render matches Serum (scores %s) and 4 breaks '
      'of each (50 cents sharp, oscillator A off, cutoff three octaves down, 6 dB down) = 20 variants. The smallest drop is **%.1f points**, none of the 20 is a MATCH, the drops of the four '
      'breaks have the medians %s (`catalog.py --group calib`, `docs/ui-design/refcompare-variants.json`, exit 1 of `run-ref-compare.sh --calibrate` if one is not lower by 10 points). '
      'The same checks on synthetic signals are the ctest `synthyboy_ref_compare_metrics`.\n' % (
          ', '.join('%.0f' % v for v in sorted({t['base']: t['base_score'] for t in var}.values())), min(t['drop'] for t in var),
          ', '.join('%s %.0f' % (b, float(np.median([t['drop'] for t in var if t['break'] == b]))) for b in ('tune50', 'osc_off', 'cutoff', 'level6'))))
    w('## 5. What was found and fixed\n')
    w('Found with controlled single-parameter experiments (a crafted preset = the init state of the plug-in plus one edit, rendered by Serum and by us, fitted: `tools/ref-compare/experiment.py`). '
      'Every fix has an assumption id (13-new-keys.md 4.46) and importer tests (`synthyboy_test_serum_import`, 35 cases).\n')
    for t in (
            'Levels (A488, A489): the file values of the master, oscillator, sub and noise volume are linear gains (the plug-in shows their square root); defaults master 0.25, oscillator 0.5625; '
            'the earlier importer assumed percent / our defaults, so every preset was 6 .. 10 dB off. Serum does not scale the level with the velocity (a velocity 64 note was 8 dB too quiet).',
            'Defaults of absent parameters (A490): "default" blocks and empty maps took our defaults instead of Serum\'s (envelope 1 attack 1 ms, release 15 ms, MG Low 12 filter, cutoff 0.5).',
            'Filter (A491, A492): the cutoff scale is 2 semitones above the 135 note law; the drive is mostly a louder output (2 .. 13 dB off at drive 50 .. 100).',
            'Envelopes (A493): the attack curve had the wrong sign; the curve knobs map through a measured table (the envelope catalog score 85.5 -> 95.6).',
            'Wavetable position (A494): position / 256, not (position - 1) / 255 (a 19 dB spectral difference on 9 frame tables).',
            'Compressor threshold (A495): it is not a linear level; the measured ceiling is mapped (the error went from 40 dB to 2 .. 10 dB).',
            'Unison level (A496): it follows the blend and the parity of the voice count in Serum; corrected.',
            'Content (A497): the first comparison rendered without the stored tables / samples (47 silent presets); the render tool now uses the import\'s content store.'):
        w('- ' + t)
    w('\n## 6. What could not be matched, and why (measured)\n')
    for t in (
            '**Effects.** The effect modules differ by 3 .. 18 LU on single module presets (catalog below). Only the compressor threshold was fitted; reverb, delay, chorus, flanger, phaser, '
            'hyper / dimension, Bode, convolve and the distortion types still use the earlier parameter laws (question 121). The voice alone ("nofx" set, effects removed in both) scored '
            '48.9 in the first run against 40.2 with the effects: the effects are 9 points of the gap.',
            '**EQ.** The Serum EQ has two bands, each Peak / High pass or Low pass / Shelf; ours has one peak band. Two peaks (most of the 738 EQ modules) end as a peak and a shelf; the '
            'high / low pass has another resonance law (7 .. 8 dB off at resonance 0 .. 20) (question 118).',
            '**Unison.** The level is corrected, the spectra are not: at low blend Serum silences the outer voices, ours does not (mel distance 3 .. 14 dB) (question 119).',
            '**Compressor depth.** Our compressor stays 3 .. 10 dB short of Serum\'s reduction above threshold 0.6 (19 % of the compressor modules) (question 120).',
            '**Modulation timing.** An envelope used as a modulation source decays slower in our engine than the Serum envelope of the same settings (the centroid of an envelope swept filter '
            'is 200 .. 400 Hz higher 0.3 s after the note on); the amplitude envelope is fitted, the modulation path is not.',
            '**Randomness.** Serum itself differs between two renders for 11.3 % of the presets; the scores of those presets have that noise built in (the floor above).',
            '**Spectra of complex presets.** After the loudness is matched the median log-mel distance is still 13 dB (15 dB before): effects, warp modes (PWM / quantize / ring-mod / self-PD '
            '30 .. 50 points under the catalog mean), sample / granular / spectral oscillators and the modulation matrix together; these are not single causes.'):
        w('- ' + t)
    cats = []
    for spec in a.catalog:
        g, p = spec.split('=', 1)
        rows = json.load(open(p))
        cats.append((g, rows))
    if cats:
        w('\n## 7. Module catalog (single module presets, scored like the presets)\n')
        w('| group | cases | mean score | MATCH | CLOSE | DIFFERENT |\n|---|---:|---:|---:|---:|---:|')
        for g, rows in cats:
            sc = [r['score'] for r in rows]
            w('| %s | %d | %.1f | %d | %d | %d |' % (g, len(rows), np.mean(sc), sum(1 for r in rows if r['class'] == 'MATCH'), sum(1 for r in rows if r['class'] == 'CLOSE'),
                                                     sum(1 for r in rows if r['class'] == 'DIFFERENT')))
        w('\nThe effect cases are real effect entries of the presets (realistic units), the others crafted values; `run-ref-compare.sh --catalog GROUP` reproduces the table.\n')
    w('## 8. Files\n')
    w('- `tools/serum-reference/` host (C++/JUCE) and `render_all.py`; `tools/ref-compare/` `ref_render.cpp` (our offline render), `ref_compare.py`, `metrics.py`, `score.py`, `scoring.json`, '
      '`report.py`, `experiment.py`, `catalog.py`, `ablate.py`, `make_doc.py`, `test_metrics.py`; `scripts/run-ref-compare.sh`.')
    w('- Reports of a run: `tests/serum-reference/report/<timestamp>/` (report.md, results.csv / json, summary.json, `ab/` with the reference and our render of the worst 30 and 20 random '
      'renders, `.ab.wav` = reference, 1 s gap, ours; `.lr.wav` = reference left, ours right).')
    open(a.out, 'w').write('\n'.join(L) + '\n')
    print('wrote', a.out)


if __name__ == '__main__':
    main()
