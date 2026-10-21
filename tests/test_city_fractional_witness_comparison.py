"""Stage189: synthetic unit tests. These do NOT emulate the original ROM."""
import copy
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from city_fractional_witness_comparison import compare
from reference_evidence_integrity import read_inputs, validate


def authored_32px_report():
    rows = []
    for height in (16, 32, 48, 64):
        for edge in range(23, 28):
            x = 200 + 10 * (edge - 23)
            rows.append({
                "drop_height_px": height, "ledge_column": edge,
                "fall_0p01": x, "fall_0p25": x + 1,
                "fall_1px": x + 2, "landing": x + 17,
            })
    return {
        "schema": "quackshot_city_drop_height_sensitivity_v1",
        "status": "SYNTHETIC_DROP_EXPERIMENT_NOT_ROM_PARITY",
        "original_rom_was_run": False,
        "original_assets_in_report": False,
        "physics_changed": False,
        "rows": rows,
    }


def simulated_external_witness(ref):
    """Fabricated numerical fixture; NEVER a claim of ROM execution."""
    return {
        "schema": "quackshot_genesis_fractional_fall_event_v1",
        "status": "PRIVATE_ORIGINAL_EMULATION_NUMERIC_ONLY",
        "original_rom_executed_locally": True,
        "raw_ram_saved": False, "raw_frames_saved": False,
        "rom_or_emulator_payload_in_report": False,
        "world_y_integer_upper": 176, "world_y_integer_lower": 208,
        "first_y_positive_subpixel_frame": 2279,
        "first_y_above_quarter_pixel_frame": 2280,
        "first_y_above_one_pixel_frame": 2281,
        "first_integer_y_increase_frame": 2281,
        "first_integer_lower_floor_frame": 2297,
        "first_exact_16_16_lower_floor_frame": 2297,
        "fractional_y_units_at_integer_176_frame_2280": 38500,
        "fall_duration_by_threshold_frames": {
            "positive_subpixel_to_integer_floor": 18,
            "over_quarter_to_integer_floor": 17,
            "over_one_to_integer_floor": 16,
            "integer_change_to_integer_floor": 16,
        },
        "frame_label_caveat": (
            "Simulated report only: callback frame labels are not "
            "a recovered 68000 update phase or a rendered video event."),
        "scope": ["Mock original city only", "Synthetic fixture, no ROM run",
                  "Never use unit test values as original observations"],
        "reference_sha256": ref["rom_sha256"],
        "core_binary_sha256": ref["genesis_core_sha256"],
        "emulator": "Genesis Plus GX libretro v1.7.4",
        "independent_original_runs": 3,
        "ram_checkpoint_phase": "post_retro_run_before_next_input",
        "video_callback_phase": "during_retro_run",
    }


class FractionalWitnessComparisonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.obs, cls.source = read_inputs(ROOT)
        cls.ref = validate(cls.obs, cls.source)

    def test_no_new_measurement_stays_explicitly_unknown(self):
        result = compare(self.obs, self.source, authored_32px_report())
        self.assertEqual(result["status"], "NO_NEW_ORIGINAL_FRACTIONAL_MEASUREMENTS")
        self.assertFalse(result["original_rom_executed_by_this_comparison"])
        self.assertIsNone(result["original_fractional_threshold_intervals"])
        self.assertIsNone(result["strictly_comparable_threshold_deltas"])
        self.assertEqual(result["authored_switch_32px_threshold_intervals"],
                         {"above_0p01": 17, "above_0p25": 16,
                          "above_1p0": 15})
        self.assertEqual(result["public_integer_only_observations"][
                         "integer_event_interval_frames"], 16)

    def test_validly_shaped_fake_report_still_unattested(self):
        fake = simulated_external_witness(self.ref)
        result = compare(self.obs, self.source, authored_32px_report(), fake)
        self.assertEqual(result["status"],
                         "USER_NUMERIC_WITNESS_NOT_INDEPENDENTLY_ATTESTED")
        self.assertFalse(result["original_rom_executed_by_this_comparison"])
        self.assertEqual(result["strictly_comparable_threshold_deltas"], {
            "quarter_pixel_synthetic_minus_private_numeric": -1,
            "one_pixel_synthetic_minus_private_numeric": -1,
        })
        self.assertNotIn("positive_subpixel_synthetic_minus_private_numeric",
                         result["strictly_comparable_threshold_deltas"])
        self.assertNotIn("fractional_y_units_at_integer_176_frame_2280", result)

    def test_frame_2280_fraction_must_agree_with_first_crossings(self):
        # All of these are deliberately forged, ROM-free fixtures.
        scenarios = (
            ("zero_but_claims_earlier_motion",
             "fractional_y_units_at_integer_176_frame_2280", 0,
             "positive threshold"),
            ("below_quarter_but_claims_quarter_crossing",
             "fractional_y_units_at_integer_176_frame_2280", 12000,
             "quarter-pixel threshold"),
            ("above_quarter_but_claims_later_crossing",
             "first_y_above_quarter_pixel_frame", 2281,
             "quarter-pixel threshold"),
        )
        for label, field, value, error in scenarios:
            fake = simulated_external_witness(self.ref)
            fake[field] = value
            if field == "first_y_above_quarter_pixel_frame":
                fake["fall_duration_by_threshold_frames"][
                    "over_quarter_to_integer_floor"] = 16
            with self.subTest(case=label), self.assertRaisesRegex(
                    ValueError, error):
                compare(self.obs, self.source, authored_32px_report(), fake)

    def test_exact_quarter_boundary_is_strictly_greater_only(self):
        # Numerically consistent forged fixtures are accepted as SHAPES,
        # never promoted to independently verified source measurements.
        cases = (
            (0, 2281, 2281),
            (1, 2280, 2281),
            (16384, 2280, 2281),
            (16385, 2280, 2280),
        )
        for fraction, positive, quarter in cases:
            fake = simulated_external_witness(self.ref)
            fake["fractional_y_units_at_integer_176_frame_2280"] = fraction
            fake["first_y_positive_subpixel_frame"] = positive
            fake["first_y_above_quarter_pixel_frame"] = quarter
            fake["fall_duration_by_threshold_frames"][
                "positive_subpixel_to_integer_floor"] = 2297 - positive
            fake["fall_duration_by_threshold_frames"][
                "over_quarter_to_integer_floor"] = 2297 - quarter
            with self.subTest(fraction=fraction):
                report = compare(self.obs, self.source,
                                 authored_32px_report(), fake)
                self.assertEqual(report["status"],
                                 "USER_NUMERIC_WITNESS_NOT_INDEPENDENTLY_ATTESTED")
                self.assertFalse(report["physics_calibration_gate"][
                                 "safe_to_modify_switch_physics"])

    def test_fake_numeric_witness_never_authorizes_physics_tuning(self):
        fake = simulated_external_witness(self.ref)
        report = compare(self.obs, self.source, authored_32px_report(), fake)
        gate = report["physics_calibration_gate"]
        self.assertFalse(gate["safe_to_modify_switch_physics"])
        self.assertEqual(gate["status"],
                         "BLOCKED_COORDINATE_ORIGIN_AND_HITBOX_UNMEASURED")
        self.assertEqual(len(gate["required_before_tuning"]), 3)
        no_witness = compare(self.obs, self.source, authored_32px_report())
        self.assertEqual(no_witness["physics_calibration_gate"]["status"],
                         "BLOCKED_NO_NEW_ORIGINAL_SUBPIXEL_MEASUREMENT")
        self.assertFalse(no_witness["physics_calibration_gate"][
                         "safe_to_modify_switch_physics"])

    def test_wrong_phase_cannot_be_mistaken_for_original_timing(self):
        for key in ("ram_checkpoint_phase", "video_callback_phase"):
            fake = simulated_external_witness(self.ref)
            fake[key] = "unknown_original_instruction_phase"
            with self.subTest(field=key), self.assertRaisesRegex(
                    ValueError, "phase contract"):
                compare(self.obs, self.source, authored_32px_report(), fake)

    def test_unverified_offline_stage188_analysis_cannot_escalate(self):
        fake = simulated_external_witness(self.ref)
        fake["status"] = "NUMERIC_SERIES_ANALYSIS_UNVERIFIED"
        fake["original_rom_executed_locally"] = False
        with self.assertRaisesRegex(ValueError, "unverified synthetic"):
            compare(self.obs, self.source, authored_32px_report(), fake)

    def test_forged_original_or_core_digest_rejected(self):
        for key in ("reference_sha256", "core_binary_sha256"):
            fake = simulated_external_witness(self.ref)
            fake[key] = "0" * 64
            with self.subTest(field=key), self.assertRaisesRegex(
                    ValueError, "SHA256"):
                compare(self.obs, self.source, authored_32px_report(), fake)

    def test_extra_raw_ram_or_frame_log_never_propagates(self):
        for field in ("ram_bytes", "frame_by_frame_ram", "rom_bytes"):
            fake = simulated_external_witness(self.ref)
            fake[field] = "SENSITIVE_BYTES"
            with self.subTest(field=field), self.assertRaisesRegex(
                    ValueError, "missing/extra fields"):
                compare(self.obs, self.source, authored_32px_report(), fake)

    def test_frame_and_duration_contradictions_rejected(self):
        for key in ("first_y_above_quarter_pixel_frame",
                    "first_integer_lower_floor_frame"):
            fake = simulated_external_witness(self.ref)
            fake[key] += 1
            with self.subTest(field=key), self.assertRaises(ValueError):
                compare(self.obs, self.source, authored_32px_report(), fake)
        fake = simulated_external_witness(self.ref)
        fake["fall_duration_by_threshold_frames"]["over_one_to_integer_floor"] = 15
        with self.assertRaisesRegex(ValueError, "duration contradicts"):
            compare(self.obs, self.source, authored_32px_report(), fake)

    def test_wrong_synthetic_provenance_and_duplicate_rows_rejected(self):
        fake = authored_32px_report()
        fake["original_rom_was_run"] = True
        with self.assertRaisesRegex(ValueError, "provenance"):
            compare(self.obs, self.source, fake)
        fake = authored_32px_report()
        fake["rows"][-1] = copy.deepcopy(fake["rows"][7])
        with self.assertRaisesRegex(ValueError, "missing/duplicate"):
            compare(self.obs, self.source, fake)

    def test_original_numeric_evidence_integrity_still_required(self):
        observations = copy.deepcopy(self.obs)
        observations["obstacles"]["ledge"]["first_falling_frame"] = 3000
        with self.assertRaises(ValueError):
            compare(observations, self.source, authored_32px_report())

    def test_baseline_only_without_artifact_or_witness(self):
        result = compare(self.obs, self.source)
        self.assertIsNone(result["authored_switch_32px_threshold_intervals"])
        self.assertIsNone(result["original_fractional_threshold_intervals"])
        self.assertFalse(result["contains_original_media_or_raw_ram"])


if __name__ == "__main__":
    unittest.main()
