"""Stage276 synthetic-only adversarial tests; no ROM or real guest data."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from verify_city_horizontal_variant import (
    GOLD,RED_PIXEL,NUMERIC,PIXELS,RED_FIELDS,NUMERIC_SOURCE,PIXEL_SOURCE,RED_SOURCE,
    HorizontalError,verify_variant_rows
)

def fixture(variant):
    red=1 if variant=="left-red" else 0
    direction=-1
    fire_i=60
    start_x=170000 if red else 130000
    shot_y=149000 if red else 197000
    active_frames=start_x//5500+1
    numeric=[]
    for i in range(120):
        active=int(fire_i+1<=i<fire_i+1+active_frames)
        age=i-(fire_i+1)
        obj=dict(seq=i,frame=1000+i,attempt=i+1,mode=1,stage=0,
                 fire=int(fire_i<=i<fire_i+8),up=0,grounded=1,
                 pending=int(i==fire_i),active=active,attached=0,
                 red=red if active else 0,
                 x_milli=start_x+5500*direction*age if active else -1,
                 y_milli=shot_y if active else -1,
                 vx_milli=5500*direction if active else 0,source=NUMERIC_SOURCE)
        numeric.append({k:str(obj[k]) for k in NUMERIC})
    color=RED_PIXEL if red else GOLD
    raster=[]
    for age in (0,4,8,16,22):
        row=numeric[fire_i+1+age]
        obj=dict(seq=len(raster),frame=int(row["frame"]),
                 x_milli=int(row["x_milli"]),y_milli=int(row["y_milli"]),
                 render_argb=color,bg_argb=0xff85bad4,readback_ok=1,
                 baseline_ok=1,restored_ok=1,source=PIXEL_SOURCE)
        raster.append({k:str(obj[k]) for k in PIXELS})
    red_rows=[]
    for i in range(120):
        unlocked=int(red and i>=35)
        before=int(red and i>35)
        pickup_before=0 if red and i>35 else 1
        pickup_after=0 if red and i>=35 else 1
        bits=2 if 5<=i<20 else (6 if red and 20<=i<=35 else 1 if (35<=i<50) else 0)
        if i==fire_i: bits=8
        obj=dict(seq=i,frame=1000+i,mode=1,stage=0,x_milli=start_x,
                 y_milli=188000,grounded=1,input_bits=bits,
                 red_before=before,red_after=unlocked,weapon=unlocked,
                 pickup_before=1 if i<=35 or not red else 0,
                 pickup_after=pickup_after,source=RED_SOURCE)
        if red and i==35:
            obj.update(red_before=0,red_after=1,weapon=1,pickup_before=1,pickup_after=0)
        red_rows.append({k:str(obj[k]) for k in RED_FIELDS})
    return numeric,raster,red_rows

class HorizontalVariant(unittest.TestCase):
    def test_left_yellow_valid(self):
        result=verify_variant_rows(*fixture("left-yellow"),"left-yellow")
        self.assertEqual(result["status"],"PASS")
        self.assertEqual(result["numeric"]["boundary_despawn"]["status"],"PASS")
        self.assertEqual(result["numeric"]["boundary_despawn"]["active_ticks"],24)
    def test_left_red_valid(self):
        result=verify_variant_rows(*fixture("left-red"),"left-red")
        self.assertEqual(result["status"],"PASS")
        self.assertEqual(result["numeric"]["boundary_despawn"]["status"],"PASS")
        self.assertEqual(result["numeric"]["boundary_despawn"]["active_ticks"],31)
    def reject(self,variant,change):
        n,p,r=fixture(variant)
        change(n,p,r)
        with self.assertRaises(HorizontalError):
            verify_variant_rows(n,p,r,variant)
    def test_yellow_wrong_direction(self):
        self.reject("left-yellow",lambda n,p,r:n[61].update(vx_milli="5500"))
    def test_yellow_wrong_color(self):
        self.reject("left-yellow",lambda n,p,r:n[61].update(red="1"))
    def test_red_wrong_color(self):
        self.reject("left-red",lambda n,p,r:n[61].update(red="0"))
    def test_red_wrong_height(self):
        self.reject("left-red",lambda n,p,r:n[61].update(y_milli="197000"))
    def test_missing_pickup_transition(self):
        self.reject("left-red",lambda n,p,r:[q.update(red_after="0",weapon="0") for q in r])
    def test_pickup_after_fire(self):
        self.reject("left-red",lambda n,p,r:[q.update(frame=str(int(q["frame"])+40)) for q in r if int(q["frame"])>=1035])
    def test_no_right_jump_approach(self):
        self.reject("left-red",lambda n,p,r:[q.update(input_bits="2") for q in r if 1020<=int(q["frame"])<=1035])
    def test_no_left_controller_input(self):
        self.reject("left-red",lambda n,p,r:[q.update(input_bits="0") for q in r if 1036<=int(q["frame"])<1060])
    def test_red_pixel_color(self):
        self.reject("left-red",lambda n,p,r:p[1].update(render_argb=str(GOLD)))
    def test_background_contains_expected_color(self):
        self.reject("left-red",lambda n,p,r:p[2].update(bg_argb=str(RED_PIXEL)))
    def test_unrestored_renderer(self):
        self.reject("left-yellow",lambda n,p,r:p[1].update(restored_ok="0"))
    def test_forged_numeric_source(self):
        self.reject("left-yellow",lambda n,p,r:n[70].update(source="host_predicted"))
    def test_forged_pickup_source(self):
        self.reject("left-red",lambda n,p,r:r[70].update(source="synthetic"))
    def test_frame_gap(self):
        self.reject("left-yellow",lambda n,p,r:n[70].update(frame="9999"))
    def test_duplicate_fire(self):
        self.reject("left-yellow",lambda n,p,r:n[90].update(fire="1"))
    def test_no_fire(self):
        self.reject("left-red",lambda n,p,r:n[60].update(fire="0"))
    def test_premature_despawn_before_left_edge(self):
        def change(n,p,r):
            n[84].update(active="0",attached="0",red="0",x_milli="-1",y_milli="-1",vx_milli="0")
        self.reject("left-yellow",change)
    def test_projectile_persists_past_left_boundary(self):
        def change(n,p,r):
            n[85].update(active="1",attached="0",red="0",x_milli="0",y_milli="197000",vx_milli="-5500")
        self.reject("left-yellow",change)
    def test_boundary_last_position_drift(self):
        self.reject("left-yellow",lambda n,p,r:n[84].update(x_milli="6000"))
    def test_projectile_reappears_after_boundary(self):
        def change(n,p,r):
            n[86].update(active="1",attached="0",red="0",x_milli="0",y_milli="197000",vx_milli="-5500")
        self.reject("left-yellow",change)
    def test_unknown_variant(self):
        n,p,r=fixture("left-yellow")
        with self.assertRaises(HorizontalError): verify_variant_rows(n,p,r,"right-red")

if __name__=="__main__":
    unittest.main()
