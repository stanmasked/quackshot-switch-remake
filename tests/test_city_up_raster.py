"""ROM-free adversarial tests for actual SDL pixel readback trace validation."""
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_city_up_guest import fixture
from tools.verify_city_up_raster import COLUMNS,SOURCE,GOLD,CYAN,RasterError,verify_pair

AGES=(1,2,4,5,7,9,11,13,15,17,19)

def examples():
    rows=fixture()
    raster=[]
    for seq,age in enumerate(AGES):
        guest=rows[9+3+age]
        payload=dict(seq=seq,frame=int(guest["frame"]),age=age,
                     shot_x=int(guest["x"]),shot_y=int(guest["y"]),
                     gold_argb=GOLD,cyan_argb=CYAN,readback_ok=1,
                     scale_x_milli=3000,scale_y_milli=3000,source=SOURCE)
        raster.append({key:str(payload[key]) for key in COLUMNS})
    return rows,raster

class RasterGuestProtocol(unittest.TestCase):
    def test_valid_readback(self):
        self.assertEqual(verify_pair(*examples())["status"],"PASS")
    def reject(self,change):
        rows,frames=examples()
        change(rows,frames)
        with self.assertRaises(RasterError):
            verify_pair(rows,frames)
    def test_no_gold(self):
        self.reject(lambda n,r:r[0].update(gold_argb="0"))
    def test_wrong_blue(self):
        self.reject(lambda n,r:r[2].update(cyan_argb=str(GOLD)))
    def test_fake_readback_success(self):
        self.reject(lambda n,r:r[3].update(readback_ok="0"))
    def test_shifted_x(self):
        self.reject(lambda n,r:r[4].update(shot_x="62"))
    def test_shifted_y(self):
        self.reject(lambda n,r:r[4].update(shot_y="-99"))
    def test_wrong_age(self):
        self.reject(lambda n,r:r[4].update(age="0"))
    def test_wrong_frame(self):
        self.reject(lambda n,r:r[4].update(frame="800"))
    def test_disordered_frames(self):
        self.reject(lambda n,r:r[5].update(frame=r[4]["frame"]))
    def test_forged_source(self):
        self.reject(lambda n,r:r[1].update(source="host_draw_prediction"))
    def test_invalid_scale(self):
        self.reject(lambda n,r:r[2].update(scale_y_milli="0"))
    def test_missing_early_readbacks(self):
        self.reject(lambda n,r:r.__delitem__(slice(0,5)))
    def test_missing_late_readbacks(self):
        self.reject(lambda n,r:r.__delitem__(slice(7,None)))
    def test_wrong_seq(self):
        self.reject(lambda n,r:r[7].update(seq="99"))
    def test_duplicate_age(self):
        self.reject(lambda n,r:r[5].update(age=r[4]["age"]))
    def test_bad_schema(self):
        self.reject(lambda n,r:r[6].update(manipulated="true"))
    def test_no_guest_video_alignment(self):
        self.reject(lambda n,r:n[13].update(visible="0"))

if __name__=="__main__":
    unittest.main()
