"""ROM-free mutation regression for the original-camera phase analyzer."""
import pathlib
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from genesis_camera_phase_model import evaluate_camera_phase, WINDOWS


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
