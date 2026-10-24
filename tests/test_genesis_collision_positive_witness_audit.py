"""ROM-free synthetic tests for the Stage282 positive collision evidence gate."""
import copy
import importlib.util
import unittest
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "tools/genesis_collision_positive_witness_audit.py"
spec = importlib.util.spec_from_file_location("positive_witness_audit", source)
audit_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit_module)
SHA = "a" * 64


def synthetic_report(positive):
    player = ({"x_min": 497, "x_max": 521, "y_min": 118, "y_max": 161}
              if positive else {"x_min": 482, "x_max": 506,
                                "y_min": 165, "y_max": 208})
    box = {"x_min": 518, "x_max": 548, "y_min": 136, "y_max": 216}
    obj3 = {
        "cpu_address": "0xFF9000", "object_type": 3, "collision_profile_index": 61,
        "collision_bounds": box,
        "overlaps_player_collision_rectangle": positive,
        "intersection_with_player": ({"x_min": 518, "x_max": 521,
                                     "y_min": 136, "y_max": 161} if positive else None),
    }
    obj6 = {"cpu_address": "0xFF9040", "object_type": 6,
            "collision_profile_index": 64,
            "collision_bounds": {"x_min": 402, "x_max": 408,
                                 "y_min": 150, "y_max": 160},
            "overlaps_player_collision_rectangle": False,
            "intersection_with_player": None}
    snapshot = {
        "schema": audit_module.PROFILE_SCHEMA, "frame": 2353,
        "player_collision_rectangle": player, "objects": [obj3, obj6],
        "global_event_ee09": "0xFF" if positive else "0x00",
        "raw_rom_exported": False, "raw_ram_exported": False,
        "frame_pixels_exported": False, "audio_exported": False,
        "collision_profile_bytes_exported": False,
    }
    accesses = [{"frame": 2353, "instruction_pc": 0xB866,
                 "cpu_address": 0xFFEE09, "access_kind": "W",
                 "width_bytes": 1, "write_value": 255}] if positive else []
    return {
        "schema": audit_module.REPORT_SCHEMA,
        "reference_rom_sha256": SHA, "frames_requested": 2400,
        "core": {"library_name": "synthetic"},
        "ram_checkpoint_phase": "post_retro_run_before_next_input",
        "input_ranges": [
            {"first_frame": 450, "end_exclusive": 462, "button": "START"},
            {"first_frame": 2070 if positive else 2050,
             "end_exclusive": 2370, "button": "RIGHT"},
        ],
        "collision_profile_trace": {
            "schema": audit_module.PROFILE_TRACE_SCHEMA, "status": "COMPLETE",
            "sampling_phase": "post_retro_run_before_next_input",
            "frame_indices_requested": [2353], "frames": [snapshot],
            "raw_rom_exported": False, "raw_ram_exported": False,
            "frame_pixels_exported": False, "audio_exported": False,
            "collision_profile_bytes_exported": False,
        },
        "cpu_access_trace": {
            "schema": audit_module.ACCESS_SCHEMA, "status": "COMPLETE",
            "overflow_count": 0, "frame_window_inclusive": [2348, 2355],
            "event_count": len(accesses), "events": accesses,
            "write_watch_ranges": [{"start_inclusive": "0xEE08",
                                    "end_exclusive": "0xEE0B"}],
            "raw_rom_exported": False, "raw_ram_exported": False,
        },
    }


