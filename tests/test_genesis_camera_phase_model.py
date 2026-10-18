"""ROM-free mutation regression for the original-camera phase analyzer."""
import pathlib
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from genesis_camera_phase_model import (evaluate_camera_phase, WINDOWS,
                                        verify_known_reference_anchors,
                                        KNOWN_CAMERA_VDP_ANCHORS)


def synthetic(lag):
    data = {}
    for label in ("right", "reverse", "jump"):
        frames = {}
        def cam(f):
            if label == "reverse":
                return 358 if f <= 2374 else 358 - 2 * (f-2374)
            return max(0, 2*(f-2123))
        for frame in range(2069, 2451):
            frames[frame] = {
                "software_camera_RAM_candidate": cam(frame),
                "render_buffer_RAM_0x852c": cam(frame-lag),
            }
        data[label] = frames
    return data


class Tests(unittest.TestCase):
    def test_candidates_identify_synthetic_ground_truth(self):
        for lag in (0,1,2,3,4):
            proof = evaluate_camera_phase(synthetic(lag))
            self.assertEqual(len(proof["windows"]), 4)
            for window in proof["windows"].values():
                self.assertEqual(window["best_lags_tied_included"], [lag])
                self.assertTrue(window["unique_best_lag"])
                self.assertEqual(window["scores"][lag]["sum_absolute_error_px"],0)
                self.assertEqual(window["strict_one_tick_lag_matches_all"],lag==1)
                self.assertEqual(len(set(s["samples"] for s in window["scores"])),1)
            self.assertFalse(proof["raw_original_samples_exported"])

    def test_missing_frame_is_rejected(self):
        data=synthetic(1)
        del data["right"][2220]
        with self.assertRaisesRegex(ValueError, "missing original consecutive"):
            evaluate_camera_phase(data)

    def test_missing_trial_is_rejected(self):
        data=synthetic(1)
        del data["reverse"]
        with self.assertRaisesRegex(ValueError,"missing original scenario"):
            evaluate_camera_phase(data)

    def test_bad_ram_value_is_rejected(self):
        data=synthetic(1)
        data["reverse"][2390]["render_buffer_RAM_0x852c"] = -1
        with self.assertRaisesRegex(ValueError,"invalid bounded"):
            evaluate_camera_phase(data)

    def test_stationary_frames_not_a_valid_lag_witness(self):
        data=synthetic(1)
        for f in data["right"]:
            data["right"][f]["software_camera_RAM_candidate"] = 0
        with self.assertRaisesRegex(ValueError,"too few camera-moving"):
            evaluate_camera_phase(data)

    def test_one_pixel_disagreement_disproves_exact_match(self):
        data=synthetic(1)
        data["right"][2240]["render_buffer_RAM_0x852c"] += 1
        row=evaluate_camera_phase(data)["windows"]["right_tracking"]
        self.assertFalse(row["strict_one_tick_lag_matches_all"])
        self.assertGreater(row["scores"][1]["sum_absolute_error_px"],0)

    def test_false_lag_due_to_constant_pixel_offset_is_ambiguous(self):
        # Pixel shift of 2 at constant 2px/tick looks exactly like lag=1
        # in steady motion. A responsible temporal classifier must abstain.
        data = synthetic(0)
        for scenario in ("right", "jump"):
            for frame, row in data[scenario].items():
                row["render_buffer_RAM_0x852c"] = max(
                    0, row["software_camera_RAM_candidate"] - 2)
        row = evaluate_camera_phase(data)["windows"]["right_tracking"]
        self.assertEqual(row["best_lags_tied_included"], [1])
        self.assertGreater(len(row["motion_signature_best_lags_including_ties"]), 1)
        self.assertEqual(row["zero_lag_with_fitted_offset_error_px"], 0)
        self.assertFalse(row["identifiable_by_motion_and_offset_controls"])
        self.assertEqual(row["lag_inference_status"],
                         "ambiguous_spatial_offset_or_motion_signature")

    def test_irregular_movement_separates_lag_from_spatial_offset(self):
        # 1,2,3,1,... pixels per tick, independently designed synthetic
        # camera path. A genuine 2-tick history can be differentiated.
        steps = (0, 1, 3, 6)
        def distance(t):
            n = max(0, t-2123)
            return (n//4)*7 + steps[n%4]
        def cam(scenario, t):
            return (max(0, 358-distance(t-2374+2123))
                    if scenario == "reverse" else distance(t))
        data = synthetic(2)
        for scenario, frames in data.items():
            for frame, row in frames.items():
                row["software_camera_RAM_candidate"] = cam(scenario, frame)
                row["render_buffer_RAM_0x852c"] = cam(scenario, frame-2)
        result = evaluate_camera_phase(data)
        for row in result["windows"].values():
            self.assertEqual(row["best_lags_tied_included"], [2])
            self.assertEqual(row["motion_signature_best_lags_including_ties"], [2])
            self.assertGreater(row["zero_lag_with_fitted_offset_error_px"], 0)
            self.assertTrue(row["identifiable_by_motion_and_offset_controls"])

    def test_fixed_published_reference_camera_vdp_anchors(self):
        data = synthetic(1)
        for scenario, landmarks in KNOWN_CAMERA_VDP_ANCHORS.items():
            for frame, (camera, render) in landmarks.items():
                data[scenario][frame] = {
                    "software_camera_RAM_candidate": camera,
                    "render_buffer_RAM_0x852c": render,
                }
        proof = verify_known_reference_anchors(data)
        self.assertEqual(proof["published_numeric_camera_vdp_anchors_verified"], 9)
        self.assertFalse(proof["original_ram_or_rom_exported"])
        data["right"][2180]["render_buffer_RAM_0x852c"] += 1
        with self.assertRaisesRegex(ValueError, "anchor: camera/VDP mismatch"):
            verify_known_reference_anchors(data)

    def test_missing_published_anchor_is_rejected(self):
        data = synthetic(1)
        del data["reverse"][2375]
        with self.assertRaisesRegex(ValueError, "anchor: missing checkpoint"):
            verify_known_reference_anchors(data)

    def test_report_is_only_numeric_aggregates(self):
        report=evaluate_camera_phase(synthetic(2))
        self.assertEqual(set(report),{
            "schema","candidate_lags_frames","windows","interpretation",
            "raw_original_samples_exported"})
        self.assertEqual(WINDOWS[1][0], "reverse_transition")
        for item in report["windows"].values():
            self.assertNotIn("original_frames",item)
            self.assertNotIn("ram_dump",item)


if __name__ == "__main__":
    unittest.main()
