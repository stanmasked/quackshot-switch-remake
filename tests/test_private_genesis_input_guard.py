"""Stage192: ROM-free tests of pinned private input integrity.

Uses artificial files and a mocked emulator; no original game payloads.
"""
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_city_fractional_fall_probe as fall
from private_genesis_input_guard import PinnedOriginalInputs


def sha(data):
    return hashlib.sha256(data).hexdigest()


class PinnedInputsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.rom = root / "synthetic-private-rom.bin"
        self.core = root / "synthetic-core.so"
        self.rom_bytes = b"NOT_A_GAME_ROM_TEST_ONLY" * 37
        self.core_bytes = b"NOT_AN_EMULATOR_TEST_ONLY" * 19
        self.rom.write_bytes(self.rom_bytes)
        self.core.write_bytes(self.core_bytes)

    def guard(self, expected_rom=None, expected_core=None):
        return PinnedOriginalInputs(
            self.rom, self.core,
            expected_rom or sha(self.rom_bytes),
            expected_core or sha(self.core_bytes))

    def test_stable_files_pass_repeated_checks(self):
        guard = self.guard()
        self.assertIsNone(guard())
        self.assertIsNone(guard())
        self.assertIsNone(guard())

    def test_wrong_original_rom_or_core_pin_is_rejected(self):
        for part in ("rom", "core"):
            with self.subTest(part=part):
                guard = (self.guard(expected_rom="0" * 64)
                         if part == "rom" else
                         self.guard(expected_core="0" * 64))
                with self.assertRaisesRegex(ValueError, "SHA256 differs"):
                    guard()

    def test_same_size_in_place_tamper_rejected(self):
        guard = self.guard()
        guard()
        self.rom.write_bytes(b"X" + self.rom_bytes[1:])
        self.assertEqual(self.rom.stat().st_size, len(self.rom_bytes))
        with self.assertRaisesRegex(ValueError, "SHA256 differs"):
            guard()

    def test_same_content_file_replacement_rejected(self):
        guard = self.guard()
        guard()
        swapped = self.core.with_suffix(".replacement")
        swapped.write_bytes(self.core_bytes)
        os.replace(swapped, self.core)
        with self.assertRaisesRegex(ValueError, "identity/content changed"):
            guard()

    def test_symlink_is_not_trusted(self):
        link = self.core.with_suffix(".link")
        try:
            link.symlink_to(self.core)
        except (NotImplementedError, OSError):
            self.skipTest("symlinks unavailable")
        bad = PinnedOriginalInputs(self.rom, link,
                                   sha(self.rom_bytes), sha(self.core_bytes))
        with self.assertRaisesRegex(ValueError, "not a regular local file"):
            bad()

    def test_invalid_expected_hash_rejected(self):
        with self.assertRaisesRegex(ValueError, "invalid expected SHA256"):
            self.guard(expected_core="not-a-hash")

    def test_private_file_names_not_in_error(self):
        guard = self.guard(expected_rom="0" * 64)
        with self.assertRaises(ValueError) as raised:
            guard()
        self.assertNotIn(self.rom.name, str(raised.exception))
        self.assertNotIn(str(self.rom.parent), str(raised.exception))


class ThreeRunBoundaryTests(unittest.TestCase):
    def good_result(self, name):
        return {
            "frames_video_callbacks": fall.TOTAL,
            "ram_checkpoint_phase": "post_retro_run_before_next_input",
            "video_callback_phase": "during_retro_run",
            "frames_without_video_callback": 0,
            "frames_with_multiple_video_callbacks": 0,
            "video_callbacks_outside_retro_run": 0,
            "video_checkpoints": [{"frame": 60, "crc32": "12345678"}],
            "input_nonzero_reads": 0 if name == "idle" else 450,
        }

    def test_six_run_boundaries_are_guarded(self):
        events = []
        names = iter(("idle", "right", "right_repeat"))

        def guarded():
            events.append("check")

        def fake_run(*args, **kwargs):
            name = next(names)
            events.append(name)
            return self.good_result(name)

        with mock.patch.object(fall, "run_core", side_effect=fake_run), \
             mock.patch.object(fall, "analyze", return_value={}):
            report = fall.measure(
                None, {"library_name": "Fake", "library_version": "0"},
                Path("/not-a-real-rom"), b"not-a-ROM",
                fall.EXPECTED_ROM_SHA256, verify_inputs=guarded)
        self.assertEqual(events, [
            "check", "idle", "check", "check", "right", "check",
            "check", "right_repeat", "check"])
        self.assertEqual(report["independent_original_runs"], 3)
        # This checks mocked orchestration, not execution of a real ROM.
        self.assertEqual(report["status"],
                         "PRIVATE_ORIGINAL_EMULATION_NUMERIC_ONLY")

    def test_guard_required_even_in_library_calls(self):
        with self.assertRaisesRegex(ValueError, "guard must be callable"):
            fall.measure(None, {}, Path("/nonexistent"), b"fake",
                         fall.EXPECTED_ROM_SHA256, verify_inputs=None)

    def test_after_run_guard_also_runs_if_core_raises(self):
        checks = []
        def fail_run(*args, **kwargs):
            raise RuntimeError("synthetic emulator failure")
        with mock.patch.object(fall, "run_core", side_effect=fail_run):
            with self.assertRaisesRegex(RuntimeError, "synthetic emulator"):
                fall.measure(None, {}, Path("/nonexistent"), b"fake",
                             fall.EXPECTED_ROM_SHA256,
                             verify_inputs=lambda: checks.append("check"))
        self.assertEqual(checks, ["check", "check"])

    def test_mutation_mid_run_prevents_private_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            rom = Path(tmp) / "fake.bin"
            core = Path(tmp) / "fake.so"
            rom.write_bytes(b"SYNTHETIC_ROM")
            core.write_bytes(b"SYNTHETIC_CORE")
            guard = PinnedOriginalInputs(
                rom, core, sha(rom.read_bytes()), sha(core.read_bytes()))
            guard()

            def tamper(*args, **kwargs):
                core.write_bytes(b"SYNTHETIC_CORX")
                return self.good_result("idle")

            with mock.patch.object(fall, "run_core", side_effect=tamper):
                with self.assertRaisesRegex(ValueError, "SHA256 differs"):
                    fall.measure(None, {}, rom, b"SYNTHETIC_ROM",
                                 fall.EXPECTED_ROM_SHA256,
                                 verify_inputs=guard)


if __name__ == "__main__":
    unittest.main()
