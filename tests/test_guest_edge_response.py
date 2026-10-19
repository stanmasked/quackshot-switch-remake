"""Stage 96: ensure button frame deltas come from actual NRO state."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_guest_edge_response import verify_edge_responses

EVENTS=(("PIT_LAUNCH_GUEST",100,280000,4),
        ("ROOF_LAUNCH_GUEST",180,390000,4),
        ("CRATE_FIRE_GUEST",300,912000,8))

def fixture():
    host="\n".join(f"{name} frame={frame} x_milli={x}" for name,frame,x,_ in EVENTS)
    rows=[]
    for frame in range(0,330):
        x=280000 if frame<=105 else 390000 if frame<=185 else 912000
        mask=2
        if 103<=frame<=105 or 184<=frame<=186:
            mask=6
        if 304<=frame<=305:
            mask=10
        y=185070 if mask&4 else 188000
        rows.append(dict(seq=str(frame),frame=str(frame),attempt=str(frame),
            x_milli=str(x),y_milli=str(y),grounded="0" if mask&4 else "1",
            input_bits=str(mask),checkpoint="0",mode="1",
            source="live_controller_qs_step"))
    return host,rows

class GuestButtonEdgeTests(unittest.TestCase):
    def setUp(self):
        self.host,self.rows=fixture()
    def test_valid(self):
        report=verify_edge_responses(self.host,self.rows)
        self.assertEqual([report["events"][e]["guest_frame_window"] for e in ("pit","roof","crate")],[3,4,4])
    def test_host_anchor_absent(self):
        self.host=self.host.replace("PIT_LAUNCH_GUEST","MISSING_PIT")
        with self.assertRaisesRegex(ValueError,"anchor"):
            verify_edge_responses(self.host,self.rows)
    def test_host_anchor_forged_position(self):
        self.host=self.host.replace("x_milli=390000","x_milli=390100")
        with self.assertRaisesRegex(ValueError,"guest-authenticated"):
            verify_edge_responses(self.host,self.rows)
    def test_stale_button_before_launch(self):
        self.rows[100]["input_bits"]="6"
        with self.assertRaisesRegex(ValueError,"grounded release"):
            verify_edge_responses(self.host,self.rows)
    def test_guest_source_swapped(self):
        self.rows[50]["source"]="synthetic_QsInput"
        with self.assertRaisesRegex(ValueError,"untrusted"):
            verify_edge_responses(self.host,self.rows)
    def test_button_missing(self):
        for i in (304,305): self.rows[i]["input_bits"]="2"
        with self.assertRaisesRegex(ValueError,"never observed"):
            verify_edge_responses(self.host,self.rows)
    def test_guest_did_not_jump(self):
        self.rows[103]["y_milli"]="188000"
        self.rows[103]["grounded"]="1"
        with self.assertRaisesRegex(ValueError,"did not change"):
            verify_edge_responses(self.host,self.rows)
    def test_dropped_frame(self):
        self.rows[120]["frame"]="130"
        with self.assertRaisesRegex(ValueError,"discontinuous"):
            verify_edge_responses(self.host,self.rows)
    def test_duplicate_host_event(self):
        self.host+="\nPIT_LAUNCH_GUEST frame=100 x_milli=280000"
        with self.assertRaisesRegex(ValueError,"duplicate"):
            verify_edge_responses(self.host,self.rows)
    def test_no_delayed_false_edge(self):
        for i in range(103,106):self.rows[i]["input_bits"]="2"
        for i in range(130,133):self.rows[i]["input_bits"]="6"
        with self.assertRaisesRegex(ValueError,"never observed"):
            verify_edge_responses(self.host,self.rows)

if __name__=="__main__":
    unittest.main()
