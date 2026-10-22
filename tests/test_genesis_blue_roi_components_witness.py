"""Stage205: synthetic original-ROI candidate contract; no original ROM."""
import copy
import sys
from pathlib import Path
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import genesis_city_visual_roi_probe as probe


def fabricated_color(roi):
    return {
        "roi":list(roi), "pixel_format":"RGB565",
        "blue_pixel_count":6,
        "blue_centroid_x_milli":134000,
        "blue_centroid_y_milli":160000,
        "blue_bbox_xyxy_exclusive":[132,157,137,162],
        "source_collision_plane_measured":False,
    }


def fabricated_component(roi):
    return {
        "schema":"quackshot_genesis_blue_components_numeric_v1",
        "roi":list(roi), "pixel_format":"RGB565", "connectivity":4,
        "min_component_pixels":5, "blue_pixel_count":6,
        "all_blue_component_count":1, "eligible_component_count":1,
        "reported_component_count":1,
        "largest_component_dominates_by_2x":True,
        "components":[{
            "blue_pixels":6, "blue_centroid_x_milli":134000,
            "blue_centroid_y_milli":160000,
            "blue_bbox_xyxy_exclusive":[132,157,137,162],
            "touches_roi_boundary":False,
        }],
        "selected_component_is_proven_player":False,
        "original_sprite_hitbox_or_contact_verified":False,
        "safe_to_modify_switch_physics":False,
    }


class OriginalColorComponentWitnessTests(unittest.TestCase):
    def test_private_numeric_sample_accepts_only_complete_bounded_components(self):
        roi=probe.DEFAULT_ROI
        self.assertEqual(
            probe.validate_component_sample(fabricated_component(roi),roi,6),
            fabricated_component(roi))

    def test_private_numeric_sample_rejects_raw_payloads_and_bad_claims(self):
        roi=probe.DEFAULT_ROI
        alterations=[
            ("top","raw_pixels",b"FAKE"),
            ("top","blue_pixel_count",7),
            ("top","reported_component_count",5),
            ("top","selected_component_is_proven_player",True),
            ("top","safe_to_modify_switch_physics",True),
            ("top","connectivity",8),
            ("nested","raw_ram",b"FAKE"),
            ("nested","blue_bbox_xyxy_exclusive",[0,0,2048,2048]),
            ("nested","blue_pixels",True),
            ("nested","blue_centroid_x_milli",500000),
        ]
        for location,key,value in alterations:
            with self.subTest(location=location,key=key):
                report=fabricated_component(roi)
                if location=="top":
                    report[key]=value
                else:
                    report["components"][0][key]=value
                with self.assertRaises(ValueError):
                    probe.validate_component_sample(report,roi,6)

    def _fake_measure(self, *, enabled, mutation=None):
        roi=probe.DEFAULT_ROI
        calls=[]
        def fake_run(core,metadata,rom_data,sha,frames,schedule,**kwargs):
            name=("idle","right","right_repeat")[len(calls)]
            calls.append(name)
            for frame in probe.MARKERS:
                ram=bytearray(65536)
                y=176 if name=="idle" else probe.EXPECTED_Y[frame]
                ram[0xA2D4:0xA2D6]=y.to_bytes(2,"little")
                kwargs["checkpoint_sink"](frame,bytes(ram))
            records=[{"frame":frame,**fabricated_color(roi)}
                     for frame in probe.MARKERS]
            candidates=[{"frame":frame,**fabricated_component(roi)}
                        for frame in probe.MARKERS]
            if name=="right_repeat" and mutation:
                candidates[0][mutation[0]]=mutation[1]
            return {
                "frames_video_callbacks":probe.TOTAL,
                "frames_without_video_callback":0,
                "frames_with_multiple_video_callbacks":0,
                "video_callbacks_outside_retro_run":0,
                "ram_checkpoint_phase":"post_retro_run_before_next_input",
                "input_nonzero_reads":71 if name=="idle" else 521,
                "video_checkpoints":["idle"] if name=="idle" else ["same"],
                "video_roi_blue_numeric":records,
                "video_roi_components_numeric":candidates if enabled else None,
            }
        guards=[]
        with mock.patch.object(probe,"run_core",side_effect=fake_run):
            result=probe.measure(
                object(),{"need_fullpath":False},Path("/NO_ROM"),b"FAKE",
                probe.EXPECTED_ROM,"test-core",roi,
                verify_inputs=lambda:guards.append("checked"),
                components=enabled)
        return result,calls,guards

    def test_private_measure_three_runs_include_only_numeric_candidates(self):
        report,calls,guards=self._fake_measure(enabled=True)
        self.assertEqual(calls,["idle","right","right_repeat"])
        self.assertEqual(len(guards),6)
        self.assertEqual([row["frame"] for row in
                          report["visual_component_candidates"]],
                         list(probe.MARKERS))
        self.assertEqual(report["visual_component_candidates"][0][
                         "components"][0]["blue_pixels"],6)
        self.assertTrue(report["component_identity_unverified"])
        self.assertFalse(report["original_sprite_identity_proven_from_blue_pixels"])
        self.assertFalse(report["original_collision_plane_verified"])
        self.assertFalse(report["safe_to_modify_switch_physics"])
        self.assertNotIn("raw_ram", report["visual_component_candidates"][0])
        self.assertNotIn("raw_frame", report["visual_component_candidates"][0])
        self.assertNotIn("raw_ram", report["visual_component_candidates"][0]["components"][0])
        self.assertNotIn("raw_pixels",str(report))

    def test_default_visual_probe_preserves_original_numeric_schema(self):
        report,calls,guards=self._fake_measure(enabled=False)
        self.assertEqual(len(report["sample_frames"]),len(probe.MARKERS))
        self.assertNotIn("visual_component_candidates",report)
        self.assertNotIn("component_identity_unverified",report)
        self.assertFalse(report["safe_to_modify_switch_physics"])

    def test_repeated_right_components_must_match(self):
        with self.assertRaisesRegex(ValueError,"not reproducible"):
            self._fake_measure(enabled=True,
                               mutation=("all_blue_component_count",2))

    def test_wrong_video_component_flag_rejected_before_private_run(self):
        with self.assertRaises(ValueError):
            probe.measure(object(),{},Path("/NO_ROM"),b"FAKE",
                          "sha","sha",probe.DEFAULT_ROI,
                          verify_inputs=lambda:None,components=1)


if __name__=="__main__":
    unittest.main()
