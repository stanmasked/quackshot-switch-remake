"""Stage 154 proof protocol: never accept red shot without real CITY pickup."""
import csv
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.verify_rooftop_red_trace import (
    CITY_COLUMNS,CITY_SOURCE,RedGuestError,verify_red,
)
from tools.verify_rooftop_guest_trace import COLUMNS,SOURCE
from tools.verify_rooftop_reentry_trace import EDGE_COLUMNS,EDGE_SOURCE
from tools.rooftop_red_overlap_witness import overlap_in_guest


def scenario():
    city = []
    jump_start = 33
    for i in range(100):
        x = 40000 + i*1900
        t = i-jump_start+1
        y = 188000
        if 1<=t<=44:
            # The real prototype has a platform at x144..272, y160;
            # our synthetic fixture only validates guest parser rules.
            y = int((188-7.2*t+0.16*t*(t+1))*1000)
            if y>140000 and i>=68:
                y=140000
        values=dict(seq=i,frame=1000+i,mode=1,stage=0,
                    x_milli=x,y_milli=y,grounded=int(i<jump_start or i>=69),
                    input_bits=2|(4 if jump_start<=i<=jump_start+6 else 0),
                    red_before=int(i>68),red_after=int(i>=68),
                    weapon=int(i>=68),pickup_before=int(i<=68),
                    pickup_after=int(i<68),source=CITY_SOURCE)
        city.append({k:str(values[k]) for k in CITY_COLUMNS})
    roof=[]
    for i in range(180):
        live=9<=i<=42
        shot=650000+5500*(i-9) if live else -1
        values=dict(seq=i,frame=1200+i,mode=1,stage=2,
                    x_milli=640000,y_milli=188000,grounded=1,
                    input_bits=8 if i==8 else 0,
                    shot_x_before_milli=shot,
                    shot_vx_before_milli=5500 if live else 0,
                    shot_red_before=int(live),shot_active_before=int(live),
                    shot_active_after=int(9<=i<42),
                    bridge_before=0,bridge_after=0,bridge_cells=0,source=SOURCE)
        roof.append({k:str(values[k]) for k in COLUMNS})
    exit_edge=dict(seq=0,frame=1100,old_mode=1,old_stage=0,mode=0,stage=0,
                   input_bits=64,bridge_before=0,bridge_after=0,bridge_cells=6,
                   ruins_relic_earned=0,red_unlocked=1,weapon=1,source=EDGE_SOURCE)
    roof_entry=dict(seq=1,frame=1199,old_mode=0,old_stage=0,mode=1,stage=2,
                    input_bits=0,bridge_before=0,bridge_after=0,bridge_cells=0,
                    ruins_relic_earned=0,red_unlocked=1,weapon=1,source=EDGE_SOURCE)
    return city,roof,[{k:str(edge[k]) for k in EDGE_COLUMNS} for edge in (exit_edge,roof_entry)]


