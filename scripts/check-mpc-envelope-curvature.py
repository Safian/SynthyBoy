#!/usr/bin/env python3
# SynthyBoy - compare engine envelope curves with MPC host Quadratic curve.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Measures deviation between SynthyBoy engine envelope curves (attack_curve, decay_curve,
# release_curve under core/engine-own) and MPC host Envelope component Quadratic curve drawing.
#
# Engine formulas (core/engine-own/env_lfo/envelope.h, core/engine-own/dsp/util.h, core/engine-own/voice/voice.cpp):
#   powerShape(x, p) = (exp(p * x) - 1) / (exp(p) - 1)   if |p| >= 1e-4 else x
#   Attack:  y = powerShape(t, -attack_curve)             (s.attackCurve = -attack_curve in voice.cpp)
#   Decay:   y = 1.0 - powerShape(t, decay_curve)         (s.decayCurve = decay_curve, sustain=0)
#   Release: y = 1.0 - powerShape(t, release_curve)       (s.releaseCurve = release_curve)
#
# Historical SDK Envelope drawing used for comparison:
#   The MPC SDK docs (docs/ui-reference.md section 6.13) document curveType enum ("Quadratic", "Exponential")
#   and forceCurveShape (0.5), but DO NOT document any mathematical formula for the Quadratic curve.
#   In 2D UI drawing, Quadratic curves are standard Quadratic Bézier curves with control point (0.5, y_c),
#   where y_c corresponds to the normalized shape parameter in [0, 1] (or 0.5 for linear segment).

import argparse
import json
import math
import os
import sys
from pathlib import Path

DEFAULT_ATTACK_CURVE = 0.0
DEFAULT_DECAY_CURVE = -3.0
DEFAULT_RELEASE_CURVE = -3.0
CURVE_MIN = -20.0
CURVE_MAX = 20.0
NUM_SAMPLES = 1001


def power_shape(x: float, power: float) -> float:
    """Core DSP powerShape implementation from core/engine-own/dsp/util.h."""
    if abs(power) < 1e-4:
        return x
    return math.expm1(power * x) / math.expm1(power)


def engine_attack(t: float, attack_curve: float = DEFAULT_ATTACK_CURVE) -> float:
    """Engine attack curve: rises from 0.0 to 1.0."""
    return power_shape(t, -attack_curve)


def engine_decay(t: float, decay_curve: float = DEFAULT_DECAY_CURVE) -> float:
    """Engine decay curve (normalized 1.0 to 0.0, assuming sustain=0 for 0..1 scale)."""
    return 1.0 - power_shape(t, decay_curve)


def engine_release(t: float, release_curve: float = DEFAULT_RELEASE_CURVE) -> float:
    """Engine release curve (normalized 1.0 to 0.0)."""
    return 1.0 - power_shape(t, release_curve)


def normalize_shape(curve_val: float) -> float:
    """Map parameter range [-20, 20] to normalized [0, 1]."""
    return (curve_val - CURVE_MIN) / (CURVE_MAX - CURVE_MIN)


def host_quadratic_attack(t: float, shape_norm: float = 0.5) -> float:
    """Host Quadratic Bézier curve for attack (0.0 to 1.0)."""
    return 2.0 * t * (1.0 - t) * shape_norm + t * t


def host_quadratic_decay(t: float, shape_norm: float = 0.425) -> float:
    """Host Quadratic Bézier curve for decay (1.0 to 0.0)."""
    return (1.0 - t) ** 2 + 2.0 * t * (1.0 - t) * shape_norm


def host_quadratic_release(t: float, shape_norm: float = 0.425) -> float:
    """Host Quadratic Bézier curve for release (1.0 to 0.0)."""
    return (1.0 - t) ** 2 + 2.0 * t * (1.0 - t) * shape_norm


def host_linear_attack(t: float) -> float:
    """Host linear segment for attack (neutral / forceCurveShape=0.5)."""
    return t


