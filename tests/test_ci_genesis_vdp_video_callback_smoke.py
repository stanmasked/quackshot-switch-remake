"""Stage202: unit contracts for ROM-free real-core video/VDP smoke.

Every ROM-shaped buffer here is an original-free artificial 68000 loop.
Actual instrumented-core execution occurs only in the GitHub smoke step.
"""
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_genesis_vdp_video_callback_smoke as smoke


def fake_source():
    data = bytearray(128 * 1024)
    data[0x100:0x110] = b"SEGA MEGA DRIVE "
    data[0x120:0x150] = smoke.EXPECTED_SYNTHETIC_HEADER.ljust(48, b" ")
    data[0x200:0x202] = bytes.fromhex("60fe")
    return bytes(data)


class RealCoreSyntheticSmokeTests(unittest.TestCase):
    def test_accepts_only_ci_authored_loop_fixture(self):
        self.assertIsNone(smoke.require_synthetic_rom(fake_source()))

    def test_refuses_all_header_only_and_wrong_code_cases(self):
        for mutation in ("size", "header", "code", "identifier"):
            data = bytearray(fake_source())
            if mutation == "size":
                data.pop()
            elif mutation == "header":
                data[0x100:0x104] = b"XXXX"
            elif mutation == "code":
                data[0x200:0x202] = b"\x4e\x75"
            else:
                data[0x120:0x124] = b"GAME"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                smoke.require_synthetic_rom(bytes(data))

    def test_blocks_arbitrary_game_payload(self):
        data = bytearray(128 * 1024)
        data[0x100:0x104] = b"SEGA"
        with self.assertRaisesRegex(ValueError, "synthetic endless loop"):
            smoke.require_synthetic_rom(bytes(data))

    def test_numeric_hook_extracts_only_four_hscroll_lines_per_plane(self):
        calls = []
        def hook(line, values, count):
            calls.append((line, count))
            if count != 6:
                return 0
            for i, value in enumerate((0, 52, 0xD000, 0, 0x07FF, 0x07FE)):
                values[i] = value
            return 1
        self.assertEqual(smoke.numeric_scroll(hook), (-1,)*4 + (-2,)*4)
        self.assertEqual(calls, [(0, 6), (64, 6), (128, 6), (192, 6)])

    def test_repeatability_requires_two_identical_numeric_runs(self):
        sample = {"video": [(0, (0,)*8)], "after": [(0, (0,)*8)]}
        with mock.patch.object(smoke, "bind_vdp_probe", return_value="fake"), \
             mock.patch.object(smoke, "run_once", side_effect=[sample, sample]) as run:
            result = smoke.verify_repeatability(None, {}, fake_source(),
                                                 "fake", Path("/fake"))
        self.assertEqual(run.call_count, 2)
        self.assertEqual(result["independent_synthetic_runs"], 2)
        self.assertTrue(result["numerical_video_and_vdp_repeatability"])
        self.assertFalse(result["original_cpu_vdp_scanout_phase_verified"])
        self.assertFalse(result["original_game_hitbox_verified"])

    def test_mismatched_repeated_hscroll_is_rejected(self):
        first = {"video": [(0, (0,)*8)]}
        second = {"video": [(0, (1,)*8)]}
        with mock.patch.object(smoke, "bind_vdp_probe", return_value="fake"), \
             mock.patch.object(smoke, "run_once", side_effect=[first, second]):
            with self.assertRaisesRegex(ValueError, "disagree"):
                smoke.verify_repeatability(None, {}, fake_source(),
                                           "fake", Path("/fake"))


if __name__ == "__main__":
    unittest.main()
