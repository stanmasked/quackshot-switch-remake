"""ROM-free, synthetic regression checks for Stage165 cold-boot comparisons."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_motion_repeatability as audit
from test_genesis_fixedpoint_motion import independently_derived_synthetic_physics


def independent_fixture():
    base = independently_derived_synthetic_physics()
    # These are stand-ins for comparison plumbing, not alleged original-game
    # reversal or eight-frame jump observations.
    base["reverse_right20_left20"] = copy.deepcopy(base["right_100"])
    base["jump_hold_8"] = copy.deepcopy(base["jump_hold_50"])
    reads = {name: sum(stop - start for start, stop, _ in schedule)
             for name, schedule in audit.SCENARIOS.items()}
    return {"traces": base, "reads": reads}


class MotionRepeatabilityTests(unittest.TestCase):
    def test_two_independent_synthetic_passes_are_identical(self):
        samples = [independent_fixture(), independent_fixture()]
        result = audit.validate_repetitions(samples)
        self.assertTrue(result["all_numeric_traces_identical"])
        self.assertEqual(result["scenario_count"], 9)
        self.assertEqual(result["cold_boots"], 18)
        self.assertEqual(result["observed_frames_per_scenario"], 122)
        self.assertEqual(len(result["scenario_digests"]), 9)
        self.assertEqual(result["metrics"]["jump"]["tap_1"]["elapsed_to_landing_frames"], 30)
        self.assertNotIn("traces", result)
        self.assertNotIn("reads", result)

    def test_single_subpixel_change_is_rejected_with_location(self):
        a, b = independent_fixture(), independent_fixture()
        b["traces"]["right_tap_8"][2080]["x_sub"] += 1
        with self.assertRaisesRegex(ValueError, r"right_tap_8 frame 2080.*x_sub"):
            audit.validate_repetitions([a, b])

    def test_missing_frame_and_scenario_are_rejected(self):
        a, b = independent_fixture(), independent_fixture()
        del b["traces"]["jump_hold_8"][2100]
        with self.assertRaisesRegex(ValueError, "Missing per-frame RAM observations"):
            audit.validate_repetitions([a, b])
        b = independent_fixture()
        del b["reads"]["jump_hold_8"]
        with self.assertRaisesRegex(ValueError, "missing or unexpected scenarios"):
            audit.validate_repetitions([a, b])

    def test_fake_controller_reads_are_rejected(self):
        a, b = independent_fixture(), independent_fixture()
        b["reads"]["reverse_right20_left20"] -= 1
        with self.assertRaisesRegex(ValueError, "input read count differs"):
            audit.validate_repetitions([a, b])

    def test_pre_input_divergence_is_rejected(self):
        a, b = independent_fixture(), independent_fixture()
        b["traces"]["jump_hold_8"][2069]["x"] += 1
        with self.assertRaisesRegex(ValueError, "different state before input"):
            audit.validate_repetitions([a, b])

    def test_invalid_numeric_data_cannot_be_hashed(self):
        a = independent_fixture()["traces"]["idle"]
        a[2074]["x_sub"] = True
        with self.assertRaisesRegex(ValueError, "Invalid 16-bit numeric field"):
            audit.numeric_digest(a)
        a = independent_fixture()["traces"]["idle"]
        a[2074]["not_a_motion_field"] = 10
        with self.assertRaisesRegex(ValueError, "Unexpected numeric fields"):
            audit.numeric_digest(a)

    def test_repeat_limits_are_enforced(self):
        with self.assertRaisesRegex(ValueError, "Repeat count"):
            audit.validate_repetitions([independent_fixture()])
        with self.assertRaisesRegex(ValueError, "Repeat count"):
            audit.validate_repetitions([independent_fixture()] * 5)

    def test_digest_covers_every_frame_and_value(self):
        a = independent_fixture()["traces"]["idle"]
        h1 = audit.numeric_digest(a)
        h2 = audit.numeric_digest(copy.deepcopy(a))
        self.assertEqual(h1, h2)
        self.assertEqual(len(h1), 64)
        a[2190]["vy_sub"] += 1
        self.assertNotEqual(h1, audit.numeric_digest(a))


if __name__ == "__main__":
    unittest.main()
