"""Opt-in physical XL control test, witnessed by an operator at the hardware."""
import os
import sys

from device import Unavailable


def run(dev, spec, step, out):
    if spec["op"] != "qlink":
        raise Unavailable("No witnessed XL wheel mapping is configured")
    if os.environ.get("SB_XL_OPERATOR") != "1" or not sys.stdin.isatty():
        raise Unavailable("Physical Q-Link requires an XL operator and SB_XL_OPERATOR=1 at a terminal")
    index = int(spec["index"])
    delta = int(spec["delta"])
    if index != 1 or abs(delta) != 1:
        raise ValueError("Only the configured Macro Q1 single-detent test is supported")
    initial = dev.params()["macro.1"]
    direction = "right" if delta > 0 else "left"
    if input(f"Turn the physical XL Q-Link {index} one detent {direction}. Type OK when done: ").strip() != "OK":
        raise Unavailable("Operator did not confirm the physical turn")
    final = dev.params()["macro.1"]
    if (final - initial) * delta <= 0:
        raise AssertionError(f"Physical Q-Link {index} {direction}: macro.1 {initial} -> {final}")
    return {"actuator": "operator-confirmed physical XL Q-Link", "index": index,
            "direction": direction, "before": initial, "after": final}
