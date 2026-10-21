"""Synthetic-only tests for private Genesis fractional fall witness.

No original-game ROM, RAM, video, audio, game inputs or savestates.
"""
import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_city_fractional_fall_probe as phase


def synthetic_series():
    """Invent 16.16 steps around independently known integer landmarks."""
    right = {}
    idle = {}
    for frame in phase.FRAME_RANGE:
        if frame <= 2278:
            y, frac = 176, 0
        elif frame == 2279:
            y, frac = 176, 12800
        elif frame == 2280:
            y, frac = 176, 38500
        elif frame == 2281:
            y, frac = 177, 11200
        elif frame < 2297:
            y, frac = 177 + (frame - 2281) * 2, 0
        else:
            y, frac = 208, 0
        right[frame] = {"x": 396, "y": y, "y_fraction": frac}
        idle[frame] = {"x": 61, "y": 176, "y_fraction": 0}
    return right, copy.deepcopy(right), idle


INPUTS = {"idle": 0, "right": 450, "right_repeat": 450}


class FractionalFallProbeTests(unittest.TestCase):
    def test_subpixel_first_fall_precedes_integer_frame(self):
        right, repeated, idle = synthetic_series()
        report = phase.analyze(right, repeated, idle, INPUTS)
        self.assertEqual(report["first_y_positive_subpixel_frame"], 2279)
        self.assertEqual(report["first_y_above_quarter_pixel_frame"], 2280)
        self.assertEqual(report["first_y_above_one_pixel_frame"], 2281)
        self.assertEqual(report["first_integer_y_increase_frame"], 2281)
        self.assertEqual(report["first_integer_lower_floor_frame"], 2297)
        self.assertEqual(report["first_exact_16_16_lower_floor_frame"], 2297)
        self.assertEqual(report["fractional_y_units_at_integer_176_frame_2280"], 38500)
        self.assertEqual(
            report["fall_duration_by_threshold_frames"],
            {"positive_subpixel_to_integer_floor": 18,
             "over_quarter_to_integer_floor": 17,
             "over_one_to_integer_floor": 16,
             "integer_change_to_integer_floor": 16})

    def test_report_does_not_serialize_ram_or_media(self):
        right, repeated, idle = synthetic_series()
        report = phase.analyze(right, repeated, idle, INPUTS)
        blob = json.dumps(report)
        self.assertEqual(report["status"], "NUMERIC_SERIES_ANALYSIS_UNVERIFIED")
        self.assertFalse(report["original_rom_executed_locally"])
        self.assertFalse(report["raw_ram_saved"])
        self.assertFalse(report["raw_frames_saved"])
        self.assertFalse(report["rom_or_emulator_payload_in_report"])
        for secret in ("raw_ram_bytes", "savestate", "pixel_data",
                       "video_frames", "rom_payload", "input_log"):
            self.assertNotIn(secret, blob)
        self.assertNotIn("frame_samples", report)

    def test_unmodified_original_integer_reference_must_remain(self):
        right, repeated, idle = synthetic_series()
        right[2281]["y"] = 176
        repeated[2281]["y"] = 176
        with self.assertRaisesRegex(ValueError, "integer landmarks"):
            phase.analyze(right, repeated, idle, INPUTS)

    def test_corrupt_fractional_word_fails_closed(self):
        right, repeated, idle = synthetic_series()
        right[2280]["y_fraction"] = 65536
        repeated[2280]["y_fraction"] = 65536
        with self.assertRaisesRegex(ValueError, "fraction word"):
            phase.analyze(right, repeated, idle, INPUTS)

    def test_missing_or_extra_sample_is_rejected(self):
        for extra in (False, True):
            right, repeated, idle = synthetic_series()
            if extra:
                right[9999] = right[2280]
            else:
                del right[2280]
            with self.assertRaisesRegex(ValueError, "missing or extra"):
                phase.analyze(right, repeated, idle, INPUTS)

    def test_repeatability_and_controller_reads_are_required(self):
        right, repeated, idle = synthetic_series()
        repeated[2280]["y_fraction"] = 1
        with self.assertRaisesRegex(ValueError, "diverged"):
            phase.analyze(right, repeated, idle, INPUTS)
        right, repeated, idle = synthetic_series()
        with self.assertRaisesRegex(ValueError, "callbacks"):
            phase.analyze(right, repeated, idle,
                          {"idle":0, "right":449, "right_repeat":450})

    def test_unexpected_motion_before_ledge_is_rejected(self):
        right, repeated, idle = synthetic_series()
        right[2250]["y_fraction"] = 1
        repeated[2250]["y_fraction"] = 1
        with self.assertRaisesRegex(ValueError, "before ledge"):
            phase.analyze(right, repeated, idle, INPUTS)

    def test_idle_reference_must_be_still(self):
        right, repeated, idle = synthetic_series()
        idle[2270]["y_fraction"] = 1
        with self.assertRaisesRegex(ValueError, "idle first-city"):
            phase.analyze(right, repeated, idle, INPUTS)


if __name__ == "__main__":
    unittest.main()
