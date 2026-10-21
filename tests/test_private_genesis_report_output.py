"""Stage193: ROM-free validation and private atomic publishing regressions.

All witnesses are synthetic fixtures. A shape-valid fixture is NOT evidence
that an original ROM was executed. No game media or binary core is opened.
"""
import copy
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from private_genesis_report_output import (
    private_output_path, validate_numeric_report, write_private_numeric_report
)
from reference_evidence_integrity import read_inputs, validate
from test_city_fractional_witness_comparison import simulated_external_witness


class PrivateNumericOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        obs, source = read_inputs(ROOT)
        cls.ref = validate(obs, source)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qs-stage193-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.out = self.root / "evidence.json"
        self.fixture = simulated_external_witness(self.ref)

    def test_numeric_only_fixture_is_unattested_and_stable(self):
        text = validate_numeric_report(self.fixture, ROOT)
        self.assertEqual(json.loads(text), self.fixture)
        self.assertNotIn("ram_bytes", text)
        self.assertNotIn("rom_payload", text)
        self.assertNotIn(str(self.root), text)

    def test_private_output_is_exclusive_and_0600(self):
        result = write_private_numeric_report(self.fixture, self.out, ROOT)
        self.assertIsNone(result)
        self.assertEqual(json.loads(self.out.read_text()), self.fixture)
        self.assertEqual(stat.S_IMODE(self.out.stat().st_mode), 0o600)
        self.assertEqual(sorted(self.root.iterdir()), [self.out])
        with self.assertRaisesRegex(ValueError, "must not already exist"):
            write_private_numeric_report(self.fixture, self.out, ROOT)

    def test_wrong_phase_is_rejected_without_creating_a_file(self):
        forged = copy.deepcopy(self.fixture)
        forged["ram_checkpoint_phase"] = "before_retro_run"
        with self.assertRaisesRegex(ValueError, "phase contract"):
            write_private_numeric_report(forged, self.out, ROOT)
        self.assertFalse(self.out.exists())
        self.assertEqual(list(self.root.iterdir()), [])

    def test_invalid_core_provenance_rejected_before_write(self):
        forged = copy.deepcopy(self.fixture)
        forged["core_binary_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA256"):
            write_private_numeric_report(forged, self.out, ROOT)
        self.assertFalse(self.out.exists())

    def test_raw_ram_field_or_inconsistent_duration_fails_closed(self):
        for mutation in ("raw", "duration"):
            with self.subTest(mutation=mutation):
                forged = copy.deepcopy(self.fixture)
                if mutation == "raw":
                    forged["raw_ram"] = "SYNTHETIC_SHOULD_NOT_PASS"
                else:
                    forged["fall_duration_by_threshold_frames"][
                        "positive_subpixel_to_integer_floor"] = 22
                with self.assertRaises(ValueError):
                    write_private_numeric_report(forged, self.out, ROOT)
                self.assertFalse(self.out.exists())

    def test_public_source_tree_and_relative_paths_rejected(self):
        with self.assertRaisesRegex(ValueError, "absolute"):
            private_output_path(Path("evidence.json"), ROOT)
        with self.assertRaisesRegex(ValueError, "public repository"):
            private_output_path(ROOT / "research" / "evidence.json", ROOT)
        with self.assertRaisesRegex(ValueError, ".json"):
            private_output_path(self.root / "evidence.log", ROOT)

    def test_symlink_output_and_existing_file_are_not_overwritten(self):
        protected = self.root / "protected.txt"
        protected.write_text("PROTECTED", encoding="utf-8")
        try:
            self.out.symlink_to(protected)
        except (OSError, NotImplementedError):
            self.skipTest("no symlinks")
        with self.assertRaisesRegex(ValueError, "must not already exist"):
            write_private_numeric_report(self.fixture, self.out, ROOT)
        self.assertEqual(protected.read_text(), "PROTECTED")

    def test_directory_symlink_rejected(self):
        linked = self.root / "linked"
        try:
            linked.symlink_to(self.root, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("no directory symlinks")
        with self.assertRaisesRegex(ValueError, "symlink"):
            private_output_path(linked / "private.json", ROOT)

    def test_same_filename_in_private_directory_does_not_leak_into_errors(self):
        self.out.write_text("old report")
        with self.assertRaises(ValueError) as raised:
            write_private_numeric_report(self.fixture, self.out, ROOT)
        self.assertNotIn(str(self.out), str(raised.exception))
        self.assertEqual(self.out.read_text(), "old report")

    def test_report_not_written_if_provenance_claim_forged(self):
        forged = copy.deepcopy(self.fixture)
        forged["original_rom_executed_locally"] = False
        with self.assertRaisesRegex(ValueError, "unverified"):
            write_private_numeric_report(forged, self.out, ROOT)
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
