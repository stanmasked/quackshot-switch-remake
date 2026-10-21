"""Synthetic source-integrity mutations; no ROM, raw RAM or graphics needed."""
import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import reference_evidence_integrity as guard


class EvidenceIntegrityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data, cls.game_source = guard.read_inputs(Path(__file__).resolve().parents[1])

    def test_unmodified_published_observations_are_consistent(self):
        result = guard.validate(self.data, self.game_source)
        self.assertEqual(result["status"], "CONSISTENT")
        self.assertIs(result["original_rom_was_run_by_this_audit"], False)
        self.assertEqual(result["measured_ledge_drop_pixels"], 32)
        self.assertEqual(result["measured_wall_x"], 498)
        self.assertEqual(result["measured_short_and_long_landing_frames"],
                         [2100, 2126])
        self.assertNotIn("raw_ram", result)
        self.assertNotIn("rom_bytes", result)
        self.assertIsInstance(json.dumps(result), str)

    def test_rom_provenance_mismatch_rejected(self):
        data = copy.deepcopy(self.data)
        data["obstacles"]["reference_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "ROM provenance"):
            guard.validate(data, self.game_source)

    def test_genesis_core_provenance_mismatch_rejected(self):
        data = copy.deepcopy(self.data)
        data["camera"]["emulator_core_sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "binary provenance"):
            guard.validate(data, self.game_source)

    def test_model_constant_drift_rejected(self):
        source = self.game_source.replace("#define QS_CITY_ACCEL (10240.0f",
                                          "#define QS_CITY_ACCEL (10241.0f")
        self.assertNotEqual(source, self.game_source)
        with self.assertRaisesRegex(ValueError, "QS_CITY_ACCEL"):
            guard.validate(self.data, source)

    def test_observation_constant_drift_rejected(self):
        data = copy.deepcopy(self.data)
        data["motion"]["conclusions"]["horizontal_acceleration_16_16_units"] += 1
        with self.assertRaisesRegex(ValueError, "measured fixed-point constant"):
            guard.validate(data, self.game_source)

    def test_obstacle_and_camera_disagreement_rejected(self):
        data = copy.deepcopy(self.data)
        data["obstacles"]["wall"]["world_xy"][0] += 1
        with self.assertRaisesRegex(ValueError, "wall/reverse markers"):
            guard.validate(data, self.game_source)
        data = copy.deepcopy(self.data)
        data["camera"]["metrics"]["wall_stop_world_x"] = 499
        with self.assertRaisesRegex(ValueError, "wall and camera"):
            guard.validate(data, self.game_source)

    def test_candidate_scroll_and_tracking_drift_rejected(self):
        data = copy.deepcopy(self.data)
        data["camera"]["metrics"]["scroll_RAM_candidate"] = "0xFEC6"
        with self.assertRaisesRegex(ValueError, "scroll-state candidate"):
            guard.validate(data, self.game_source)
        data = copy.deepcopy(self.data)
        data["camera"]["metrics"]["keyframes"]["reverse"][-1]["camera_RAM_candidate"] += 5
        with self.assertRaisesRegex(ValueError, "reverse camera keyframe"):
            guard.validate(data, self.game_source)

    def test_stale_model_formula_is_not_used_as_active_truth(self):
        # Original Stage14 fitted response must not overwrite the later
        # measured fixed-point acceleration or imply equivalence.
        result = guard.validate(self.data, self.game_source)
        self.assertEqual(result["measured_horizontal_acceleration_16_16"], 10240)
        self.assertIn("historical exponential model",
                      " ".join(result["not_proven"]))


if __name__ == "__main__":
    unittest.main()
