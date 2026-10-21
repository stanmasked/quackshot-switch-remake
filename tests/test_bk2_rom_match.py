"""Synthetic BK2 + dummy ROM digest-matching tests; no proprietary assets."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zlib
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from bk2_reference_audit import BK2ValidationError, LOG_KEY
from bk2_rom_match import check_rom, MAX_ROM_BYTES, smd_to_linear, main as match_main


def synth_movie(path, claim):
    content = {
        "Header.txt": f"MovieVersion BizHawk v2.0.0\nPlatform GEN\nSHA1 {claim}\n",
        "SyncSettings.json": json.dumps({"o": {"Region": 0}}),
        "Input Log.txt": "\n".join(["[Input]", LOG_KEY,
                "|..|............|............|", "[/Input]", ""]),
    }
    with ZipFile(path, "w") as arc:
        for name, value in content.items():
            arc.writestr(name, value)


class LocalROMMatchTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.movie = Path(temp.name) / "private-movie.bk2"
        self.rom = Path(temp.name) / "synthetic.gen"
        self.bytes = b"SYNTHETIC NOT A REAL GAME" + b"\x00" * 4096
        self.rom.write_bytes(self.bytes)

    def test_32_hex_claim_matches_legacy_md5_candidate(self):
        synth_movie(self.movie, hashlib.md5(self.bytes).hexdigest())
        result = check_rom(self.movie, self.rom)
        self.assertEqual(result["status"], "candidate_hash_match")
        self.assertEqual(result["candidate_hash_algorithm"], "md5_candidate_for_32_digit_SHA1_field")
        self.assertFalse(result["replay_compatibility_verified"])
        self.assertNotIn("rom_digest", result)
        self.assertNotIn(str(self.rom), json.dumps(result))

    def test_40_hex_claim_matches_sha1(self):
        synth_movie(self.movie, hashlib.sha1(self.bytes).hexdigest())
        result = check_rom(self.movie, self.rom)
        self.assertEqual(result["status"], "candidate_hash_match")
        self.assertEqual(result["candidate_hash_algorithm"], "sha1")

    def test_8_hex_claim_matches_legacy_crc32_candidate(self):
        synth_movie(self.movie, format(zlib.crc32(self.bytes) & 0xffffffff, "08X"))
        result = check_rom(self.movie, self.rom)
        self.assertEqual(result["status"], "candidate_hash_match")
        self.assertEqual(result["candidate_hash_algorithm"], "crc32_candidate_for_8_digit_SHA1_field")
        self.assertFalse(result["replay_compatibility_verified"])

    def test_8_hex_claim_mismatch(self):
        declared = "00000000" if (zlib.crc32(self.bytes) & 0xffffffff) else "11111111"
        synth_movie(self.movie, declared)
        self.assertEqual(check_rom(self.movie, self.rom)["status"], "candidate_hash_mismatch")

    def test_signed_smd_movie_digest_matches_deinterleaved_bytes(self):
        # Build a synthetic one-block SMD from linear dummy bytes.
        linear = bytes(i % 251 for i in range(16 * 1024))
        header = bytearray(512)
        header[8:10] = b"\xaa\xbb"
        block = linear[1::2] + linear[0::2]
        self.rom.write_bytes(header + block)
        self.assertEqual(smd_to_linear(self.rom.read_bytes()), linear)
        synth_movie(self.movie, hashlib.md5(linear).hexdigest())
        result = check_rom(self.movie, self.rom)
        self.assertEqual(result["status"], "candidate_hash_match")
        self.assertEqual(result["matching_rom_representation"], "smd_deinterleaved")
        self.assertTrue(result["detected_smd_copier_header"])
        self.assertFalse(result["replay_compatibility_verified"])

    def test_unsigned_header_no_implicit_normalization(self):
        header = bytearray(512)
        self.rom.write_bytes(header + bytes(range(256)) * 64)
        synth_movie(self.movie, hashlib.md5(self.rom.read_bytes()[512:]).hexdigest())
        result = check_rom(self.movie, self.rom)
        self.assertFalse(result["detected_smd_copier_header"])
        self.assertEqual(result["status"], "candidate_hash_mismatch")

    def test_rom_output_never_overwrites_local_bk2_or_original_rom(self):
        synth_movie(self.movie, hashlib.md5(self.bytes).hexdigest())
        before_rom = self.rom.read_bytes()
        before_movie = self.movie.read_bytes()
        self.assertEqual(match_main([str(self.movie), str(self.rom), "--out", str(self.rom)]), 2)
        self.assertEqual(match_main([str(self.movie), str(self.rom), "--out", str(self.movie)]), 2)
        self.assertEqual(self.rom.read_bytes(), before_rom)
        self.assertEqual(self.movie.read_bytes(), before_movie)

    def test_32_hex_claim_mismatch(self):
        synth_movie(self.movie, "0" * 32)
        self.assertEqual(check_rom(self.movie, self.rom)["status"], "candidate_hash_mismatch")

    def test_invalid_digest_claim_is_inconclusive(self):
        synth_movie(self.movie, "1234")
        result = check_rom(self.movie, self.rom)
        self.assertEqual(result["status"], "unverifiable_movie_hash")
        self.assertIsNone(result["candidate_digest_matches_local_rom"])

    def test_local_rom_too_small_rejected(self):
        synth_movie(self.movie, "0" * 32)
        self.rom.write_bytes(b"abc")
        with self.assertRaises(BK2ValidationError):
            check_rom(self.movie, self.rom)

    def test_local_rom_too_large_rejected(self):
        synth_movie(self.movie, "0" * 32)
        with self.rom.open("wb") as f:
            f.seek(MAX_ROM_BYTES)
            f.write(b"x")
        with self.assertRaises(BK2ValidationError):
            check_rom(self.movie, self.rom)

    def test_missing_rom_fails_closed(self):
        synth_movie(self.movie, "0" * 32)
        self.rom.unlink()
        with self.assertRaises(BK2ValidationError):
            check_rom(self.movie, self.rom)

    def test_corrupted_movie_rejected_before_rom_comparison(self):
        self.movie.write_bytes(b"unreadable")
        with self.assertRaises(BK2ValidationError):
            check_rom(self.movie, self.rom)

if __name__ == "__main__":
    unittest.main()
