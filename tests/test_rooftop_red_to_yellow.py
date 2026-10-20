"""Strict synthetic protocol regressions; real acceptance needs NeXium guests."""
import pathlib
import sys
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_rooftop_second_red_trace import fixture as two_red_fixture,host_text
from tools.verify_rooftop_second_red_trace import verify_second_red
from tools.verify_rooftop_red_to_yellow import (
    WEAPON_COLUMNS,WEAPON_SOURCE,RedToYellowError,verify_red_to_yellow,weapon_values,
)

def fixture():
    city,roof,edges=two_red_fixture()
    first=verify_second_red(city,roof,edges)
    base=dict(roof[-1])
    extra=[]
    for i in range(80):
        row=dict(base)
        row.update(seq=str(int(base["seq"])+i+1),
                   frame=str(int(base["frame"])+i+1),
                   input_bits="8" if i==4 else "0",
                   shot_active_before="0",shot_active_after="0",
                   shot_red_before="0",shot_x_before_milli="-1",
                   shot_vx_before_milli="0",
                   bridge_before="1" if i>=14 else "0",
                   bridge_after="1" if i>=13 else "0",
                   bridge_cells="6" if i>=13 else "0")
        if 5<=i<=13:
            row["shot_active_before"]="1"
            row["shot_active_after"]="0" if i==13 else "1"
            row["shot_x_before_milli"]=str(650000+5500*(i-5))
            row["shot_vx_before_milli"]="5500"
        extra.append(row)
    roof+=extra
    weapon=[]
    for i,row in enumerate(roof):
        w=dict(seq=row["seq"],frame=row["frame"],mode="1",stage="2",
               change_pressed="1" if i==360 else "0",
               weapon_before="1" if i<=360 else "0",
               weapon_after="1" if i<360 else "0",
               red_unlocked="1",source=WEAPON_SOURCE)
        weapon.append(w)
    events=host_text(first)+(
        "ROOFTOP_YELLOW_SELECTED_GUEST frame=1631\n"
        "ROOFTOP_YELLOW_FIRE_GUEST frame=1635\n"
        "ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST frame=1644\n")
    return city,roof,edges,weapon,events

class RedToYellowProtocol(unittest.TestCase):
    def accept(self,c,r,e,w,h):
        return verify_red_to_yellow(c,r,e,w,h)
    def test_realistic_red_red_yellow_accepts(self):
        c,r,e,w,h=fixture()
        report=self.accept(c,r,e,w,h)
        self.assertEqual(report["bridge_solid_cells"],6)
        self.assertEqual(report["yellow_change_frame"],1631)
    def bad(self,mutate):
        c,r,e,w,h=fixture()
        out=mutate(c,r,e,w,h)
        # Mutations of row lists often return a list-comprehension value.
        # Only an explicit replacement transcript may override host text.
        if isinstance(out,str):h=out
        with self.assertRaises((ValueError,RedToYellowError)):
            self.accept(c,r,e,w,h)
    def test_held_nintendo_x_counts_as_one_press(self):
        city,roof,edges,weapon,host=fixture()
        for i in range(361,370):
            weapon[i]["change_pressed"]="1"
        report=self.accept(city,roof,edges,weapon,host)
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(report["bridge_solid_cells"],6)

    def test_no_weapon_log(self):self.bad(lambda c,r,e,w,h:w.clear())
    def test_weapon_source_fake(self):self.bad(lambda c,r,e,w,h:w[360].update(source="host"))
    def test_guest_seq_mismatch(self):self.bad(lambda c,r,e,w,h:w[360].update(seq="0"))
    def test_duplicate_change_button(self):
        # Frame 360 is the first press; frame 361 is merely a hold.
        # After a released frame 361/362, a NEW rising edge at 363
        # would be a second button press, not a continuation.
        self.bad(lambda c,r,e,w,h:w[363].update(change_pressed="1"))
    def test_weapon_stays_red(self):self.bad(lambda c,r,e,w,h:w[360].update(weapon_after="1"))
    def test_unauthorized_weapon_change(self):self.bad(lambda c,r,e,w,h:w[350].update(weapon_after="0"))
    def test_fake_yellow_red_projectile(self):self.bad(lambda c,r,e,w,h:r[368].update(shot_red_before="1"))
    def test_no_yellow_fire(self):self.bad(lambda c,r,e,w,h:r[364].update(input_bits="0"))
    def test_bridge_open_too_soon(self):self.bad(lambda c,r,e,w,h:r[363].update(bridge_after="1",bridge_cells="6"))
    def test_bridge_never_open(self):self.bad(lambda c,r,e,w,h:[p.update(bridge_after="0",bridge_before="0",bridge_cells="0") for p in r[360:]])
    def test_bridge_misses_yellow_switch(self):self.bad(lambda c,r,e,w,h:r[373].update(shot_x_before_milli="680000"))
    def test_bridge_later_closes(self):self.bad(lambda c,r,e,w,h:r[398].update(bridge_after="0",bridge_cells="0"))
    def test_frame_discontinuity(self):self.bad(lambda c,r,e,w,h:r[389].update(frame="2000"))
    def test_forged_change_event(self):self.bad(lambda c,r,e,w,h:h.replace("frame=1631","frame=9999"))
    def test_forged_bridge_event(self):self.bad(lambda c,r,e,w,h:h.replace("frame=1644","frame=9999"))
    def test_duplicate_yellow_event(self):self.bad(lambda c,r,e,w,h:h+"ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST frame=1644\n")

if __name__=="__main__":unittest.main()
