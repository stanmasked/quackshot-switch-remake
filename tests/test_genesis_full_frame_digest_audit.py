import copy
import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_full_frame_digest_audit as audit


def fake_run(name):
    is_control = name == "right_control"
    digest_name = "right_control" if is_control else "near_wall_jump"
    inputs = audit.CONTROL_INPUTS if is_control else audit.JUMP_INPUTS
    rows = []
    crcs = []
    plateau_full = hashlib.sha256(b"constant-full-buffer").hexdigest()
    plateau_visible = hashlib.sha256(b"constant-visible-pixels").hexdigest()
    for frame in audit.EXPECTED_FRAMES:
        if 2364 <= frame <= 2384:
            full, visible, crc = plateau_full, plateau_visible, "7BF0AAE9"
        elif frame == 2363:
            full = hashlib.sha256(b"frame-2363-full").hexdigest()
            visible = hashlib.sha256(b"frame-2363-visible").hexdigest()
            crc = "95731692"
        elif frame == 2385:
            full = hashlib.sha256(b"frame-2385-full").hexdigest()
            visible = hashlib.sha256(b"frame-2385-visible").hexdigest()
            crc = "A7AB14AA"
        else:
            full = hashlib.sha256(f"{digest_name}-frame-{frame}-full".encode()).hexdigest()
            visible = hashlib.sha256(f"{digest_name}-frame-{frame}-visible".encode()).hexdigest()
            crc = hashlib.sha256(f"{digest_name}-crc-{frame}".encode()).hexdigest()[:8].upper()
        if is_control and 2364 <= frame <= 2384:
            full = hashlib.sha256(f"control-full-{frame}".encode()).hexdigest()
            visible = hashlib.sha256(f"control-visible-{frame}".encode()).hexdigest()
        elif is_control and frame in (2363, 2385):
            full = hashlib.sha256(f"control-full-{frame}".encode()).hexdigest()
            visible = hashlib.sha256(f"control-visible-{frame}".encode()).hexdigest()
        rows.append({
            "frame": frame, "size": [320, 224], "pitch": 1440,
            "pixel_format": 2, "byte_count": 1440 * 224,
            "sha256": full, "visible_byte_count": 320 * 224 * 2,
            "visible_sha256": visible,
        })
        crcs.append({"frame": frame, "crc32": crc})
    if is_control:
        crcs = [{"frame": f, "crc32": hashlib.sha256(f"control-crc-{f}".encode()).hexdigest()[:8].upper()}
                for f in audit.EXPECTED_FRAMES]
    return {
        "input_ranges": copy.deepcopy(inputs),
        "input_nonzero_reads": 521 if is_control else 551,
        "frames_requested": 2600,
        "frames_video_callbacks": 2600,
        "frames_without_video_callback": 0,
        "frames_with_multiple_video_callbacks": 0,
        "video_callback_phase": "during_retro_run",
        "video_digest_frames_requested": list(audit.EXPECTED_FRAMES),
        "video_frame_digests": rows,
        "video_crc32": crcs,
    }


def fake_report():
    return {
        "schema": audit.SCHEMA,
        "stage": 217,
        "status": "NUMERIC_DIGESTS_ONLY",
        "reference_rom_sha256": audit.ROM_SHA,
        "research_core_sha256": audit.INSTRUMENTED_CORE_SHA,
        "frame_window_inclusive": [2350, 2410],
        "frames_requested_per_run": 2600,
        "frame_size": [320, 224],
        "video_callback_phase": "during_retro_run",
        "runs": {name: fake_run(name) for name in audit.RUN_NAMES},
        "original_media_exported": False,
        "raw_frame_pixels_exported": False,
        "raw_ram_exported": False,
        "full_frame_bytes_saved": False,
        "player_identity_verified": False,
        "collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
    }


class FullFrameDigestAuditTests(unittest.TestCase):
    def test_valid_synthetic_digest_report_passes(self):
        result = audit.audit_report(fake_report())
        self.assertEqual(result["status"], "PASS_REPEAT_SHA256_AND_21_FRAME_PLATEAU_CONFIRMED")
        self.assertEqual(result["frames_checked_per_run"], 61)
        self.assertTrue(result["jump_repeat_full_buffer_sha256_exact"])
        self.assertTrue(result["jump_repeat_visible_pixel_sha256_exact"])
        self.assertEqual(result["longest_visible_pixel_sha256_run"]["first_frame"], 2364)
        self.assertEqual(result["longest_visible_pixel_sha256_run"]["last_frame"], 2384)
        self.assertEqual(result["longest_visible_pixel_sha256_run"]["length"], 21)
        self.assertFalse(result["player_identity_verified"])
        self.assertFalse(result["collision_geometry_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_missing_frame_digest_rejected(self):
        report = fake_report()
        report["runs"]["near_wall_jump"]["video_frame_digests"].pop()
        with self.assertRaisesRegex(ValueError, "digest count"):
            audit.audit_report(report)

    def test_repeat_digest_mismatch_rejected_even_if_top_level_claims_are_false(self):
        report = fake_report()
        report["runs"]["near_wall_jump_repeat"]["video_frame_digests"][10]["visible_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "repeat differs"):
            audit.audit_report(report)

    def test_repeat_crc_mismatch_rejected(self):
        report = fake_report()
        report["runs"]["near_wall_jump_repeat"]["video_crc32"][0]["crc32"] = "00000000"
        with self.assertRaisesRegex(ValueError, "repeat differs"):
            audit.audit_report(report)

    def test_wrong_rom_or_core_pin_rejected(self):
        for key in ("reference_rom_sha256", "research_core_sha256"):
            report = fake_report()
            report[key] = "0" * 64
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "pinned source"):
                audit.audit_report(report)

    def test_raw_frame_payload_rejected(self):
        report = fake_report()
        report["raw_pixels"] = "not allowed"
        with self.assertRaisesRegex(ValueError, "top-level schema"):
            audit.audit_report(report)

    def test_unsupported_physics_claim_rejected(self):
        report = fake_report()
        report["safe_to_modify_switch_physics"] = True
        with self.assertRaisesRegex(ValueError, "unsupported data-export"):
            audit.audit_report(report)

    def test_wrong_frame_geometry_rejected(self):
        report = fake_report()
        report["runs"]["near_wall_jump"]["video_frame_digests"][0]["pitch"] = 600
        with self.assertRaisesRegex(ValueError, "geometry"):
            audit.audit_report(report)

    def test_malformed_digest_rejected(self):
        report = fake_report()
        report["runs"]["near_wall_jump"]["video_frame_digests"][0]["sha256"] = "z" * 64
        with self.assertRaisesRegex(ValueError, "malformed SHA"):
            audit.audit_report(report)

    def test_wrong_crc_plateau_anchor_rejected(self):
        report = fake_report()
        report["runs"]["near_wall_jump"]["video_crc32"][20]["crc32"] = "00000000"
        report["runs"]["near_wall_jump_repeat"]["video_crc32"][20]["crc32"] = "00000000"
        with self.assertRaisesRegex(ValueError, "CRC32 plateau"):
            audit.audit_report(report)

    def test_right_control_must_differ_visually(self):
        report = fake_report()
        report["runs"]["right_control"]["video_frame_digests"] = copy.deepcopy(
            report["runs"]["near_wall_jump"]["video_frame_digests"])
        with self.assertRaisesRegex(ValueError, "jump/control"):
            audit.audit_report(report)


if __name__ == "__main__":
    unittest.main()
