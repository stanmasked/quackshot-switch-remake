"""Stage 153: regression tests for genuine ROOFTOP stage reentry evidence."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tests"))
from test_rooftop_guest_trace import make_valid
from tools.verify_rooftop_guest_trace import COLUMNS, SOURCE
from tools.verify_rooftop_reentry_trace import (
    EDGE_COLUMNS, EDGE_SOURCE, ReentryError, verify_reentry,
)


def fixture():
    first = make_valid()
    second = []
    for i in range(65):
        data = {
            "seq": 118 + i, "frame": 1123 + i, "mode": 1, "stage": 2,
            "x_milli": 40000, "y_milli": 188000, "grounded": 1,
            "input_bits": 0, "shot_x_before_milli": -1, "shot_vx_before_milli": 0,
            "shot_red_before": 0, "shot_active_before": 0, "shot_active_after": 0,
            "bridge_before": 0, "bridge_after": 0, "bridge_cells": 0, "source": SOURCE,
        }
        second.append({key: str(data[key]) for key in COLUMNS})
    edges = [
        dict(seq=0, frame=999, old_mode=0, old_stage=0, mode=1, stage=2,
             input_bits=0, bridge_before=0, bridge_after=0, bridge_cells=0,
             ruins_relic_earned=0, red_unlocked=0, weapon=0, source=EDGE_SOURCE),
        dict(seq=1, frame=1118, old_mode=1, old_stage=2, mode=0, stage=2,
             input_bits=0, bridge_before=1, bridge_after=1, bridge_cells=6,
             ruins_relic_earned=0, red_unlocked=0, weapon=0, source=EDGE_SOURCE),
        dict(seq=2, frame=1122, old_mode=0, old_stage=2, mode=1, stage=2,
             input_bits=0, bridge_before=1, bridge_after=0, bridge_cells=0,
             ruins_relic_earned=0, red_unlocked=0, weapon=0, source=EDGE_SOURCE),
    ]
    return first + second, [{k: str(e[k]) for k in EDGE_COLUMNS} for e in edges]


class ReentryTraceProtocol(unittest.TestCase):
    def test_open_then_closed_real_reentry(self):
        rows, edges = fixture()
        report = verify_reentry(rows, edges)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["second_visit_frames"], 65)
        self.assertEqual(report["bridge_solid_cells_after_reentry"], 0)

    def bad(self, mutate):
        rows, edges = fixture()
        mutate(rows, edges)
        with self.assertRaises((ReentryError, ValueError)):
            verify_reentry(rows, edges)

    def test_missing_exit(self):
        self.bad(lambda r,e: e.pop(1))

    def test_forged_source(self):
        self.bad(lambda r,e: e[2].update(source="host_injected"))

    def test_reversed_transition(self):
        self.bad(lambda r,e: e[2].update(frame="1117"))

    def test_no_real_map_reentry(self):
        self.bad(lambda r,e: e[2].update(old_mode="1"))

    def test_bridge_not_open_on_exit(self):
        self.bad(lambda r,e: e[1].update(bridge_before="0"))

    def test_old_bridge_not_carried_into_reentry(self):
        self.bad(lambda r,e: e[2].update(bridge_before="0"))

    def test_bridge_still_open_on_reentry(self):
        self.bad(lambda r,e: e[2].update(bridge_after="1",bridge_cells="6"))

    def test_partial_bridge_after_reentry(self):
        self.bad(lambda r,e: e[2].update(bridge_cells="2"))

    def test_lost_earned_relic(self):
        self.bad(lambda r,e: e[2].update(ruins_relic_earned="1"))

    def test_stale_projectile_after_reentry(self):
        self.bad(lambda r,e: r[145].update(shot_active_after="1"))

    def test_fire_during_reentry(self):
        self.bad(lambda r,e: r[150].update(input_bits="8"))

    def test_bridge_reopens_without_fire(self):
        self.bad(lambda r,e: r[170].update(bridge_after="1",bridge_cells="6"))

    def test_short_second_observation(self):
        self.bad(lambda r,e: r.__delitem__(slice(150,None)))

    def test_second_frame_gap(self):
        self.bad(lambda r,e: r[152].update(frame="9000"))

    def test_bad_second_spawn(self):
        self.bad(lambda r,e: r[118].update(x_milli="700000"))

    def test_missing_frame_at_first_exit(self):
        self.bad(lambda r,e: r.__delitem__(117))

    def test_fake_yellow_shot(self):
        self.bad(lambda r,e: r[15].update(shot_red_before="1"))


if __name__ == "__main__":
    unittest.main()
