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
    def test_checkpoint_teleport(self):
        self.runs[1]["checkpoint_x_px"]=920
        with self.assertRaisesRegex(ValueError,"position"):
            compare(self.runs)

if __name__=="__main__":
    unittest.main()
