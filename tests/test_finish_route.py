"""Stage 93: strict live-controller milestone and anti-replay fixtures."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_finish_route import verify

def fixture():
    finish=[]
    for tick in range(723,877,3):
        x=1132.125
        if tick>=760:
            x=1144.656 + min(38, (tick-760)*1.625)
        if tick>=822:
            x=1182.656 + (tick-822)*1.625
        heart=int(tick>=753)
        ledge=int(813<=tick<=822)
        last=tick==876
        row={"seq":str(len(finish)),"frame":str(860+tick),
             "attempt":str(0 if last else tick),
             "x_milli":str(int(x*1000) if not last else 1224000),
             "y_milli":str(124000 if ledge else (188000 if last else 91000)),
             "vy_milli":str(-8000 if tick==723 else 0),
             "grounded":str(int(ledge)),"input_bits":str(2 if 756<=tick<801 or tick>=819 else 0),
             "checkpoint":str(0 if last else 1),"crate":"0",
             "spring_flash":str(12 if tick==723 else 0),
             "heart":str(0 if last else heart),
             "ledge":str(0 if last else ledge),"cleared":str(int(last)),
             "mode":str(0 if last else 1),"best":str(876 if last else 0),
             "source":"live_controller_qs_step"}
        finish.append(row)
    postcheck=[dict(frame=finish[0]["frame"],attempt=finish[0]["attempt"],
        x_milli=finish[0]["x_milli"],spring_flash=finish[0]["spring_flash"],
        source="live_controller_qs_step")]
    obstacle=[dict(checkpoint="1",attempt="420",source="live_controller_qs_step")]
    return finish,postcheck,obstacle

class FinishTests(unittest.TestCase):
    def setUp(self):self.finish,self.post,self.obst=fixture()
    def test_valid(self):self.assertEqual(verify(self.finish,self.post,self.obst)["best_clear_frames"],876)
    def test_reject_wrong_source(self):
        self.finish[10]["source"]="qs_step_simulated_inputs"
        with self.assertRaisesRegex(ValueError,"source"):verify(self.finish,self.post,self.obst)
    def test_reject_bad_link(self):
        self.post[0]["attempt"]="9999"
        with self.assertRaisesRegex(ValueError,"not linked"):verify(self.finish,self.post,self.obst)
    def test_reject_no_heart(self):
        for r in self.finish:r["heart"]="0"
        with self.assertRaisesRegex(ValueError,"heart"):verify(self.finish,self.post,self.obst)
    def test_reject_no_ledge(self):
        for r in self.finish:r["ledge"]="0"
        with self.assertRaisesRegex(ValueError,"ledge"):verify(self.finish,self.post,self.obst)
    def test_reject_no_clear(self):
        self.finish[-1]["cleared"]="0"
        with self.assertRaisesRegex(ValueError,"completed"):verify(self.finish,self.post,self.obst)
    def test_reject_no_steering(self):
        for r in self.finish:r["input_bits"]="0"
        with self.assertRaisesRegex(ValueError,"controller"):verify(self.finish,self.post,self.obst)
    def test_reject_reset(self):
        self.finish[30]["attempt"]="0"
        with self.assertRaisesRegex(ValueError,"reset"):verify(self.finish,self.post,self.obst)
    def test_reject_dropped_frames(self):
        self.finish[31]["frame"]=str(int(self.finish[30]["frame"])+25)
        with self.assertRaisesRegex(ValueError,"simulation frames"):verify(self.finish,self.post,self.obst)

if __name__=="__main__":unittest.main()