class RedGuestProtocol(unittest.TestCase):
    def test_realistic_red_stays_closed(self):
        c,r,e=scenario()
        result=verify_red(c,r,e)
        self.assertEqual(result["status"],"PASS")
        self.assertEqual(result["bridge_solid_cells"],0)

    def bad(self,mutation):
        c,r,e=scenario()
        mutation(c,r,e)
        with self.assertRaises((RedGuestError,ValueError)):
            verify_red(c,r,e)

    def test_host_witness_reads_real_numeric_frame(self):
        _city, roof, _edges = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            path=pathlib.Path(tmp)/"guest.csv"
            with path.open("w", newline="", encoding="utf-8") as fd:
                writer=csv.DictWriter(fd,fieldnames=COLUMNS)
                writer.writeheader()
                writer.writerows(roof)
            frame,x=overlap_in_guest(path)
            self.assertIsInstance(frame,int)
            self.assertGreaterEqual(frame,1209)
            self.assertIsInstance(x,int)
            self.assertGreater(x,0)

    def test_host_witness_rejects_header_string_as_frame(self):
        _city, roof, _edges = scenario()
        forged={k:k for k in COLUMNS}
        # Even a forged row with matching source cannot count as numeric.
        forged["source"]=SOURCE
        with tempfile.TemporaryDirectory() as tmp:
            path=pathlib.Path(tmp)/"guest.csv"
            with path.open("w", newline="", encoding="utf-8") as fd:
                writer=csv.DictWriter(fd,fieldnames=COLUMNS)
                writer.writeheader()
                writer.writerow(forged)
            self.assertIsNone(overlap_in_guest(path))

    def test_host_witness_rejects_open_bridge(self):
        _city, roof, _edges = scenario()
        for row in roof:
            row["bridge_cells"]="6"
        with tempfile.TemporaryDirectory() as tmp:
            path=pathlib.Path(tmp)/"guest.csv"
            with path.open("w", newline="", encoding="utf-8") as fd:
                writer=csv.DictWriter(fd,fieldnames=COLUMNS)
                writer.writeheader()
                writer.writerows(roof)
            self.assertIsNone(overlap_in_guest(path))

    def test_injected_red_unlock(self):
        self.bad(lambda c,r,e: c[0].update(red_before="1"))

    def test_no_real_pickup(self):
        self.bad(lambda c,r,e: c[68].update(pickup_before="0"))

    def test_fake_weapon(self):
        self.bad(lambda c,r,e: c[68].update(weapon="0"))

    def test_hero_not_at_pickup(self):
        self.bad(lambda c,r,e: c[68].update(x_milli="140000"))

    def test_no_jump(self):
        self.bad(lambda c,r,e: [p.update(input_bits="2") for p in c])

    def test_no_ground_approach(self):
        self.bad(lambda c,r,e: [p.update(grounded="0") for p in c])

    def test_duplicate_city_frame(self):
        self.bad(lambda c,r,e: c[23].update(frame=c[22]["frame"]))

    def test_teleport_to_pickup(self):
        self.bad(lambda c,r,e: c[68].update(x_milli="200000"))

    def test_recreated_red_pickup(self):
        self.bad(lambda c,r,e: c[72].update(pickup_before="1"))

    def test_red_lost_at_rooftop_entry(self):
        self.bad(lambda c,r,e: e[1].update(red_unlocked="0"))

    def test_wrong_selected_weapon_at_entry(self):
        self.bad(lambda c,r,e: e[1].update(weapon="0"))

    def test_no_authenticated_city_exit(self):
        self.bad(lambda c,r,e: e.pop(0))

    def test_city_exit_wrong_source(self):
        self.bad(lambda c,r,e: e[0].update(source="host_generated"))

    def test_city_exit_was_not_map(self):
        self.bad(lambda c,r,e: e[0].update(mode="1"))

    def test_city_exit_unlocked_state_lost(self):
        self.bad(lambda c,r,e: e[0].update(red_unlocked="0"))

    def test_city_exit_not_connected_to_last_city_tick(self):
        self.bad(lambda c,r,e: e[0].update(frame="1140"))

    def test_bad_map_transition_order(self):
        self.bad(lambda c,r,e: e[1].update(frame="1101"))

    def test_rooftop_frame_gap_after_entry(self):
        self.bad(lambda c,r,e: e[1].update(frame="1197"))

    def test_missing_real_fire(self):
        self.bad(lambda c,r,e: r[8].update(input_bits="0"))

    def test_second_unexpected_fire(self):
        self.bad(lambda c,r,e: r[80].update(input_bits="8"))

    def test_shot_is_yellow(self):
        self.bad(lambda c,r,e: r[15].update(shot_red_before="0"))

    def test_red_projectile_teleport(self):
        self.bad(lambda c,r,e: r[20].update(shot_x_before_milli="690000"))

    def test_red_did_not_cross_switch(self):
        self.bad(lambda c,r,e: [p.update(shot_x_before_milli="680000") for p in r[17:43]])

    def test_bridge_opened_after_red(self):
        self.bad(lambda c,r,e: r[26].update(bridge_after="1",bridge_cells="6"))

    def test_second_stage_sample_out_of_order(self):
        self.bad(lambda c,r,e: r[35].update(frame="999"))

    def test_wrong_guest_source(self):
        self.bad(lambda c,r,e: c[12].update(source="host_generated"))


if __name__=="__main__":
    unittest.main()
