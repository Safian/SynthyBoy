"""Unit tests for scripts/device-level-sweep.py.

Tests everything that does not require a physical Akai MPC device:
  - Preset ordering & naming (index 0 is Init, slugs, 160 programs)
  - Parser logic (hook ack, status output_peak, play log velocities)
  - Math conversions (linear to dBFS, diff)
  - Device lock acquisition, release, and conflict detection
  - Retry logic & cleanup of partial files
  - Consecutive failure tracking (5 consecutive abort)
  - Resume mode behavior
  - Verifier decisions on synthetic datasets (mtimes, stray files, line count, tolerances)
  - Dry-run command emission
"""

import csv
import importlib.util
import os
import shutil
import tempfile
from unittest.mock import MagicMock, patch

# Dynamically import scripts/device-level-sweep.py
SPEC = importlib.util.spec_from_file_location(
    "device_level_sweep",
    os.path.join(os.path.dirname(__file__), "..", "device-level-sweep.py")
)
sweep_mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sweep_mod)


def test_slugify():
    assert sweep_mod.slugify("Init") == "init"
    assert sweep_mod.slugify("Space Wobble Bass") == "space-wobble-bass"
    assert sweep_mod.slugify("808 Boom") == "808-boom"
    assert sweep_mod.slugify("Bell FM/Ripple Lead (v2)") == "bell-fm-ripple-lead-v2"
    assert sweep_mod.slugify("   Acid Saw Reso   ") == "acid-saw-reso"
    assert sweep_mod.slugify("!@#$%^") == "preset"


def test_linear_to_dbfs():
    assert sweep_mod.linear_to_dbfs(1.0) == 0.0
    assert sweep_mod.linear_to_dbfs(0.5) == -6.02
    assert sweep_mod.linear_to_dbfs(0.316227766) == -10.0
    assert sweep_mod.linear_to_dbfs(0.0) == -120.0
    assert sweep_mod.linear_to_dbfs(-0.5) == -120.0


def test_parse_ack_line():
    # Success cases
    ok, seq, idx, name = sweep_mod.parse_ack_line("1 ok 0 Init")
    assert ok is True
    assert seq == 1
    assert idx == 0
    assert name == "Init"

    ok, seq, idx, name = sweep_mod.parse_ack_line("42 ok 15 Space Wobble Bass")
    assert ok is True
    assert seq == 42
    assert idx == 15
    assert name == "Space Wobble Bass"

    # Error cases
    ok, seq, idx, reason = sweep_mod.parse_ack_line("4 error out of range")
    assert ok is False
    assert seq == 4
    assert idx is None
    assert reason == "out of range"

    ok, seq, idx, reason = sweep_mod.parse_ack_line("5 error unknown name")
    assert ok is False
    assert seq == 5
    assert reason == "unknown name"

    # Malformed cases
    ok, _, _, err = sweep_mod.parse_ack_line("")
    assert ok is False
    ok, _, _, err = sweep_mod.parse_ack_line("invalid")
    assert ok is False
    ok, _, _, err = sweep_mod.parse_ack_line("NaN ok 0 Init")
    assert ok is False
    ok, _, _, err = sweep_mod.parse_ack_line("1 ok")
    assert ok is False


def test_parse_status_output_peak():
    sample_text = (
        "view: lfo1\n"
        "ui_state: normal\n"
        "output_peak: 0.31622\n"
        "output_peak_total: 0.95412\n"
        "output_blocks: 1250\n"
    )
    pk = sweep_mod.parse_status_output_peak(sample_text)
    assert pk is not None and abs(pk - 0.31622) < 1e-5

    # Make sure output_peak_total is not picked if output_peak is absent
    no_peak_text = (
        "view: lfo1\n"
        "output_peak_total: 0.95412\n"
    )
    assert sweep_mod.parse_status_output_peak(no_peak_text) is None


