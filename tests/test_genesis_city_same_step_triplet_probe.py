"""Stage200 ROM-free same-retro_run-index RAM/video/VDP triplet tests.

All world positions, VDP numbers and blue pixels here are FABRICATED.
The stubbed libretro core never opens an original ROM or original media.
"""
import copy
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_city_same_step_triplet_probe as study


def fake_visual(roi):
    return {
        "roi": list(roi), "pixel_format": "RGB565",
        "blue_pixel_count": 10,
        "blue_centroid_x_milli": 135000,
        "blue_centroid_y_milli": 160000,
        "blue_bbox_xyxy_exclusive": [133, 157, 140, 164],
        "source_collision_plane_measured": False
    }


def line(scroll, line_id):
    return {
        "line": line_id, "vdp_reg11": 0, "vdp_reg13": 52,
        "hscroll_vram_base_address": 0xD000,
        "hscroll_scanline_mask": 0,
        "plane_A_HScroll_11bit_signed": -scroll,
        "plane_B_HScroll_11bit_signed": -(scroll // 2),
        "plane_A_highbits": 0,
        "plane_B_highbits": 0
    }


def fixture(roi=(100, 80, 85, 140)):
    right = {}
    idle = {}
    for frame in study.MARKERS:
        scroll = {2180: 90, 2220: 155, 2280: 255,
                  2297: 281, 2350: 358}[frame]
        right[frame] = {
            "player_world_x": study.EXPECTED_X[frame],
            "player_world_y": study.EXPECTED_Y[frame],
            "camera_ram": scroll+2,
            "render_buffer_ram": scroll,
            "vdp_scanlines": [line(scroll, ln) for ln in study.SCANLINES],
            "blue_visual": fake_visual(roi),
        }
        idle[frame] = {
            "player_world_x": 61, "player_world_y": 176,
            "camera_ram": 0, "render_buffer_ram": 0,
            "vdp_scanlines": [line(0, ln) for ln in study.SCANLINES],
            "blue_visual": fake_visual(roi),
        }
    return (
        {"idle": idle, "right": right, "right_repeat": copy.deepcopy(right)},
        {"idle": 71, "right": 521, "right_repeat": 521},
        {"idle": ["idle"], "right": ["same"], "right_repeat": ["same"]},
        roi,
    )


def verify(traces, reads, digests, roi):
    return study.validate_triplet(
        traces, reads, digests, roi,
        rom_sha=study.ROM_SHA, core_sha=study.INSTRUMENTED_CORE_SHA)


class FrameTripletTests(unittest.TestCase):
    def test_fake_valid_triplet_is_limited_to_libretro_step(self):
        result = verify(*fixture())
        self.assertEqual(result["schema"],
                         "quackshot_private_genesis_ram_video_vdp_same_step_v1")
        self.assertEqual([r["frame"] for r in result["frames"]],
                         list(study.MARKERS))
        self.assertEqual(result["hardware_vdp_scanlines_verified"], 120)
        self.assertFalse(result["same_cpu_vdp_scanout_phase_verified"])
        self.assertFalse(result["original_collision_hitbox_or_contact_plane_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])
        self.assertFalse(result["independently_attested_by_report"])
        self.assertFalse(result["original_media_raw_ram_vram_or_pixels_saved"])
        self.assertNotIn("vdp_scanlines", result["frames"][0])
        self.assertNotIn("blue_bbox_xyxy_exclusive", result["frames"][0])

    def test_wrong_sha_and_controller_count_rejected(self):
        tr, rd, dg, roi = fixture()
        with self.assertRaisesRegex(ValueError, "pinned"):
            study.validate_triplet(tr, rd, dg, roi,
                rom_sha="0"*64, core_sha=study.INSTRUMENTED_CORE_SHA)
        with self.assertRaisesRegex(ValueError, "pinned"):
            study.validate_triplet(tr, rd, dg, roi,
                rom_sha=study.ROM_SHA, core_sha="0"*64)
        rd["right"] -= 1
        with self.assertRaisesRegex(ValueError, "controller callback"):
            verify(tr, rd, dg, roi)

    def test_duplicate_missing_marker_and_new_payload_rejected(self):
        tr, rd, dg, roi = fixture()
        tr["right"][2280]["raw_ram"] = b"fake_bytes"
        with self.assertRaisesRegex(ValueError, "fields"):
            verify(tr, rd, dg, roi)
        tr, rd, dg, roi = fixture()
        del tr["right_repeat"][2297]
        with self.assertRaisesRegex(ValueError, "marker"):
            verify(tr, rd, dg, roi)
        tr, rd, dg, roi = fixture()
        tr["right"][2220]["blue_visual"]["RGB_frame"] = "pixels"
        tr["right_repeat"][2220]["blue_visual"]["RGB_frame"] = "pixels"
        with self.assertRaisesRegex(ValueError, "forbidden"):
            verify(tr, rd, dg, roi)

    def test_wrong_vdp_plane_scroll_or_phase_flags_rejected(self):
        for mutation in ("plane_a", "plane_b", "register", "line"):
            tr, rd, dg, roi = fixture()
            for label in ("right", "right_repeat"):
                sample=tr[label][2220]["vdp_scanlines"][0]
                if mutation == "plane_a":
                    sample["plane_A_HScroll_11bit_signed"] += 1
                elif mutation == "plane_b":
                    sample["plane_B_HScroll_11bit_signed"] += 1
                elif mutation == "register":
                    sample["vdp_reg11"] = 2
                else:
                    sample["line"] = 1
            with self.subTest(mutation=mutation), self.assertRaisesRegex(
                ValueError, "VDP hardware"):
                verify(tr, rd, dg, roi)

    def test_visual_center_bounds_and_empty_sample_rules(self):
        for mutation in ("center", "box", "count_type", "empty"):
            tr, rd, dg, roi = fixture()
            for label in ("right", "right_repeat"):
                v = tr[label][2280]["blue_visual"]
                if mutation == "center":
                    v["blue_centroid_x_milli"] = 9999999
                elif mutation == "box":
                    v["blue_bbox_xyxy_exclusive"] = [0,0,1,1]
                elif mutation == "count_type":
                    v["blue_pixel_count"] = True
                else:
                    v["blue_pixel_count"] = 0
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                verify(tr, rd, dg, roi)

    def test_repeatability_fails_if_vdp_or_color_changes(self):
        for section, key, value in (
            ("top", "render_buffer_ram", 154),
            ("visual", "blue_centroid_x_milli", 135111),
        ):
            tr, rd, dg, roi = fixture()
            if section == "top":
                tr["right_repeat"][2220][key] = value
            else:
                tr["right_repeat"][2220]["blue_visual"][key] = value
            with self.assertRaisesRegex(ValueError, "reproducible"):
                verify(tr, rd, dg, roi)
        tr, rd, dg, roi = fixture()
        dg["right_repeat"] = ["other"]
        with self.assertRaisesRegex(ValueError, "video digests"):
            verify(tr, rd, dg, roi)

    def test_integer_y_and_world_x_rechecked(self):
        for key, value in (("player_world_x", 297),
                           ("player_world_y", 175)):
            tr, rd, dg, roi = fixture()
            tr["right"][2220][key] = value
            tr["right_repeat"][2220][key] = value
            with self.assertRaisesRegex(ValueError, "world landmarks"):
                verify(tr, rd, dg, roi)

    def test_private_report_output_never_overwrites_or_writes_to_git(self):
        tr, rd, dg, roi = fixture()
        result = verify(tr, rd, dg, roi)
        with tempfile.TemporaryDirectory(prefix="qs-stage200-") as tmp:
            target = Path(tmp)/"triplet.json"
            study.private_write(result, target, ROOT)
            self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o600)
            self.assertEqual(json.loads(target.read_text()), result)
            with self.assertRaises(ValueError):
                study.private_write(result, target, ROOT)
        inside = ROOT/"forbidden-same-step-stage200.json"
        with self.assertRaisesRegex(ValueError, "public repository"):
            study.private_write(result, inside, ROOT)
        self.assertFalse(inside.exists())

    def test_collect_uses_one_step_index_for_ram_vdp_and_video(self):
        calls, attest = [], []
        scroll = {"current": 0}
        source_traces, _, _, roi = fixture()

        def ram_bytes(sample):
            buffer = bytearray(65536)
            for offset, value in (
                (0xA2D0, sample["player_world_x"]),
                (0xA2D4, sample["player_world_y"]),
                (0x8524, sample["camera_ram"]),
                (0x852C, sample["render_buffer_ram"]),
            ):
                buffer[offset:offset+2] = value.to_bytes(2,"little")
            return bytes(buffer)

        def patched_snapshot(_hook, vdp_line):
            return line(scroll["current"], vdp_line)

        def fake_run(core, meta, rom, sha, frames, schedule, **kwargs):
            name = ("idle", "right", "right_repeat")[len(calls)]
            calls.append(name)
            for frame in study.MARKERS:
                ram = ram_bytes(source_traces[name][frame])
                scroll["current"] = source_traces[name][frame]["render_buffer_ram"]
                kwargs["checkpoint_sink"](frame, ram)
            return {
                "frames_video_callbacks": study.TOTAL,
                "frames_without_video_callback": 0,
                "frames_with_multiple_video_callbacks": 0,
                "video_callbacks_outside_retro_run": 0,
                "ram_checkpoint_phase": "post_retro_run_before_next_input",
                "video_callback_phase": "during_retro_run",
                "input_nonzero_reads": 71 if name == "idle" else 521,
                "video_checkpoints": ["idle"] if name=="idle" else ["right"],
                "video_roi_blue_numeric": [
                    {"frame":f, **source_traces[name][f]["blue_visual"]}
                    for f in study.MARKERS
                ],
            }
        with mock.patch.object(study, "bind_vdp_probe", return_value="fake"), \
             mock.patch.object(study, "vdp_number_snapshot",
                               side_effect=patched_snapshot), \
             mock.patch.object(study, "run_core", side_effect=fake_run):
            result = study.collect(
                object(), {"need_fullpath": False}, Path("/synthetic"),
                b"fake", roi, lambda: attest.append("checked"))
        self.assertEqual(calls, ["idle", "right", "right_repeat"])
        self.assertEqual(len(attest), 6)
        self.assertEqual(result["hardware_vdp_scanlines_verified"], 120)
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_collect_rejects_invalid_video_phase_and_checks_inputs_after_failure(self):
        def broken_run(*_args, **_kw):
            return {
                "frames_video_callbacks": study.TOTAL,
                "frames_without_video_callback": 0,
                "frames_with_multiple_video_callbacks": 0,
                "video_callbacks_outside_retro_run": 0,
                "ram_checkpoint_phase": "pre_retro_run",
                "video_callback_phase": "during_retro_run",
            }
        checks=[]
        with mock.patch.object(study, "bind_vdp_probe", return_value="fake"), \
             mock.patch.object(study, "run_core", side_effect=broken_run):
            with self.assertRaisesRegex(ValueError, "phase"):
                study.collect(object(), {}, Path("/synthetic"), b"fake",
                              (100,80,85,140), lambda: checks.append(1))
        self.assertEqual(len(checks), 2)


if __name__ == "__main__":
    unittest.main()
