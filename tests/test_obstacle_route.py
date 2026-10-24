"""Stage 91: reject fabricated, incomplete or discontinuous controller routes."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_obstacle_route import verify

def scenario():
    data=[]
    for t in range(0,375,3):
        x=40+t*1.625
        mask=2
        if 146<=t<=166 or 220<=t<=240:mask=6
        data.append(dict(seq=str(len(data)),frame=str(800+t),attempt=str(t),
                         x_milli=str(int(x*1000)),y_milli="188000",
                         grounded="0" if 175<=t<=196 else "1",
                         input_bits=str(mask),checkpoint="1" if t>=363 else "0",
                         mode="1",source="live_controller_qs_step"))
    return data

class TestObstacle(unittest.TestCase):
    def setUp(self):self.rows=scenario()
    def test_valid_sequence(self):self.assertGreater(verify(self.rows)["checkpoint_tick"],0)
    def test_host_delayed_pit_button_seen_at_313_pixels(self):
        # Model the real profile-5 NRO witness: physical B begins only
        # after the host's valid x=287px pre-send anchor.
        for row in self.rows:
            x=int(row["x_milli"])
            if 275000<=x<310000:
                row["input_bits"]="2"
            elif 311000<=x<=318000:
                row["input_bits"]="6"
        self.assertLess(0,verify(self.rows)["pit_jump_tick"])

    def test_too_late_pit_button_cannot_satisfy_witness(self):
        for row in self.rows:
            x=int(row["x_milli"])
            if 275000<=x<=318000:
                row["input_bits"]="2"
            elif 325000<=x<=337000:
                row["input_bits"]="6"
        with self.assertRaisesRegex(ValueError,"event missing"):
            verify(self.rows)

    def test_wrong_provenance(self):
        self.rows[1]["source"]="qs_step_simulated_inputs"
        with self.assertRaisesRegex(ValueError,"forged source"):verify(self.rows)
    def test_prearmed_checkpoint(self):
        self.rows[0]["checkpoint"]="1"
        with self.assertRaisesRegex(ValueError,"preactivated"):verify(self.rows)
    def test_missing_jump(self):
        for r in self.rows:r["input_bits"]="2"
        with self.assertRaisesRegex(ValueError,"event missing"):verify(self.rows)
    def test_missing_wall(self):
        self.rows=self.rows[:80]
        for i,r in enumerate(self.rows):r["seq"]=str(i)
        with self.assertRaisesRegex(ValueError,"sustained"):verify(self.rows)
    def test_teleport(self):
        self.rows[55]["x_milli"]="40000"
        with self.assertRaisesRegex(ValueError,"teleport|respawn"):verify(self.rows)
    def test_duplicate_frame(self):
        self.rows[45]["frame"]=self.rows[44]["frame"]
        with self.assertRaisesRegex(ValueError,"nonmonotone"):verify(self.rows)
if __name__=="__main__":unittest.main()
