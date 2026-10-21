"""Stage198 transient video-pixel numeric ROI tests. Synthetic colors only."""
import ctypes as C
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from genesis_blue_roi_numeric import blue_roi_numeric, validate_roi
from genesis_libretro_probe import run_core, BUTTONS, RETRO_DEVICE_JOYPAD
from test_genesis_checkpoint_phase_contract import FakeLibretroCore


class SyntheticBlueVideoCore(FakeLibretroCore):
    """Delivers only a fake 4x3 RGB565 framebuffer via real libretro callbacks."""
    def __init__(self, video_counts=None):
        super().__init__(video_counts=video_counts)
        pixels, self.pitch = artificial_frame(2, [(1, 0, 0x001F),
                                                  (2, 2, 0x001F)], padding=0)
        self.blue_buffer = C.create_string_buffer(bytes(pixels),
                                                   len(pixels))

    def retro_run(self):
        self.events.append(("run_started", self.frame))
        self.poll()
        self.input(0, RETRO_DEVICE_JOYPAD, 0, BUTTONS["RIGHT"])
        self.memory[0] = bytes([self.frame + 1])
        for _ in range(self.video_counts.get(self.frame, 1)):
            self.video(C.addressof(self.blue_buffer), 4, 3, self.pitch)
        self.events.append(("run_completed", self.frame))
        self.frame += 1


def artificial_frame(pixel_format, points, width=4, height=3, padding=6):
    bpp = 4 if pixel_format == 1 else 2
    pitch = width * bpp + padding
    data = bytearray(pitch * height)
    for x, y, color in points:
        off = y * pitch + x * bpp
        data[off:off+bpp] = color.to_bytes(bpp, "little")
    return data, pitch


