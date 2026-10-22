"""Stage207: ROM-free opt-in numeric blue-color candidates through secure CLI.

Only fabricated RAM/video/VDP reports and fake original ROM bytes are used.
"""
from pathlib import Path
import copy
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_private_bundle_launcher as launcher
from test_genesis_blue_roi_components_triplet import synthetic_triplet

ROI = (100, 80, 85, 140)


def report_with_components():
    result, _, _ = synthetic_triplet(enabled=True)
    result["original_rom_executed_locally"] = True
    return result


class SecurePrivateColorLauncherTests(unittest.TestCase):
    def test_whitelisted_original_numeric_component_report(self):
        report = report_with_components()
        self.assertIsNone(launcher._validate_component_report(report, ROI, True))
        self.assertEqual([item["frame"] for item in
                          report["visual_component_candidates"]],
                         list(launcher.MARKERS))
        self.assertFalse(report["safe_to_modify_switch_physics"])

    def test_opt_in_only_with_triplet_and_bool(self):
        for mode, flag in (
            ("bracket", True), ("triplet", 1), ("triplet", None),
            ("triplet", "true"), ("bracket", "yes")
        ):
            with self.subTest(mode=mode, flag=flag), \
                 self.assertRaises(ValueError):
                launcher._validate_component_option(mode, flag)
        self.assertIsNone(launcher._validate_component_option("triplet", True))
        self.assertIsNone(launcher._validate_component_option("triplet", False))

    def test_reject_wrong_frame_index_count_and_unverified_flag(self):
        for modification in ("missing", "duplicate", "wrong_blue", "identity",
                             "hitbox", "status"):
            report=report_with_components()
            if modification=="missing":
                report["visual_component_candidates"].pop()
            elif modification=="duplicate":
                report["visual_component_candidates"][1]["frame"]=2180
            elif modification=="wrong_blue":
                report["frames"][2]["blue_pixel_count"]=1
            elif modification=="identity":
                report["component_identity_unverified"]=False
            elif modification=="hitbox":
                report["original_collision_hitbox_or_contact_plane_verified"]=True
            else:
                report["safe_to_modify_switch_physics"]=True
            with self.subTest(modification=modification), \
                 self.assertRaises(ValueError):
                launcher._validate_component_report(report, ROI, True)

    def test_reject_raw_pixel_injection_into_nested_numeric_component(self):
        report=report_with_components()
        report["visual_component_candidates"][0]["components"][0][
            "raw_frame"] = b"FORBIDDEN SYNTHETIC BYTES"
        with self.assertRaises(ValueError):
            launcher._validate_component_report(report, ROI, True)

    def test_opted_out_reports_cannot_include_unrequested_components(self):
        report=report_with_components()
        with self.assertRaisesRegex(ValueError, "unexpected"):
            launcher._validate_component_report(report, ROI, False)
        del report["visual_component_candidates"]
        self.assertIsNone(launcher._validate_component_report(report, ROI, False))

    def test_secured_triplet_option_reaches_collector_and_stays_private(self):
        report=report_with_components()
        with tempfile.TemporaryDirectory(prefix="qs-stage207-") as tmp:
            output=Path(tmp)/"private-report.json"
            guard=mock.Mock()
            with mock.patch.object(
                launcher, "prepare_run", return_value=(guard, output)), \
                mock.patch.object(
                    launcher, "setup_core", return_value=("FAKE", {})), \
                mock.patch.object(
                    launcher, "safe_rom", return_value=(b"NOT_A_ROM", launcher.ROM_SHA)), \
                mock.patch.object(
                    launcher, "collect_triplet", return_value=report) as triplet, \
                mock.patch.object(launcher, "collect_bracket") as bracket, \
                mock.patch.object(launcher, "private_write") as writer:
                self.assertIsNone(launcher.run_private(
                    Path(tmp)/"bundle", Path(tmp)/"synthetic.gen",
                    output, ROI, "triplet", component_candidates=True))
            self.assertTrue(triplet.call_args.kwargs["component_candidates"])
            bracket.assert_not_called()
            writer.assert_called_once_with(
                report, output, Path(tmp)/"bundle")
            self.assertEqual(guard.call_count, 2)
            self.assertFalse(output.exists())

    def test_invalid_component_report_cannot_be_written(self):
        bad=report_with_components()
        bad["visual_component_candidates"][0]["components"][0]["raw_rom"] = b"FAKE"
        with tempfile.TemporaryDirectory(prefix="qs-stage207-") as tmp:
            dest=Path(tmp)/"should-not-exist.json"
            with mock.patch.object(
                launcher, "prepare_run", return_value=(lambda: None, dest)), \
                mock.patch.object(launcher, "setup_core", return_value=("FAKE",{})), \
                mock.patch.object(
                    launcher, "safe_rom", return_value=(b"FAKE", launcher.ROM_SHA)), \
                mock.patch.object(launcher, "collect_triplet", return_value=bad), \
                mock.patch.object(launcher, "private_write") as writer:
                with self.assertRaises(ValueError):
                    launcher.run_private(
                        Path(tmp)/"bundle", Path(tmp)/"artificial.gen",
                        dest, ROI, "triplet", component_candidates=True)
                writer.assert_not_called()

    def test_bracket_cannot_open_any_original_input_for_color_option(self):
        with mock.patch.object(launcher, "prepare_run") as prepare, \
             mock.patch.object(launcher, "safe_rom") as loader:
            with self.assertRaises(ValueError):
                launcher.run_private(
                    Path("/not-executed"), Path("/not-loaded.gen"),
                    Path("/never-written.json"), ROI,
                    "bracket", component_candidates=True)
            prepare.assert_not_called()
            loader.assert_not_called()

    def test_preflight_cannot_accept_component_flag_or_print_original_paths(self):
        script=ROOT/"tools"/"genesis_private_bundle_launcher.py"
        with tempfile.TemporaryDirectory(prefix="qs-stage207-") as tmp:
            proc=subprocess.run([
                sys.executable,str(script),"--preflight",
                "--bundle",tmp,"--component-candidates"
            ],capture_output=True,text=True,timeout=20)
            self.assertNotEqual(proc.returncode,0)
            self.assertEqual(proc.stdout,"")
            self.assertIn("REJECTED",proc.stderr)
            self.assertNotIn(tmp,proc.stderr)


if __name__ == "__main__":
    unittest.main()
