"""Stage 97: reject stuck, duplicate and uncommanded guest B/Y pulses."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_guest_button_pulses import verify_pulses


def fixture(wall=False):
    host=("PIT_LAUNCH_GUEST frame=100 x_milli=280000\n"
          "ROOF_LAUNCH_GUEST frame=180 x_milli=390000\n"
          +("WALL_RECOVERY_GUEST frame=225 x_milli=548000\n" if wall else "")+
          "CRATE_FIRE_GUEST frame=300 x_milli=912000")
    rows=[]
    for frame in range(330):
        x=(280000 if frame<=113 else 390000 if frame<=195 else
           548000 if frame<=241 else 912000)
        b=(103<=frame<=112 or 184<=frame<=195 or
           (wall and 228<=frame<=239))
        y=304<=frame<=313
        mask=2+(4 if b else 0)+(8 if y else 0)
        rows.append(dict(seq=str(frame),frame=str(frame),attempt=str(frame),
                         x_milli=str(x),y_milli="185070" if b else "188000",
                         grounded="0" if b else "1",input_bits=str(mask),
                         checkpoint="0",mode="1",source="live_controller_qs_step"))
    return host,rows


class GuestPulseTests(unittest.TestCase):
    def setUp(self):self.host,self.rows=fixture()
    def test_exact_two_jumps_and_one_shot(self):
        result=verify_pulses(self.host,self.rows)
        self.assertEqual(result["guest_press_counts"],{"B":2,"Y":1})
    def test_legitimate_wall_recovery(self):
        self.host,self.rows=fixture(wall=True)
        result=verify_pulses(self.host,self.rows)
        self.assertEqual(result["guest_press_counts"]["B"],3)
        self.assertEqual(result["wall_recovery"]["guest_button_frame"],228)
    def test_stuck_y(self):
        for row in self.rows:
            if int(row["frame"])>=304:row["input_bits"]="10"
        with self.assertRaisesRegex(ValueError,"stuck Y"):
            verify_pulses(self.host,self.rows)
    def test_extra_y_shot(self):
        for i in range(320,325):self.rows[i]["input_bits"]="10"
        with self.assertRaisesRegex(ValueError,"uncommanded or repeated Y"):
            verify_pulses(self.host,self.rows)
    def test_extra_jump_without_recovery(self):
        for i in range(230,242):
            self.rows[i]["input_bits"]="6"
            self.rows[i]["y_milli"]="185070"
            self.rows[i]["grounded"]="0"
        with self.assertRaisesRegex(ValueError,"uncommanded or repeated B"):
            verify_pulses(self.host,self.rows)
    def test_wall_recovery_not_authenticated(self):
        self.host,self.rows=fixture(wall=True)
        self.host=self.host.replace("x_milli=548000","x_milli=940000")
        with self.assertRaisesRegex(ValueError,"not anchored"):
            verify_pulses(self.host,self.rows)
    def test_too_short_b_press(self):
        for i in range(109,113):
            self.rows[i]["input_bits"]="2"
            self.rows[i]["grounded"]="1"
        with self.assertRaisesRegex(ValueError,"abnormal guest frame duration"):
            verify_pulses(self.host,self.rows)
    def test_retrigger_during_press(self):
        self.rows[108]["input_bits"]="2"
        with self.assertRaisesRegex(ValueError,"uncommanded or repeated B"):
            verify_pulses(self.host,self.rows)
    def test_wall_recovery_marker_missing(self):
        self.host,self.rows=fixture(wall=True)
        self.host=self.host.replace("WALL_RECOVERY_GUEST","UNVERIFIED_WALL")
        with self.assertRaisesRegex(ValueError,"uncommanded or repeated B"):
            verify_pulses(self.host,self.rows)


if __name__=="__main__":
    unittest.main()
