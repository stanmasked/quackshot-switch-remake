"""Synthetic BizHawk Genesis BK2 tests. No original TAS, ROM, or game assets."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from bk2_reference_audit import audit, BK2ValidationError, LOG_KEY, main as audit_main

def movie(path, frames=None, logkey=LOG_KEY, platform="GEN", members=None):
    if frames is None:
        frames = ["|..|............|............|", "|..|...R........|............|",
                  "|..|...RA..S....|............|", "|..|.......S....|............|",
                  "|..|............|............|"]
    header = (f"MovieVersion BizHawk v2.0.0\nPlatform {platform}\n"
              "GameName Example synthetic only\nSHA1 1234\n")
    contents = {"Header.txt": header,
                "SyncSettings.json": json.dumps({"o": {"Region": 0}}),
                "Input Log.txt": "\n".join(["[Input]", logkey, *frames, "[/Input]"]) + "\n"}
    if members:
        contents.update(members)
    with ZipFile(path, "w", compression=ZIP_DEFLATED) as z:
        for name, value in contents.items():
            z.writestr(name, value)

class BK2AuditTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "synth.bk2"

    def test_aggregate_and_exact_edges(self):
        movie(self.path)
        r = audit(self.path)
        self.assertEqual(r["frame_count"], 5)
        self.assertEqual(r["controller1_held_frames"]["Right"], 2)
        self.assertEqual(r["controller1_held_frames"]["Start"], 2)
        self.assertEqual(r["controller1_press_edges"]["Start"], 1)
        self.assertEqual(r["rle_segment_count"], 5)
        self.assertEqual(r["first_nonidle_frame"], 1)
        self.assertFalse(r["rom_sha1_is_40_hex"])
        self.assertNotIn("start_press_frames", r)
        self.assertNotIn("input_log", r)

    def test_roundtrip_deterministic(self):
        movie(self.path)
        self.assertEqual(audit(self.path), audit(self.path))

    def test_bad_controller_symbol_fails(self):
        movie(self.path, frames=["|..|...X.......|............|"])
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_mismatched_logkey_fails(self):
        movie(self.path, logkey="LogKey:#P1 Right|")
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_unsupported_platform_fails(self):
        movie(self.path, platform="NES")
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_second_player_detected(self):
        movie(self.path, frames=["|..|............|...R........|"])
        self.assertEqual(audit(self.path)["controller2_active_frames"], 1)

    def test_simultaneous_directions_reported_not_rejected(self):
        movie(self.path, frames=["|..|UD..........|............|"])
        self.assertEqual(audit(self.path)["opposing_up_down_frames"], 1)

    def test_valid_system_power_reset_columns(self):
        # Genesis BK2 system field is [Power, Reset], in that order.
        flags = ("..", "P.", ".R", "PR")
        movie(self.path, frames=[
            f"|{system}|............|............|" for system in flags
        ])
        result = audit(self.path)
        self.assertEqual(result["frame_count"], len(flags))
        self.assertEqual(result["system_active_frames"], 3)
        self.assertEqual(result["controller1_unique_states"], 1)

    def test_misplaced_system_flags_fail_closed(self):
        # The old permissive set check incorrectly accepted these pairs.
        for invalid in (".P", "R.", "PP", "RR", "RP", "PRR", "p."):
            with self.subTest(system=invalid):
                movie(self.path, frames=[
                    f"|{invalid}|............|............|"
                ])
                with self.assertRaises(BK2ValidationError):
                    audit(self.path)

    def test_invalid_sync_settings_json_fails(self):
        movie(self.path)
        with ZipFile(self.path, "r") as source:
            members = {name: source.read(name) for name in source.namelist()}
        members["SyncSettings.json"] = b'{"broken": '
        with ZipFile(self.path, "w", compression=ZIP_DEFLATED) as target:
            for name, payload in members.items():
                target.writestr(name, payload)
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_corrupted_input_member_crc_fails_closed(self):
        movie(self.path)
        with ZipFile(self.path, "r") as source:
            members = {name: source.read(name) for name in source.namelist()}
        with ZipFile(self.path, "w", compression=ZIP_DEFLATED) as target:
            for name, payload in members.items():
                target.writestr(name, payload)
        # Flip a byte inside the compressed Input Log payload, not the
        # central directory, so ZipFile's CRC validation must reject it.
        data = bytearray(self.path.read_bytes())
        name_offset = data.find(b"Input Log.txt")
        self.assertGreaterEqual(name_offset, 30)
        local_header = name_offset - 30
        name_len = int.from_bytes(data[local_header + 26:local_header + 28], "little")
        extra_len = int.from_bytes(data[local_header + 28:local_header + 30], "little")
        compressed_start = local_header + 30 + name_len + extra_len
        self.assertLess(compressed_start, len(data))
        data[compressed_start] ^= 0x01
        self.path.write_bytes(data)
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_corrupted_core_state_member_fails_closed(self):
        # CoreState affects whether this audit classifies the movie as a
        # savestate, so it must not be trusted merely because the ZIP lists it.
        movie(self.path, members={"CoreState": b"synthetic-state" * 10})
        with ZipFile(self.path, "r") as source:
            members = {name: source.read(name) for name in source.namelist()}
        with ZipFile(self.path, "w", compression=ZIP_DEFLATED) as target:
            for name, payload in members.items():
                target.writestr(name, payload)
        data = bytearray(self.path.read_bytes())
        name_offset = data.find(b"CoreState")
        self.assertGreaterEqual(name_offset, 30)
        local_header = name_offset - 30
        name_len = int.from_bytes(data[local_header + 26:local_header + 28], "little")
        extra_len = int.from_bytes(data[local_header + 28:local_header + 30], "little")
        compressed_start = local_header + 30 + name_len + extra_len
        self.assertLess(compressed_start, len(data))
        data[compressed_start] ^= 0x01
        self.path.write_bytes(data)
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_missing_input_fails(self):
        with ZipFile(self.path, "w") as z:
            z.writestr("Header.txt", "Platform GEN\n")
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_duplicate_member_fails(self):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with ZipFile(self.path, "w") as z:
                z.writestr("Header.txt", "Platform GEN\n")
                z.writestr("Header.txt", "Platform GEN\n")
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_wrong_frame_delimiter_fails(self):
        movie(self.path, frames=["|..|............|"])
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_core_state_takes_priority_over_header(self):
        movie(self.path, members={"CoreState": b"synthetic-state"})
        result = audit(self.path)
        self.assertTrue(result["core_state_in_movie"])
        self.assertEqual(result["movie_start_kind"], "savestate")

    def test_no_core_state_means_start_not_savestate(self):
        movie(self.path)
        result = audit(self.path)
        self.assertFalse(result["core_state_in_movie"])
        self.assertEqual(result["movie_start_kind"], "power_on_or_reset")

    def test_archive_size_bound_is_enforced(self):
        movie(self.path)
        with self.path.open("r+b") as stream:
            stream.seek(16_000_000)
            stream.write(b"x")
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

    def test_audit_output_never_overwrites_local_bk2(self):
        movie(self.path)
        original = self.path.read_bytes()
        self.assertEqual(audit_main([str(self.path), "--out", str(self.path)]), 2)
        self.assertEqual(self.path.read_bytes(), original)

    def test_oversized_payload_fails(self):
        movie(self.path, members={"padding.bin": "x" * 15_000_001})
        with self.assertRaises(BK2ValidationError):
            audit(self.path)

if __name__ == "__main__":
    unittest.main()