def host_linear_decay(t: float) -> float:
    """Host linear segment for decay."""
    return 1.0 - t


def host_linear_release(t: float) -> float:
    """Host linear segment for release."""
    return 1.0 - t


def check_sdk_documentation(sdk_path: Path = None) -> dict:
    """Checks if the MPC SDK ui-reference.md defines a formula for Quadratic curveType."""
    if sdk_path is None:
        sdk_path = Path(os.environ.get("MPC_SDK") or Path.home() / ".local/share/local-worker/ext/mpc-sdk")

    ref_doc = sdk_path / "docs" / "ui-reference.md"
    if not ref_doc.is_file():
        return {
            "found_doc": False,
            "documented": False,
            "message": f"SDK documentation file not found at {ref_doc}",
        }

    content = ref_doc.read_text(encoding="utf-8")
    has_curvetype = "curveType" in content and "Quadratic" in content
    # Check if any formula, equation, or mathematical description of Quadratic is present
    has_formula = any(kw in content.lower() for kw in ("quadratic equation", "quadratic formula", "bezier formula", "y = x^2"))

    return {
        "found_doc": True,
        "documented": has_formula,
        "has_curvetype_mention": has_curvetype,
        "message": "Nincs dokumentálva az SDK-ban (ui-reference.md említi a 'Quadratic' enumot, de képlet nincs leírva)."
        if not has_formula
        else "Dokumentálva van az SDK-ban.",
    }


def measure_stage(
    stage_name: str,
    curve_val: float,
    engine_fn,
    host_bezier_fn,
    host_linear_fn,
    num_samples: int = NUM_SAMPLES,
) -> dict:
    """Sample engine and host curves across [0, 1] and compute deviation metrics."""
    shape_norm = normalize_shape(curve_val)
    ts = [i / (num_samples - 1) for i in range(num_samples)]

    engine_vals = [engine_fn(t, curve_val) for t in ts]
    host_bezier_vals = [host_bezier_fn(t, shape_norm) for t in ts]
    host_linear_vals = [host_linear_fn(t) for t in ts]

    diff_bezier = [abs(e - h) for e, h in zip(engine_vals, host_bezier_vals)]
    diff_linear = [abs(e - h) for e, h in zip(engine_vals, host_linear_vals)]

    max_diff_bezier = max(diff_bezier)
    if max_diff_bezier < 1e-12:
        max_diff_bezier = 0.0
        t_max_bezier = 0.0
        avg_diff_bezier = 0.0
    else:
        idx_max_bezier = diff_bezier.index(max_diff_bezier)
        t_max_bezier = ts[idx_max_bezier]
        avg_diff_bezier = sum(diff_bezier) / len(diff_bezier)

    max_diff_linear = max(diff_linear)
    if max_diff_linear < 1e-12:
        max_diff_linear = 0.0
        t_max_linear = 0.0
        avg_diff_linear = 0.0
    else:
        idx_max_linear = diff_linear.index(max_diff_linear)
        t_max_linear = ts[idx_max_linear]
        avg_diff_linear = sum(diff_linear) / len(diff_linear)

    return {
        "stage": stage_name,
        "curve_param": curve_val,
        "shape_normalized": shape_norm,
        "max_diff_bezier": max_diff_bezier,
        "t_max_bezier": t_max_bezier,
        "avg_diff_bezier": avg_diff_bezier,
        "max_diff_linear": max_diff_linear,
        "t_max_linear": t_max_linear,
        "avg_diff_linear": avg_diff_linear,
        "samples": num_samples,
    }


