"""Stage 94: reject missing, copied, unstable or non-live CI gamepad runs."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_nexium_repeats import compare

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
        with self.assertRaisesRegex(ValueError,"unstable roof rise"):
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
    def test_blocked_roof_without_recovery(self):
        self.runs[1]["roof_path"]="ground_then_wall_recovery"
        with self.assertRaisesRegex(ValueError,"blocked roof"):
            compare(self.runs)
    def test_checkpoint_teleport(self):
        self.runs[1]["checkpoint_x_px"]=920
        with self.assertRaisesRegex(ValueError,"position"):
            compare(self.runs)

if __name__=="__main__":
    unittest.main()
