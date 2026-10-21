"""ROM-free tests of Stage184 event-marker provenance and alignment."""
import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from city_event_gap import build_report, original_numbers, parse_marker
from reference_evidence_integrity import read_inputs, validate

GOOD = (
    "STAGE170_LANDMARK_SEQUENCE_PASS "
    "synthetic_drop=215 synthetic_landing=231 synthetic_wall=280 "
    "relative_original_deltas=[+4,+4,+6] "
    "synthetic_reverse_lag=12 original_reverse_lag=15 "
    "synthetic_reverse_x30=456.500 original_reverse_x30=455 "
    "synthetic_reverse_x70=402.000 original_reverse_x70=397"
)


class EventGapReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.observations, source = read_inputs(ROOT)
        cls.evidence = validate(cls.observations, source)
        cls.original = original_numbers(cls.observations)

    def test_source_data_and_independent_intervals(self):
        self.assertEqual([self.original[k] for k in ("fall", "land", "wall")],
                         [211, 227, 274])
        self.assertEqual(self.original["reverse_camera_lag"], 15)
        self.assertEqual(self.original["reverse_x30"], 455)
        self.assertEqual(self.original["reverse_x70"], 397)

    def test_time_origin_separated_from_causal_residuals(self):
        report = build_report(parse_marker(GOOD), self.original, self.evidence)
        self.assertEqual(report["status"], "DIAGNOSTIC_ONLY_NOT_ROM_PARITY")
        self.assertFalse(report["original_rom_was_run"])
        self.assertTrue(report["synthetic_game_was_run"])
        self.assertEqual(report["raw_frame_differences"],
                         {"fall": 4, "land": 4, "wall": 6})
        self.assertEqual(
            report["origin_alignment"]["event_residuals_after_single_alignment"],
            {"fall": 0, "land": 0, "wall": 2})
        self.assertEqual(report["causal_intervals_frames"]["original"],
                         {"fall_to_land": 16, "land_to_wall": 47, "fall_to_wall": 63})
        self.assertEqual(report["causal_intervals_frames"]["synthetic_minus_original"],
                         {"fall_to_land": 0, "land_to_wall": 2, "fall_to_wall": 2})
        self.assertEqual(report["reverse_camera_lag_frames"]["synthetic_minus_original"], -3)
        self.assertEqual(report["reverse_world_x"]["reverse_x30"]["synthetic_minus_original"], 1.5)
        self.assertEqual(report["reverse_world_x"]["reverse_x70"]["synthetic_minus_original"], 5.0)

    def test_marker_is_rejected_when_malformed_or_duplicated(self):
        for invalid in ("", GOOD + "\n" + GOOD,
                        GOOD.replace("synthetic_drop=215", "synthetic_drop=oops"),
                        GOOD.replace("synthetic_landing=231", "synthetic_landing=205")):
            with self.subTest(invalid=invalid[:45]):
                with self.assertRaises(ValueError):
                    parse_marker(invalid)

    def test_false_original_deltas_are_rejected(self):
        data = parse_marker(GOOD)
        data["fall_delta"] += 1
        with self.assertRaisesRegex(ValueError, "contradicts"):
            build_report(data, self.original, self.evidence)

    def test_forged_original_values_are_rejected(self):
        data = parse_marker(GOOD)
        data["original_reverse_lag"] = 9
        with self.assertRaisesRegex(ValueError, "contradicts"):
            build_report(data, self.original, self.evidence)

    def test_inconsistent_original_camera_records_are_rejected(self):
        altered = copy.deepcopy(self.observations)
        altered["camera"]["metrics"]["first_camera_step_left_after_reverse"] = 2480
        with self.assertRaises(ValueError):
            original_numbers(altered)


if __name__ == "__main__":
    unittest.main()
