#!/usr/bin/env python3
"""SynthyBoy - ref-compare: writes (or replaces) section 9 "Round 2" of docs/ui-design/REFCOMPARE-REPORT.md from two results files of the same scoring: the run before round 2 (develop as it was)
and the run after it. The text of the laws is fixed here, the numbers come from the files.

  make_doc_round2.py --before RESULTS.json --after RESULTS.json --voice-before RESULTS.json --voice-after RESULTS.json --out docs/ui-design/REFCOMPARE-REPORT.md [--tip HASH]

--voice-*: the same comparison on the voice only set (the 656 presets without the effect racks, rendered by the real Serum 2 too): the effects are not part of the numbers there.
"""
import argparse
import collections
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import report as rp  # noqa: E402


def load(p):
    return json.load(open(p))['records']


def stats(recs):
    ok = [r for r in recs if r.get('score') is not None]
    gap = [r['r_lufs_ours'] - r['r_lufs_ref'] for r in recs if r.get('r_lufs_ours') is not None and r.get('r_lufs_ref') is not None and r['r_lufs_ref'] > -60 and r['r_lufs_ours'] > -90]
    med = lambda k: float(np.median([r[k] for r in ok if r.get(k) is not None]))
    return {'n': len(recs), 'counts': collections.Counter(r['class'] for r in recs), 'mean': float(np.mean([r['score'] for r in ok])), 'median': float(np.median([r['score'] for r in ok])),
            'gap_med': float(np.median(gap)), 'gap_abs': float(np.mean(np.abs(gap))), 'gap6': int(np.sum(np.abs(gap) > 6)), 'ngap': len(gap),
            'd_lufs': med('d_lufs'), 'd_mel': med('d_mel'), 'd_stft': med('d_stft'), 'd_env_db': med('d_env_db'), 'd_centroid': med('d_centroid'), 'd_sidemid': med('d_sidemid'),
            'fam': {f: float(np.mean([r['sub'][n] for r in ok for n in names if n in r.get('sub', {})])) for f, names in rp.FAMILIES.items()}}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--before', required=True)
    ap.add_argument('--after', required=True)
    ap.add_argument('--voice-before', default='')
    ap.add_argument('--voice-after', default='')
    ap.add_argument('--out', required=True)
    ap.add_argument('--tip', default='')
    a = ap.parse_args()
    B, A = stats(load(a.before)), stats(load(a.after))
    L = []
    w = L.append
    w('## 9. Round 2 (branch refcmp-a): level laws, the unison law of Serum as an engine capability\n')
    w('Same scoring, same 956 renders, the Serum references of round 1 (not rendered again). Tip: %s.\n' % (a.tip or 'see git log'))
    w('| | before | after |\n|---|---:|---:|')
    for c in ('MATCH', 'CLOSE', 'DIFFERENT', 'FAILED'):
        w('| %s | %d | %d |' % (c, B['counts'][c], A['counts'][c]))
    w('| mean score | %.2f | %.2f |' % (B['mean'], A['mean']))
    w('| median score | %.2f | %.2f |' % (B['median'], A['median']))
    w('| median loudness gap ours - Serum (LU) | %+.2f | %+.2f |' % (B['gap_med'], A['gap_med']))
    w('| mean absolute loudness gap (LU) | %.2f | %.2f |' % (B['gap_abs'], A['gap_abs']))
    w('| renders more than 6 LU off | %d of %d | %d of %d |' % (B['gap6'], B['ngap'], A['gap6'], A['ngap']))
    w('| median log-mel distance (dB) | %.2f | %.2f |' % (B['d_mel'], A['d_mel']))
    w('| median multi-resolution STFT loss | %.2f | %.2f |' % (B['d_stft'], A['d_stft']))
    w('| median 10 ms envelope distance (dB) | %.2f | %.2f |' % (B['d_env_db'], A['d_env_db']))
    w('| median spectral centroid distance (octaves) | %.2f | %.2f |' % (B['d_centroid'], A['d_centroid']))
    w('')
    w('| family (mean sub-score) | before | after |\n|---|---:|---:|')
    for f in B['fam']:
        w('| %s | %.1f | %.1f |' % (f, B['fam'][f], A['fam'][f]))
    w('')
    if a.voice_before and a.voice_after:
        VB, VA = stats(load(a.voice_before)), stats(load(a.voice_after))
        w('The voice only set (the 656 presets with the effect racks removed in both Serum and ours, C3 only, 656 renders): mean score %.1f -> %.1f, MATCH %d -> %d, CLOSE %d -> %d, median loudness gap %+.2f -> %+.2f LU.\n' % (
            VB['mean'], VA['mean'], VB['counts']['MATCH'], VA['counts']['MATCH'], VB['counts']['CLOSE'], VA['counts']['CLOSE'], VB['gap_med'], VA['gap_med']))
    w('### 9.1 What was found (measured on the real plug-in, controlled single parameter experiments; assumptions A501 .. A507 in `13-new-keys.md` 4.47, questions 123 .. 128)\n')
    w('- **The 211 renders that got worse in round 1 were not the master level.** A master sweep 0.02 .. 1 on a crafted preset agrees with the plug-in within 0.1 dB above 0.1 (the linear gain law of A488 is right). '
      'They came from the same level law that was assumed wrongly in the rows of the filter and of every effect: the level knob `kParamLevelOut` is the gain **(2 v) ^ 2** in Serum (default 0.5 = unity, 1 = +12.04 dB), '
      'the importer had 20 log10 v (6 dB too quiet at the default value, 12 dB at 1): the presets with a high master (or any value of the knob above its default) came out too loud after the round 1 master fix, the others too quiet. '
      'Fixed for the voice filters and the 12 effect rows (A507). The DIRECT and BUS level knobs are linear gain x 2 (default 0.5) and the bus sends are cubic (a send of 50 % adds 1.02 dB), A502.')
    w('- **A measurement artefact on every first note.** The offline render of our engine heard the smoothers ramp from the defaults to the loaded values for about 100 ms (a 25 dB thump on a closed filter that Serum, rendered after eight silent blocks, does not have). `synthyboy_ref_render` calls `engine->reset()` after the load (A504); every earlier first-note comparison was affected.')
    w('- **Unison (A503).** The voices of Serum are evenly spaced, the outer ones at 200 x d ^ 1.25 cents (d = 0.25 by default); the level of the centre voice (one for an odd count, two for an even count) and of the outer voices follows the blend by two shapes that are the same for 3 .. 16 voices times a gain per voice count (0.4384 at 3, 0.3016 at 16, 0.32 each at 2); the stereo spread moves a voice into its own channel with the measured gains (2.52 at 100 %) and does not move or change the centre voice. Ours had the Vital law (the centre voice 6 dB too loud at high blend for odd counts, the voices not evenly spaced). This is now the engine capability `oscN.unison.law = Serum` (key block B33, `kCap2SerumUnison`, off / on proof and the measured levels in `core/engine-vital/tests/test_serum_unison.cpp`); the importer sets it and the linear detune; the level correction of A496 is gone. Measured table: `tools/ref-compare/` (the experiments are in the commit message of the engine change).')
    w('- **Filters (A501).** The resonance of the state variable types (L / H / B 12 .. 24) takes level (-6.5 dB at 30, -13 dB at 100: the 7 .. 8 dB difference at resonance 0 .. 20 for HP / LP in the earlier note was this and the first note artefact), the ladders keep their level with the resonance and get louder with the drive, DirtyMg gets quieter; `tools/ref-compare/fit_filter.py` measures a type on a grid, `make_filter_laws.py` writes the tables. The shapes of the MG and state variable types match (residual 0.2 .. 1.5 dB over 40 harmonics). LadderEMS and HP12 stay uncorrected (the static law made the real presets worse, question 123).')
    w('- **Multisample instruments (A505):** 10.9 dB quieter than Serum at the same oscillator volume (30 instruments, spread 1.6 dB); the importer adds it to the zones.')
    w('- **Checked and fine:** the master level law, the envelope sustain law (the amplitude squared in both), the filter cutoff scale and the modulation of the cutoff by an envelope (+100 / +50 / -50 % at the centroid within 5 %), unison detune of a single voice, the bus level law after the fix.')
    w('')
    w('### 9.2 What remains and why\n')
    w('- **Effect chains.** Single effect modules are within 3 dB (catalog), but chains (two compressors and two EQs: 10 dB short; flanger, phaser, bode, utility, convolve, splitters have the largest per module level gaps) leave a median loudness bias and a wide scatter (mean absolute gap %.1f LU); the effects round (questions 118, 120, 121, 124).' % A['gap_abs'])
    w('- **Arpeggiator and clips (34 presets, question 127):** not modelled, the median loudness gap of those presets is about -9 LU.')
    w('- **Sample, granular and spectral oscillators (question 125),** the start thump and the cutoff floor of a closed filter (question 126), the static filter law of LadderEMS / HP12 (question 123), warp modes (FM from the sub, tube distortion: 5 .. 10 LU) and the modulation matrix timing.')
    w('- **No gate was lowered, no preset was special cased**; the scoring and the thresholds are those of round 1.')
    text = '\n'.join(L) + '\n'
    doc = open(a.out).read()
    m = re.search(r'\n## 9\. Round 2.*', doc, re.S)
    doc = (doc[:m.start()] if m else doc.rstrip('\n')) + '\n\n' + text
    open(a.out, 'w').write(doc)
    print('wrote section 9 of', a.out)


if __name__ == '__main__':
    main()
