"""Stage 107: late-roof runs must really be two grounded wall recoveries."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_blocked_roof_repeats import compare_blocked

def sample(i):
    return {
        "run_id":i,
        "nro_sha256":"a"*64,
        "input_delay_ms":25 if i==4 else 45,
        "poll_interval_ms":90 if i==4 else 95,
        "roof_path":"ground_then_wall_recovery",
        "wall_recovery_used":True,
        "guest_hold_frames":{"B":[22,24,23],"Y":[12]},
        "guest_jump_arcs":{"roof":{"rise_px":27.0,"landing_y_px":188.0,
                                   "landing_frame":300}},
        "wall_recovery_arc":{"rise_px":64.258,"landing_x_px":605.0,
                             "pressed_frame":370},
        "best_clear_frames":830+i,
    }

class BlockedRoofTests(unittest.TestCase):
    def setUp(self):self.runs=[sample(4),sample(5)]
    def test_two_real_wall_routes(self):
        self.assertEqual(compare_blocked(self.runs)["roof_path_counts"]["ground_then_wall_recovery"],2)
    def test_missing_second_run(self):
        with self.assertRaisesRegex(ValueError,"DISTINCT"):
            compare_blocked(self.runs[:1])
    def test_duplicate_run_id(self):
        self.runs[1]["run_id"]=4
        with self.assertRaisesRegex(ValueError,"DISTINCT"):
            compare_blocked(self.runs)
    def test_false_direct_route(self):
        self.runs[1]["roof_path"]="direct_sky_ledge"
        with self.assertRaisesRegex(ValueError,"physically recover"):
            compare_blocked(self.runs)
    def test_missing_third_guest_b(self):
        self.runs[0]["guest_hold_frames"]["B"].pop()
        with self.assertRaisesRegex(ValueError,"three guest B"):
            compare_blocked(self.runs)
    def test_bad_wall_height(self):
        self.runs[0]["wall_recovery_arc"]["rise_px"]=20.0
        with self.assertRaisesRegex(ValueError,"impossible recovery"):
            compare_blocked(self.runs)
    def test_bad_landing(self):
        self.runs[0]["wall_recovery_arc"]["landing_x_px"]=555.0
        with self.assertRaisesRegex(ValueError,"impossible recovery"):
            compare_blocked(self.runs)
    def test_tampered_nro(self):
        self.runs[1]["nro_sha256"]="b"*64
        with self.assertRaisesRegex(ValueError,"digest mismatch"):
            compare_blocked(self.runs)
    def test_missing_delay(self):
        self.runs[1]["input_delay_ms"]=0
        with self.assertRaisesRegex(ValueError,"profile mismatch"):
            compare_blocked(self.runs)
    def test_unstable_finish(self):
        self.runs[1]["best_clear_frames"]=1030
        with self.assertRaisesRegex(ValueError,"unstable"):
            compare_blocked(self.runs)

if __name__=="__main__":
    unittest.main()