class PositiveWitnessGateTests(unittest.TestCase):
    def setUp(self):
        self.a = synthetic_report(True)
        self.b = copy.deepcopy(self.a)
        self.n = synthetic_report(False)

    def verify(self):
        return audit_module.audit(self.a, self.b, self.n, 2353, SHA)

    def assert_denied(self, message):
        with self.assertRaisesRegex(ValueError, message):
            self.verify()

    def test_accepts_repeated_positive_and_negative_numeric_controls(self):
        result = self.verify()
        self.assertEqual(result["status"], "NUMERIC_WRITER_AND_OVERLAP_REPRODUCED")
        self.assertEqual(result["positive_repeat_count"], 2)
        self.assertFalse(result["raw_rom_exported"])
        self.assertFalse(result["safe_to_modify_switch_physics"])
        self.assertNotIn("input_ranges", result)
        self.assertNotIn("reference_rom_sha256", result)

    def test_rejects_inconsistent_positive_replays(self):
        self.b["collision_profile_trace"]["frames"][0]["global_event_ee09"] = "0x01"
        self.assert_denied("deterministic")

    def test_rejects_positive_without_writer(self):
        cpu = self.a["cpu_access_trace"]
        cpu["events"] = []
        cpu["event_count"] = 0
        self.b = copy.deepcopy(self.a)
        self.assert_denied("writer")

    def test_rejects_wrong_writer_pc(self):
        for run in (self.a, self.b):
            run["cpu_access_trace"]["events"][0]["instruction_pc"] = 0xB864
        self.assert_denied("writer")

    def test_rejects_cross_revision_reports(self):
        self.n["reference_rom_sha256"] = "b" * 64
        self.assert_denied("ROM revision")

    def test_rejects_positive_without_overlap(self):
        for run in (self.a, self.b):
            sample = run["collision_profile_trace"]["frames"][0]
            sample["player_collision_rectangle"] = self.n["collision_profile_trace"]["frames"][0]["player_collision_rectangle"]
            obj = sample["objects"][0]
            obj["overlaps_player_collision_rectangle"] = False
            obj["intersection_with_player"] = None
        self.assert_denied("overlap")

    def test_rejects_tampered_overlap_indicator(self):
        self.a["collision_profile_trace"]["frames"][0]["objects"][0]["overlaps_player_collision_rectangle"] = False
        self.assert_denied("contradicts")

    def test_rejects_negative_event(self):
        self.n["collision_profile_trace"]["frames"][0]["global_event_ee09"] = "0xFF"
        self.assert_denied("negative control")

    def test_rejects_negative_hidden_writer(self):
        self.n["cpu_access_trace"]["events"] = copy.deepcopy(self.a["cpu_access_trace"]["events"])
        self.n["cpu_access_trace"]["event_count"] = 1
        self.assert_denied("negative control")

    def test_rejects_identical_control_input(self):
        self.n["input_ranges"] = copy.deepcopy(self.a["input_ranges"])
        self.assert_denied("different inputs")

    def test_rejects_mismatched_positive_inputs(self):
        self.b["input_ranges"][1]["first_frame"] = 2100
        self.assert_denied("positive replays")

    def test_rejects_cpu_overflow(self):
        self.a["cpu_access_trace"]["status"] = "OVERFLOW"
        self.a["cpu_access_trace"]["overflow_count"] = 2
        self.assert_denied("complete CPU")

    def test_rejects_wrong_phase(self):
        self.a["collision_profile_trace"]["sampling_phase"] = "pre_retro_run"
        self.assert_denied("collision profile trace")

    def test_rejects_unwatched_writer(self):
        self.a["cpu_access_trace"]["write_watch_ranges"] = []
        self.assert_denied("watch range")

    def test_rejects_invalid_frame_list(self):
        self.a["collision_profile_trace"]["frame_indices_requested"] = [2352]
        self.assert_denied("frame mismatch")

    def test_rejects_core_metadata_mismatch(self):
        self.n["core"]["library_name"] = "other"
        self.assert_denied("core metadata")

    def test_rejects_missing_private_export_guard(self):
        self.a["collision_profile_trace"]["raw_rom_exported"] = True
        self.assert_denied("unsafe private")

    def test_rejects_bool_in_numeric_geometry(self):
        self.a["collision_profile_trace"]["frames"][0]["player_collision_rectangle"]["x_min"] = True
        self.assert_denied("non-integer")

    def test_rejects_wrong_profile_index(self):
        self.a["collision_profile_trace"]["frames"][0]["objects"][0]["collision_profile_index"] = 0x40
        self.assert_denied("type/profile")


if __name__ == "__main__":
    unittest.main()