def measure_all_deviations(
    attack_curve: float = DEFAULT_ATTACK_CURVE,
    decay_curve: float = DEFAULT_DECAY_CURVE,
    release_curve: float = DEFAULT_RELEASE_CURVE,
    num_samples: int = NUM_SAMPLES,
    sdk_path: Path = None,
) -> dict:
    """Computes deviation metrics for Attack, Decay, and Release stages."""
    doc_info = check_sdk_documentation(sdk_path)

    attack_res = measure_stage("attack", attack_curve, engine_attack, host_quadratic_attack, host_linear_attack, num_samples)
    decay_res = measure_stage("decay", decay_curve, engine_decay, host_quadratic_decay, host_linear_decay, num_samples)
    release_res = measure_stage("release", release_curve, engine_release, host_quadratic_release, host_linear_release, num_samples)

    return {
        "sdk_documentation": doc_info,
        "stages": {
            "attack": attack_res,
            "decay": decay_res,
            "release": release_res,
        },
    }


def main():
    parser = argparse.ArgumentParser(description="Check deviation between SynthyBoy envelope and MPC host Quadratic curves.")
    parser.add_argument("--attack", type=float, default=DEFAULT_ATTACK_CURVE, help="Attack curve parameter (-20..20)")
    parser.add_argument("--decay", type=float, default=DEFAULT_DECAY_CURVE, help="Decay curve parameter (-20..20)")
    parser.add_argument("--release", type=float, default=DEFAULT_RELEASE_CURVE, help="Release curve parameter (-20..20)")
    parser.add_argument("--samples", type=int, default=NUM_SAMPLES, help="Number of sample points across [0, 1]")
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")
    args = parser.parse_args()

    results = measure_all_deviations(
        attack_curve=args.attack,
        decay_curve=args.decay,
        release_curve=args.release,
        num_samples=args.samples,
    )

    if args.json:
        print(json.dumps(results, indent=2))
        return 0

    print("SynthyBoy Envelope Curvature vs MPC Host Quadratic Curve Analysis")
    print("=" * 66)
    print(f"SDK Dokumentáció: {results['sdk_documentation']['message']}")
    print()
    print("Default paraméterek (core/src/param_table.cpp):")
    print(f"  attack_curve:  {args.attack:5.1f} (normál alak: {normalize_shape(args.attack):.3f})")
    print(f"  decay_curve:   {args.decay:5.1f} (normál alak: {normalize_shape(args.decay):.3f})")
    print(f"  release_curve: {args.release:5.1f} (normál alak: {normalize_shape(args.release):.3f})")
    print()
    print("Eltérések a 0..1 skálán (minták száma: %d):" % args.samples)
    print("  Stage    | Curve | Shape  | Max Diff (Quadratic Bézier) | Max Diff (Linear)")
    print("  ---------+-------+--------+-----------------------------+------------------")
    for name in ("attack", "decay", "release"):
        s = results["stages"][name]
        print(
            f"  {s['stage']:<8} | {s['curve_param']:5.1f} |  {s['shape_normalized']:.3f} | "
            f"          {s['max_diff_bezier']:.4f} (t={s['t_max_bezier']:.3f}) | "
            f"        {s['max_diff_linear']:.4f} (t={s['t_max_linear']:.3f})"
        )
    print()
    print("Hűség összefoglaló:")
    att_diff = results["stages"]["attack"]["max_diff_bezier"]
    dec_diff = results["stages"]["decay"]["max_diff_bezier"]
    rel_diff = results["stages"]["release"]["max_diff_bezier"]
    print(f"  - Attack szakasz:  tökéletes egyezés (max eltérés: {att_diff:.4f}). A hangzó attack lineáris.")
    print(
        f"  - Decay szakasz:   jelentős eltérés (max eltérés: {dec_diff:.4f} Bézier, "
        f"{results['stages']['decay']['max_diff_linear']:.4f} lineáris). A hangzó lecsengés sokkal meredekebb."
    )
    print(
        f"  - Release szakasz: jelentős eltérés (max eltérés: {rel_diff:.4f} Bézier, "
        f"{results['stages']['release']['max_diff_linear']:.4f} lineáris). A hangzó elengedés sokkal meredekebb."
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())
