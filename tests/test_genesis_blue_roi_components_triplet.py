"""Stage205: ROM-free original SHA-pinned triplet component integration tests."""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT / "tools"))
import genesis_city_same_step_triplet_probe as triplet
from test_genesis_city_same_step_triplet_probe import fixture, line


def fake_blue_components(roi):
    return {
        "schema":"quackshot_genesis_blue_components_numeric_v1",
        "roi":list(roi), "pixel_format":"RGB565",
        "connectivity":4, "min_component_pixels":5,
        "blue_pixel_count":10,
        "all_blue_component_count":1, "eligible_component_count":1,
        "reported_component_count":1,
        "largest_component_dominates_by_2x":True,
        "components":[{
            "blue_pixels":10,"blue_centroid_x_milli":135000,
            "blue_centroid_y_milli":160000,
            "blue_bbox_xyxy_exclusive":[133,157,140,164],
            "touches_roi_boundary":False
        }],
        "selected_component_is_proven_player":False,
        "original_sprite_hitbox_or_contact_verified":False,
        "safe_to_modify_switch_physics":False,
    }


def synthetic_triplet(*, enabled, alteration=None):
    traces,reads,digests,roi=fixture()
    calls=[]
    step_scroll={"value":0}
    guard=[]
    def ram_row(item):
        payload=bytearray(65536)
        for at,value in ((0xA2D0,item["player_world_x"]),
                         (0xA2D4,item["player_world_y"]),
                         (0x8524,item["camera_ram"]),
                         (0x852C,item["render_buffer_ram"])):
            payload[at:at+2]=value.to_bytes(2,"little")
        return bytes(payload)
    def snap(_hook,scanline):
        return line(step_scroll["value"],scanline)
    def fake_run(_core,_meta,_rom,_sha,_frames,_script,**kwargs):
        label=("idle","right","right_repeat")[len(calls)]
        calls.append(label)
        assert kwargs.get("video_roi_components") is enabled
        for frame in triplet.MARKERS:
            row=traces[label][frame]
            step_scroll["value"]=row["render_buffer_ram"]
            kwargs["checkpoint_sink"](frame,ram_row(row))
        numeric=[{"frame":frame,**traces[label][frame]["blue_visual"]}
                 for frame in triplet.MARKERS]
        candidate=[{"frame":frame,**fake_blue_components(roi)}
                   for frame in triplet.MARKERS]
        if alteration and label=="right_repeat":
            loc,key,value=alteration
            if loc=="top":
                candidate[0][key]=value
            else:
                candidate[0]["components"][0][key]=value
        return {
            "frames_video_callbacks":triplet.TOTAL,
            "frames_without_video_callback":0,
            "frames_with_multiple_video_callbacks":0,
            "video_callbacks_outside_retro_run":0,
            "ram_checkpoint_phase":"post_retro_run_before_next_input",
            "video_callback_phase":"during_retro_run",
            "video_roi_blue_numeric":numeric,
            "video_roi_components_numeric":candidate if enabled else None,
            "input_nonzero_reads":reads[label],
            "video_checkpoints":digests[label],
        }
    with mock.patch.object(triplet,"bind_vdp_probe",return_value="synthetic"), \
         mock.patch.object(triplet,"vdp_number_snapshot",side_effect=snap), \
         mock.patch.object(triplet,"run_core",side_effect=fake_run):
        result=triplet.collect(object(),{},Path("/synthetic"),
                               b"not-a-real-ROM",roi,
                               lambda:guard.append("sha_checked"),
                               component_candidates=enabled)
    return result,calls,guard


class InstrumentedOriginalTripletComponentsTests(unittest.TestCase):
    def test_components_include_numeric_only_from_three_pinned_runs(self):
        report,calls,guard=synthetic_triplet(enabled=True)
        self.assertEqual(calls,["idle","right","right_repeat"])
        self.assertEqual(len(guard),6)
        self.assertEqual(len(report["visual_component_candidates"]),5)
        self.assertTrue(report["component_identity_unverified"])
        self.assertEqual(report["visual_component_candidates"][0]["components"][0][
                         "blue_pixels"],10)
        self.assertFalse(report["safe_to_modify_switch_physics"])
        self.assertFalse(report["original_collision_hitbox_or_contact_plane_verified"])
        self.assertNotIn("raw_frame",str(report))
        self.assertNotIn("raw_ram",str(report))

    def test_legacy_triplet_report_unmodified_when_disabled(self):
        report,calls,guard=synthetic_triplet(enabled=False)
        self.assertEqual(len(report["frames"]),5)
        self.assertNotIn("visual_component_candidates",report)
        self.assertNotIn("component_identity_unverified",report)

    def test_duplicate_changed_numeric_repeat_is_rejected(self):
        with self.assertRaisesRegex(ValueError,"not reproducible"):
            synthetic_triplet(enabled=True,
                alteration=("nested","blue_pixels",9))

    def test_raw_pixel_injection_is_rejected_without_serialization(self):
        with self.assertRaisesRegex(ValueError,"privacy"):
            synthetic_triplet(enabled=True,
                alteration=("top","raw_frame_pixels",b"BAD"))

    def test_component_flag_bool_only(self):
        with self.assertRaises(ValueError):
            triplet.collect(object(),{},Path("/synthetic"),b"",
                            (100,80,85,140),lambda:None,
                            component_candidates=1)


if __name__=="__main__":
    unittest.main()
