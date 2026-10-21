"""Unit tests for Stage186 source-free height and event-time audit."""
import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from city_drop_height_audit import parse_rows, summarize
from city_event_gap import original_numbers
from reference_evidence_integrity import read_inputs, validate


def example_trace():
    data = []
    for drop in (16, 32, 48, 64):
        length = {16: 12, 32: 17, 48: 21, 64: 24}[drop]
        for edge in range(23, 28):
            fall = 200 + (edge - 23) * 10
            data.append(
                f"STAGE186_DROP_ROW drop_px={drop} edge={edge} "
                f"unsupported={fall} moved={fall} "
                f"moved_quarter={fall+1} moved_one={fall+2} "
                f"land={fall+length} fall_x={edge*16-11:.5f} "
                f"land_x={edge*16+length:.5f}")
    data.append(
        "STAGE186_DROP_SWEEP_PASS rows=20 heights_px=16,32,48,64 "
        "edges=23..27 event_thresholds=ungrounded,0.01,0.25,1.0 "
        "original_assets=none"
    )
    return "\n".join(data)


class DropHeightAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data, src = read_inputs(ROOT)
        validate(data, src)
        cls.original = original_numbers(data)

    def test_height_groups_and_32px_original_diagnostic(self):
        data = summarize(parse_rows(example_trace()), self.original)
        self.assertFalse(data["original_rom_was_run"])
        self.assertFalse(data["physics_changed"])
        self.assertEqual(data["original_observed_fall_to_land_frames"], 16)
        self.assertEqual([x["synthetic_fall_to_land_frames"]
                          for x in data["height_summaries"]], [12,17,21,24])
        self.assertEqual(data["height_summaries"][1][
            "difference_to_original_32px_interval_frames"], 1)

    def test_missing_duplicate_and_unrecognized_rows_fail_closed(self):
        good = example_trace()
        rows = good.splitlines()
        for invalid in (good + "\n" + rows[0],
                        "\n".join(rows[1:]),
                        "\n".join(rows[:-1]),
                        good + "\nunexpected_original_rom=YES"):
            with self.subTest(invalid=invalid[-80:]):
                with self.assertRaises(ValueError):
                    parse_rows(invalid)

    def test_invalid_event_order_rejected(self):
        bad = example_trace().replace(
            "moved_quarter=201 moved_one=202 land=212",
            "moved_quarter=215 moved_one=202 land=212", 1)
        self.assertNotEqual(example_trace(), bad)
        with self.assertRaisesRegex(ValueError, "chronological"):
            parse_rows(bad)

    def test_height_cannot_affect_horizontal_departure(self):
        rows = parse_rows(example_trace())
        rows[32,23]["unsupported"] += 1
        with self.assertRaisesRegex(ValueError, "departure"):
            summarize(rows, self.original)

    def test_horizontal_shift_cannot_change_vertical_interval(self):
        rows = parse_rows(example_trace())
        rows[32,24]["fall_to_land_frames"] += 1
        with self.assertRaisesRegex(ValueError, "horizontal"):
            summarize(rows, self.original)

    def test_unobserved_original_value_not_silently_accepted(self):
        rows = parse_rows(example_trace())
        forged = copy.deepcopy(self.original)
        forged["land"] += 2
        with self.assertRaisesRegex(ValueError, "observation"):
            summarize(rows, forged)


if __name__ == "__main__":
    unittest.main()
