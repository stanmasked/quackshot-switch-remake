"""Stage160: adversarial protocol checks for the *second* real yellow bridge opening."""
import pathlib
import sys
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_rooftop_yellow_reset import fixture as reset_fixture
from tools.verify_rooftop_yellow_rearm import verify_rearm,YellowRearmError

def fixture():
    city,roof,edges,weapon,host=reset_fixture()
    base=dict(roof[-1])
    for i in range(80):
        r=dict(base)
        r.update(seq=str(len(roof)),frame=str(1891+i),
             input_bits="8" if i==12 else "0",x_milli=base["x_milli"],
             shot_active_before="0",shot_active_after="1" if i==12 else "0",
             shot_red_before="0",shot_x_before_milli="-1",shot_vx_before_milli="0",
             bridge_before="1" if i>=22 else "0",
             bridge_after="1" if i>=21 else "0",
             bridge_cells="6" if i>=21 else "0")
        if 13<=i<=21:
            r.update(shot_active_before="1",
                     shot_active_after="1" if i<21 else "0",
                     # Hit frame i=21 advances 699000 -> 704500 milli-pixels,
                     # overlapping the real switch start at 704000.
                     shot_x_before_milli=str(655000+5500*(i-13)),
                     shot_vx_before_milli="5500")
        roof.append(r)
        weapon.append(dict(seq=r["seq"],frame=r["frame"],mode="1",stage="2",
                           change_pressed="0",weapon_before="0",weapon_after="0",
                           red_unlocked="1",source="live_rooftop_weapon_pad_qs_step"))
    host+=("ROOFTOP_YELLOW_REARM_APPROACH_GUEST frame=1897 x_milli=640000\n"
           "ROOFTOP_YELLOW_REARM_FIRE_SENT\n"
           "ROOFTOP_YELLOW_REARM_FIRE_GUEST frame=1903\n"
           "ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST frame=1912\n"
           "ROOFTOP_YELLOW_REARM_STABLE_GUEST frame=1960\n")
    return city,roof,edges,weapon,host

class YellowRearmProtocol(unittest.TestCase):
    def test_valid_red_red_yellow_reset_yellow_reopen(self):
        report=verify_rearm(*fixture())
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(report["bridge_solid_cells_after_rearm"],6)
        self.assertEqual(report["yellow_rearm_fire_frame"],1903)
        self.assertEqual(report["yellow_rearm_open_frame"],1912)
    def bad(self,fn):
        c,r,e,w,h=fixture()
        new=fn(c,r,e,w,h)
        if isinstance(new,str):h=new
        with self.assertRaises((ValueError,YellowRearmError)):
            verify_rearm(c,r,e,w,h)
    def test_missing_second_fire(self):
        self.bad(lambda c,r,e,w,h:r[592].update(input_bits="0"))
    def test_rearm_weapon_red(self):
        self.bad(lambda c,r,e,w,h:w[600].update(weapon_after="1"))
    def test_rearm_change_button(self):
        self.bad(lambda c,r,e,w,h:w[597].update(change_pressed="1"))
    def test_rearm_fire_not_spawning_shot(self):
        self.bad(lambda c,r,e,w,h:r[592].update(shot_active_after="0"))
    def test_yellow_shot_red_instead(self):
        self.bad(lambda c,r,e,w,h:r[600].update(shot_red_before="1"))
    def test_yellow_shot_teleports(self):
        self.bad(lambda c,r,e,w,h:r[599].update(shot_x_before_milli="699999"))
    def test_player_teleport_in_third_visit(self):
        self.bad(lambda c,r,e,w,h:r[601].update(x_milli="780000"))
    def test_wrong_rooftop_stage_after_rearm(self):
        self.bad(lambda c,r,e,w,h:r[620].update(stage="0"))
    def test_out_of_range_controller_bits(self):
        self.bad(lambda c,r,e,w,h:r[620].update(input_bits="512"))
    def test_bridge_before_after_discontinuity(self):
        self.bad(lambda c,r,e,w,h:r[625].update(bridge_before="0"))
    def test_rearm_shot_missing_middle_frame(self):
        self.bad(lambda c,r,e,w,h:r[598].update(shot_active_before="0"))
    def test_bridge_opened_before_yellow(self):
        self.bad(lambda c,r,e,w,h:r[595].update(bridge_after="1",bridge_cells="6"))
    def test_bridge_did_not_open(self):
        self.bad(lambda c,r,e,w,h:r[601].update(bridge_after="0",bridge_cells="0"))
    def test_bridge_not_stable(self):
        self.bad(lambda c,r,e,w,h:r[625].update(bridge_after="0",bridge_cells="0"))
    def test_switch_not_overlapped(self):
        self.bad(lambda c,r,e,w,h:r[601].update(shot_x_before_milli="300000"))
    def test_fake_controller_fire_frame(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_FIRE_GUEST frame=1903","REARM_FIRE_GUEST frame=1902"))
    def test_fake_controller_open_frame(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_BRIDGE_OPEN_GUEST frame=1912","REARM_BRIDGE_OPEN_GUEST frame=1911"))
    def test_host_fake_approach_position(self):
        self.bad(lambda c,r,e,w,h:h.replace("x_milli=640000","x_milli=710000"))
    def test_host_too_early_stable(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_STABLE_GUEST frame=1960","REARM_STABLE_GUEST frame=1913"))
    def test_rearm_without_100_closed_frames(self):
        self.bad(lambda c,r,e,w,h:r[480].update(bridge_cells="6"))
    def test_forged_third_scene_edge(self):
        self.bad(lambda c,r,e,w,h:e[5].update(bridge_after="1"))
    def test_unmatched_weapon_tick(self):
        self.bad(lambda c,r,e,w,h:w[605].update(frame="9999"))
    def test_old_yellow_first_impact_missing(self):
        self.bad(lambda c,r,e,w,h:r[373].update(bridge_cells="0"))
if __name__=="__main__":
    unittest.main()