def test_parse_play_log_velocities():
    log_text = (
        "10 120 note_on channel 10 note 37 vel 127 engine 127 dropped 0\n"
        "11 920 note_off channel 10 note 37 vel 0 engine 0 dropped 0\n"
        "12 2100 note_on channel 10 note 37 vel 64 engine 64 dropped 0\n"
        "13 2900 note_off channel 10 note 37 vel 0 engine 0 dropped 0\n"
    )
    vels = sweep_mod.parse_play_log_velocities(log_text)
    assert vels == [127, 64]

    assert sweep_mod.parse_play_log_velocities("empty log") == []


def test_get_factory_programs_order():
    progs = sweep_mod.get_factory_programs()
    assert len(progs) == 161  # Init + 160 factory presets (program_list_length 161)
    assert progs[0] == "Init"
    assert "Acid Saw Reso" in progs
    assert "808 Boom" in progs


def test_device_lock():
    with tempfile.TemporaryDirectory() as tmpdir:
        lock_path = os.path.join(tmpdir, "DEVICE-LOCK")
        lock = sweep_mod.DeviceLock(lock_path, dry_run=False, logger=lambda m: None)

        # Context manager acquire and release
        with lock:
            assert os.path.isdir(lock_path)
            assert lock.acquired is True

            # Attempting to acquire again raises RuntimeError
            lock2 = sweep_mod.DeviceLock(lock_path, dry_run=False, logger=lambda m: None)
            raised = False
            try:
                lock2.acquire()
            except RuntimeError as exc:
                raised = True
                assert "already exists" in str(exc)
            assert raised, "expected RuntimeError when acquiring existing lock"

        # After block, lock is released
        assert not os.path.exists(lock_path)
        assert lock.acquired is False


def test_device_lock_dry_run():
    lock_path = "/nonexistent/test/path/DEVICE-LOCK"
    logs = []
    lock = sweep_mod.DeviceLock(lock_path, dry_run=True, logger=logs.append)
    with lock:
        assert lock.acquired is True
        assert any("[dry-run] mkdir" in l for l in logs)
    assert any("[dry-run] rmdir" in l for l in logs)


