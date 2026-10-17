import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_libretro_probe as probe


class GenesisFrontendTests(unittest.TestCase):
    def test_disjoint_input_ranges(self):
        r = probe.parse_script("2:5:RIGHT,4:7:B,9:11:START")
        self.assertEqual(probe.buttons_at(0, r), [])
        self.assertEqual(probe.buttons_at(3, r), ["RIGHT"])
        self.assertEqual(probe.buttons_at(4, r), ["B", "RIGHT"])
        self.assertEqual(probe.buttons_at(5, r), ["B"])
        self.assertEqual(probe.buttons_at(9, r), ["START"])

    def test_merges_overlap_without_duplicate_button(self):
        self.assertEqual(probe.buttons_at(3, probe.parse_script(
            "1:5:A,2:4:A")), ["A"])

    def test_rejects_invalid_ranges(self):
        for spec in ("3:2:B", "-1:2:UP", "0:10001:LEFT",
                     "0:2:Z", "0:2", "hello", "3:3:RIGHT"):
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                probe.parse_script(spec)

    def test_empty_script(self):
        self.assertEqual(probe.parse_script(""), [])
        self.assertEqual(probe.buttons_at(100, []), [])

    def test_button_ids_are_joypad_standard(self):
        self.assertEqual(probe.BUTTONS["START"], 3)
        self.assertEqual(probe.BUTTONS["RIGHT"], 7)
        self.assertEqual(probe.BUTTONS["B"], 0)
        self.assertEqual(probe.BUTTONS["A"], 8)

    def test_hash_mismatch_protects_reference(self):
        data = bytearray(0x400)
        data[0x100:0x104] = b"SEGA"
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "synthetic.gen"
            p.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                probe.safe_rom(p, "0" * 64)
            rom, checksum = probe.safe_rom(p, hashlib.sha256(data).hexdigest())
            self.assertEqual(rom, data)
            self.assertEqual(len(checksum), 64)

    def test_invalid_header_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "synthetic.gen"
            p.write_bytes(bytes(0x400))
            with self.assertRaisesRegex(ValueError, "Sega Mega Drive"):
                probe.safe_rom(p, None)

    def test_too_large_rom_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "synthetic.gen"
            with p.open("wb") as f:
                f.seek(probe.MAX_ROM_BYTES)
                f.write(b"!")
            with self.assertRaisesRegex(ValueError, "ROM size"):
                probe.safe_rom(p, None)

    def test_has_no_implicit_content_locations(self):
        self.assertNotIn("rom", probe.setup_core.__code__.co_consts)
        self.assertEqual(probe.RETRO_MEMORY_SYSTEM_RAM, 2)

    def test_idle_vs_input_video_divergence_is_numeric_only(self):
        idle = {"reference_rom_sha256": "test", "frames_requested": 120,
                "input_nonzero_reads": 0,
                "video_checkpoints": [
                    {"frame": 0, "crc32": "AAAAAAAA"},
                    {"frame": 60, "crc32": "BBBBBBBB"}]}
        scripted = {"reference_rom_sha256": "test", "frames_requested": 120,
                    "input_nonzero_reads": 10,
                    "video_checkpoints": [
                        {"frame": 0, "crc32": "AAAAAAAA"},
                        {"frame": 60, "crc32": "CCCCCCCC"}]}
        witness = probe.compare_idle_to_input(idle, scripted)
        self.assertEqual(witness["first_different_video_checkpoint"], 60)
        self.assertEqual(witness["different_video_checkpoint_count"], 1)
        self.assertEqual(witness["core_input_nonzero_reads_scripted"], 10)
        self.assertNotIn("image", witness)

    def test_comparison_rejects_other_rom(self):
        template = {"reference_rom_sha256": "test", "frames_requested": 120,
                    "input_nonzero_reads": 0, "video_checkpoints": []}
        other = dict(template)
        other["reference_rom_sha256"] = "different"
        with self.assertRaisesRegex(ValueError, "same ROM"):
            probe.compare_idle_to_input(template, other)

    def test_comparison_rejects_missing_frame(self):
        template = {"reference_rom_sha256": "test", "frames_requested": 120,
                    "input_nonzero_reads": 0,
                    "video_checkpoints": [{"frame": 0, "crc32": "FFFFFFFF"}]}
        other = dict(template)
        other["video_checkpoints"] = []
        with self.assertRaisesRegex(ValueError, "Missing checkpoint"):
            probe.compare_idle_to_input(template, other)

    def test_callback_signature_declared(self):
        self.assertTrue(callable(probe.VideoCb))
        self.assertTrue(callable(probe.InputStateCb))
        self.assertTrue(callable(probe.EnvCb))


if __name__ == "__main__":
    unittest.main()
