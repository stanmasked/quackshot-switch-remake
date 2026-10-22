"""Stage205: fabricated blue-pixel components; NO Genesis ROM/frame assets."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from genesis_blue_roi_components import blue_roi_components_numeric as components
from genesis_blue_roi_numeric import blue_roi_numeric
from genesis_libretro_probe import run_core
from test_genesis_visual_roi_numeric import (
    artificial_frame, SyntheticBlueVideoCore)


def colored(pixel_format, locations, width=10, height=6, padding=6):
    blue = {0: 0x001F, 1: 0x000000FF, 2: 0x001F}[pixel_format]
    return artificial_frame(pixel_format,
                            [(x, y, blue) for x, y in locations],
                            width=width, height=height, padding=padding)


class GenesisConnectedBlueComponentsTests(unittest.TestCase):
    def test_all_three_libretro_pixel_formats_same_integer_components(self):
        large = [(x, y) for x in (1, 2) for y in (1, 2, 3)]
        medium = [(5, 1), (5, 2)]
        isolated = [(8, 4)]
        for fmt in (0, 1, 2):
            with self.subTest(format=fmt):
                data, pitch = colored(fmt, large + medium + isolated)
                result = components(data, 10, 6, pitch, fmt, (0, 0, 10, 6),
                                    min_component_pixels=2)
                self.assertEqual(result["blue_pixel_count"], 9)
                self.assertEqual(result["all_blue_component_count"], 3)
                self.assertEqual(result["eligible_component_count"], 2)
                self.assertEqual(result["reported_component_count"], 2)
                self.assertTrue(result["largest_component_dominates_by_2x"])
                self.assertEqual([x["blue_pixels"] for x in result["components"]],
                                 [6, 2])
                self.assertEqual(result["components"][0]["blue_centroid_x_milli"], 1500)
                self.assertEqual(result["components"][0]["blue_centroid_y_milli"], 2000)
                self.assertEqual(result["components"][0]["blue_bbox_xyxy_exclusive"],
                                 [1, 1, 3, 4])
                self.assertFalse(result["components"][0]["touches_roi_boundary"])
                self.assertFalse(result["selected_component_is_proven_player"])
                self.assertFalse(result["original_sprite_hitbox_or_contact_verified"])
                self.assertFalse(result["safe_to_modify_switch_physics"])
                self.assertNotIn("pixels", result["components"][0])

    def test_disconnected_diagonal_blue_pixels_are_not_merged(self):
        data, pitch = colored(2, [(1, 1), (2, 2)])
        r = components(data, 10, 6, pitch, 2, (0, 0, 10, 6),
                       min_component_pixels=2)
        self.assertEqual(r["all_blue_component_count"], 2)
        self.assertEqual(r["eligible_component_count"], 0)
        self.assertEqual(r["components"], [])
        self.assertFalse(r["largest_component_dominates_by_2x"])

    def test_roi_clipping_and_boundary_indicator(self):
        data, pitch = colored(2, [(2, 1), (2, 2), (3, 2)])
        r = components(data, 10, 6, pitch, 2, (2, 1, 3, 3),
                       min_component_pixels=2)
        self.assertEqual(r["components"][0]["blue_bbox_xyxy_exclusive"],
                         [2, 1, 4, 3])
        self.assertTrue(r["components"][0]["touches_roi_boundary"])
        self.assertEqual(r["roi"], [2, 1, 3, 3])

    def test_component_threshold_and_top_k_are_bounded(self):
        regions = [(1, y) for y in range(2)] + [(3, y) for y in range(3)] + [
            (5, y) for y in range(4)]
        data, pitch = colored(2, regions)
        r = components(data, 10, 6, pitch, 2, (0, 0, 10, 6),
                       min_component_pixels=2, max_components=1)
        self.assertEqual(r["eligible_component_count"], 3)
        self.assertEqual(r["reported_component_count"], 1)
        self.assertEqual(r["components"][0]["blue_pixels"], 4)
        self.assertFalse(r["largest_component_dominates_by_2x"])

    def test_component_count_matches_original_numeric_blue_filter(self):
        for fmt in (0, 1, 2):
            with self.subTest(fmt=fmt):
                data, pitch = colored(fmt, [(0, 0), (1, 0), (3, 1)],
                                      width=5, height=4)
                r = components(data, 5, 4, pitch, fmt, (0, 0, 5, 4))
                old = blue_roi_numeric(data, 5, 4, pitch, fmt, (0, 0, 5, 4))
                self.assertEqual(r["blue_pixel_count"], old["blue_pixel_count"])
                self.assertEqual(r["blue_pixel_count"], 3)

    def test_reject_invalid_options_memory_and_coordinates(self):
        data, pitch = colored(2, [])
        cases = [
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"max_components":5}),
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"min_component_pixels":1}),
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"min_component_pixels":False}),
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"max_components":True}),
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"max_components":0}),
            (data[:-1], 10, 6, pitch, 2, (0, 0, 10, 6), {}),
            (data, 10, 6, 5, 2, (0, 0, 10, 6), {}),
            (data, 10, 6, pitch, 6, (0, 0, 10, 6), {}),
            (data, 10, 6, pitch, 2, (5, 0, 6, 6), {}),
            (data, 10, 6, pitch, 2, (0, 0, 10, 6), {"min_component_pixels":1025}),
        ]
        for args in cases:
            with self.subTest(options=args[-1]), self.assertRaises(ValueError):
                components(*args[:-1], **args[-1])

    def test_roi_budget_cannot_exceed_65536_pixels(self):
        data = bytes(600 * 600 * 2)
        with self.assertRaisesRegex(ValueError, "65536"):
            components(data, 600, 600, 1200, 2, (0, 0, 300, 300))

    def test_actual_libretro_video_callback_exposes_only_numeric_components(self):
        fake = SyntheticBlueVideoCore()
        result = run_core(fake, {"need_fullpath":False}, b"SYNTHETIC", "fake", 4,
                          [], video_roi=(0, 0, 4, 3),
                          video_roi_frames=(0, 2), video_roi_components=True)
        self.assertEqual([e["frame"] for e in result["video_roi_components_numeric"]],
                         [0, 2])
        self.assertEqual([e["blue_pixel_count"]
                          for e in result["video_roi_components_numeric"]], [2, 2])
        self.assertEqual([e["eligible_component_count"]
                          for e in result["video_roi_components_numeric"]], [0, 0])
        self.assertIsNone(result["video_roi_vdp_numeric"])
        self.assertFalse(fake.initialized)

    def test_disabled_components_have_no_output_and_wrong_flags_rejected(self):
        fake = SyntheticBlueVideoCore()
        result = run_core(fake, {"need_fullpath":False}, b"SYNTHETIC", "fake", 2,
                          [], video_roi=(0, 0, 4, 3), video_roi_frames=(0,))
        self.assertIsNone(result["video_roi_components_numeric"])
        for flag in (1, "yes", None):
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                run_core(None, {"need_fullpath":False}, b"FAKE", "fake", 2,
                         [], video_roi=(0, 0, 4, 3),
                         video_roi_frames=(0,), video_roi_components=flag)
        with self.assertRaises(ValueError):
            run_core(None, {"need_fullpath":False}, b"FAKE", "fake", 2,
                     [], video_roi_components=True)

    def test_missing_or_duplicate_video_callback_rejected(self):
        for counts in ({1:0},{1:2}):
            fake = SyntheticBlueVideoCore(video_counts=counts)
            with self.subTest(counts=counts), self.assertRaisesRegex(
                    RuntimeError, "ROI callback cadence"):
                run_core(fake, {"need_fullpath":False}, b"FAKE", "fake", 3, [],
                         video_roi=(0, 0, 4, 3), video_roi_frames=(1,),
                         video_roi_components=True)
            self.assertFalse(fake.initialized)


if __name__ == "__main__":
    unittest.main()
