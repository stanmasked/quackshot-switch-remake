import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_input_causality as c


class CausalInputTests(unittest.TestCase):
    def test_parse_three_independent_variants(self):
        found = c.parse_variants("walk@10:30:RIGHT,jump@40:42:B,fire@50:51:A")
        self.assertEqual([name for name, _ in found], ["walk", "jump", "fire"])

    def test_duplicate_variant_refused(self):
        with self.assertRaisesRegex(ValueError, "unique"):
            c.parse_variants("right@10:20:RIGHT,right@30:40:LEFT")

    def test_injection_or_unbounded_label_refused(self):
        for bad in ("file/../../secret@1:2:A", "x" * 33 + "@1:2:B",
                    "_hidden@1:2:A", "s p a c e@1:2:A"):
            with self.subTest(bad=bad):
                with self.assertRaisesRegex(ValueError, "labels"):
                    c.parse_variants(bad)

    def test_multiple_windows_in_one_variant_refused(self):
        with self.assertRaises(ValueError):
            c.parse_variants("combo@1:4:A:5:8:B")

    def test_more_than_sixteen_variants_refused(self):
        values = ",".join(f"case{i}@1:2:A" for i in range(17))
        with self.assertRaisesRegex(ValueError, "16 variants"):
            c.parse_variants(values)

    def test_no_variants_refused(self):
        with self.assertRaisesRegex(ValueError, "variant"):
            c.parse_variants("")

    def test_identical_crc_series(self):
        witness = c.compare_checkpoints({0: "a", 60: "b"}, {0: "a", 60: "b"})
        self.assertEqual(witness["changed_count"], 0)
        self.assertIsNone(witness["first_changed_frame"])

    def test_crc_difference_reports_first_and_last(self):
        witness = c.compare_checkpoints(
            {0: "a", 60: "b", 120: "c"},
            {0: "a", 60: "x", 120: "y"})
        self.assertEqual(witness["changed_count"], 2)
        self.assertEqual(witness["first_changed_frame"], 60)
        self.assertEqual(witness["last_changed_frame"], 120)

    def test_mismatched_sample_frames_refused(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            c.compare_checkpoints({0: "A"}, {60: "A"})

    def test_summary_does_not_contain_rom_or_ram_contents(self):
        report = {"video_checkpoints": [{"frame": 0, "crc32": "AABBCCDD"}],
                  "frames_video_callbacks": 60, "input_nonzero_reads": 7}
        stats = c.summarize_run(
            report, {0: "11223344"}, {0: "AABBCCDD"}, {0: "11223344"})
        self.assertEqual(stats["actual_joypad_nonzero_reads"], 7)
        self.assertEqual(stats["video_crc_differences"]["changed_count"], 0)
        self.assertNotIn("raw_ram", str(stats))
        self.assertNotIn("private_path", str(stats))

    def test_observed_witness_is_bound_to_reference_and_has_no_rom_bytes(self):
        import json
        root = Path(__file__).resolve().parents[1]
        witness = json.loads((root / "research/observations/user_rom_input_causality_2026-10-17.json").read_text())
        profile = json.loads((root / "research/reference_profiles/user_rom_2026-10-17.json").read_text())
        self.assertEqual(witness["reference_rom_sha256"], profile["raw_sha256"])
        self.assertEqual(witness["original_rom_bytes"], profile["rom_size_bytes"])
        outcomes = {v["test"]: v for v in witness["observations"]}
        self.assertEqual(outcomes["START_at_3200"]["first_changed_video_frame"], 3202)
        for button in ("RIGHT_at_1300", "B_at_1300", "A_at_1300", "LEFT_at_3200"):
            self.assertEqual(outcomes[button]["changed_video_frames"], 0)
            self.assertEqual(outcomes[button]["changed_ram_snapshots"], 0)
        sweep = witness["additional_right_window_scan"]
        self.assertEqual(len(sweep["tested_first_frames"]), 15)
        self.assertEqual(sweep["changed_video_checkpoints_per_run"], [0] * 15)
        self.assertNotIn("rom_data", witness)
        self.assertNotIn("ram_bytes", witness)

    def test_invalid_stride_rejected_before_core_execution(self):
        for stride in (0, 601):
            with self.subTest(stride=stride):
                with self.assertRaisesRegex(ValueError, "stride"):
                    c.study(None, {"library_version": "mock"}, Path("none.gen"),
                            b"", "hash", 60, "", [("right", "1:2:RIGHT")], stride)

    def test_variant_horizon_refused_after_baseline_validation(self):
        # Both checks run before actually touching the libretro library.
        with self.assertRaisesRegex(ValueError, "Baseline extends"):
            c.study(None, {}, Path("none.gen"), b"", "hash", 60,
                    "59:61:START", [("right", "1:2:RIGHT")], 60)


if __name__ == "__main__":
    unittest.main()
