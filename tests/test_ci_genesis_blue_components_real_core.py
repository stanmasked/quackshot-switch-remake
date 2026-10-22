"""Stage206 ROM-free regression: only invented component data and ROM bytes."""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ci_genesis_blue_components_real_core as smoke
def fake_source():
    # Exact bytes from CI's original-free synthetic 68000 fixture builder.
    data = bytearray(128 * 1024)
    data[0:4] = (0x00FFFF00).to_bytes(4, "big")
    data[4:8] = (0x200).to_bytes(4, "big")
    data[0x100:0x110] = b"SEGA MEGA DRIVE "
    data[0x120:0x150] = b"OPENAI TEST ONLY NO GAME ASSETS".ljust(48, b" ")
    data[0x150:0x180] = b"INDEPENDENT SYNTHETIC CPU LOOP".ljust(48, b" ")
    data[0x180:0x18e] = b"GM 99999999-00"
    data[0x1a4:0x1a8] = (len(data) - 1).to_bytes(4, "big")
    data[0x1f0:0x1f1] = b"U"
    data[0x200:0x202] = bytes.fromhex("60fe")
    data[0x18e:0x190] = (0x60fe).to_bytes(2, "big")
    return bytes(data)


def synthetic_result():
    rows, old = [], []
    for frame in smoke.SAMPLES:
        rows.append({
            "frame":frame,
            "schema":"quackshot_genesis_blue_components_numeric_v1",
            "roi":list(smoke.ROI),
            "pixel_format":"RGB565",
            "connectivity":4,
            "min_component_pixels":5,
            "blue_pixel_count":0,
            "all_blue_component_count":0,
            "eligible_component_count":0,
            "reported_component_count":0,
            "largest_component_dominates_by_2x":False,
            "components":[],
            "selected_component_is_proven_player":False,
            "original_sprite_hitbox_or_contact_verified":False,
            "safe_to_modify_switch_physics":False,
        })
        old.append({"frame":frame,"blue_pixel_count":0})
    return {
        "frames_video_callbacks":90,
        "frames_without_video_callback":0,
        "frames_with_multiple_video_callbacks":0,
        "video_callbacks_outside_retro_run":0,
        "video_callback_phase":"during_retro_run",
        "video_roi_blue_numeric":old,
        "video_roi_components_numeric":rows,
        "video_checkpoints":[{"frame":f,"crc32":"0x00000000"}
                             for f in range(90)],
    }


class RealCompiledComponentsSmokeContractTests(unittest.TestCase):
    def test_numeric_callback_report_accepts_empty_synthetic_scene(self):
        output=smoke.verify_numeric_report(synthetic_result())
        self.assertEqual(len(output["components"]),len(smoke.SAMPLES))
        self.assertEqual(len(output["video_crc"]),90)
        self.assertEqual(output["components"][0][1],0)
        self.assertNotIn("original_rom",str(output))

    def test_unknown_color_or_raw_memory_payload_rejected(self):
        for field in ("rgb_pixel_bytes","raw_vram","video_texture"):
            report=synthetic_result()
            report["video_roi_components_numeric"][0][field]=b"FAKE"
            with self.subTest(field=field),self.assertRaisesRegex(
                ValueError,"extra or corrupted"):
                smoke.verify_numeric_report(report)

    def test_false_claims_and_wrong_component_counts_rejected(self):
        for key,value in (
            ("selected_component_is_proven_player",True),
            ("original_sprite_hitbox_or_contact_verified",True),
            ("safe_to_modify_switch_physics",True),
            ("blue_pixel_count",9),
            ("reported_component_count",1),
            ("connectivity",8),
            ("min_component_pixels",False),
        ):
            report=synthetic_result()
            report["video_roi_components_numeric"][0][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):
                smoke.verify_numeric_report(report)

    def test_missing_duplicate_or_reordered_frames_rejected(self):
        for mutation in ("missing","duplicate","reversed","CRC"):
            report=synthetic_result()
            rows=report["video_roi_components_numeric"]
            if mutation=="missing":
                rows.pop()
            elif mutation=="duplicate":
                rows[1]["frame"]=rows[0]["frame"]
            elif mutation=="reversed":
                rows.reverse()
            else:
                report["video_checkpoints"].pop()
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):
                smoke.verify_numeric_report(report)

    def test_real_core_runner_must_reproduce_full_numeric_and_crc(self):
        first=synthetic_result()
        with mock.patch.object(smoke,"run_core",
                               side_effect=[first,copy.deepcopy(first)]) as run:
            report=smoke.repeatable_synthetic_runs(
                object(),{},fake_source(),"fake",Path("/fake"))
        self.assertEqual(run.call_count,2)
        self.assertTrue(all(call.kwargs["video_roi_components"] is True
                            for call in run.call_args_list))
        self.assertTrue(report["repeatable_numeric_components"])
        self.assertEqual(report["synthetic_independent_runs"],2)
        self.assertFalse(report["source_game_rom_used"])
        self.assertFalse(report["safe_to_modify_switch_physics"])
        self.assertFalse(report["original_sprite_or_hitbox_identified"])

    def test_real_core_runner_rejects_changed_second_numeric_sample(self):
        first=synthetic_result()
        second=copy.deepcopy(first)
        second["video_roi_components_numeric"][0][
            "blue_pixel_count"]=1
        second["video_roi_blue_numeric"][0]["blue_pixel_count"]=1
        with mock.patch.object(smoke,"run_core",
                               side_effect=[first,second]):
            with self.assertRaises(ValueError):
                smoke.repeatable_synthetic_runs(
                    object(),{},fake_source(),"fake",Path("/fake"))

    def test_real_core_runner_rejects_changed_second_crc(self):
        first=synthetic_result()
        second=copy.deepcopy(first)
        second["video_checkpoints"][20]["crc32"]="0xFFFFFFFF"
        with mock.patch.object(smoke,"run_core",
                               side_effect=[first,second]):
            with self.assertRaisesRegex(ValueError,"disagree"):
                smoke.repeatable_synthetic_runs(
                    object(),{},fake_source(),"fake",Path("/fake"))

    def test_whole_rom_digest_does_not_allow_disguised_extra_game_data(self):
        original = fake_source()
        self.assertIsNone(smoke.require_stage206_fixture(original))
        changed = bytearray(original)
        changed[0x300] = 0xFF  # passes header/loop checks but not exact SHA
        with self.assertRaisesRegex(ValueError, "checksum changed"):
            smoke.require_stage206_fixture(bytes(changed))

    def test_source_game_cannot_enter_compiled_smoke(self):
        with mock.patch.object(smoke,"run_core") as runner:
            with self.assertRaises(ValueError):
                smoke.repeatable_synthetic_runs(
                    object(),{},b"some ROM bytes","bad",Path("/fake"))
            runner.assert_not_called()


if __name__=="__main__":
    unittest.main()
