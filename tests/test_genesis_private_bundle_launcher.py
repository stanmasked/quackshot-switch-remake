"""Stage204: fail-closed, local-only original-game research launcher.

The unit tests use an invented core byte sequence and never load a game ROM.
The actual positive full-SHA preflight runs separately on a real, ROM-free,
instrumented Genesis Plus GX binary built in GitHub Actions.
"""
import copy
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_genesis_private_bundle as bundle
import genesis_private_bundle_launcher as launcher


class PrivateBundleLauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="qs-stage204-")
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        self.bundle_dir = self.home / "bundle"
        self.bundle_dir.mkdir()
        (self.bundle_dir / bundle.CORE_NAME).write_bytes(
            b"SYNTHETIC_CORE_BINARY_NOT_GENESIS")
        self.synthetic_manifest = bundle.build(ROOT, self.bundle_dir)

    def fake_trusted_manifest(self):
        data = copy.deepcopy(self.synthetic_manifest)
        data.update({
            "status": launcher.HISTORIC_CORE_STATUS,
            "packaged_core_matches_historical_binary": True,
            "packaged_core_sha256": launcher.INSTRUMENTED_CORE_SHA,
            "historical_instrumented_core_sha256": launcher.INSTRUMENTED_CORE_SHA,
        })
        return data

    def test_invalid_core_preflight_rejected_without_touching_original_media(self):
        with mock.patch.object(launcher, "PinnedOriginalInputs") as guard:
            with self.assertRaisesRegex(ValueError, "not the historical"):
                launcher.preflight(self.bundle_dir)
            guard.assert_not_called()

    def test_preflight_never_needs_original_game_rom(self):
        manifest = self.fake_trusted_manifest()
        (self.bundle_dir / bundle.MANIFEST).write_text("{}")
        with mock.patch.object(launcher, "verify", return_value=manifest), \
             mock.patch.object(launcher, "validate", return_value={
                 "rom_sha256": launcher.ROM_SHA}), \
             mock.patch.object(launcher, "read_inputs", return_value=({}, "")), \
             mock.patch.object(launcher, "validate_instrumented_vdp_archive",
                               return_value=None), \
             mock.patch.object(launcher, "safe_rom") as rom_reader, \
             mock.patch.object(launcher, "setup_core") as core_reader:
            actual=launcher.preflight(self.bundle_dir)
        self.assertTrue(actual["bundle_sha_verified"])
        self.assertTrue(actual["historical_binary_match"])
        self.assertFalse(actual["original_game_executed_by_preflight"])
        self.assertFalse(actual["safe_to_modify_switch_physics"])
        rom_reader.assert_not_called()
        core_reader.assert_not_called()

    def test_forged_historic_match_or_reference_metadata_is_rejected(self):
        baseline = self.fake_trusted_manifest()
        for key, value in (
            ("packaged_core_matches_historical_binary", False),
            ("packaged_core_sha256", "0" * 64),
            ("source_rom_sha256_reference", "0" * 64),
            ("safe_to_modify_switch_physics", True),
            ("original_game_media_in_bundle", True),
        ):
            modified = dict(baseline, **{key: value})
            with self.subTest(field=key), \
                 mock.patch.object(launcher, "verify", return_value=modified):
                with self.assertRaisesRegex(ValueError, "not the historical"):
                    launcher.preflight(self.bundle_dir)

    def test_preflight_does_not_accept_rom_output_cli_arguments(self):
        cmd = [sys.executable, str(ROOT / "tools/genesis_private_bundle_launcher.py"),
               "--preflight", "--bundle", str(self.bundle_dir)]
        attempt = subprocess.run(
            cmd + ["--rom", str(self.home/"missing.gen")],
            capture_output=True, text=True, timeout=20)
        self.assertNotEqual(attempt.returncode, 0)
        self.assertEqual(attempt.stdout, "")
        self.assertIn("REJECTED", attempt.stderr)
        self.assertNotIn(str(self.home), attempt.stderr)

    def test_rom_does_not_load_when_bundle_integrity_fails(self):
        source = self.home / "original-private.gen"
        source.write_bytes(b"FAKE_NOT_AN_ORIGINAL_GAME")
        out = self.home / "original-numbers.json"
        with mock.patch.object(launcher, "safe_rom") as read_rom, \
             mock.patch.object(launcher, "setup_core") as load_core:
            with self.assertRaisesRegex(ValueError, "not the historical"):
                launcher.run_private(self.bundle_dir, source, out,
                                     (100, 80, 85, 140), "bracket")
        read_rom.assert_not_called()
        load_core.assert_not_called()
        self.assertFalse(out.exists())

    def test_rom_must_remain_outside_bundle_and_output_fresh(self):
        guard=mock.Mock()
        outside=self.home / "source.gen"
        outside.write_bytes(b"FAKE_NOT_ORIGINAL")
        (self.bundle_dir/"in-bundle.gen").write_bytes(b"FAKE")
        trusted=mock.patch.object(launcher, "preflight", return_value={})
        with trusted, mock.patch.object(launcher, "PinnedOriginalInputs",
                                        return_value=guard):
            with self.assertRaisesRegex(ValueError, "outside"):
                launcher.prepare_run(self.bundle_dir,
                                     self.bundle_dir/"in-bundle.gen",
                                     self.home/"out.json",
                                     (100,80,85,140), "bracket")
            prepped=launcher.prepare_run(
                self.bundle_dir,outside,self.home/"fresh.json",
                (100,80,85,140),"triplet")
            self.assertIs(prepped[0],guard)
            self.assertEqual(prepped[1],self.home/"fresh.json")
            existing=self.home/"occupied.json"
            existing.write_text("{}")
            with self.assertRaises(ValueError):
                launcher.prepare_run(self.bundle_dir,outside,existing,
                                     (100,80,85,140),"bracket")
        self.assertEqual(guard.call_count,1)

    def test_unknown_modes_and_invalid_roi_refused_before_core(self):
        source=self.home/"original-private.gen"
        source.write_bytes(b"FAKE")
        out=self.home/"number.json"
        with mock.patch.object(launcher,"preflight",return_value={}), \
             mock.patch.object(launcher,"setup_core") as core:
            with self.assertRaisesRegex(ValueError, "mode"):
                launcher.prepare_run(self.bundle_dir,source,out,
                                     (100,80,85,140),"unreviewed-mode")
            with self.assertRaises(ValueError):
                launcher.prepare_run(self.bundle_dir,source,out,
                                     (100,80,-1,140),"bracket")
            core.assert_not_called()

    def test_private_numerical_run_produces_no_visible_original_numbers(self):
        source = self.home / "hidden-original.gen"
        source.write_bytes(b"FAKE")
        dest = self.home / "hidden-report.json"
        guard = mock.Mock()
        report = {
            "safe_to_modify_switch_physics": False,
            "original_rom_executed_locally": True,
            "reference_rom_sha256": launcher.ROM_SHA,
            "instrumented_core_sha256": launcher.INSTRUMENTED_CORE_SHA,
        }
        with mock.patch.object(launcher, "prepare_run",
                               return_value=(guard, dest)), \
             mock.patch.object(launcher, "setup_core",
                               return_value=("fakecore", {})), \
             mock.patch.object(launcher, "safe_rom",
                               return_value=(b"FAKE", launcher.ROM_SHA)), \
             mock.patch.object(launcher, "collect_bracket",
                               return_value=report) as collect, \
             mock.patch.object(launcher, "private_write") as writer:
            self.assertIsNone(launcher.run_private(
                self.bundle_dir, source, dest, (100,80,85,140),"bracket"))
        collect.assert_called_once()
        writer.assert_called_once_with(report,dest,self.bundle_dir)
        self.assertEqual(guard.call_count,2)

    def test_synthetic_binary_hash_remains_distinct_from_original_baseline(self):
        result = bundle.verify(self.bundle_dir)
        self.assertFalse(result["packaged_core_matches_historical_binary"])
        self.assertNotEqual(
            hashlib.sha256((self.bundle_dir / bundle.CORE_NAME).read_bytes()).hexdigest(),
            launcher.INSTRUMENTED_CORE_SHA)


if __name__ == "__main__":
    unittest.main()
