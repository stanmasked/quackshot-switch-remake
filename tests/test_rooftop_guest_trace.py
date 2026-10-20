"""Regression corpus for fail-closed future NeXium ROOFTOP telemetry."""
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import COLUMNS,SOURCE,RooftopTraceError,verify

def make_valid():
    rows=[]
    for i in range(118):
        # 8..12 FIRE triggers an independently travelling projectile;
        # activation at tick 15, after x=693,000 and vx=5,500.
        fire=(i==8)
        if 9<=i<=15:
            shot_x=693000-(15-i)*5500
            shot_active=1
        else:
            shot_x=-1
            shot_active=0
        right=i>=16
        x=640000+3000*(i-15) if right else 640000
        values=dict(seq=i,frame=1000+i,mode=1,stage=2,
            x_milli=x,y_milli=188000,grounded=1,
            input_bits=(8 if fire else 0)|(2 if right else 0),
            shot_x_before_milli=shot_x,
            shot_vx_before_milli=5500 if shot_active else 0,
            shot_red_before=0,shot_active_before=shot_active,
            shot_active_after=0 if i==15 else shot_active,
            bridge_before=int(i>=16),bridge_after=int(i>=15),
            bridge_cells=6*int(i>=15),source=SOURCE)
        rows.append({k:str(values[k]) for k in COLUMNS})
    return rows

class RooftopGuestTraceTests(unittest.TestCase):
    def test_valid_complete_trace(self):
        report=verify(make_valid())
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(report["trigger_frame"],1015)
    def bad(self,mutator):
        rows=make_valid()
        mutator(rows)
        with self.assertRaises(RooftopTraceError):
            verify(rows)
    def test_no_fire_edge(self):
        self.bad(lambda rows: rows[8].update(input_bits="0"))
    def test_no_bullet_flight(self):
        self.bad(lambda rows: rows[13].update(shot_x_before_milli="-1",shot_active_before="0",shot_vx_before_milli="0"))
    def test_red_shot_forgery(self):
        self.bad(lambda rows: rows[15].update(shot_red_before="1"))
    def test_premature_bridge(self):
        self.bad(lambda rows: rows[10].update(bridge_after="1",bridge_cells="6"))
    def test_missing_six_solid_cells(self):
        self.bad(lambda rows: rows[15].update(bridge_cells="4"))
    def test_no_grounded_crossing(self):
        self.bad(lambda rows: [row.update(grounded="0") for row in rows[16:]])
    def test_teleported_player(self):
        self.bad(lambda rows: rows[30].update(x_milli="1100000"))
    def test_pre_bridge_teleport(self):
        self.bad(lambda rows: rows[14].update(x_milli="745000"))
    def test_projectile_position_ignores_actual_velocity(self):
        self.bad(lambda rows: rows[12].update(shot_x_before_milli="675000"))
    def test_duplicate_guest_frame(self):
        self.bad(lambda rows: rows[27].update(frame=rows[26]["frame"]))
    def test_wrong_stage(self):
        self.bad(lambda rows: rows[20].update(stage="0"))
    def test_wrong_source(self):
        self.bad(lambda rows: rows[20].update(source="simulated_fake_input"))
    def test_untrusted_boolean(self):
        self.bad(lambda rows: rows[5].update(bridge_after="9"))
    def test_no_shot_consumption(self):
        self.bad(lambda rows: rows[15].update(shot_active_after="1"))
    def test_bad_input_monotonicity(self):
        self.bad(lambda rows: rows[37].update(seq="12"))

if __name__=="__main__":
    unittest.main()
