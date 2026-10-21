"""Stage185 tests: complete authored grid, causal sensitivities, fail closed."""
import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from city_geometry_sensitivity import analyze, parse_sweep
from city_event_gap import original_numbers
from reference_evidence_integrity import read_inputs, validate


def example_output():
    rows = []
    for edge in range(23, 28):
        for wall in range(31, 35):
            fall = 190 + (edge - 23) * 10
            land = fall + 17
            stop = 280 + (wall - 31) * 10
            rows.append(
                f"STAGE185_GEOMETRY_ROW edge={edge} wall={wall} "
                f"fall={fall} land={land} stop={stop} "
                f"fall_x={edge*16-11:.5f} land_x={edge*16+18:.5f} "
                f"stop_x={wall*16-12:.5f}"
            )
    rows.append("STAGE185_GEOMETRY_SWEEP_PASS rows=20 "
                "edge_tiles=23..27 wall_tiles=31..34 "
                "source_geometry=synthetic original_assets=none")
    return "\n".join(rows)


class GeometrySensitivityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        observations, src = read_inputs(ROOT)
        validate(observations, src)
        cls.original = original_numbers(observations)

    def test_complete_matrix_and_diagnostic_deltas(self):
        rows = parse_sweep(example_output())
        report = analyze(rows, self.original)
        self.assertEqual(report["summary"]["tested_tile_pairs"], 20)
        self.assertEqual(report["summary"]["ledge_finite_differences"], 16)
        self.assertEqual(report["summary"]["wall_finite_differences"], 15)
        self.assertEqual(report["summary"]["ledge_fall_delay_range_frames"], [10, 10])
        self.assertEqual(report["summary"]["wall_stop_delay_range_frames"], [10, 10])
        self.assertEqual(report["comparison"]["original_event_intervals"],
                         {"fall_to_land": 16, "land_to_stop": 47})
        self.assertFalse(report["original_rom_was_run"])
        self.assertFalse(report["original_assets_in_report"])

    def test_duplicate_missing_and_bad_footer_rejected(self):
        good = example_output()
        lines = good.splitlines()
        with self.assertRaisesRegex(ValueError, "duplicate"):
            parse_sweep(good + "\n" + lines[0])
        with self.assertRaisesRegex(ValueError, "grid"):
            parse_sweep("\n".join(lines[1:]))
        with self.assertRaisesRegex(ValueError, "footer"):
            parse_sweep("\n".join(lines[:-1]))

    def test_unexpected_control_character_or_injected_line_rejected(self):
        with self.assertRaisesRegex(ValueError, "invalid"):
            parse_sweep(example_output() + "\nINJECTED_FILE=rom.bin")

    def test_wrong_downstream_event_dependency_rejected(self):
        rows = parse_sweep(example_output())
        rows[24, 32]["land"] += 1
        with self.assertRaisesRegex(ValueError, "downstream wall"):
            analyze(rows, self.original)

    def test_wrong_monotonic_ledge_response_rejected(self):
        rows = parse_sweep(example_output())
        rows[24, 31]["fall"] = rows[23, 31]["fall"]
        with self.assertRaisesRegex(ValueError, "ledge shift"):
            analyze(rows, self.original)

    def test_forged_original_reference_rejected(self):
        rows = parse_sweep(example_output())
        forged = copy.deepcopy(self.original)
        forged["wall"] = 999
        with self.assertRaisesRegex(ValueError, "source-observation"):
            analyze(rows, forged)


if __name__ == "__main__":
    unittest.main()
