"""Stage156 fail-closed two-visit RED projectile proof protocol."""
import pathlib
import sys
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_rooftop_red_trace import scenario
from tools.verify_rooftop_second_red_trace import (
    SecondRedGuestError, verify_second_red, verify_host_transcript,
)
from tools.verify_rooftop_reentry_trace import EDGE_COLUMNS,EDGE_SOURCE

def fixture():
    city,first,edges=scenario()
    leave=dict(seq=2,frame=1380,old_mode=1,old_stage=2,mode=0,stage=2,
               input_bits=64,bridge_before=0,bridge_after=0,bridge_cells=0,
               ruins_relic_earned=0,red_unlocked=1,weapon=1,source=EDGE_SOURCE)
    reenter=dict(seq=3,frame=1450,old_mode=0,old_stage=2,mode=1,stage=2,
                 input_bits=32,bridge_before=0,bridge_after=0,bridge_cells=0,
                 ruins_relic_earned=0,red_unlocked=1,weapon=1,source=EDGE_SOURCE)
    edges.extend({k:str(e[k]) for k in EDGE_COLUMNS} for e in (leave,reenter))
    second=[]
    for i,p in enumerate(first):
        row=dict(p)
        row["frame"]=str(1451+i)
        row["seq"]=str(180+i)
        second.append(row)
    return city,first+second,edges

def host_text(result):
    return ("ROOFTOP_EXIT_GUEST frame="+str(result["first_exit_frame"])+"\n"
            "ROOFTOP_REENTRY_RED_GUEST frame="+str(result["second_rooftop_entry_frame"])+"\n"
            "ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST frame="+str(result["second_red_frame"])+
            " x_milli="+str(result["second_red_x_milli"])+"\n")

class SecondRedGuestProtocol(unittest.TestCase):
    def test_two_real_red_visits_pass(self):
        c,r,e=fixture()
        out=verify_second_red(c,r,e)
        self.assertEqual(out["status"],"PASS")
        self.assertLess(out["first_red_frame"],out["first_exit_frame"])
        self.assertGreater(out["second_red_frame"],out["second_rooftop_entry_frame"])
        self.assertEqual(out["bridge_solid_cells"],0)
        self.assertTrue(verify_host_transcript(host_text(out),e,out))

    def bad(self,fn):
        c,r,e=fixture()
        fn(c,r,e)
        with self.assertRaises((SecondRedGuestError, ValueError)):
            verify_second_red(c,r,e)

    def test_no_four_authentic_edges(self):
        self.bad(lambda c,r,e:e.pop())

    def test_reentry_wrong_weapon(self):
        self.bad(lambda c,r,e:e[3].update(weapon="0"))

    def test_reentry_without_real_select(self):
        self.bad(lambda c,r,e:e[3].update(input_bits="0"))

    def test_first_rooftop_exit_not_start(self):
        self.bad(lambda c,r,e:e[2].update(input_bits="0"))

    def test_first_rooftop_exit_opens_bridge(self):
        self.bad(lambda c,r,e:e[2].update(bridge_before="1"))

    def test_reentry_not_contiguous_with_first_guest_frame(self):
        self.bad(lambda c,r,e:r[180].update(frame="1453"))

    def test_second_shot_is_yellow(self):
        self.bad(lambda c,r,e:r[196].update(shot_red_before="0"))

    def test_second_firing_without_true_pad_fire(self):
        self.bad(lambda c,r,e:r[188].update(input_bits="0"))

    def test_second_red_shot_teleports(self):
        self.bad(lambda c,r,e:r[200].update(shot_x_before_milli="850000"))

    def test_second_red_activates_bridge(self):
        self.bad(lambda c,r,e:r[208].update(bridge_after="1",bridge_cells="6"))

    def test_second_red_did_not_reach_switch(self):
        self.bad(lambda c,r,e:[p.update(shot_x_before_milli="680000") for p in r[197:223]])

    def test_second_session_forge_source(self):
        self.bad(lambda c,r,e:r[190].update(source="host_assertion"))

    def test_red_reentry_duplicate_frame(self):
        self.bad(lambda c,r,e:r[222].update(frame=r[221]["frame"]))

    def test_host_nonnumeric_hit_frame_rejected(self):
        c,r,e=fixture()
        report=verify_second_red(c,r,e)
        fake=host_text(report).replace("frame="+str(report["second_red_frame"]),
                                        "frame=frame")
        with self.assertRaises(SecondRedGuestError):
            verify_host_transcript(fake,e,report)

    def test_host_mismatched_red_position_rejected(self):
        c,r,e=fixture()
        report=verify_second_red(c,r,e)
        fake=host_text(report).replace("x_milli="+str(report["second_red_x_milli"]),
                                      "x_milli=999999")
        with self.assertRaises(SecondRedGuestError):
            verify_host_transcript(fake,e,report)

    def test_host_duplicate_reentry_rejected(self):
        c,r,e=fixture()
        report=verify_second_red(c,r,e)
        text=host_text(report)
        fake=text+"ROOFTOP_REENTRY_RED_GUEST frame=1450\n"
        with self.assertRaises(SecondRedGuestError):
            verify_host_transcript(fake,e,report)

if __name__=="__main__":
    unittest.main()