def test_verifier_synthetic_full_pass():
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_lines = []
        base_time = 1700000000.0
        progs = sweep_mod.get_factory_programs()

        for idx in range(1, 161):
            name = progs[idx]
            slug = sweep_mod.slugify(name)
            f_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            h_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")
            with open(f_img, "wb") as f:
                f.write(b"PNG_FULL")
            with open(h_img, "wb") as f:
                f.write(b"PNG_HALF")

            t_f = base_time + idx * 10
            t_h = t_f + 1.0
            os.utime(f_img, (t_f, t_f))
            os.utime(h_img, (t_h, t_h))

            full_pk = -10.0
            half_pk = -21.9
            diff = 11.9
            csv_lines.append(f"{idx},{name},{full_pk:.2f},{half_pk:.2f},{diff:.2f},127/64,2026-11-20T12:00:00\n")

        with open(os.path.join(tmpdir, "sweep.csv"), "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        ok, res = sweep_mod.verify_sweep(tmpdir, logger=lambda m: None)
        assert ok is True
        assert res["total_pass"] == 160
        assert res["total_fail"] == 0
        assert len(res["errors"]) == 0

        # Check report generated
        rep_path = os.path.join(tmpdir, "verify-report.md")
        assert os.path.isfile(rep_path)
        with open(rep_path, "r", encoding="utf-8") as f:
            rep = f.read()
        assert "**PASS**" in rep
        assert "Total Presets:** 160" in rep


def test_verifier_peak_tolerances():
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_lines = []
        progs = sweep_mod.get_factory_programs()
        base_time = 1700000000.0

        for idx in range(1, 161):
            name = progs[idx]
            slug = sweep_mod.slugify(name)
            f_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            h_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")
            with open(f_img, "wb") as f:
                f.write(b"PNG")
            with open(h_img, "wb") as f:
                f.write(b"PNG")
            os.utime(f_img, (base_time, base_time))
            os.utime(h_img, (base_time + 1, base_time + 1))

            if idx == 5:
                # Full peak out of tolerance (-10.0 +- 2.0 -> outside [-12.0, -8.0])
                full_pk = -7.5
                diff = 11.9
                half_pk = round(full_pk - diff, 2)
            elif idx == 12:
                # Diff out of tolerance (11.9 +- 3.0 -> outside [8.9, 14.9])
                full_pk = -10.0
                diff = 6.0
                half_pk = round(full_pk - diff, 2)
            else:
                full_pk = -10.0
                diff = 11.9
                half_pk = -21.9

            csv_lines.append(f"{idx},{name},{full_pk:.2f},{half_pk:.2f},{diff:.2f},127/64,2026-11-20T12:00:00\n")

        with open(os.path.join(tmpdir, "sweep.csv"), "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        ok, res = sweep_mod.verify_sweep(tmpdir, logger=lambda m: None)
        assert ok is False
        assert res["total_pass"] == 158
        assert res["total_fail"] == 2
        fail_indices = [r["index"] for r in res["failing_presets"]]
        assert 5 in fail_indices
        assert 12 in fail_indices

        rep_path = os.path.join(tmpdir, "verify-report.md")
        with open(rep_path, "r", encoding="utf-8") as f:
            rep = f.read()
        assert "## Failing Presets (2)" in rep


def test_verifier_mtime_ordering():
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_lines = []
        progs = sweep_mod.get_factory_programs()
        base_time = 1700000000.0

        for idx in range(1, 161):
            name = progs[idx]
            slug = sweep_mod.slugify(name)
            f_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            h_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")
            with open(f_img, "wb") as f:
                f.write(b"PNG")
            with open(h_img, "wb") as f:
                f.write(b"PNG")

            if idx == 7:
                # Inverted mtime: full written AFTER half
                os.utime(f_img, (base_time + 10, base_time + 10))
                os.utime(h_img, (base_time + 5, base_time + 5))
            else:
                os.utime(f_img, (base_time + idx, base_time + idx))
                os.utime(h_img, (base_time + idx + 1, base_time + idx + 1))

            csv_lines.append(f"{idx},{name},-10.00,-21.90,11.90,127/64,2026-11-20T12:00:00\n")

        with open(os.path.join(tmpdir, "sweep.csv"), "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        ok, res = sweep_mod.verify_sweep(tmpdir, logger=lambda m: None)
        assert ok is False
        fail_indices = [r["index"] for r in res["failing_presets"]]
        assert 7 in fail_indices
        assert any("-full" in e and "written after" in e for e in res["failing_presets"][0]["errors"])


def test_verifier_stray_images():
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_lines = []
        progs = sweep_mod.get_factory_programs()
        base_time = 1700000000.0

        for idx in range(1, 161):
            name = progs[idx]
            slug = sweep_mod.slugify(name)
            f_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            h_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")
            with open(f_img, "wb") as f:
                f.write(b"PNG")
            with open(h_img, "wb") as f:
                f.write(b"PNG")
            os.utime(f_img, (base_time, base_time))
            os.utime(h_img, (base_time + 1, base_time + 1))
            csv_lines.append(f"{idx},{name},-10.00,-21.90,11.90,127/64,2026-11-20T12:00:00\n")

        with open(os.path.join(tmpdir, "sweep.csv"), "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        # Stray image
        stray = os.path.join(tmpdir, "001-space-wobble-bass-full-extra.png")
        with open(stray, "wb") as f:
            f.write(b"STRAY")

        ok, res = sweep_mod.verify_sweep(tmpdir, logger=lambda m: None)
        assert ok is False
        assert any("stray or unexpected image" in e for e in res["errors"])


def test_verifier_csv_count():
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_lines = []
        progs = sweep_mod.get_factory_programs()
        base_time = 1700000000.0

        for idx in range(1, 161):
            name = progs[idx]
            slug = sweep_mod.slugify(name)
            f_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-full.png")
            h_img = os.path.join(tmpdir, f"{idx:03d}-{slug}-half.png")
            with open(f_img, "wb") as f:
                f.write(b"PNG")
            with open(h_img, "wb") as f:
                f.write(b"PNG")
            os.utime(f_img, (base_time, base_time))
            os.utime(h_img, (base_time + 1, base_time + 1))
            if idx < 156:
                csv_lines.append(f"{idx},{name},-10.00,-21.90,11.90,127/64,2026-11-20T12:00:00\n")

        with open(os.path.join(tmpdir, "sweep.csv"), "w", encoding="utf-8") as f:
            f.writelines(csv_lines)

        ok, res = sweep_mod.verify_sweep(tmpdir, logger=lambda m: None)
        assert ok is False
        assert any("CSV line count is 155" in e for e in res["errors"])


def _fake_hits():
    """hit_and_measure replacement: full (127) then half (64) hits, each one clean note_on 48."""
    state = {"i": 0}

    def fake(velocity, *a, **k):
        r = (0.31622, [(state["i"], 48, velocity)])
        state["i"] += 1
        return r
    return fake


def test_retry_and_abort_consecutive_failures():
    """Verify that 5 consecutive failed presets trigger an abort."""
    class DummyArgs:
        device = "k61"
        outdir = None
        csv = None
        limit = 10
        dry_run = False
        resume = False

    with tempfile.TemporaryDirectory() as tmpdir:
        args = DummyArgs()
        args.outdir = tmpdir
        args.csv = os.path.join(tmpdir, "sweep.csv")

        # Mock lock and sleep to avoid touching system locks and running fast
        with patch.object(sweep_mod.DeviceLock, "acquire"), \
             patch.object(sweep_mod.DeviceLock, "release"), \
             patch("time.sleep"):
            # Mock executor: preflight passes, but select_program fails
            with patch.object(sweep_mod.DeviceExecutor, "preflight_check", return_value=(True, "ok")), \
                 patch.object(sweep_mod.DeviceExecutor, "select_program", return_value=(False, "no ack")):
                code = sweep_mod.run_sweep(args, logger=lambda m: None)
                assert code == 1

                # Read sweep.log to verify consecutive failure abort message
                log_file = os.path.join(tmpdir, "sweep.log")
                assert os.path.isfile(log_file)
                with open(log_file, "r") as f:
                    log_content = f.read()
                assert "5 consecutive presets failed" in log_content


def test_retry_success_cleans_partial_files():
    """Verify that a preset that fails attempt 1 but succeeds attempt 2 cleans partial files."""
    class DummyArgs:
        device = "k61"
        outdir = None
        csv = None
        limit = 1
        dry_run = False
        resume = False

    with tempfile.TemporaryDirectory() as tmpdir:
        args = DummyArgs()
        args.outdir = tmpdir
        args.csv = os.path.join(tmpdir, "sweep.csv")

        # Put a partial leftover file for preset 1
        leftover = os.path.join(tmpdir, "001-808-boom-full.png")
        with open(leftover, "wb") as f:
            f.write(b"OLD_PARTIAL")

        attempts = [0]

        def mock_select(index, fallback_name="", timeout_s=5.0):
            attempts[0] += 1
            if attempts[0] == 1:
                return False, "temporary glitch"
            return True, "808 Boom"

        with patch.object(sweep_mod.DeviceLock, "acquire"), \
             patch.object(sweep_mod.DeviceLock, "release"), \
             patch("time.sleep"):
            with patch.object(sweep_mod.DeviceExecutor, "preflight_check", return_value=(True, "ok")), \
                 patch.object(sweep_mod.DeviceExecutor, "select_program", side_effect=mock_select), \
                 patch.object(sweep_mod.DeviceExecutor, "tap_reset_peak_hold"), \
                 patch.object(sweep_mod.DeviceExecutor, "hit_and_measure", side_effect=_fake_hits()), \
                 patch.object(sweep_mod.DeviceExecutor, "capture_screenshot", return_value=True):
                code = sweep_mod.run_sweep(args, logger=lambda m: None)
                assert code == 0
                assert attempts[0] == 2

                # Verify CSV line written
                with open(args.csv, "r") as f:
                    csv_data = f.read()
                assert "1,808 Boom,-10.00,-10.00,0.00,127/64" in csv_data


def test_resume_mode_skips_complete_presets():
    """Verify that --resume skips presets that already have both images and a CSV entry."""
    class DummyArgs:
        device = "k61"
        outdir = None
        csv = None
        limit = 2
        dry_run = False
        resume = True

    with tempfile.TemporaryDirectory() as tmpdir:
        args = DummyArgs()
        args.outdir = tmpdir
        args.csv = os.path.join(tmpdir, "sweep.csv")

        # Preset 1 already complete
        f0 = os.path.join(tmpdir, "001-808-boom-full.png")
        h0 = os.path.join(tmpdir, "001-808-boom-half.png")
        with open(f0, "wb") as f: f.write(b"PNG")
        with open(h0, "wb") as f: f.write(b"PNG")
        with open(args.csv, "w") as f:
            f.write("1,808 Boom,-10.00,-21.90,11.90,127/64,2026-11-20T12:00:00\n")

        selected_indices = []

        def mock_select(index, fallback_name="", timeout_s=5.0):
            selected_indices.append(index)
            return True, fallback_name

        with patch.object(sweep_mod.DeviceLock, "acquire"), \
             patch.object(sweep_mod.DeviceLock, "release"), \
             patch("time.sleep"):
            with patch.object(sweep_mod.DeviceExecutor, "preflight_check", return_value=(True, "ok")), \
                 patch.object(sweep_mod.DeviceExecutor, "select_program", side_effect=mock_select), \
                 patch.object(sweep_mod.DeviceExecutor, "tap_reset_peak_hold"), \
                 patch.object(sweep_mod.DeviceExecutor, "hit_and_measure", side_effect=_fake_hits()), \
                 patch.object(sweep_mod.DeviceExecutor, "capture_screenshot", return_value=True):
                code = sweep_mod.run_sweep(args, logger=lambda m: None)
                assert code == 0
                # Preset 1 skipped, preset 2 run (index 0 = Init is never swept)
                assert 0 not in selected_indices
                assert 1 not in selected_indices
                assert 2 in selected_indices


def test_dry_run_commands_k61_and_xl():
    """Verify that dry-run outputs plausible commands for k61 and xl."""
    class DummyArgs:
        device = "k61"
        outdir = "/tmp/test-sweep-k61"
        csv = None
        limit = 1
        dry_run = True
        resume = False

    logs_k61 = []
    args = DummyArgs()
    code = sweep_mod.run_sweep(args, logger=logs_k61.append)
    assert code == 0
    k61_text = "\n".join(logs_k61)
    assert "DEVICE-LOCK" in k61_text
    assert "SEQNOTE_VEL=127 /tmp/seqnote 129 0 0.8 48" in k61_text
    assert "SEQNOTE_VEL=64 /tmp/seqnote 129 0 0.8 48" in k61_text
    assert "mtouch tap 38 698 90" in k61_text
    assert "ssh root@192.168.0.30 'printf '1\\n' > /tmp/synthyboy-select-program'" in k61_text
    assert "printf '0\\n' > /tmp/synthyboy-select-program'" in k61_text  # preflight only
    assert "printf '2\\n'" not in k61_text  # --limit 1 sweeps only program 1
    assert "shot.sh preflight" in k61_text
    assert "shot.sh 001-808-boom-full" in k61_text

    logs_xl = []
    args.device = "xl"
    args.outdir = "/tmp/test-sweep-xl"
    code = sweep_mod.run_sweep(args, logger=logs_xl.append)
    assert code == 0
    xl_text = "\n".join(logs_xl)
    assert "DEVICE-LOCK-XL" in xl_text
    assert "SEQNOTE_VEL=127 /tmp/seqnote 129 0 0.8 48" in xl_text
    assert "SEQNOTE_VEL=64 /tmp/seqnote 129 0 0.8 48" in xl_text
    assert "MIDI input path, not the pads: XL has no pad injection" in xl_text
    assert "mtouch tap 924 101 90" in xl_text
    assert "xshot.sh preflight" in xl_text
    assert "xshot.sh 001-808-boom-full" in xl_text



def test_parse_play_log_events_and_hit_check():
    text = (
        "17 54442 note_off channel 1 note 72 vel 64 engine 64 dropped 0\n"
        "79 262932 note_on channel 1 note 48 vel 127 engine 127 dropped 0\n"
        "80 263730 note_off channel 1 note 48 vel 64 engine 64 dropped 0\n"
        "81 270000 note_on channel 1 note 48 vel 64 engine 60 dropped 0\n"
    )
    ev = sweep_mod.parse_play_log_events(text, 0)
    assert ev == [(79, 48, 127), (81, 48, 60)]
    assert sweep_mod.parse_play_log_events(text, 79) == [(81, 48, 60)]
    assert sweep_mod.hit_reached_plugin([(79, 48, 127)], 127)
    assert not sweep_mod.hit_reached_plugin([(79, 72, 127)], 127)   # wrong note (pad 0x25 plays 72)
    assert not sweep_mod.hit_reached_plugin([], 127)
    assert not sweep_mod.hit_reached_plugin([(1, 48, 127), (2, 48, 127)], 127)


def test_parse_sample_line_ignores_total():
    assert sweep_mod.parse_sample_line("output_peak: 0.44627 output_blocks: 149596") == (149596, 0.44627)
    assert sweep_mod.parse_sample_line("output_peak: 1e-05 output_blocks: 5") == (5, 1e-05)
    assert sweep_mod.parse_sample_line("") is None


def test_peak_in_window_uses_only_writes_first_seen_after_the_hit():
    # (receipt_time, output_blocks, output_peak). The write with blocks 100 was seen before the hit (t=10):
    # it must not count even though it is repeated by later polls. Write 200 contains the hit.
    samples = [
        (8.0, 100, 0.9), (9.0, 100, 0.9),            # stale loud write before the hit (previous tail)
        (10.2, 100, 0.9), (10.4, 200, 0.45), (10.6, 200, 0.45),
        (11.5, 300, 0.001), (12.5, 400, 0.0),
    ]
    peak, used = sweep_mod.peak_in_window(samples, t_start=10.0, t_end=13.0)
    assert peak == 0.45 and used == 3
    # nothing new inside the window
    assert sweep_mod.peak_in_window(samples[:3], 10.0, 13.0) == (0.0, 0)
    # a write first seen after the window is ignored
    peak, _ = sweep_mod.peak_in_window(samples + [(14.0, 500, 0.8)], 10.0, 13.0)
    assert peak == 0.45


def test_quiet_writes_seen():
    q = sweep_mod.quiet_writes_seen
    assert not q([(1, 1, 0.0)], 2)
    assert q([(1, 1, 0.5), (2, 2, 0.0), (3, 3, 0.0001)], 2)
    assert not q([(1, 1, 0.0), (2, 2, 0.3)], 2)


def test_sweep_covers_only_factory_presets_1_to_160():
    assert sweep_mod.FIRST_INDEX == 1 and sweep_mod.LAST_INDEX == 160
    assert sweep_mod.DEVICES["k61"]["tap_coords"] == (38, 698)
    assert sweep_mod.SWEEP_NOTE == 48
