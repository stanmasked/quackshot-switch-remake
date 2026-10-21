"""ROM-free Stage196 original-Y ambiguity proof, not a gameplay replay."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from city_y_anchor_identifiability import analyze
from reference_evidence_integrity import read_inputs


class CityAnchorIdentifiabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.obs, cls.source = read_inputs(ROOT)

    def test_three_distinct_origins_share_same_observed_delta(self):
        result = analyze(self.obs, self.source)
        self.assertEqual(result["status"],
                         "UNDERDETERMINED_BY_PUBLISHED_NUMERIC_Y_ONLY")
        rows = result["hypothetical_uniform_translations_not_original_measurements"]
        self.assertEqual([(r["translation_px"], r["shifted_upper_y"],
                           r["shifted_lower_y"]) for r in rows],
                         [(0, 176, 208), (-10, 166, 198), (-20, 156, 188)])
        self.assertEqual([r["shifted_delta_y"] for r in rows], [32] * 3)
        self.assertEqual(len({r["shifted_upper_y"] for r in rows}), 3)
        self.assertIsNone(result["original_anchor_selected"])
        self.assertTrue(all(not r["original_collision_anchor_verified"]
                            for r in rows))
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_coincidence_with_authored_top_does_not_establish_original_anchor(self):
        result = analyze(self.obs, self.source)
        row = result["hypothetical_uniform_translations_not_original_measurements"][-1]
        self.assertTrue(row["coincides_with_authored_top_pair"])
        self.assertFalse(row["original_collision_anchor_verified"])
        self.assertFalse(result["original_collision_box_measured"])
        self.assertEqual(result["published_original_numeric_y"]["physical_anchor"],
                         "UNKNOWN")

    def test_source_collision_dimensions_are_reviewed_before_reporting(self):
        changed = self.source.replace("hits_tiles(g, trial, h->y, 12, 20)",
                                      "hits_tiles(g, trial, h->y, 12, 22)")
        self.assertNotEqual(changed, self.source)
        with self.assertRaisesRegex(ValueError, "collision box"):
            analyze(self.obs, changed)
        changed = self.source.replace(
            "h->y = floorf((trial + 20) / QS_TILE) * QS_TILE - 20;",
            "h->y = floorf((trial + 24) / QS_TILE) * QS_TILE - 24;")
        self.assertNotEqual(changed, self.source)
        with self.assertRaisesRegex(ValueError, "landing rule"):
            analyze(self.obs, changed)

    def test_changed_reference_numbers_are_rejected(self):
        obs = copy.deepcopy(self.obs)
        obs["obstacles"]["ledge"]["world_y_upper_floor"] = 175
        with self.assertRaises(ValueError):
            analyze(obs, self.source)

    def test_report_never_claims_real_emulation_or_parity(self):
        report = analyze(self.obs, self.source)
        self.assertFalse(report["original_rom_was_run"])
        self.assertFalse(report["original_game_media_in_report"])
        self.assertFalse(report["physics_changed"])
        data = json.dumps(report)
        for forbidden in ("pixel_data", "raw_ram", "save_state",
                          "original_collision_box_verified",
                          "original_anchor_selected\": \"foot\""):
            self.assertNotIn(forbidden, data)

    def test_cli_writes_only_numeric_research_to_temporary_path(self):
        with tempfile.TemporaryDirectory(prefix="qs-anchor196-") as d:
            target = Path(d) / "numeric.json"
            completed = subprocess.run([
                sys.executable,
                str(ROOT / "tools/city_y_anchor_identifiability.py"),
                "--root", str(ROOT), "--out", str(target)
            ], capture_output=True, text=True, check=True, timeout=20)
            self.assertEqual(completed.stdout, "")
            report = json.loads(target.read_text(encoding="utf-8"))
            self.assertEqual(report["schema"],
                             "quackshot_city_original_y_anchor_identifiability_v1")
            self.assertIsNone(report["original_anchor_selected"])


if __name__ == "__main__":
    unittest.main()
