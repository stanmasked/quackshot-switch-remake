"""Stage198 public-safe fabricated original visual summaries only."""
import copy
import json
import stat
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from genesis_city_visual_roi_probe import (MARKERS, EXPECTED_Y,
                                           summarize, private_write)


def fake_samples():
    roi = (100, 80, 85, 140)
    right = {
        f: {
            "player_ram_y": EXPECTED_Y[f],
            "visual": {
                "roi": list(roi), "pixel_format": "RGB565",
                "blue_pixel_count": 25,
                "blue_centroid_x_milli": 134000,
                "blue_centroid_y_milli": 160000,
                "blue_bbox_xyxy_exclusive": [130, 150, 140, 160],
                "source_collision_plane_measured": False,
            }
        } for f in MARKERS
    }
    idle = copy.deepcopy(right)
    for row in idle.values():
        row["player_ram_y"] = 176
        row["visual"]["blue_pixel_count"] = 0
        row["visual"]["blue_centroid_x_milli"] = None
        row["visual"]["blue_centroid_y_milli"] = None
        row["visual"]["blue_bbox_xyxy_exclusive"] = None
    traces = {"idle": idle, "right": right,
              "right_repeat": copy.deepcopy(right)}
    reads = {"idle": 71, "right": 521, "right_repeat": 521}
    digests = {"idle": ["idle"], "right": ["right"],
               "right_repeat": ["right"]}
    return traces, reads, digests, roi


class OriginalVisualNumericSummaryTests(unittest.TestCase):
    def report(self):
        return summarize(*fake_samples(), "0" * 64, "1" * 64)

    def test_success_is_only_a_visual_heuristic_not_a_collision_witness(self):
        result = self.report()
        self.assertEqual(result["status"],
                         "PRIVATE_BLUE_PIXEL_ROI_HEURISTIC_NOT_COLLISION_MEASUREMENT")
        self.assertFalse(result["original_collision_plane_verified"])
        self.assertFalse(result["original_sprite_identity_proven_from_blue_pixels"])
        self.assertFalse(result["independently_attested_by_this_report"])
        self.assertFalse(result["safe_to_modify_switch_physics"])
        self.assertFalse(result["original_pixels_or_raw_ram_saved"])
        self.assertEqual([item["frame"] for item in result["sample_frames"]],
                         list(MARKERS))
        self.assertNotIn("idle", result)
        self.assertNotIn("right_repeat", result)

    def test_repeat_color_or_ram_drift_rejected(self):
        for field, value in (("player_ram_y", 177),
                             ("visual", None)):
            traces, reads, digests, roi = fake_samples()
            traces["right_repeat"][2280][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                summarize(traces, reads, digests, roi, "0"*64, "1"*64)

    def test_wrong_landmarks_reads_and_video_digest_rejected(self):
        traces, reads, digests, roi = fake_samples()
        traces["right"][2297]["player_ram_y"] = 207
        traces["right_repeat"][2297]["player_ram_y"] = 207
        with self.assertRaisesRegex(ValueError, "original city integer"):
            summarize(traces, reads, digests, roi, "0"*64, "1"*64)

        traces, reads, digests, roi = fake_samples()
        reads["right"] = 520
        with self.assertRaisesRegex(ValueError, "controller reads"):
            summarize(traces, reads, digests, roi, "0"*64, "1"*64)

        traces, reads, digests, roi = fake_samples()
        digests["right_repeat"] = ["changed"]
        with self.assertRaisesRegex(ValueError, "video digests"):
            summarize(traces, reads, digests, roi, "0"*64, "1"*64)

    def test_extra_raw_frames_and_missing_samples_rejected(self):
        traces, reads, digests, roi = fake_samples()
        traces["right"][2280]["raw_frame_bytes"] = "illegal"
        with self.assertRaisesRegex(ValueError, "payload"):
            summarize(traces, reads, digests, roi, "0"*64, "1"*64)

        traces, reads, digests, roi = fake_samples()
        del traces["right"][2280]
        with self.assertRaisesRegex(ValueError, "missing/extra"):
            summarize(traces, reads, digests, roi, "0"*64, "1"*64)

    def test_private_output_permissions_and_exclusive_create(self):
        with tempfile.TemporaryDirectory(prefix="qs-visual198-") as d:
            path = Path(d)/"numeric.json"
            result = self.report()
            private_write(result, path, ROOT)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertEqual(json.loads(path.read_text()), result)
            with self.assertRaises(ValueError):
                private_write(result, path, ROOT)

    def test_report_denied_inside_repository(self):
        path = ROOT / "should-never-create-private-visual198.json"
        with self.assertRaisesRegex(ValueError, "inside the public repository"):
            private_write(self.report(), path, ROOT)
        self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
