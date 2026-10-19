"""Stage 92 regression: detect fake crate / stale Y / missing bounce."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_postcheck import verify

def fixture():
    rows=[]
    for t in range(0,345,3):
        x=628.375+t*1.625
        mask=8 if t==171 else 2
        crate=0 if t>=195 else 1
        flash=max(0,12-(t-318)) if t>=318 else 0
        row=dict(seq=str(len(rows)),frame=str(1222+t),attempt=str(420+t),
            x_milli=str(round(x*1000)),y_milli="188000",
            vy_milli="-8000" if flash else "0",grounded="0" if flash else "1",
            input_bits=str(mask),checkpoint="1",crate=str(crate),
            spring_flash=str(flash),heart="0",ledge="0",cleared="0",
            mode="1",source="live_controller_qs_step")
        rows.append(row)
    stage91=[dict(frame=rows[0]["frame"],attempt=rows[0]["attempt"],
                  x_milli=rows[0]["x_milli"],input_bits=rows[0]["input_bits"],
                  checkpoint="1",source="live_controller_qs_step")]
    return rows,stage91

class PostcheckVerifierTests(unittest.TestCase):
    def setUp(self):self.rows,self.obstacles=fixture()
    def test_accept_realistic(self):
        x=verify(self.rows,self.obstacles)
        self.assertGreater(x["spring_tick"],x["crate_broken_tick"])
    def test_reject_programmed_route(self):
        self.rows[2]["source"]="qs_step_simulated_inputs"
        with self.assertRaisesRegex(ValueError,"provenance"):verify(self.rows,self.obstacles)
    def test_reject_replayed_trace(self):
        self.rows[30]["frame"]=self.rows[29]["frame"]
        with self.assertRaisesRegex(ValueError,"discontinuous"):verify(self.rows,self.obstacles)
    def test_reject_checkpoint_forgery(self):
        self.obstacles[0]["x_milli"]="999999"
        with self.assertRaisesRegex(ValueError,"not linked"):verify(self.rows,self.obstacles)
    def test_reject_no_y(self):
        for r in self.rows:r["input_bits"]="2"
        with self.assertRaisesRegex(ValueError,"Y shot"):verify(self.rows,self.obstacles)
    def test_reject_no_crate_break(self):
        for r in self.rows:r["crate"]="1"
        with self.assertRaisesRegex(ValueError,"destroyed crate"):verify(self.rows,self.obstacles)
    def test_reject_no_spring(self):
        for r in self.rows:r["spring_flash"]="0"
        with self.assertRaisesRegex(ValueError,"spring bounce"):verify(self.rows,self.obstacles)
    def test_reject_teleport(self):
        self.rows[80]["x_milli"]="80000"
        with self.assertRaisesRegex(ValueError,"teleport"):verify(self.rows,self.obstacles)

if __name__=="__main__":unittest.main()
