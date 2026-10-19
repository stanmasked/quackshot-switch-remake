"""Stage 98: prove actual rise/apex/landing, reject fake/shortened jumps."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_jump_arc import verify_jump_arcs


def fixture():
    host=("PIT_LAUNCH_GUEST frame=100 x_milli=280000\n"
          "ROOF_LAUNCH_GUEST frame=180 x_milli=390000\n"
          "CRATE_FIRE_GUEST frame=300 x_milli=912000")
    rows=[]
    for frame in range(330):
        x=280000 if frame<=100 else (
          280000+1900*(frame-100) if frame<=157 else
          390000 if frame<=180 else
          390000+1000*(frame-180) if frame<=213 else 912000)
        if 103<=frame<157:
            elapsed=frame-103
            if elapsed<=27:
                y=185070-int(61328*elapsed/27)
            else:
                y=123742+int(64258*(elapsed-27)/26)
            grounded=0
        elif 184<=frame<213:
            elapsed=frame-184
            if elapsed<=27:
                y=185070-int(61328*elapsed/27)
            else:
                y=123742+int(200*(elapsed-27))
            grounded=0
        else:
            y=124000 if 213<=frame<300 else 188000
            grounded=1
        bits=2+(4 if (103<=frame<=112 or 184<=frame<=195) else 0)+(
              8 if 304<=frame<=313 else 0)
        rows.append(dict(seq=str(frame),frame=str(frame),attempt=str(frame),
                    x_milli=str(x),y_milli=str(y),grounded=str(grounded),
                    input_bits=str(bits),checkpoint="0",mode="1",
                    source="live_controller_qs_step"))
    return host,rows


class GuestArcTests(unittest.TestCase):
    def setUp(self):self.host,self.rows=fixture()
    def test_complete_pit_and_roof_arcs(self):
        r=verify_jump_arcs(self.host,self.rows)
        self.assertEqual(tuple(r["arcs"]),("pit","roof"))
        self.assertEqual(r["arcs"]["roof"]["landing_y_px"],124.0)
    def test_apex_missing(self):
        for row in self.rows[103:157]:row["y_milli"]="184000"
        with self.assertRaisesRegex(ValueError,"height|ascent"):
            verify_jump_arcs(self.host,self.rows)
    def test_no_ground_landing(self):
        for row in self.rows[157:184]:row["grounded"]="0"
        with self.assertRaisesRegex(ValueError,"landed"):
            verify_jump_arcs(self.host,self.rows)
    def test_roof_lands_on_wrong_surface(self):
        self.rows[213]["y_milli"]="188000"
        with self.assertRaisesRegex(ValueError,"landing"):
            verify_jump_arcs(self.host,self.rows)
    def test_apex_starts_with_physics_teleport(self):
        for row in self.rows[104:130]:row["y_milli"]="123742"
        with self.assertRaisesRegex(ValueError,"ascent"):
            verify_jump_arcs(self.host,self.rows)
    def test_no_horizontal_travel(self):
        for row in self.rows[103:158]:row["x_milli"]="281900"
        with self.assertRaisesRegex(ValueError,"landing"):
            verify_jump_arcs(self.host,self.rows)
    def test_airborne_position_teleport(self):
        self.rows[142]["x_milli"]="999999"
        with self.assertRaisesRegex(ValueError,"teleport"):
            verify_jump_arcs(self.host,self.rows)
    def test_input_jumps_are_required(self):
        for row in self.rows[103:113]:row["input_bits"]="2"
        with self.assertRaises(ValueError):verify_jump_arcs(self.host,self.rows)
    def test_lost_guest_frame_rejected(self):
        self.rows[130]["frame"]="160"
        with self.assertRaises(ValueError):verify_jump_arcs(self.host,self.rows)


if __name__=="__main__":unittest.main()
