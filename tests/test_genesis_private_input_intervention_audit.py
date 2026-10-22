"""Stage210: synthetic-only tests for private original input intervention audit.

Every number here is invented. These tests do not load or embed the original
ROM, private capture reports, graphics, or save states.
"""
import copy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_private_input_intervention_audit as audit


CAMERA = [92, 157, 254, 282, 358]
RENDER = [90, 155, 252, 280, 358]


def fake_component(role, frame_index):
    if role == "left":
        return {
            "blue_bbox_xyxy_exclusive": [43, 124, 62, 140],
            "blue_centroid_x_milli": 50000,
            "blue_centroid_y_milli": 132000,
            "blue_pixels": 80,
            "touches_roi_boundary": False,
        }
    return {
        "blue_bbox_xyxy_exclusive": [128, 123, 149, 139],
        "blue_centroid_x_milli": 136000,
        "blue_centroid_y_milli": 131000 + frame_index * 1000,
        "blue_pixels": 70 + frame_index,
        "touches_roi_boundary": False,
    }


def fake_row(role, run, i):
    frame = audit.MARKERS[i]
    idle = run == "idle"
    right = run in ("right", "right_repeat")
    present = (role == "left" and idle) or (role == "right" and right)
    component = fake_component(role, i) if present else None
    return {
        "frame": frame,
        "world_x": 61 if idle else audit.EXPECTED_X[frame],
        "world_y": 176 if idle else audit.EXPECTED_Y_RIGHT[frame],
        "camera": 0 if idle else CAMERA[i],
        "render_buffer": 0 if idle else RENDER[i],
        "color_blue_pixel_count": 100 if present else 0,
        "connected_color_groups": 5 if present else 0,
        "largest_color_group": component,
    }


def fake_report(role):
    return {
        "schema": audit.SCHEMA,
        "original_rom_executed_locally": True,
        "reference_rom_sha256": audit.ROM_SHA,
        "research_core_sha256": audit.INSTRUMENTED_CORE_SHA,
        "roi": list(audit.ROI[role]),
        "runs": {
            name: [fake_row(role, name, i) for i in range(len(audit.MARKERS))]
            for name in ("idle", "right")
        },
        "same_right_replay_exact": True,
        "independent_identity_or_hitbox_proven": False,
        "safe_to_modify_switch_physics": False,
    } | {"runs": {**{
        "idle": [fake_row(role, "idle", i) for i in range(len(audit.MARKERS))],
        "right": [fake_row(role, "right", i) for i in range(len(audit.MARKERS))],
    }, "right_repeat": [fake_row(role, "right_repeat", i)
                        for i in range(len(audit.MARKERS))]}}


