"""Synthetic-only counter and zero-branch regressions."""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_counter_transition_audit as counter


def a(frame, pc, addr, kind, val, width=2):
    out = {"frame": frame, "instruction_pc": pc, "cpu_address": addr,
           "ram_offset": addr & 65535, "width_bytes": width,
           "access_kind": kind, "access_value": val}
    if kind == "W":
        out["write_value"] = val
    return out


def p(frame, pc, z=False):
    return {"frame": frame, "instruction_pc": pc, "a0_address": counter.POINTER,
            "a1_address": 0, "ccr_low5": 4 if z else 0}


def synthetic(active=True):
    access, pcs = [], []
    if active:
        access.append(a(100, counter.INIT, counter.COUNT, "W", 16))
        pcs.append(p(100, counter.INIT))
        for i in range(16):
            frame = 101 + i
            access.extend((a(frame, counter.STEP, counter.COUNT, "R", 16-i),
                           a(frame, counter.STEP, counter.COUNT, "W", 15-i)))
            pcs.extend((p(frame, counter.STEP),
                        p(frame, counter.BRANCH, i == 15)))
        access.append(a(116, counter.RESET, counter.STATE, "W", 0, 1))
        pcs.append(p(116, counter.RESET, True))
    def trace(schema, events):
        return {"schema": schema, "status": "COMPLETE", "overflow_count": 0,
                "frame_window_inclusive": [100, 227], "event_count": len(events),
                "events": events, **{flag: False for flag in counter.PRIVACY}}
    return {"cpu_access_trace": trace(
        "quackshot_private_m68k_cpu_access_trace_v2", access),
        "cpu_control_flow_trace": dict(trace(
            "quackshot_private_m68k_control_flow_trace_v2", pcs),
            object_pointer_filter="A0 low 24 bits equal 0xFFA2C0")}


class CounterTransitionTests(unittest.TestCase):
    def setUp(self):
        self.data = synthetic()

    def test_sixteen_steps_and_zero_branch(self):
        result = counter.audit(self.data)
        self.assertEqual((result["steps"], result["terminal_frame"]), (16, 116))
        self.assertFalse(result["gameplay_semantics_verified"])

    def test_absent_negative_control(self):
        self.assertEqual(counter.audit(synthetic(False), negative=True)["mode"], "ABSENT")

    def test_negative_control_rejects_active_counter(self):
        with self.assertRaises(counter.AuditError):
            counter.audit(self.data, negative=True)

    def test_missing_counter_rejected(self):
        with self.assertRaises(counter.AuditError):
            counter.audit(synthetic(False))

    def test_wrong_decrement_rejected(self):
        events = self.data["cpu_access_trace"]["events"]
        events[2]["access_value"] = events[2]["write_value"] = 10
        with self.assertRaisesRegex(counter.AuditError, "write mismatch"):
            counter.audit(self.data)

    def test_early_zero_flag_rejected(self):
        self.data["cpu_control_flow_trace"]["events"][2]["ccr_low5"] = 4
        with self.assertRaisesRegex(counter.AuditError, "zero-flag"):
            counter.audit(self.data)

    def test_missing_reset_rejected(self):
        report = self.data["cpu_access_trace"]
        report["events"].pop()
        report["event_count"] -= 1
        with self.assertRaises(counter.AuditError):
            counter.audit(self.data)

    def test_wrong_object_pointer_rejected(self):
        self.data["cpu_control_flow_trace"]["events"][0]["a0_address"] += 1
        with self.assertRaisesRegex(counter.AuditError, "pointer"):
            counter.audit(self.data)

    def test_duplicate_step_rejected(self):
        report = self.data["cpu_access_trace"]
        report["events"].insert(4, copy.deepcopy(report["events"][3]))
        report["event_count"] += 1
        with self.assertRaises(counter.AuditError):
            counter.audit(self.data)

    def test_privacy_violation_rejected(self):
        self.data["cpu_access_trace"]["raw_rom_exported"] = True
        with self.assertRaisesRegex(counter.AuditError, "privacy"):
            counter.audit(self.data)

    def test_extra_payload_rejected(self):
        self.data["cpu_access_trace"]["events"][1]["raw_bytes"] = "unwanted"
        with self.assertRaisesRegex(counter.AuditError, "fields"):
            counter.audit(self.data)

    def test_overflow_rejected(self):
        self.data["cpu_control_flow_trace"]["overflow_count"] = 1
        with self.assertRaisesRegex(counter.AuditError, "overflow"):
            counter.audit(self.data)

    def test_different_trace_windows_rejected(self):
        self.data["cpu_control_flow_trace"]["frame_window_inclusive"] = [100, 226]
        with self.assertRaisesRegex(counter.AuditError, "windows differ"):
            counter.audit(self.data)

    def test_cli_emits_only_compact_verdict(self):
        with tempfile.TemporaryDirectory() as td:
            inp, out = Path(td)/"synthetic.json", Path(td)/"summary.json"
            inp.write_text(json.dumps(self.data), encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(counter.__file__)),
                                     "--report", str(inp), "--out", str(out)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            summary = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(summary["mode"], "COUNTDOWN_VERIFIED")
            self.assertNotIn("events", summary)

    def test_bad_input_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            inp, out = Path(td)/"synthetic.json", Path(td)/"summary.json"
            inp.write_text("{broken", encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(counter.__file__)),
                                     "--report", str(inp), "--out", str(out)],
                                    capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
