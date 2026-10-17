import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_camera_state_probe as camera


def interpolate(anchors, frame):
    if frame <= anchors[0][0]:
        return anchors[0][1]
    for (before, a), (after, b) in zip(anchors, anchors[1:]):
        if frame <= after:
            return round(a + (b-a)*(frame-before)/(after-before))
    return anchors[-1][1]


def synthetic_camera_trials():
    """Independent artificial RAM fields, never original Sega code/assets."""
    walking = [
        (2069, 61), (2123, 141), (2124, 142),
        (2180, 233), (2220, 298), (2344, 498),
    ]
    reverse_x = [
        (2360, 498), (2374, 481), (2375, 480),
        (2390, 455), (2430, 397),
    ]
    reverse_scroll = [
        (2360, 358), (2374, 358), (2375, 357),
        (2390, 333), (2430, 273),
    ]
    trials = {}
    for name in camera.SCENARIOS:
        sampled = {}
        for frame in range(camera.START, camera.END_EXCLUSIVE):
            if name == "idle":
                x, scroll = 61, 0
            else:
                x = interpolate(walking, frame)
                scroll = 0 if frame < 2124 else x-141
                if frame >= 2344:
                    x, scroll = 498, 358
                if name == "reverse" and frame > 2360:
                    x = interpolate(reverse_x, frame)
                    scroll = interpolate(reverse_scroll, frame)
                if name == "jump" and frame >= 2345:
                    x = 498 + int((frame-2344)*1.625)
                    scroll = x-141
            anomalies = {
                "idle": (), "right": (2203, 2281),
                "right_repeat": (2203, 2281),
                "reverse": (2203, 2281), "jump": (2203, 2281, 2393),
            }
            sampled[frame] = {
                "world_x": x, "world_y": 176,
                "scroll_candidate": scroll,
                "scroll_mirror_candidate": (scroll+384 if frame in anomalies[name]
                                            else scroll),
                "secondary_scroll_candidate": max(0, scroll-2),
                "sprite_object_world_x": max(0, x-16),
            }
        trials[name] = sampled
    return trials


COUNTS = {
    "idle":0, "right":450, "right_repeat":450,
    "reverse":360, "jump":480,
}


class CameraStateTests(unittest.TestCase):
    def test_numeric_decoder_is_little_endian_bounded(self):
        ram = bytearray(65536)
        ram[0x8524:0x8526] = bytes((0x66, 0x01))
        ram[0xA2D0:0xA2D2] = bytes((0xF2, 0x01))
        result = camera.decode(ram)
        self.assertEqual(result["scroll_candidate"], 358)
        self.assertEqual(result["world_x"], 498)
        self.assertEqual(camera.RAM_FIELDS["scroll_mirror_candidate"], 0xFEC6)

    def test_truncated_ram_rejected(self):
        with self.assertRaisesRegex(ValueError, "64 KiB"):
            camera.decode(bytes(100))

    def test_independent_synthetic_camera_hysteresis(self):
        metrics = camera.validate(synthetic_camera_trials(), COUNTS)
        self.assertEqual(metrics["first_scroll_frame_right"], 2124)
        self.assertEqual(metrics["first_camera_step_left_after_reverse"], 2375)
        self.assertEqual(metrics["camera_right_anchor_world_minus_scroll"],
                         {"minimum":140, "maximum":142})
        self.assertEqual(metrics["camera_left_anchor_world_minus_scroll"],
                         {"minimum":122, "maximum":124})
        self.assertEqual(metrics["wall_stop_camera_candidate"], 358)
        self.assertEqual(metrics["mirror_difference_frames"]["right"],
                         [2203, 2281])

    def test_replay_difference_rejected(self):
        trials = synthetic_camera_trials()
        trials["right_repeat"][2220]["scroll_candidate"] += 1
        with self.assertRaisesRegex(ValueError, "deterministic"):
            camera.validate(trials, COUNTS)

    def test_missing_dense_frame_rejected_before_unsafe_conclusions(self):
        trials = synthetic_camera_trials()
        del trials["right"][2280]
        with self.assertRaisesRegex(ValueError, "incomplete"):
            camera.validate(trials, COUNTS)

    def test_missing_observed_button_read_rejected(self):
        bad = dict(COUNTS, reverse=0)
        with self.assertRaisesRegex(ValueError, "Controller input"):
            camera.validate(synthetic_camera_trials(), bad)

    def test_first_scroll_shift_detects_regression(self):
        trials = synthetic_camera_trials()
        trials["right"][2124]["scroll_candidate"] = 0
        trials["right_repeat"][2124]["scroll_candidate"] = 0
        with self.assertRaisesRegex(ValueError, "First right-tracking"):
            camera.validate(trials, COUNTS)

    def test_reverse_deadzone_detects_regression(self):
        trials = synthetic_camera_trials()
        trials["reverse"][2374]["scroll_candidate"] -= 1
        with self.assertRaisesRegex(ValueError, "left tracking"):
            camera.validate(trials, COUNTS)

    def test_wrong_camera_anchor_rejected(self):
        trials = synthetic_camera_trials()
        trials["right"][2290]["scroll_candidate"] += 10
        trials["right_repeat"][2290]["scroll_candidate"] += 10
        with self.assertRaisesRegex(ValueError, "rightward camera"):
            camera.validate(trials, COUNTS)

    def test_wrong_jump_camera_anchor_rejected(self):
        trials = synthetic_camera_trials()
        trials["jump"][2330]["scroll_candidate"] += 10
        with self.assertRaisesRegex(ValueError, "during jumping"):
            camera.validate(trials, COUNTS)

    def test_mirror_is_not_unconditionally_identical(self):
        trials = synthetic_camera_trials()
        trials["right"][2203]["scroll_mirror_candidate"] = (
            trials["right"][2203]["scroll_candidate"]
        )
        trials["right_repeat"][2203]["scroll_mirror_candidate"] = (
            trials["right_repeat"][2203]["scroll_candidate"]
        )
        with self.assertRaisesRegex(ValueError, "mirrored scroll"):
            camera.validate(trials, COUNTS)

    def test_witness_uses_reference_rom_hash_and_no_original_data(self):
        path = Path(__file__).resolve().parents[1] / (
            "research/observations/user_rom_camera_state_2026-10-18.json"
        )
        if not path.is_file():
            return
        report = json.loads(path.read_text())
        self.assertEqual(
            report["reference_rom_sha256"],
            "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
        )
        self.assertEqual(report["metrics"]["first_scroll_frame_right"], 2124)
        self.assertEqual(report["metrics"]["first_camera_step_left_after_reverse"], 2375)
        self.assertNotIn("raw_RAM", report)
        self.assertNotIn("video_pixels", report)


if __name__ == "__main__":
    unittest.main()
