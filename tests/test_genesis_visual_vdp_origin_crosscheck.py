"""Stage199 ROM-free VDP/blue-centroid numeric crosscheck from published data."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from genesis_visual_vdp_origin_crosscheck import analyze, VDP_PATH
from reference_evidence_integrity import read_inputs


class VisualVDPCrosscheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.obs, cls.source = read_inputs(ROOT)
        cls.vdp = json.loads((ROOT / VDP_PATH).read_text(encoding="utf-8"))

    def test_two_historical_centroids_do_not_share_an_exact_offset(self):
        result = analyze(self.obs, self.source, self.vdp)
        rows = result["historical_two_point_crosscheck"]
        self.assertEqual([r["frame"] for r in rows], [2220, 2400])
        self.assertEqual([r["screen_x_candidate_px"] for r in rows],
                         [143, 140])
        self.assertEqual([r["historical_blue_centroid_x_milli"] for r in rows],
                         [134720, 134164])
        self.assertEqual([r["blue_minus_candidate_milli"] for r in rows],
                         [-8280, -5836])
        self.assertEqual(result["blue_centroid_delta_milli"], -556)
        self.assertEqual(result["vdp_based_screen_x_candidate_delta_px"], -3)
        self.assertEqual(result["visual_vs_screen_origin_offset_difference_milli"],
                         2444)
        self.assertFalse(result[
            "one_exact_constant_offset_fits_both_historical_color_means"])

    def test_no_false_original_hitbox_or_rom_execution_claim(self):
        report = analyze(self.obs, self.source, self.vdp)
        for key in (
            "rom_executed_by_this_audit",
            "original_pixels_or_raw_ram_in_report",
            "blue_pixel_identity_or_foot_contact_verified",
            "original_collision_box_or_anchor_verified",
            "same_cpu_vdp_raster_phase_independently_proven",
            "safe_to_modify_switch_physics",
        ):
            self.assertIs(report[key], False)
        self.assertEqual(report["status"],
                         "HISTORICAL_COLOR_CENTROIDS_ARE_NOT_VERIFIED_COLLISION_ORIGINS")
        self.assertNotIn("original_sprite_pixels", str(report))

    def test_changed_blue_centroid_is_rejected(self):
        wrong = copy.deepcopy(self.obs)
        wrong["obstacles"]["camera_heuristic"][
            "screen_palette_blue_centroid_x_at_2220"] = 134.71
        with self.assertRaises(ValueError):
            analyze(wrong, self.source, self.vdp)

    def test_injected_nonfinite_or_boolean_centroid_rejected(self):
        for value in (True, None, "134.72", float("nan"),
                      float("inf"), 134.72001):
            with self.subTest(value=value):
                wrong = copy.deepcopy(self.obs)
                wrong["obstacles"]["camera_heuristic"][
                    "screen_palette_blue_centroid_x_at_2220"] = value
                with self.assertRaises(ValueError):
                    analyze(wrong, self.source, self.vdp)

    def test_corrupt_hardware_scroll_or_world_x_rejected(self):
        for frame, key, value in (
            (2220, "plane_a", -154),
            (2220, "world_x", 299),
            (2400, "ram_hscroll_buffer", 357),
            (2400, "plane_b", -178),
        ):
            with self.subTest(frame=frame,key=key):
                changed = copy.deepcopy(self.vdp)
                next(item for item in changed["keyframes"]
                     if item["frame"] == frame)[key] = value
                with self.assertRaises(ValueError):
                    analyze(self.obs, self.source, changed)

    def test_malformed_provenance_replays_or_duplicate_frames_rejected(self):
        for field, value in (
            ("reference_rom_sha256", "0"*64),
            ("schema", "forged"),
            ("original_rom_in_public_repository", True),
        ):
            changed = copy.deepcopy(self.vdp)
            changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(self.obs, self.source, changed)
        changed = copy.deepcopy(self.vdp)
        changed["measurement"]["identical_right_replays"] = False
        with self.assertRaises(ValueError):
            analyze(self.obs, self.source, changed)
        changed = copy.deepcopy(self.vdp)
        changed["keyframes"].append(copy.deepcopy(changed["keyframes"][0]))
        with self.assertRaises(ValueError):
            analyze(self.obs, self.source, changed)

    def test_cli_is_rom_free_and_reports_numbers_without_media(self):
        with tempfile.TemporaryDirectory(prefix="qs-vdp199-") as d:
            output = Path(d) / "audit.json"
            p = subprocess.run([
                sys.executable,
                str(ROOT / "tools/genesis_visual_vdp_origin_crosscheck.py"),
                "--root", str(ROOT), "--out", str(output)
            ], capture_output=True, text=True, timeout=20, check=True)
            self.assertEqual(p.stdout, "")
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertFalse(report["rom_executed_by_this_audit"])
            self.assertFalse(report["safe_to_modify_switch_physics"])
            self.assertEqual(report["visual_vs_screen_origin_offset_difference_milli"],
                             2444)
            again = subprocess.run([
                sys.executable,
                str(ROOT / "tools/genesis_visual_vdp_origin_crosscheck.py"),
                "--root", str(ROOT), "--out", str(output)
            ], capture_output=True, text=True, timeout=20)
            self.assertNotEqual(again.returncode, 0)


if __name__ == "__main__":
    unittest.main()
