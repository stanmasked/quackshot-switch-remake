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

    def test_video_stride_validation_is_strict(self):
        for stride in (0, 601, -1):
            with self.subTest(stride=stride):
                with self.assertRaisesRegex(ValueError, "video_stride"):
                    probe.run_core(None, {"need_fullpath": False},
                                   b"", "mock", 2, [], video_stride=stride)


    def test_frame_digest_hashes_visible_rows_separately_from_padding(self):
        payload = b"abcdPAD1efghPAD2"
        result = probe.frame_digest_numeric(payload, 2, 2, 8, 2)
        self.assertEqual(result["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(result["visible_sha256"], hashlib.sha256(b"abcdefgh").hexdigest())
        self.assertEqual(result["byte_count"], 16)
        self.assertEqual(result["visible_byte_count"], 8)

    def test_frame_digest_rejects_bad_geometry(self):
        for args in ((b"abc", 2, 2, 4, 2), (b"abcdefgh", 2, 2, 3, 2),
                     (b"abcd", 2, 1, 4, 7)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                probe.frame_digest_numeric(*args)

    def test_parse_frame_indices_is_strict_and_sorted(self):
        self.assertEqual(probe.parse_frame_indices("3,1,2", 4), [1, 2, 3])
        self.assertEqual(probe.parse_frame_indices("", 4), [])
        for spec in ("1,1", "-1,2", "1,4", "x,2", " 1,2"):
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                probe.parse_frame_indices(spec, 4)

    def test_run_core_rejects_duplicate_or_out_of_range_digest_frames(self):
        for frames in ([0, 0], [2], [-1]):
            with self.subTest(frames=frames), self.assertRaisesRegex(ValueError, "video_digest_frames"):
                probe.run_core(None, {"need_fullpath": False}, b"", "mock", 2, [],
                               video_digest_frames=frames)


    def test_parse_cpu_trace_window_is_inclusive_and_strict(self):
        self.assertEqual(probe.parse_cpu_trace_window("3:5", 10), (3, 5))
        self.assertIsNone(probe.parse_cpu_trace_window("", 10))
        for spec in ("-1:2", "2:1", "0:10", "0:128", "1:2:3", " 1:2", "x:2"):
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                probe.parse_cpu_trace_window(spec, 10 if spec != "0:128" else 129)

    def test_run_core_rejects_invalid_cpu_trace_windows_before_core_access(self):
        for window in ((0, 2), (2, 1), (-1, 0), (0, 128), [0, "1"]):
            with self.subTest(window=window), self.assertRaisesRegex(ValueError, "cpu_write_trace_window"):
                probe.run_core(None, {"need_fullpath": False}, b"", "mock", 2, [],
                               cpu_write_trace_window=window)

    def test_run_core_rejects_invalid_control_flow_windows_before_core_access(self):
        for window in ((0, 2), (2, 1), (-1, 0), (0, 128), [0, "1"]):
            with self.subTest(window=window), self.assertRaisesRegex(ValueError, "cpu_control_flow_trace_window"):
                probe.run_core(None, {"need_fullpath": False}, b"", "mock", 2, [],
                               cpu_control_flow_trace_window=window)

    def test_run_core_rejects_mismatched_cpu_trace_windows_before_core_access(self):
        with self.assertRaisesRegex(ValueError, "windows must match"):
            probe.run_core(None, {"need_fullpath": False}, b"", "mock", 4, [],
                           cpu_write_trace_window=(0, 1),
                           cpu_control_flow_trace_window=(2, 3))

    def test_published_genesis_witness_matches_pinned_rom(self):
        import json
        root = Path(__file__).resolve().parents[1]
        witness = json.loads((root / "research/observations/user_rom_libretro_start_witness_2026-10-17.json").read_text())
        profile = json.loads((root / "research/reference_profiles/user_rom_2026-10-17.json").read_text())
        self.assertEqual(witness["reference_rom_sha256"], profile["raw_sha256"])
        self.assertEqual(witness["reference_rom_bytes"], profile["rom_size_bytes"])
        exact = witness["frame_exact_comparison"]
        self.assertEqual(exact["frames_compared"], 600)
        self.assertEqual(exact["first_differing_frame"], 450)
        self.assertEqual(exact["frame_before_action_matching"], 449)
        self.assertEqual(exact["observed_libretro_nonzero_start_reads"], 15)
        self.assertEqual(exact["video_stride"], 1)

    def test_callback_signature_declared(self):
        self.assertTrue(callable(probe.VideoCb))
        self.assertTrue(callable(probe.InputStateCb))
        self.assertTrue(callable(probe.EnvCb))


if __name__ == "__main__":
    unittest.main()
