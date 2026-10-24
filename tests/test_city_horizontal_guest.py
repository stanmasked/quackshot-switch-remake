"""Stage275: synthetic adversarial tests; no ROM, no guest traces embedded."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_city_horizontal_guest import (
    NUMERIC,PIXELS,NUMERIC_SOURCE,PIXEL_SOURCE,GOLD,
    HorizontalError,verify_numeric,verify_pixels,
)

def fixture():
    fire=12
    rows=[]
    for i in range(120):
        active=int(fire+1<=i<fire+61)
        a=i-(fire+1)
        data=dict(seq=i,frame=800+i,attempt=i+1,mode=1,stage=0,
                  fire=int(fire<=i<fire+8),up=0,grounded=1,
                  pending=int(i==fire),active=active,attached=0,red=0,
                  x_milli=(57500+5500*a) if active else -1,
                  y_milli=197000 if active else -1,
                  vx_milli=5500 if active else 0,source=NUMERIC_SOURCE)
        rows.append({k:str(data[k]) for k in NUMERIC})
    raster=[]
    for age in (0,4,8,16,22):
        p=rows[fire+1+age]
        data=dict(seq=len(raster),frame=int(p["frame"]),
                  x_milli=int(p["x_milli"]),y_milli=int(p["y_milli"]),
                  render_argb=GOLD,bg_argb=0xff85bad4,
                  readback_ok=1,baseline_ok=1,restored_ok=1,
                  source=PIXEL_SOURCE)
        raster.append({k:str(data[k]) for k in PIXELS})
    return rows,raster

class GuestProof(unittest.TestCase):
    def test_valid(self):
        n,r=fixture()
        self.assertEqual(verify_numeric(n)["observed_positions"],15)
        self.assertEqual(verify_pixels(n,r)["negative_controls"],5)
    def reject(self,change,numeric=False):
        n,r=fixture()
        change(n,r)
        with self.assertRaises(HorizontalError):
            verify_numeric(n) if numeric else verify_pixels(n,r)
    def test_no_fire(self):
        self.reject(lambda n,r:n[12].update(fire="0"),True)
    def test_missing_delay(self):
        self.reject(lambda n,r:n[12].update(active="1"),True)
    def test_missing_pending(self):
        self.reject(lambda n,r:n[12].update(pending="0"),True)
    def test_early_shot(self):
        self.reject(lambda n,r:n[6].update(pending="1"),True)
    def test_wrong_velocity(self):
        self.reject(lambda n,r:n[16].update(vx_milli="6000"),True)
    def test_wrong_y(self):
        self.reject(lambda n,r:n[16].update(y_milli="0"),True)
    def test_dropped_frame(self):
        self.reject(lambda n,r:n[17].update(frame="820"),True)
    def test_horizontal_position_forged(self):
        self.reject(lambda n,r:n[24].update(x_milli="120000"),True)
    def test_second_fire_edge(self):
        self.reject(lambda n,r:n[55].update(fire="1"),True)
    def test_forged_game_source(self):
        self.reject(lambda n,r:n[30].update(source="host_draw_prediction"),True)
    def test_unknown_schema(self):
        self.reject(lambda n,r:n[30].update(fake="1"),True)
    def test_wrong_render_color(self):
        self.reject(lambda n,r:r[0].update(render_argb="0"))
    def test_color_already_background(self):
        self.reject(lambda n,r:r[1].update(bg_argb=str(GOLD)))
    def test_readback_failed(self):
        self.reject(lambda n,r:r[0].update(readback_ok="0"))
    def test_negative_control_failed(self):
        self.reject(lambda n,r:r[1].update(baseline_ok="0"))
    def test_restoration_failed(self):
        self.reject(lambda n,r:r[2].update(restored_ok="0"))
    def test_fake_raster_source(self):
        self.reject(lambda n,r:r[2].update(source="synthetic"))
    def test_shifted_pixel(self):
        self.reject(lambda n,r:r[3].update(x_milli="12345"))
    def test_wrong_pixel_frame(self):
        self.reject(lambda n,r:r[3].update(frame="1000"))
    def test_no_early_sample(self):
        self.reject(lambda n,r:r.__delitem__(slice(0,2)))
    def test_no_late_sample(self):
        self.reject(lambda n,r:r.__delitem__(slice(3,None)))
    def test_duplicate_raster_frame(self):
        self.reject(lambda n,r:r[3].update(frame=r[2]["frame"]))
    def test_missing_raster_sample(self):
        self.reject(lambda n,r:r.__delitem__(slice(2,None)))
if __name__=="__main__":
    unittest.main()
