"""Stage 94: reject missing, copied, unstable or non-live CI gamepad runs."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_nexium_repeats import compare, _align_jump_milestone, _verify_spring_heart_nudge

DIGEST="b"*64
def three():
    return [
      dict(run_id=i,nro_sha256=DIGEST,pit_jump_tick=190+i,
           pit_cross_tick=244+i,roof_jump_tick=260+i,
           wall_cross_tick=379+i,checkpoint_tick=417+i,
           shot_tick=594+i,crate_broken_tick=601+i,
           spring_tick=727+i,heart_tick=741+i,
           ledge_tick=801+i,best_clear_frames=825+i,
           heart_and_ledge_samples=35,checkpoint_x_px=628.375,
           spring_x_px=1132.125,ledge_x_px=1188.531,
           input_delay_ms={1:0,2:25,3:45}[i],
           poll_interval_ms={1:85,2:90,3:95}[i],
           guest_frame_windows={"pit":1+i,"roof":3+i,"crate":2+i},
           guest_hold_frames={"B":[22,23],"Y":[12]},
           guest_jump_arcs={
             "pit":{"rise_px":64.258,"flight_frames":57,"landing_x_px":383.0,
                    "landing_y_px":188.0},
             "roof":{"rise_px":64.258,"flight_frames":32,"landing_x_px":423.0,
                     "landing_y_px":124.0,"landing_frame":283}},
           roof_path="direct_sky_ledge",
           wall_recovery_arc=None,
           wall_recovery_used=False,
           spring_heart_alignment_used=False,
           source="live NeXium controller; independent CI guest NRO")
      for i in (1,2,3)]




class SkyLedgeControllerBrakeTests(unittest.TestCase):
    """Stage 120: check the actual host key-release gate at the sky ledge.

    A 90ms poll can advance several NRO frames before the host releases
    RIGHT. The recovered run #4 overshot the ledge (X=1206.687), unlike
    successful run #5 (landing X=1198.562). Preserve both as evidence.
    """

    @staticmethod
    def gate(x, mode="1", step="7"):
        import os
        import re
        import subprocess
        script=(Path(__file__).resolve().parents[1] /
                "tools" / "nexium_ci_probe.sh").read_text(encoding="utf-8")
        match=re.search(r'(?ms)^\s*elif \[ "\$step" -eq 7 \].*?; then',
                        script)
        if match is None:
            raise AssertionError("missing CI sky-ledge key-release gate")
        shell=match.group(0).strip().replace("elif ","if ",1)+ " echo PASS; fi"
        result=subprocess.run(["bash","-c",shell],
                              env=dict(os.environ,step=str(step),
                                       fin_x=str(x),fin_mode=str(mode)),
                              text=True,capture_output=True,check=True)
        return result.stdout.strip()=="PASS"

    def test_release_at_early_sample_of_failed_guest_run(self):
        # Run #4: frame 1590, X=1174.656; old gate at 1180px
        # missed this moment and released only at X=1194.156.
        self.assertTrue(self.gate(1174656))

    def test_release_before_last_successful_ledge_position(self):
        # The other blocked-roof run landed at X=1198.562.
        self.assertTrue(self.gate(1181156))
        self.assertTrue(self.gate(1190906))

    def test_do_not_release_early_or_in_wrong_state(self):
        self.assertFalse(self.gate(1170999))
        self.assertFalse(self.gate(1171000,mode="0"))
        self.assertFalse(self.gate(1185000,step="6"))
        self.assertFalse(self.gate("not-a-position"))

class SpringControllerShellGateTests(unittest.TestCase):
    """Stage 111: execute the actual CI shell gate, not a Python imitation."""

    @staticmethod
    def probe(**overrides):
        import os
        import re
        import subprocess
        script=(Path(__file__).resolve().parents[1] /
                "tools" / "nexium_ci_probe.sh").read_text(encoding="utf-8")
        match=re.search(
            r'(?ms)^\s*elif \[ "\$step" -eq 6 \] && '
            r'\[ "\$heart_nudge_done" -eq 0 \].*?; then',
            script)
        if not match:
            raise AssertionError("CI script has no spring heart gate")
        # Reuse Bash's own condition language: the original elif has
        # the same semantics as if within a standalone fixture.
        shell=match.group(0).strip().replace("elif ","if ",1)+" echo PASS; fi"
        profile=dict(step="6",heart_nudge_done="0",fin_heart="0",
                     fin_mode="1",fin_ground="0",fin_checkpoint="1",
                     fin_crate="0",fin_ledge="0",
                     fin_source="live_controller_qs_step",
                     fin_x="1139781",fin_y="100000")
        profile.update({k:str(v) for k,v in overrides.items()})
        result=subprocess.run(["bash","-c",shell],
                              env=dict(os.environ,**profile),
                              capture_output=True,text=True,check=True)
        return result.stdout.strip()=="PASS"

    def test_real_spring_flight_permits_one_nudge(self):
        self.assertTrue(self.probe())

    def test_grounded_or_stale_guest_data_rejected(self):
        for values in ({"fin_ground":"1"},
                       {"fin_source":"unknown"},
                       {"fin_checkpoint":"0"},
                       {"fin_crate":"1"},
                       {"fin_ledge":"1"}):
            with self.subTest(values=values):
                self.assertFalse(self.probe(**values))

    def test_heart_collected_or_already_nudged_rejected(self):
        self.assertFalse(self.probe(fin_heart="1"))
        self.assertFalse(self.probe(heart_nudge_done="1"))

    def test_out_of_bounds_and_invalid_coordinates_rejected(self):
        for values in ({"fin_x":"1136999"},
                       {"fin_x":"1141001"},
                       {"fin_y":"49999"},
                       {"fin_y":"160001"},
                       {"fin_x":"unknown"}):
            with self.subTest(values=values):
                self.assertFalse(self.probe(**values))


class SpringHeartAlignmentTests(unittest.TestCase):
    """Stage 110: do not mistake a host log line for real guest input."""

    def setUp(self):
        self.rows=[
            dict(frame="700",x_milli="1139781",y_milli="100000",
                 heart="0",mode="1",grounded="0",input_bits="0"),
            dict(frame="703",x_milli="1143031",y_milli="98000",
                 heart="0",mode="1",grounded="0",input_bits="2"),
            dict(frame="706",x_milli="1153031",y_milli="93000",
                 heart="1",mode="1",grounded="0",input_bits="2"),
        ]
        self.lines=("SPRING_REACHED_GUEST frame=680 x_milli=1130000\n"
                    "SPRING_HEART_ALIGN_GUEST frame=700 x_milli=1139781 y_milli=100000\n"
                    "AIR_HEART_COLLECTED_GUEST frame=706 x_milli=1153031\n")

    def test_real_guest_response(self):
        self.assertTrue(_verify_spring_heart_nudge(self.lines,self.rows))

    def test_optional_pulse_not_fabricated(self):
        self.assertFalse(_verify_spring_heart_nudge(
            "SPRING_REACHED_GUEST frame=680 x_milli=1130000\n",self.rows))

    def test_duplicate_nudge_rejected(self):
        line="SPRING_HEART_ALIGN_GUEST frame=700 x_milli=1139781 y_milli=100000\n"
        with self.assertRaisesRegex(ValueError,"repeated"):
            _verify_spring_heart_nudge(self.lines+line,self.rows)

    def test_forged_position_rejected(self):
        self.rows[0]["x_milli"]="1141000"
        with self.assertRaisesRegex(ValueError,"matching airborne"):
            _verify_spring_heart_nudge(self.lines,self.rows)

    def test_guest_never_pressed_right(self):
        self.rows[1]["input_bits"]="0"
        self.rows[2]["input_bits"]="0"
        with self.assertRaisesRegex(ValueError,"no timely guest"):
            _verify_spring_heart_nudge(self.lines,self.rows)

    def test_heart_collected_before_nudge(self):
        # An earlier first pickup cannot be legitimised by a later pulse
        # even if a corrupt trace repeats heart=0 at the marker.
        self.rows.insert(0,dict(frame="697",x_milli="1135000",
                                y_milli="104000",heart="1",mode="1",
                                grounded="0",input_bits="0"))
        with self.assertRaisesRegex(ValueError,"did not precede"):
            _verify_spring_heart_nudge(self.lines,self.rows)

    def test_spurious_grounded_witness(self):
        self.rows[0]["grounded"]="1"
        with self.assertRaisesRegex(ValueError,"matching airborne"):
            _verify_spring_heart_nudge(self.lines,self.rows)

    def test_unbracketed_pulse(self):
        with self.assertRaisesRegex(ValueError,"not bracketed"):
            _verify_spring_heart_nudge(self.lines.splitlines()[1]+"\n",self.rows)

class GuestMilestoneAlignmentTests(unittest.TestCase):
    def setUp(self):
        # The B rising edge at attempt 255 is real, but the hero crosses
        # the X=390 landmark only in the next 3-frame obstacle sample.
        self.edge={"nro_stage_tick":255,"nro_first_button_frame":1098}
        self.rows=[
            {"attempt":"254","frame":"1097","input_bits":"2","x_milli":"385000"},
            {"attempt":"255","frame":"1098","input_bits":"6","x_milli":"385000"},
            {"attempt":"258","frame":"1101","input_bits":"6","x_milli":"391000"}]
    def measure(self,tick=258):
        return _align_jump_milestone(self.edge,tick,self.rows,390000,437000)
    def test_real_stage104_roof_offset_allowed(self):
        self.assertEqual(self.measure(),3)
    def test_exact_stage_tick_allowed(self):
        self.edge["nro_stage_tick"]=258
        self.edge["nro_first_button_frame"]=1101
        self.assertEqual(self.measure(),0)
    def test_unbounded_delay_rejected(self):
        with self.assertRaisesRegex(ValueError,"contradict"):
            self.measure(261)
    def test_released_button_cannot_pass(self):
        self.rows[-1]["input_bits"]="2"
        with self.assertRaisesRegex(ValueError,"contradict"):
            self.measure()
    def test_witnessed_position_must_cross_landmark(self):
        self.rows[-1]["x_milli"]="389000"
        with self.assertRaisesRegex(ValueError,"contradict"):
            self.measure()
    def test_unknown_guest_edge_frame_rejected(self):
        self.edge["nro_first_button_frame"]=1100
        with self.assertRaisesRegex(ValueError,"contradict"):
            self.measure()
    def test_no_matching_milestone_sample_rejected(self):
        self.rows.pop()
        with self.assertRaisesRegex(ValueError,"contradict"):
            self.measure()

class RepeatedLiveRunsTests(unittest.TestCase):
    def setUp(self): self.runs=three()
    def test_three_completed_runs(self):
        data=compare(self.runs)
        self.assertEqual(data["repetitions"],3)
        self.assertEqual(data["milestone_spreads_frames"]["best_clear_frames"],2)
    def test_one_missing_run(self):
        with self.assertRaisesRegex(ValueError,"DISTINCT"):
            compare(self.runs[:2])
    def test_duplicate_runner_identity(self):
        self.runs[2]["run_id"]=2
        with self.assertRaisesRegex(ValueError,"DISTINCT"):
            compare(self.runs)
    def test_changed_nro(self):
        self.runs[1]["nro_sha256"]="c"*64
        with self.assertRaisesRegex(ValueError,"digest"):
            compare(self.runs)
    def test_fake_guest(self):
        self.runs[0]["source"]="qs_step_simulated_inputs"
        with self.assertRaisesRegex(ValueError,"non-live"):
            compare(self.runs)
    def test_unstable_finish(self):
        self.runs[2]["best_clear_frames"]=1000
        with self.assertRaisesRegex(ValueError,"unstable"):
            compare(self.runs)
    def test_unstable_heart(self):
        self.runs[2]["heart_tick"]=870
        self.runs[2]["ledge_tick"]=890
        self.runs[2]["best_clear_frames"]=920
        with self.assertRaisesRegex(ValueError,"unstable"):
            compare(self.runs)
    def test_bad_event_order(self):
        self.runs[1]["crate_broken_tick"]=590
        with self.assertRaisesRegex(ValueError,"chronology"):
            compare(self.runs)
    def test_non_numeric_tick(self):
        self.runs[2]["spring_tick"]="727"
        with self.assertRaisesRegex(ValueError,"numeric"):
            compare(self.runs)
    def test_missing_jump_arcs(self):
        self.runs[1]["guest_jump_arcs"]={}
        with self.assertRaisesRegex(ValueError,"jump arcs"):
            compare(self.runs)
    def test_large_jump_height_disagreement(self):
        self.runs[2]["guest_jump_arcs"]["roof"]["rise_px"]=67.4
        with self.assertRaisesRegex(ValueError,"unstable roof_direct rise"):
            compare(self.runs)
    def test_blocked_roof_with_verified_recovery(self):
        run=self.runs[1]
        run["roof_path"]="ground_then_wall_recovery"
        run["guest_jump_arcs"]["roof"]={
            "rise_px":27.43,"flight_frames":31,
            "landing_x_px":453.218,"landing_y_px":188.0,
            "landing_frame":311}
        run["wall_recovery_arc"]={
            "rise_px":64.454,"flight_frames":56,
            "landing_x_px":605.281,"landing_y_px":188.0,
            "pressed_frame":381}
        run["wall_recovery_used"]=True
        run["guest_hold_frames"]["B"].append(22)
        self.assertEqual(compare(self.runs)["roof_path_counts"]["ground_then_wall_recovery"],1)
    def test_direct_roof_rejects_recovery_summary(self):
        run=self.runs[1]
        run["wall_recovery_arc"]={
            "rise_px":64.454,"flight_frames":56,
            "landing_x_px":605.281,"landing_y_px":188.0,
            "pressed_frame":381}
        run["wall_recovery_used"]=True
        run["guest_hold_frames"]["B"].append(22)
        with self.assertRaisesRegex(ValueError,"direct roof cannot"):
            compare(self.runs)
    def test_blocked_roof_without_recovery(self):
        self.runs[1]["roof_path"]="ground_then_wall_recovery"
        with self.assertRaisesRegex(ValueError,"blocked roof"):
            compare(self.runs)
    def test_archived_stage98_physics_routes(self):
        # Reduced, non-proprietary landmarks from three real NeXium
        # guest-obstacle CSVs. Two runs took the direct roof; the
        # delayed-input run was blocked and jumped again at the wall.
        actual=[
          {"pit":(64.258,57,376.843),
           "roof":(64.258,33,423.187)},
          {"pit":(64.258,57,380.093),
           "roof":(27.149,28,453.218)},
          {"pit":(64.454,55,378.468),
           "roof":(64.258,33,423.187)},
        ]
        for run,record in zip(self.runs,actual):
            pit_height,pit_frames,pit_x=record["pit"]
            roof_height,roof_frames,roof_x=record["roof"]
            run["guest_jump_arcs"]["pit"].update(
                rise_px=pit_height,flight_frames=pit_frames,
                landing_x_px=pit_x,landing_y_px=188.0)
            run["guest_jump_arcs"]["roof"].update(
                rise_px=roof_height,flight_frames=roof_frames,
                landing_x_px=roof_x,landing_y_px=124.0)
        delayed=self.runs[1]
        delayed["roof_path"]="ground_then_wall_recovery"
        delayed["guest_jump_arcs"]["roof"].update(
            landing_y_px=188.0,landing_frame=1111)
        delayed["wall_recovery_arc"]={
            "rise_px":64.454,"flight_frames":56,
            "landing_x_px":605.281,"landing_y_px":188.0,
            "pressed_frame":1181}
        delayed["wall_recovery_used"]=True
        delayed["guest_hold_frames"]["B"].append(24)
        report=compare(self.runs)
        self.assertEqual(report["roof_path_counts"],
                         {"direct_sky_ledge":2,
                          "ground_then_wall_recovery":1})
        self.assertEqual(report["guest_jump_arc_spreads"]["pit"]["rise_px"],0.196)
        self.assertEqual(report["guest_jump_arc_spreads"]["roof_direct"]["rise_px"],0)
        self.assertEqual(report["guest_jump_arc_spreads"]["roof_blocked"]["flight_frames"],0)

    def test_archived_blocked_roof_cannot_claim_direct_route(self):
        self.runs[1]["guest_jump_arcs"]["roof"].update(
            rise_px=27.149,flight_frames=28,
            landing_x_px=453.218,landing_y_px=188.0)
        # Without the authenticated recovery this must fail closed:
        # merely completing the level cannot substitute for a jump.
        with self.assertRaisesRegex(ValueError,"direct roof landing"):
            compare(self.runs)

    def test_checkpoint_teleport(self):
        self.runs[1]["checkpoint_x_px"]=920
        with self.assertRaisesRegex(ValueError,"position"):
            compare(self.runs)

if __name__=="__main__":
    unittest.main()