class SyntheticPrivateInputInterventionTests(unittest.TestCase):
    def pair(self):
        return fake_report("left"), fake_report("right")

    def test_valid_synthetic_pair_passes_without_identity_claim(self):
        result = audit.analyze_pair(*self.pair())
        self.assertEqual(result["tested_frames"], list(audit.MARKERS))
        self.assertTrue(result["left_idle_color_present_and_right_color_absent"])
        self.assertTrue(result["right_idle_color_absent_and_right_color_present"])
        self.assertTrue(result["right_replay_identical_in_both_rois"])
        self.assertTrue(result["RAM_camera_and_render_buffer_agree_across_ROIs"])
        self.assertFalse(result["original_execution_provenance_independently_attested"])
        self.assertFalse(result["color_groups_identified_as_Donald"])
        self.assertFalse(result["original_hitbox_or_contact_plane_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_wrong_rom_or_core_hash_rejected(self):
        for key in ("reference_rom_sha256", "research_core_sha256"):
            left, right = self.pair()
            right[key] = "0" * 64
            with self.subTest(key=key), self.assertRaises(ValueError):
                audit.analyze_pair(left, right)

    def test_wrong_roi_rejected(self):
        left, right = self.pair()
        left["roi"] = [0, 0, 32, 60]
        with self.assertRaises(ValueError):
            audit.analyze_pair(left, right)

    def test_unproven_hitbox_or_physics_claim_rejected(self):
        for key in ("independent_identity_or_hitbox_proven",
                    "safe_to_modify_switch_physics"):
            left, right = self.pair()
            right[key] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                audit.analyze_pair(left, right)

    def test_wrong_right_repeat_rejected_even_when_flag_claims_exact(self):
        left, right = self.pair()
        right["runs"]["right_repeat"][2]["camera"] += 1
        with self.assertRaisesRegex(ValueError, "RIGHT and RIGHT-repeat"):
            audit.analyze_pair(left, right)

    def test_idle_world_x_must_remain_stationary(self):
        left, right = self.pair()
        left["runs"]["idle"][0]["world_x"] = 62
        with self.assertRaisesRegex(ValueError, "RAM/camera"):
            audit.analyze_pair(left, right)

    def test_right_world_y_transition_must_match_numeric_baseline(self):
        left, right = self.pair()
        right["runs"]["right"][3]["world_y"] = 176
        right["runs"]["right_repeat"][3]["world_y"] = 176
        with self.assertRaisesRegex(ValueError, "RAM/camera"):
            audit.analyze_pair(left, right)

    def test_right_roi_must_be_empty_during_idle(self):
        left, right = self.pair()
        right["runs"]["idle"][0]["color_blue_pixel_count"] = 100
        right["runs"]["idle"][0]["connected_color_groups"] = 5
        right["runs"]["idle"][0]["largest_color_group"] = fake_component("right", 0)
        with self.assertRaisesRegex(ValueError, "right ROI does not show"):
            audit.analyze_pair(left, right)

    def test_left_roi_must_be_empty_during_right(self):
        left, right = self.pair()
        left["runs"]["right"][0]["color_blue_pixel_count"] = 100
        left["runs"]["right"][0]["connected_color_groups"] = 5
        left["runs"]["right"][0]["largest_color_group"] = fake_component("left", 0)
        left["runs"]["right_repeat"][0] = copy.deepcopy(left["runs"]["right"][0])
        with self.assertRaisesRegex(ValueError, "left ROI does not show"):
            audit.analyze_pair(left, right)

    def test_component_must_be_inside_roi(self):
        left, right = self.pair()
        right["runs"]["right"][0]["largest_color_group"][
            "blue_bbox_xyxy_exclusive"] = [122, 123, 149, 139]
        right["runs"]["right_repeat"][0] = copy.deepcopy(right["runs"]["right"][0])
        with self.assertRaisesRegex(ValueError, "outside declared ROI"):
            audit.analyze_pair(left, right)

    def test_edge_clipped_component_rejected(self):
        left, right = self.pair()
        right["runs"]["right"][0]["largest_color_group"]["touches_roi_boundary"] = True
        right["runs"]["right_repeat"][0] = copy.deepcopy(right["runs"]["right"][0])
        with self.assertRaisesRegex(ValueError, "edge-clipped"):
            audit.analyze_pair(left, right)

    def test_ram_camera_and_render_buffer_must_agree_across_rois(self):
        left, right = self.pair()
        right["runs"]["right"][1]["camera"] += 1
        right["runs"]["right_repeat"][1]["camera"] += 1
        with self.assertRaisesRegex(ValueError, "differs between ROIs"):
            audit.analyze_pair(left, right)

    def test_extra_raw_pixel_or_memory_field_rejected(self):
        left, right = self.pair()
        right["runs"]["right"][0]["frame_pixels"] = [0, 1, 2]
        with self.assertRaises(ValueError):
            audit.analyze_pair(left, right)

    def test_incorrect_empty_roi_counts_rejected(self):
        left, right = self.pair()
        right["runs"]["idle"][0]["connected_color_groups"] = 0
        right["runs"]["idle"][0]["largest_color_group"] = fake_component("right", 0)
        with self.assertRaisesRegex(ValueError, "empty ROI claims"):
            audit.analyze_pair(left, right)

    def test_right_repeat_top_level_flag_cannot_be_false(self):
        left, right = self.pair()
        right["same_right_replay_exact"] = False
        with self.assertRaises(ValueError):
            audit.analyze_pair(left, right)


if __name__ == "__main__":
    unittest.main()
