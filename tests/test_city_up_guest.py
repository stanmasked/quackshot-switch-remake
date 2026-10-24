"""Synthetic adversarial regression tests for numeric CITY upward guest proof."""
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.verify_city_up_guest import COLUMNS,SOURCE,WitnessError,verify

def fixture():
    samples=[]
    for i in range(72):
        start=9
        age=i-start-3
        active=int(start<=i<start+23)
        visible=int(start+3<=i<start+23)
        dy=-((127*age-age*age+15)//16) if visible else 0
        vals=dict(seq=i,frame=601+i,attempt=1+i,mode=1,stage=0,
                  up=int(start<=i<start+8),fire=int(start<=i<start+8),
                  grounded=1,active=active,age=(age if active else 0),
                  visible=visible,x=61 if visible else -1,
                  y=154+dy if visible else -1,
                  legacy_shots=0,source=SOURCE)
        samples.append({key:str(vals[key]) for key in COLUMNS})
    return samples

class GuestWitnessTests(unittest.TestCase):
    def test_valid(self):
        result=verify(fixture())
        self.assertEqual(result["visible_positions"],20)
        self.assertEqual(result["first_visible_frame"]-result["shot_frame"],3)
        self.assertEqual(result["disappeared_frame"]-result["first_visible_frame"],20)
    def reject(self,change):
        rows=fixture()
        change(rows)
        with self.assertRaises(WitnessError):
            verify(rows)
    def test_no_input(self):
        self.reject(lambda r:r[9].update(fire="0"))
    def test_early_visible(self):
        self.reject(lambda r:r[10].update(visible="1",x="61",y="154"))
    def test_constant_speed_fake(self):
        self.reject(lambda r:r[21].update(y=str(154-8*9)))
    def test_short_trace(self):
        self.reject(lambda r:r.__delitem__(slice(50,None)))
    def test_missed_frame(self):
        self.reject(lambda r:r[20].update(frame="600"))
    def test_skipped_age(self):
        self.reject(lambda r:r[16].update(age="99"))
    def test_extra_legacy_shot(self):
        self.reject(lambda r:r[14].update(legacy_shots="1"))
    def test_forged_source(self):
        self.reject(lambda r:r[15].update(source="host_synthesized"))
    def test_lingering_shot(self):
        self.reject(lambda r:r[32].update(active="1"))
    def test_missing_event(self):
        self.reject(lambda r:r[9].update(grounded="0"))
    def test_stage_mismatch(self):
        self.reject(lambda r:r[5].update(stage="2"))
    def test_repeated_fire(self):
        self.reject(lambda r:r[30].update(up="1",fire="1"))
    def test_corrupt_schema(self):
        self.reject(lambda r:r[12].update(hidden="data"))

if __name__=="__main__":
    unittest.main()
