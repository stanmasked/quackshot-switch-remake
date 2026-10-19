"""Stage 94: reject missing, copied, unstable or non-live CI gamepad runs."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_nexium_repeats import compare, _align_jump_milestone

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
           source="live NeXium controller; independent CI guest NRO")
      for i in (1,2,3)]


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
