"""Stage201 ROM-free VDP callback-vs-post-run bracket regression.

Uses synthetic libretro frames and invented numerical HScroll. Never reads
an actual Genesis ROM or pixel asset.
"""
import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import genesis_city_video_vdp_bracket_probe as probe
from genesis_libretro_probe import run_core
from test_genesis_visual_roi_numeric import SyntheticBlueVideoCore
from test_genesis_city_same_step_triplet_probe import fixture, line


def identical_bracket():
    during={frame:[-90,-90,-90,-90,-45,-45,-45,-45]
            for frame in probe.MARKERS}
    return during,copy.deepcopy(during)


class Stage201Brackets(unittest.TestCase):
    def test_identical_numerical_bracket_does_not_attest_cpu_scanout(self):
        result=probe.phase_bracket(*identical_bracket())
        self.assertFalse(result["changed_hscroll_at_any_observation_boundary"])
        self.assertFalse(result["equal_values_prove_same_cpu_vdp_scanout_phase"])
        self.assertFalse(result["original_collision_or_sprite_identity_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])
        self.assertEqual([r["libretro_frame"] for r in result["checks"]],
                         list(probe.MARKERS))

    def test_hscroll_change_detected_within_a_single_libretro_index(self):
        before,after=identical_bracket()
        after[2220][2]=-92
        after[2220][5]=-46
        report=probe.phase_bracket(before,after)
        self.assertTrue(report["changed_hscroll_at_any_observation_boundary"])
        changed=next(x for x in report["checks"] if x["libretro_frame"]==2220)
        self.assertEqual(changed["plane_a_deltas_by_scanline"],[0,0,-2,0])
        self.assertEqual(changed["plane_b_deltas_by_scanline"],[0,-1,0,0])
        self.assertFalse(changed["all_plane_scroll_values_equal_at_two_observation_points"])

    def test_missing_extra_and_out_of_range_vdp_samples_fail_closed(self):
        for bad in ("missing","extra","bool","short","out_of_range","bytes"):
            before,after=identical_bracket()
            if bad=="missing":
                del before[2280]
            elif bad=="extra":
                after[2281]=[0]*8
            elif bad=="bool":
                after[2220][0]=False
            elif bad=="short":
                before[2180]=[0]*7
            elif bad=="out_of_range":
                before[2220][0]=-1025
            else:
                before[2220]=b"fake"
            with self.subTest(bad=bad),self.assertRaises(ValueError):
                probe.phase_bracket(before,after)

    def test_libretro_hook_invoked_in_video_callback_for_requested_frames_only(self):
        fake=SyntheticBlueVideoCore()
        frames=[]
        def snapshot(frame):
            frames.append(frame)
            return tuple([-frame]*4+[frame]*4)
        result=run_core(
            fake,{"need_fullpath":False},b"SYNTHETIC","fake",4,[],
            video_roi=(0,0,4,3),video_roi_frames=(0,2),
            video_roi_vdp_hook=snapshot,video_stride=3)
        self.assertEqual(frames,[0,2])
        self.assertEqual(result["video_roi_vdp_numeric"],[
            {"frame":0,"plane_scroll":[0]*8},
            {"frame":2,"plane_scroll":[-2]*4+[2]*4}])
        self.assertEqual([x["frame"] for x in result["video_roi_blue_numeric"]],
                         [0,2])
        self.assertFalse(fake.initialized)

    def test_wrong_hook_output_cannot_escape_ctypes_callback(self):
        for response in (
            b"fake source RAM bytes", {"raw_vram":"bad"},
            [0]*7,[0]*7+[True],[-1025]*8,
        ):
            fake=SyntheticBlueVideoCore()
            with self.subTest(response_type=type(response).__name__):
                with self.assertRaisesRegex(RuntimeError,"ROI callback"):
                    run_core(
                        fake,{"need_fullpath":False},b"FAKE","fake",2,[],
                        video_roi=(0,0,4,3),video_roi_frames=(1,),
                        video_roi_vdp_hook=lambda _frame:response)
            self.assertFalse(fake.initialized)

    def test_callback_failure_and_duplicate_video_fail_closed(self):
        for video_counts in ({1:0},{1:2}):
            fake=SyntheticBlueVideoCore(video_counts=video_counts)
            with self.subTest(counts=video_counts):
                with self.assertRaisesRegex(RuntimeError,"ROI callback"):
                    run_core(
                        fake,{"need_fullpath":False},b"FAKE","fake",3,[],
                        video_roi=(0,0,4,3),video_roi_frames=(1,),
                        video_roi_vdp_hook=lambda _f:[0]*8)
            self.assertFalse(fake.initialized)

    def test_vdp_hook_requires_valid_roi_before_starting_emulator(self):
        bad=(
            {"video_roi":None,"video_roi_vdp_hook":lambda _: [0]*8},
            {"video_roi":(0,0,4,3),"video_roi_frames":[1],
             "video_roi_vdp_hook":"not_callable"},
        )
        for kwargs in bad:
            with self.assertRaises(ValueError):
                run_core(None,{"need_fullpath":False},b"FAKE","fake",
                         3,[],**kwargs)

    def test_private_triplet_only_declares_bracket_observations(self):
        source,reads,digests,roi=fixture()
        calls=[]
        state={"scroll":0}
        def fake_ram_frame(row):
            ram=bytearray(65536)
            for off,num in ((0xA2D0,row["player_world_x"]),
                            (0xA2D4,row["player_world_y"]),
                            (0x8524,row["camera_ram"]),
                            (0x852C,row["render_buffer_ram"])):
                ram[off:off+2]=num.to_bytes(2,"little")
            return bytes(ram)
        def vdp_row(_hook, scanline):
            return line(state["scroll"],scanline)
        def fake_run(*args,**kwargs):
            label=("idle","right","right_repeat")[len(calls)]
            calls.append(label)
            visual=[];sideband=[]
            for frame in probe.MARKERS:
                sample=source[label][frame]
                state["scroll"]=sample["render_buffer_ram"]
                during=kwargs["video_roi_vdp_hook"](frame)
                sideband.append({"frame":frame,"plane_scroll":list(during)})
                kwargs["checkpoint_sink"](frame,fake_ram_frame(sample))
                visual.append({"frame":frame,**sample["blue_visual"]})
            return {
                "frames_video_callbacks":probe.TOTAL,
                "frames_without_video_callback":0,
                "frames_with_multiple_video_callbacks":0,
                "video_callbacks_outside_retro_run":0,
                "ram_checkpoint_phase":"post_retro_run_before_next_input",
                "video_callback_phase":"during_retro_run",
                "video_roi_blue_numeric":visual,
                "video_roi_vdp_numeric":sideband,
                "input_nonzero_reads":reads[label],
                "video_checkpoints":digests[label],
            }
        guards=[]
        with mock.patch.object(probe,"bind_vdp_probe",return_value="fake"),\
             mock.patch.object(probe,"vdp_number_snapshot",side_effect=vdp_row),\
             mock.patch.object(probe,"run_core",side_effect=fake_run):
            report=probe.collect(object(),{},Path("/not-original"),
                                 b"synthetic",roi,lambda:guards.append(True))
        self.assertEqual(calls,["idle","right","right_repeat"])
        self.assertEqual(len(guards),6)
        self.assertFalse(report["vdp_callback_vs_post_run"][
            "changed_hscroll_at_any_observation_boundary"])
        self.assertFalse(report["safe_to_modify_switch_physics"])
        self.assertFalse(report["original_cpu_vdp_scanout_phase_verified"])
        self.assertNotIn("vdp_scanlines",str(report))


if __name__=="__main__":
    unittest.main()
