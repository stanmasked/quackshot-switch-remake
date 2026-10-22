"""Stage209: 11 ROM-free fabricated triple-ROI consistency regressions.

The reports are synthesized from fake libretro data. No original game media
or pixels are read or embedded in the public test suite.
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import genesis_private_cross_roi_consistency as cross
from test_genesis_blue_roi_components_triplet import synthetic_triplet

WIDE=(100,80,85,140)
FOCUS=(115,115,60,75)
TIGHT=(123,120,32,60)


def synthetic_reports():
    baseline,_,_=synthetic_triplet(enabled=True)
    result=[]
    for roi,with_edge in ((WIDE,True),(FOCUS,False),(TIGHT,False)):
        report=copy.deepcopy(baseline)
        report["video_roi"]=list(roi)
        for row,components in zip(report["frames"],
                                  report["visual_component_candidates"]):
            # All geometry and color groups are invented, nonoriginal.
            candidate={
                "blue_pixels":10,
                "blue_centroid_x_milli":135000,
                "blue_centroid_y_milli":160000,
                "blue_bbox_xyxy_exclusive":[133,157,140,164],
                "touches_roi_boundary":False,
            }
            stationary={
                "blue_pixels":30,
                "blue_centroid_x_milli":143000,
                "blue_centroid_y_milli":205000,
                "blue_bbox_xyxy_exclusive":[100,192,185,220],
                "touches_roi_boundary":True,
            }
            comps=([stationary,candidate] if with_edge else [candidate])
            count=40 if with_edge else 10
            color_x=142000 if with_edge else 135000
            color_y=190000 if with_edge else 160000
            row["blue_pixel_count"]=count
            row["blue_centroid_x_milli"]=color_x
            row["blue_centroid_y_milli"]=color_y
            row["blue_minus_vdp_candidate_x_milli"]=(
                color_x-1000*row["vdp_screen_x_candidate"]
            )
            components.update({
                "roi":list(roi),
                "blue_pixel_count":count,
                "all_blue_component_count":len(comps),
                "eligible_component_count":len(comps),
                "reported_component_count":len(comps),
                "largest_component_dominates_by_2x":with_edge or True,
                "components":comps,
            })
        result.append(report)
    return result


class OriginalFreeCrossRoiAuditTests(unittest.TestCase):
    def test_three_fabricated_reports_pass_with_no_player_claims(self):
        result=cross.analyze(*synthetic_reports())
        self.assertEqual(result["reports_compared"],3)
        self.assertEqual(result["tested_libretro_frames"],list(cross.MARKERS))
        self.assertTrue(result["numeric_ram_camera_vdp_same_across_all_reports"])
        self.assertTrue(result["wide_large_edge_color_group_unchanged_at_all_five_frames"])
        self.assertTrue(result["narrow_color_group_exactly_matched_at_all_five_frames"])
        self.assertFalse(result["color_group_identified_as_original_player"])
        self.assertFalse(result["invariant_color_group_identified_as_background"])
        self.assertFalse(result["original_contact_plane_or_hitbox_verified"])
        self.assertFalse(result["local_execution_provenance_independently_attested"])
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_disagreeing_original_numeric_state_blocked(self):
        data=synthetic_reports()
        data[1]["frames"][0]["player_world_x"]=1
        with self.assertRaises(ValueError):
            cross.analyze(*data)

    def test_bad_provenance_or_privacy_flag_is_blocked(self):
        for field,value in (
            ("instrumented_core_sha256","0"*64),
            ("component_identity_unverified",False),
            ("safe_to_modify_switch_physics",True),
        ):
            data=synthetic_reports()
            data[2][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                cross.analyze(*data)

    def test_nonnested_or_swapped_roi_refused(self):
        d=synthetic_reports()
        with self.assertRaisesRegex(ValueError,"nest strictly"):
            cross.analyze(d[0],d[2],d[1])
        d=synthetic_reports()
        d[2]["video_roi"]=[115,115,60,75]
        d[2]["visual_component_candidates"][0]["roi"]=[115,115,60,75]
        with self.assertRaises(ValueError):
            cross.analyze(*d)

    def test_tight_component_not_present_in_focus_fails_closed(self):
        d=synthetic_reports()
        d[1]["visual_component_candidates"][1]["components"][0][
            "blue_centroid_y_milli"]=161000
        with self.assertRaisesRegex(ValueError,"not identical"):
            cross.analyze(*d)

    def test_player_candidate_touching_roi_edge_refused(self):
        d=synthetic_reports()
        d[2]["visual_component_candidates"][0]["components"][0][
            "touches_roi_boundary"]=True
        with self.assertRaisesRegex(ValueError,"candidate"):
            cross.analyze(*d)

    def test_wide_static_color_component_changed_between_frames_refused(self):
        d=synthetic_reports()
        d[0]["visual_component_candidates"][3]["components"][0][
            "blue_pixels"]=29
        with self.assertRaisesRegex(ValueError,"wide color region changed"):
            cross.analyze(*d)

    def test_injected_original_ram_or_frame_bytes_refused(self):
        for where in ("top","numeric","component"):
            d=synthetic_reports()
            if where=="top":
                d[0]["original_rom_bytes"] = [42]
            elif where=="numeric":
                d[1]["frames"][0]["raw_ram"] = [0,1]
            else:
                d[2]["visual_component_candidates"][2]["components"][0][
                    "frame_pixels"]= [255,255,255]
            with self.subTest(where=where),self.assertRaises(ValueError):
                cross.analyze(*d)

    def test_empty_candidate_or_frame_list_fails_closed(self):
        d=synthetic_reports()
        d[2]["visual_component_candidates"][0]["components"]=[]
        d[2]["visual_component_candidates"][0]["reported_component_count"]=0
        d[2]["visual_component_candidates"][0]["eligible_component_count"]=0
        d[2]["visual_component_candidates"][0]["all_blue_component_count"]=0
        with self.assertRaisesRegex(ValueError,"missing candidate"):
            cross.analyze(*d)

    def test_extra_blue_region_persists_in_focus_refused(self):
        d=synthetic_reports()
        # A copied wide component physically outside FOCUS must not be
        # accepted even if it is included in an otherwise well-formed JSON.
        extra=copy.deepcopy(d[0]["visual_component_candidates"][0]["components"][0])
        focus=d[1]["visual_component_candidates"][0]
        focus["components"].append(extra)
        focus["reported_component_count"]=2
        focus["eligible_component_count"]=2
        focus["all_blue_component_count"]=2
        focus["blue_pixel_count"]=40
        d[1]["frames"][0]["blue_pixel_count"]=40
        with self.assertRaises(ValueError):
            cross.analyze(*d)

    def test_cli_does_not_echo_original_numeric_report_path_on_error(self):
        with tempfile.TemporaryDirectory(prefix="qs209-") as tmp:
            path=Path(tmp)/"private_original_numbers.json"
            path.write_text("{}")
            cmd=[sys.executable,
                 str(ROOT/"tools/genesis_private_cross_roi_consistency.py"),
                 "--bundle",tmp, "--wide",str(path),
                 "--focus",str(path), "--tight",str(path)]
            process=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
            self.assertNotEqual(process.returncode,0)
            self.assertEqual(process.stdout,"")
            self.assertEqual(process.stderr,
                "GENESIS_STAGE209_PRIVATE_CROSS_ROI_NUMERIC_REJECTED\n")
            self.assertNotIn(str(path),process.stderr)


if __name__=="__main__":
    unittest.main()