class PrivateBlueROINumericTests(unittest.TestCase):
    def test_all_libretro_pixel_formats_report_same_blue_geometry(self):
        for pixel_format, blue, red in (
            (0, 0x001F, 0x7C00),
            (1, 0x000000FF, 0x00FF0000),
            (2, 0x001F, 0xF800),
        ):
            with self.subTest(pixel_format=pixel_format):
                pixels, pitch = artificial_frame(pixel_format, [
                    (1, 0, blue), (2, 2, blue), (3, 1, red)])
                actual = blue_roi_numeric(pixels, 4, 3, pitch,
                                          pixel_format, (0, 0, 4, 3))
                self.assertEqual(actual["blue_pixel_count"], 2)
                self.assertEqual(actual["blue_centroid_x_milli"], 1500)
                self.assertEqual(actual["blue_centroid_y_milli"], 1000)
                self.assertEqual(actual["blue_bbox_xyxy_exclusive"],
                                 [1, 0, 3, 3])
                self.assertFalse(actual["source_collision_plane_measured"])
                self.assertNotIn("pixels", actual)
                self.assertNotIn("buffer", actual)

    def test_empty_roi_is_non_assertion_not_player_absence(self):
        pixels, pitch = artificial_frame(2, [(0, 0, 0x001F)])
        report = blue_roi_numeric(pixels, 4, 3, pitch, 2, (2, 1, 2, 2))
        self.assertEqual(report["blue_pixel_count"], 0)
        self.assertIsNone(report["blue_centroid_x_milli"])
        self.assertIsNone(report["blue_centroid_y_milli"])
        self.assertIsNone(report["blue_bbox_xyxy_exclusive"])

    def test_blue_threshold_rejects_neutral_cyan_and_green(self):
        # RGB565: white / green / cyan / blue
        pixels, pitch = artificial_frame(2, [
            (0, 0, 0xFFFF), (1, 0, 0x07E0),
            (2, 0, 0x07FF), (3, 0, 0x001F)])
        report = blue_roi_numeric(pixels, 4, 3, pitch, 2, (0, 0, 4, 1))
        self.assertEqual(report["blue_pixel_count"], 1)
        self.assertEqual(report["blue_bbox_xyxy_exclusive"], [3, 0, 4, 1])

    def test_roi_rejects_misalignment_bad_stride_and_short_pixels(self):
        pixels, pitch = artificial_frame(0, [])
        for args in (
            (pixels, 4, 3, 7, 0, (0, 0, 4, 3)),
            (pixels[:-1], 4, 3, pitch, 0, (0, 0, 4, 3)),
            (pixels, 4, 3, pitch, 3, (0, 0, 4, 3)),
            (pixels, 4, 3, pitch, 0, (3, 0, 2, 1)),
            (pixels, 4, 3, pitch, 0, (0, 1, 1, 3)),
        ):
            with self.subTest(args=args[1:]), self.assertRaises(ValueError):
                blue_roi_numeric(*args)

    def test_invalid_roi_types_fail_closed(self):
        for bad in ((0, 0, 0, 2), (0, 0, -1, 2),
                    (0.0, 0, 4, 3), (True, 0, 4, 3),
                    (0, 0, 4), "0,0,4,3", (0, 0, 1025, 3)):
            with self.subTest(roi=bad), self.assertRaises(ValueError):
                validate_roi(bad)

    def test_actual_libretro_callback_returns_only_selected_numeric_frames(self):
        fake = SyntheticBlueVideoCore()
        result = run_core(fake, {"need_fullpath": False}, b"SYNTHETIC",
                          "synthetic", 4, [], video_stride=3,
                          video_roi=(0, 0, 4, 3),
                          video_roi_frames=(0, 2))
        points = result["video_roi_blue_numeric"]
        self.assertEqual([entry["frame"] for entry in points], [0, 2])
        self.assertEqual([entry["blue_pixel_count"] for entry in points], [2, 2])
        self.assertEqual(points[0]["blue_centroid_x_milli"], 1500)
        self.assertEqual(points[0]["blue_centroid_y_milli"], 1000)
        self.assertNotIn("pixel_bytes", str(result))
        self.assertEqual([entry["frame"] for entry in result["video_checkpoints"]],
                         [0, 3])
        self.assertFalse(fake.initialized)

    def test_missing_selected_video_callback_rejected_and_core_unloaded(self):
        fake = SyntheticBlueVideoCore(video_counts={1: 0})
        with self.assertRaisesRegex(RuntimeError, "ROI callback cadence"):
            run_core(fake, {"need_fullpath": False}, b"fake", "synthetic",
                     3, [], video_roi=(0, 0, 4, 3),
                     video_roi_frames=(1,))
        self.assertFalse(fake.initialized)
        self.assertIn("deinit", fake.events)

    def test_duplicate_selected_video_callback_rejected(self):
        fake = SyntheticBlueVideoCore(video_counts={1: 2})
        with self.assertRaisesRegex(RuntimeError, "ROI callback cadence"):
            run_core(fake, {"need_fullpath": False}, b"fake", "synthetic",
                     3, [], video_roi=(0, 0, 4, 3),
                     video_roi_frames=(1,))
        self.assertFalse(fake.initialized)

    def test_libretro_guard_rejects_unsafe_roi_configuration_before_core(self):
        opts = [
            dict(video_roi=(0, 0, 4, 3), video_roi_frames=[]),
            dict(video_roi=(0, 0, 4, 3), video_roi_frames=[1, 1]),
            dict(video_roi=(0, 0, 4, 3), video_roi_frames=[5]),
            dict(video_roi=(0, 0, 4, 3), video_roi_frames=[True]),
            dict(video_roi=(0, 0, 4, 3), video_roi_frames=list(range(25))),
            dict(video_roi=None, video_roi_frames=[1]),
            dict(video_roi=(0, 0, -1, 3), video_roi_frames=[1]),
        ]
        for arg in opts:
            with self.subTest(arg=arg), self.assertRaises(ValueError):
                run_core(None, {"need_fullpath": False}, b"",
                         "test", 5, [], **arg)


if __name__ == "__main__":
    unittest.main()
