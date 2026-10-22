"""Stage208: synthetic-only offline numeric report receipt checks.

No genuine Genesis ROM, frame, save state, or replay required.
"""
import copy
import json
from pathlib import Path
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import genesis_offline_numeric_report_audit as audit
from genesis_city_video_vdp_bracket_probe import phase_bracket
from test_genesis_blue_roi_components_triplet import synthetic_triplet


def triplet(color=False):
    report, _, _ = synthetic_triplet(enabled=color)
    return report


def bracket():
    old = triplet()
    during = {frame:[-90]*4+[-45]*4 for frame in audit.MARKERS}
    after = copy.deepcopy(during)
    after[2220][0] = -92
    return {
        "schema":"quackshot_genesis_video_hscroll_same_step_bracket_v1",
        "original_rom_executed_locally":True,
        "reference_rom_sha256":audit.ROM_SHA,
        "instrumented_core_sha256":audit.INSTRUMENTED_CORE_SHA,
        "three_runs_repeated_exact":True,
        "triplet_sample_frames":old["frames"],
        "vdp_callback_vs_post_run":phase_bracket(during, after),
        "original_collision_plane_verified":False,
        "original_cpu_vdp_scanout_phase_verified":False,
        "original_media_or_raw_memory_exported":False,
        "safe_to_modify_switch_physics":False,
    }


class SyntheticNumericReportAuditTests(unittest.TestCase):
    def test_plain_triplet_accepted_without_attesting_original_run(self):
        result = audit.audit_report(triplet())
        self.assertEqual(result["frames_checked"], 5)
        self.assertFalse(result["local_provenance_independently_attested"])
        self.assertFalse(result["original_hitbox_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_color_triplet_accepted_without_recognizing_player(self):
        r = audit.audit_report(triplet(color=True))
        self.assertEqual(r["frames_checked"], 5)
        self.assertFalse(r["safe_to_modify_switch_physics"])

    def test_bracket_accepted_without_attesting_cpu_scanout_phase(self):
        r = audit.audit_report(bracket())
        self.assertEqual(r["schema"],
            "quackshot_genesis_video_hscroll_same_step_bracket_v1")
        self.assertFalse(r["local_provenance_independently_attested"])

    def test_wrong_pinned_original_metadata_refused(self):
        for field in ("reference_rom_sha256", "instrumented_core_sha256"):
            r = triplet(color=True)
            r[field] = "0" * 64
            with self.subTest(field=field), self.assertRaisesRegex(
                ValueError, "provenance label"):
                audit.audit_report(r)

    def test_raw_data_in_top_frame_or_component_rejected(self):
        for where in ("top", "frame", "component"):
            r = triplet(color=True)
            if where == "top":
                r["original_rom_bytes"] = "no"
            elif where == "frame":
                r["frames"][0]["framebuffer_hex"] = "no"
            else:
                r["visual_component_candidates"][0]["components"][0][
                    "source_rgb"] = "no"
            with self.subTest(where=where), self.assertRaises(ValueError):
                audit.audit_report(r)

    def test_original_geometry_and_scanout_facts_stay_unknown(self):
        for field in ("safe_to_modify_switch_physics",
                      "same_cpu_vdp_scanout_phase_verified",
                      "original_collision_hitbox_or_contact_plane_verified"):
            r=triplet()
            r[field]=True
            with self.subTest(field=field), self.assertRaises(ValueError):
                audit.audit_report(r)
        r=bracket()
        r["original_cpu_vdp_scanout_phase_verified"]=True
        with self.assertRaises(ValueError):
            audit.audit_report(r)

    def test_original_control_frames_and_deltas_are_exact(self):
        for field, value in (
            ("frame", 2220),
            ("vdp_screen_x_candidate", 20),
            ("blue_minus_vdp_candidate_x_milli", 777),
            ("player_world_x", 1),
            ("blue_pixel_count", True),
        ):
            r=triplet()
            r["frames"][0][field]=value
            with self.subTest(field=field), self.assertRaises(ValueError):
                audit.audit_report(r)

    def test_bracket_changed_status_and_delta_types_are_consistent(self):
        for altered in ("row_extra","frame", "delta_type",
                        "all_equal", "aggregate", "phase"):
            r=bracket()
            b=r["vdp_callback_vs_post_run"]
            if altered == "row_extra":
                b["checks"][0]["raw_vram"] = "untrusted"
            elif altered == "frame":
                b["checks"][0]["libretro_frame"] = 2220
            elif altered == "delta_type":
                b["checks"][1]["plane_a_deltas_by_scanline"][0] = True
            elif altered == "all_equal":
                b["checks"][1][
                    "all_plane_scroll_values_equal_at_two_observation_points"] = True
            elif altered == "aggregate":
                b["changed_hscroll_at_any_observation_boundary"] = False
            else:
                b["equal_values_prove_same_cpu_vdp_scanout_phase"] = True
            with self.subTest(altered=altered), self.assertRaises(ValueError):
                audit.audit_report(r)

    def test_private_read_requires_0600_and_absolute_json_outside_git(self):
        with tempfile.TemporaryDirectory(prefix="qs-stage208-") as td:
            home=Path(td)
            bundle=home/"bundle"
            bundle.mkdir()
            report=home/"safe.json"
            report.write_text(json.dumps(triplet()),encoding="utf-8")
            report.chmod(0o600)
            self.assertEqual(audit.read_private_report(bundle, report)["schema"],
                             triplet()["schema"])
            report.chmod(0o644)
            with self.assertRaisesRegex(ValueError, "permissions"):
                audit.read_private_report(bundle, report)
            report.chmod(0o600)
            with self.assertRaises(ValueError):
                audit.read_private_report(bundle, Path("relative.json"))
            git=home/"public_repo"
            git.mkdir()
            (git/".git").mkdir()
            inside=git/"private.json"
            inside.write_text("{}")
            inside.chmod(0o600)
            with self.assertRaisesRegex(ValueError, "git checkout"):
                audit.read_private_report(bundle, inside)

    def test_symlink_or_oversized_report_refused_without_reading_original(self):
        with tempfile.TemporaryDirectory(prefix="qs-stage208-") as td:
            home=Path(td)
            bundle=home/"bundle"
            bundle.mkdir()
            safe=home/"private.json"
            safe.write_text("{}")
            safe.chmod(0o600)
            linked=home/"linked.json"
            linked.symlink_to(safe)
            with self.assertRaises(ValueError):
                audit.read_private_report(bundle, linked)
            safe.write_bytes(b" " * (audit.MAX_JSON_BYTES+1))
            safe.chmod(0o600)
            with self.assertRaisesRegex(ValueError, "size"):
                audit.read_private_report(bundle, safe)

    def test_cli_rejection_prints_no_private_report_path(self):
        with tempfile.TemporaryDirectory(prefix="qs-stage208-") as td:
            home=Path(td)
            script=ROOT/"tools/genesis_offline_numeric_report_audit.py"
            private=home/"original-private.json"
            private.write_text("{}")
            p=subprocess.run([
                sys.executable,str(script),"--bundle",str(home),
                "--report",str(private)
            ],capture_output=True,text=True,timeout=20)
            self.assertNotEqual(p.returncode,0)
            self.assertEqual(p.stdout,"")
            self.assertEqual(p.stderr,
                             "GENESIS_STAGE208_NUMERIC_REPORT_REJECTED\n")
            self.assertNotIn(str(private),p.stderr)


if __name__=="__main__":
    unittest.main()
