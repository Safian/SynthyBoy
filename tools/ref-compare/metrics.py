"""SynthyBoy - ref-compare: the audio-similarity metrics (numpy / scipy, deterministic: no randomness, no threads, fixed FFT sizes).

Every metric returns a *distance* (0 = identical, larger = further apart) in a unit a person can read (dB, octaves, cents, seconds, LU) together with the raw values of both
sides, so a report can say which metric is off and by how much. The mapping of the distances to a 0..100 score is in score.py (calibrated, scoring.json).
"""
import math

import os

for _v in ('OPENBLAS_NUM_THREADS', 'OMP_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ.setdefault(_v, '1')   # the comparison runs in parallel processes: no thread oversubscription inside one

import numpy as np  # noqa: E402
from scipy import signal, sparse  # noqa: E402

SR = 48000
EPS_DB = -120.0


def db(x, floor=EPS_DB):
    return np.maximum(20.0 * np.log10(np.maximum(x, 1e-12)), floor)


def pdb(x, floor=EPS_DB):
    return np.maximum(10.0 * np.log10(np.maximum(x, 1e-24)), floor)


def mono(a):
    return 0.5 * (a[:, 0] + a[:, 1])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# time alignment: the first transient
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def rms_env(x, win, hop):
    """RMS envelope (linear) with window `win` and hop `hop` samples (centered frames)."""
    n = len(x)
    if n < win:
        return np.array([math.sqrt(float(np.mean(x * x)) if n else 0.0)])
    c = np.concatenate(([0.0], np.cumsum(x.astype(np.float64) ** 2)))
    starts = np.arange(0, n - win + 1, hop)
    return np.sqrt((c[starts + win] - c[starts]) / win)


def estimate_offset(ref, ours, max_ms=50.0, window_s=0.5):
    """Offset of `ours` relative to `ref` in samples (positive = ours is later), by the cross-correlation of the 1 ms RMS envelopes (dB, clipped at -50 dB under their own peak)
    of the first `window_s` seconds. Both are normalised by their own peak, so a level difference does not matter, only when the first transient happens."""
    hop = SR // 1000
    e = []
    for x in (ref, ours):
        env = rms_env(mono(x)[: int((window_s + 0.1) * SR)], 2 * hop, hop)
        peak = float(env.max()) if len(env) else 0.0
        if peak < 1e-7:
            return 0, False
        e.append(np.clip(db(env / peak), -50.0, 0.0) + 50.0)
    n = min(len(e[0]), len(e[1]), int(window_s * 1000))
    ea, eb = e[0][:n], e[1][:n]
    maxlag = int(max_ms)
    best, lag_best = -2.0, 0
    for lag in range(-maxlag, maxlag + 1):
        # normalised cross-correlation (Pearson) over the part where both exist: ours later than ref by `lag` ms when eb[t + lag] ~ ea[t]
        if lag >= 0:
            x, y = ea[: n - lag], eb[lag:n]
        else:
            x, y = ea[-lag:n], eb[: n + lag]
        if len(x) < 100:
            continue
        x, y = x - x.mean(), y - y.mean()
        den = math.sqrt(float(np.dot(x, x)) * float(np.dot(y, y)))
        if den < 1e-9:
            continue
        v = float(np.dot(x, y)) / den
        if v > best + 1e-9:
            best, lag_best = v, lag
    if best < 0.5:
        return 0, False
    return lag_best * hop, True


def shift(x, samples, n):
    """`x` (frames, 2) delayed by -samples (so that a positive offset is removed), padded / trimmed to n frames."""
    out = np.zeros((n, x.shape[1]), dtype=np.float32)
    if samples >= 0:
        m = min(n, len(x) - samples)
        if m > 0:
            out[:m] = x[samples:samples + m]
    else:
        m = min(n - (-samples), len(x))
        if m > 0:
            out[-samples:-samples + m] = x[:m]
    return out


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# spectral analysis
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
_WIN_CACHE = {}
_MEL_CACHE = {}


def stft_mag(x, n_fft, hop):
    """Magnitude spectrogram (frames, bins), Hann window, scaled so that a full-scale sine reads 1.0 in its bin."""
    w = _WIN_CACHE.setdefault(n_fft, np.hanning(n_fft + 1)[:-1].astype(np.float64))
    n = len(x)
    if n < n_fft:
        x = np.pad(x, (0, n_fft - n))
        n = n_fft
    frames = 1 + (n - n_fft) // hop
    idx = np.arange(n_fft)[None, :] + hop * np.arange(frames)[:, None]
    spec = np.fft.rfft(x[idx].astype(np.float64) * w[None, :], axis=1)
    return np.abs(spec) * (2.0 / w.sum())


def mel_bank(n_fft, n_mels=80, fmin=30.0, fmax=20000.0):
    key = (n_fft, n_mels, fmin, fmax)
    if key in _MEL_CACHE:
        return _MEL_CACHE[key]

    def hz2mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)

    def mel2hz(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    pts = mel2hz(np.linspace(hz2mel(fmin), hz2mel(fmax), n_mels + 2))
    freqs = np.linspace(0, SR / 2, n_fft // 2 + 1)
    fb = np.zeros((n_mels, len(freqs)))
    for m in range(n_mels):
        lo, ce, hi = pts[m], pts[m + 1], pts[m + 2]
        up = (freqs - lo) / max(ce - lo, 1e-9)
        dn = (hi - freqs) / max(hi - ce, 1e-9)
        fb[m] = np.maximum(0.0, np.minimum(up, dn))
    fb /= np.maximum(fb.sum(axis=1, keepdims=True), 1e-12)   # area normalised: a flat spectrum reads the same in every band
    _MEL_CACHE[key] = sparse.csr_matrix(fb)
    return _MEL_CACHE[key]


def log_mel(x, n_fft=2048, hop=512):
    mag = stft_mag(x, n_fft, hop)
    return pdb((mel_bank(n_fft) @ (mag ** 2).T).T, -100.0)


def mel_distance(a, b):
    """Mean absolute difference (dB) of the log-mel spectrograms over the time-frequency cells where either side is audible (above -90 dB), cells clipped at 40 dB."""
    A, B = log_mel(a), log_mel(b)
    n = min(len(A), len(B))
    A, B = A[:n], B[:n]
    active = (np.maximum(A, B) > -90.0)
    if not active.any():
        return 0.0
    return float(np.mean(np.minimum(np.abs(A - B)[active], 40.0)))


def mr_stft_loss(a, b, sizes=(512, 1024, 2048)):
    """Multi-resolution STFT loss (as in audio-similarity work, e.g. Parallel WaveGAN / auraloss): per resolution the spectral convergence ||X-Y||_F / ||X||_F (X = reference) and
    the L1 distance of the log magnitudes (natural log, magnitudes floored at 1e-5), averaged over three resolutions. Returns (sc, logmag, combined = sc + logmag)."""
    scs, lms = [], []
    for n_fft in sizes:
        X, Y = stft_mag(a, n_fft, n_fft // 4), stft_mag(b, n_fft, n_fft // 4)
        n = min(len(X), len(Y))
        X, Y = X[:n], Y[:n]
        denom = float(np.linalg.norm(X))
        scs.append(float(np.linalg.norm(X - Y)) / denom if denom > 1e-9 else (0.0 if float(np.linalg.norm(Y)) < 1e-9 else 1.0))
        lms.append(float(np.mean(np.abs(np.log(np.maximum(X, 1e-5)) - np.log(np.maximum(Y, 1e-5))))))
    sc, lm = float(np.mean(scs)), float(np.mean(lms))
    return sc, lm, sc + lm


def centroid_rolloff(x, n_fft=2048, hop=512, roll=0.85):
    mag = stft_mag(x, n_fft, hop)
    power = mag ** 2
    freqs = np.linspace(0, SR / 2, n_fft // 2 + 1)
    tot = power.sum(axis=1)
    cen = (power * freqs[None, :]).sum(axis=1) / np.maximum(tot, 1e-30)
    cs = np.cumsum(power, axis=1)
    ro = freqs[np.minimum((cs < (roll * tot)[:, None]).sum(axis=1), len(freqs) - 1)]
    level = pdb(tot, -120.0)
    return cen, ro, level


def trajectory_distance(a, b):
    """Mean |log2 ratio| (octaves) of the spectral centroid and of the 85 % rolloff over the frames where both are above -70 dB (total power in the frame)."""
    ca, ra, la = centroid_rolloff(a)
    cb, rb, lb = centroid_rolloff(b)
    n = min(len(ca), len(cb))
    ok = (la[:n] > -70.0) & (lb[:n] > -70.0)
    if not ok.any():
        return 0.0, 0.0, float(np.median(ca)), float(np.median(cb))
    dc = float(np.mean(np.abs(np.log2(np.maximum(ca[:n][ok], 20.0) / np.maximum(cb[:n][ok], 20.0)))))
    dr = float(np.mean(np.abs(np.log2(np.maximum(ra[:n][ok], 20.0) / np.maximum(rb[:n][ok], 20.0)))))
    return dc, dr, float(np.median(ca[:n][ok])), float(np.median(cb[:n][ok]))


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# envelope, segments, tail
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def envelope_db(x, ms=10):
    hop = SR * ms // 1000
    return db(rms_env(mono(x) if x.ndim == 2 else x, hop, hop), -100.0)


def envelope_distance(a, b):
    """(1 - Pearson correlation) and the mean absolute difference (dB) of the 10 ms RMS envelopes (dB, floored at -80), over the frames where either side is above -70 dB."""
    ea, eb = np.maximum(envelope_db(a), -80.0), np.maximum(envelope_db(b), -80.0)
    n = min(len(ea), len(eb))
    ea, eb = ea[:n], eb[:n]
    act = np.maximum(ea, eb) > -70.0
    if act.sum() < 3:
        return 0.0, 0.0
    x, y = ea[act], eb[act]
    mae = float(np.mean(np.abs(x - y)))
    if x.std() < 0.5 and y.std() < 0.5:
        corr = 1.0   # two flat envelopes: the level difference is in mae
    elif x.std() < 1e-6 or y.std() < 1e-6:
        corr = 0.0
    else:
        corr = float(np.corrcoef(x, y)[0, 1])
    return 1.0 - corr, mae


def segment_levels(x, t_on, t_off, total):
    """Mean power level (dB) of attack (on .. on+0.25 s), sustain (on+0.25 s .. off) and release (off .. end) of the mono signal."""
    m = mono(x).astype(np.float64)
    out = []
    for lo, hi in ((t_on, t_on + 0.25), (t_on + 0.25, t_off), (t_off, total)):
        i, j = int(lo * SR), int(min(hi, total) * SR)
        seg = m[i:j]
        out.append(float(pdb(np.mean(seg * seg), -100.0)) if len(seg) else -100.0)
    return out


def tail_time(x, t_off, threshold_db=-60.0):
    """Seconds after the note-off until the 20 ms RMS envelope (max of both channels) is below the threshold for good (the render ends 1.5 s after the note-off: that is the cap)."""
    hop = SR * 20 // 1000
    env = np.maximum(db(rms_env(x[:, 0], hop, hop), -120.0), db(rms_env(x[:, 1], hop, hop), -120.0))
    i0 = int(t_off * 1000 / 20)
    seg = env[i0:]
    if len(seg) == 0:
        return 0.0
    above = np.nonzero(seg > threshold_db)[0]
    if len(above) == 0:
        return 0.0
    return float((above[-1] + 1) * 0.02)


def noise_floor_db(x, t_off):
    """Level (dB RMS) of the quietest 50 ms window of the release phase."""
    seg = mono(x)[int(t_off * SR):]
    win = SR // 20
    if len(seg) < win:
        return -120.0
    env = rms_env(seg, win, win // 2)
    return float(db(np.array([env.min()]), -120.0)[0])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# pitch
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def f0_track(x, t_from, t_to, step=0.2, win=4096):
    """Pitch (Hz) and clarity per analysis point: normalised autocorrelation of the mono signal with the McLeod rule (the first key maximum above 0.93 of the highest one)."""
    m = mono(x).astype(np.float64)
    w = np.hanning(win)
    lo, hi = SR // 2000, SR // 30
    out = []
    t = t_from
    while t + win / SR <= t_to:
        i = int(t * SR)
        seg = m[i:i + win] * w
        if float(np.dot(seg, seg)) < 1e-10:
            t += step
            continue
        n2 = 2 * win
        f = np.fft.rfft(seg, n2)
        ac = np.fft.irfft(f * np.conj(f))[:win]
        cs = np.cumsum(seg ** 2)
        lags = np.arange(1, min(hi, win - 1) + 1)
        m_tau = cs[win - 1 - lags] + (cs[win - 1] - cs[lags - 1])   # sum of x_j^2 + x_(j+tau)^2 over the overlap
        nsdf = np.zeros(win)
        nsdf[lags] = 2.0 * ac[lags] / np.maximum(m_tau, 1e-12)
        seg_n = nsdf[lo:hi + 1]
        peaks = [k for k in range(1, len(seg_n) - 1) if seg_n[k] > seg_n[k - 1] and seg_n[k] >= seg_n[k + 1] and seg_n[k] > 0.0]
        if peaks:
            top = max(seg_n[k] for k in peaks)
            k = next(k for k in peaks if seg_n[k] >= 0.93 * top)
            a, b, c = seg_n[k - 1], seg_n[k], seg_n[k + 1]
            den = a - 2 * b + c
            frac = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
            out.append((SR / (lo + k + frac), float(seg_n[k])))
        else:
            out.append((0.0, 0.0))
        t += step
    return out


def pitch_comparison(a, b, t_from, t_to):
    """(cents error of b against a folded to +-600, octave error flag, f0 a, f0 b, tonal_a, tonal_b). Tonal = the median clarity is above 0.8."""
    ta, tb = f0_track(a, t_from, t_to), f0_track(b, t_from, t_to)
    if not ta or not tb:
        return None
    ca, cb = float(np.median([c for _, c in ta])), float(np.median([c for _, c in tb]))
    fa = float(np.median([f for f, c in ta if f > 0])) if any(f > 0 for f, _ in ta) else 0.0
    fb = float(np.median([f for f, c in tb if f > 0])) if any(f > 0 for f, _ in tb) else 0.0
    tonal_a, tonal_b = ca > 0.8, cb > 0.8
    if fa <= 0 or fb <= 0:
        return None
    cents = 1200.0 * math.log2(fb / fa)
    folded = (cents + 600.0) % 1200.0 - 600.0
    octave = abs(cents) > 600.0
    return folded, octave, fa, fb, tonal_a, tonal_b, cents


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# stereo, loudness
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def stereo_stats(x):
    l, r = x[:, 0].astype(np.float64), x[:, 1].astype(np.float64)
    el, er = float(np.mean(l * l)), float(np.mean(r * r))
    mid, side = 0.5 * (l + r), 0.5 * (l - r)
    em, es = float(np.mean(mid * mid)), float(np.mean(side * side))
    if el < 1e-14 and er < 1e-14:
        return 0.0, -60.0, 1.0
    bal = float(pdb(np.array([el + 1e-14]))[0] - pdb(np.array([er + 1e-14]))[0])
    sm = float(pdb(np.array([es + 1e-14]))[0] - pdb(np.array([em + 1e-14]))[0])
    if l.std() < 1e-9 or r.std() < 1e-9:
        corr = 1.0 if (l.std() < 1e-9 and r.std() < 1e-9) else 0.0
    else:
        corr = float(np.corrcoef(l, r)[0, 1])
    return bal, max(sm, -60.0), corr


_K1 = (np.array([1.53512485958697, -2.69169618940638, 1.19839281085285]), np.array([1.0, -1.69065929318241, 0.73248077421585]))
_K2 = (np.array([1.0, -2.0, 1.0]), np.array([1.0, -1.99004745483398, 0.99007225036621]))


def lufs(x):
    """Integrated loudness (ITU-R BS.1770-4, K-weighted, 400 ms blocks with 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU) of a 48 kHz stereo signal; -100 if silent."""
    ch = []
    for c in range(2):
        y = signal.lfilter(_K1[0], _K1[1], x[:, c].astype(np.float64))
        ch.append(signal.lfilter(_K2[0], _K2[1], y))
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    if len(ch[0]) < blk:
        ch = [np.pad(c, (0, blk - len(c))) for c in ch]
    ms = []
    for s in range(0, len(ch[0]) - blk + 1, hop):
        ms.append(sum(float(np.mean(c[s:s + blk] ** 2)) for c in ch))
    ms = np.array(ms)
    loud = -0.691 + 10.0 * np.log10(np.maximum(ms, 1e-30))
    g = ms[loud > -70.0]
    if len(g) == 0:
        return -100.0
    rel = -0.691 + 10.0 * np.log10(g.mean()) - 10.0
    g2 = ms[loud > max(-70.0, rel)]
    if len(g2) == 0:
        return -100.0
    return float(-0.691 + 10.0 * np.log10(g2.mean()))


def peak_db(x):
    return float(db(np.array([np.abs(x).max()]), -120.0)[0])


# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
# everything for one pair
# ------------------------------------------------------------------------------------------------------------------------------------------------------------------
def compare_pair(ref, ours, t_on=0.0, t_off=2.5, total=4.0):
    """`ref`, `ours`: float arrays (frames, 2) at 48 kHz, already time-aligned and of equal length. Returns a dict of distances (key d_*) and raw values (key r_*)."""
    out = {}
    ma, mb = mono(ref), mono(ours)
    out['r_peak_ref'], out['r_peak_ours'] = peak_db(ref), peak_db(ours)
    out['d_peak'] = abs(out['r_peak_ref'] - out['r_peak_ours'])
    la, lb = lufs(ref), lufs(ours)
    out['r_lufs_ref'], out['r_lufs_ours'] = la, lb
    out['d_lufs'] = abs(la - lb)
    ec, emae = envelope_distance(ref, ours)
    out['d_env_corr'], out['d_env_db'] = ec, emae
    sa, sb = segment_levels(ref, t_on, t_off, total), segment_levels(ours, t_on, t_off, total)
    for name, u, v in zip(('attack', 'sustain', 'release'), sa, sb):
        out['r_seg_%s_ref' % name], out['r_seg_%s_ours' % name] = u, v
        out['d_seg_%s' % name] = abs(u - v) if max(u, v) > -90.0 else 0.0
    out['d_mel'] = mel_distance(ma, mb)
    sc, lm, comb = mr_stft_loss(ma, mb)
    out['d_stft_sc'], out['d_stft_log'], out['d_stft'] = sc, lm, comb
    dc, dr, ca, cb = trajectory_distance(ma, mb)
    out['d_centroid'], out['d_rolloff'], out['r_centroid_ref'], out['r_centroid_ours'] = dc, dr, ca, cb
    pc = pitch_comparison(ref, ours, t_on + 0.3, max(t_off - 0.1, t_on + 0.5))
    if pc is None:
        out['d_f0_cents'] = None
        out['r_tonal_ref'] = out['r_tonal_ours'] = False
        out['r_f0_ref'] = out['r_f0_ours'] = 0.0
        out['f0_octave_error'] = False
    else:
        folded, octave, fa, fb, ta, tb, raw = pc
        out['r_f0_ref'], out['r_f0_ours'] = fa, fb
        out['r_tonal_ref'], out['r_tonal_ours'] = bool(ta), bool(tb)
        out['f0_octave_error'] = bool(octave)
        out['r_f0_cents'] = raw
        # only when the reference is tonal; a tonal reference that we render as noise is a miss of 600 cents
        out['d_f0_cents'] = (min(abs(raw), 600.0) if tb else 600.0) if ta else None   # an octave is an error of 600 cents and more: it is capped, not folded
    ba, sma, ca_ = stereo_stats(ref)
    bb, smb, cb_ = stereo_stats(ours)
    out['r_balance_ref'], out['r_balance_ours'] = ba, bb
    out['r_sidemid_ref'], out['r_sidemid_ours'] = sma, smb
    out['r_corr_ref'], out['r_corr_ours'] = ca_, cb_
    out['d_balance'], out['d_sidemid'], out['d_corr'] = abs(ba - bb), abs(sma - smb), abs(ca_ - cb_)
    na, nb = noise_floor_db(ref, t_off), noise_floor_db(ours, t_off)
    out['r_floor_ref'], out['r_floor_ours'] = na, nb
    out['d_floor'] = abs(na - nb) if max(na, nb) > -100.0 else 0.0
    ta_, tb_ = tail_time(ref, t_off), tail_time(ours, t_off)
    out['r_tail_ref'], out['r_tail_ours'] = ta_, tb_
    out['d_tail'] = abs(ta_ - tb_)
    return out
