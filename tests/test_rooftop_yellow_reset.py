"""Stage158 fail-closed third-entry yellow-open-to-closed guest protocol."""
import pathlib
import sys
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tests"))
from test_rooftop_red_to_yellow import fixture as yellow_fixture
from tools.verify_rooftop_yellow_reset import verify_yellow_reset,YellowResetError
from tools.verify_rooftop_reentry_trace import EDGE_COLUMNS,EDGE_SOURCE

def fixture():
    city,roof,edges,weapon,host=yellow_fixture()
    exit_edge=dict(seq=4,frame=1711,old_mode=1,old_stage=2,mode=0,stage=2,
                   input_bits=64,bridge_before=1,bridge_after=1,bridge_cells=6,
                   ruins_relic_earned=0,red_unlocked=1,weapon=0,source=EDGE_SOURCE)
    entry=dict(seq=5,frame=1750,old_mode=0,old_stage=2,mode=1,stage=2,
               input_bits=32,bridge_before=1,bridge_after=0,bridge_cells=0,
               ruins_relic_earned=0,red_unlocked=1,weapon=0,source=EDGE_SOURCE)
    edges.extend([{k:str(z[k]) for k in EDGE_COLUMNS} for z in (exit_edge,entry)])
    last=dict(roof[-1])
    for i in range(140):
        r=dict(last)
        r.update(seq=str(len(roof)),frame=str(1751+i),
                 bridge_before="0",bridge_after="0",bridge_cells="0",
                 shot_active_before="0",shot_active_after="0",
                 shot_red_before="0",shot_x_before_milli="-1",
                 shot_vx_before_milli="0",input_bits="0")
        roof.append(r)
        weapon.append(dict(seq=r["seq"],frame=r["frame"],mode="1",stage="2",
                           change_pressed="0",weapon_before="0",weapon_after="0",
                           red_unlocked="1",source="live_rooftop_weapon_pad_qs_step"))
    host+=("ROOFTOP_YELLOW_OPEN_EXIT_GUEST frame=1711\n"
           "ROOFTOP_YELLOW_RESET_ENTRY_GUEST frame=1750\n"
           "ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=1890\n")
    return city,roof,edges,weapon,host

class YellowBridgeResetProtocol(unittest.TestCase):
    def test_real_open_bridge_resets_on_third_entry(self):
        report=verify_yellow_reset(*fixture())
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(report["third_visit_closed_frames"],140)
        self.assertEqual(report["bridge_solid_cells_after_reentry"],0)
        self.assertEqual(report["selected_weapon_after_reentry"],0)
    def test_host_checkpoint_can_precede_final_guest_frame(self):
        a,b,c,d,e=fixture()
        checkpoint=e.replace("ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=1890",
                             "ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=1870")
        report=verify_yellow_reset(a,b,c,d,checkpoint)
        self.assertEqual(report["third_visit_closed_frames"],140)
        self.assertEqual(report["final_rooftop_frame"],1890)

    def bad(self,mutate):
        a,b,c,d,e=fixture()
        new=mutate(a,b,c,d,e)
        if isinstance(new,str): e=new
        with self.assertRaises((ValueError,YellowResetError)):
            verify_yellow_reset(a,b,c,d,e)
    def test_missing_real_start_exit(self):
        self.bad(lambda a,b,c,d,e:c[4].update(input_bits="0"))
    def test_exit_bridge_was_not_open(self):
        self.bad(lambda a,b,c,d,e:c[4].update(bridge_before="0"))
    def test_missing_real_nintendo_a_reentry(self):
        self.bad(lambda a,b,c,d,e:c[5].update(input_bits="0"))
    def test_reentry_did_not_close_bridge(self):
        self.bad(lambda a,b,c,d,e:c[5].update(bridge_after="1",bridge_cells="6"))
    def test_yellow_weapon_dropped_on_map(self):
        self.bad(lambda a,b,c,d,e:c[5].update(weapon="1"))
    def test_missing_open_exit_edge(self):
        self.bad(lambda a,b,c,d,e:c.pop(4))
    def test_third_visit_partial_bridge(self):
        self.bad(lambda a,b,c,d,e:b[445].update(bridge_cells="3"))
    def test_bridge_opens_later_without_yellow_fire(self):
        self.bad(lambda a,b,c,d,e:b[460].update(bridge_before="1",bridge_after="1",bridge_cells="6"))
    def test_stale_yellow_shot_survives_reentry(self):
        self.bad(lambda a,b,c,d,e:b[440].update(shot_active_before="1",
                   shot_x_before_milli="650000",shot_vx_before_milli="5500"))
    def test_third_weapon_changed_from_yellow(self):
        self.bad(lambda a,b,c,d,e:d[465].update(weapon_after="1"))
    def test_third_roof_guest_frame_duplicate(self):
        self.bad(lambda a,b,c,d,e:b[470].update(frame=b[469]["frame"]))
    def test_roof_sample_during_map(self):
        self.bad(lambda a,b,c,d,e:b[440].update(frame="1730"))
    def test_no_100_frame_reset_observation(self):
        self.bad(lambda a,b,c,d,e:(b.__delitem__(slice(530,None)),
                                   d.__delitem__(slice(530,None))))
    def test_fake_host_exit_frame(self):
        self.bad(lambda a,b,c,d,e:e.replace("ROOFTOP_YELLOW_OPEN_EXIT_GUEST frame=1711",
                                             "ROOFTOP_YELLOW_OPEN_EXIT_GUEST frame=1710"))
    def test_duplicate_host_reentry(self):
        self.bad(lambda a,b,c,d,e:e+"ROOFTOP_YELLOW_RESET_ENTRY_GUEST frame=1750\n")
    def test_fake_host_closed_frame(self):
        # Only 90 third-visit frames by 1840; 100 are mandatory.
        self.bad(lambda a,b,c,d,e:e.replace("ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=1890",
                                             "ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=1840"))
    def test_guest_emits_1401_frames(self):
        def mutate(a,b,c,d,e):
            last=dict(b[-1]);w=dict(d[-1])
            for i in range(821):
                r=dict(last)
                r.update(seq=str(len(b)),frame=str(1891+i))
                b.append(r)
                q=dict(w);q.update(seq=r["seq"],frame=r["frame"]);d.append(q)
        self.bad(mutate)


class YellowResetWindowReadiness(unittest.TestCase):
    """Fail closed until the real NeXium window and NRO guest are ready."""
    @classmethod
    def setUpClass(cls):
        from tools.verify_nested_nexium_route import check_route, embedded_program
        cls.script = ROOT / "tools" / "nexium_rooftop_yellow_reset_probe.sh"
        cls.source = cls.script.read_text(encoding="utf-8")
        cls.inner = embedded_program(cls.script)
        cls.check_route = check_route
        cls.begin = "# BEGIN YELLOW RESET WINDOW AND GAME READY GATE"
        cls.end = "# END YELLOW RESET WINDOW AND GAME READY GATE"

    def test_nested_route_and_shell_parse(self):
        self.assertEqual(self.check_route(self.script)["status"], "PASS")
        self.assertEqual(self.inner.count(self.begin), 1)
        self.assertEqual(self.inner.count(self.end), 1)

    def test_wait_is_bounded_and_requires_real_guest_frames(self):
        gate = self.inner.split(self.begin, 1)[1].split(self.end, 1)[0]
        for proof in (
            "for startup_poll in $(seq 1 80)",
            "for boot_poll in $(seq 1 100)",
            'xdotool search --onlyvisible --name NeXium',
            'grep -qF "[guest-present] queued=300"',
            'kill -0 "$emu_pid"',
            "NEXIUM_WINDOW_READY",
            "NEXIUM_GUEST_FRAMES_READY",
            "BLOCKED: no emulator window after bounded startup poll",
            "BLOCKED: no authenticated NRO guest frames",
        ):
            self.assertIn(proof, gate)

    def test_controller_input_cannot_precede_readiness_gate(self):
        end = self.inner.index(self.end)
        first_input = self.inner.index('xdotool key --clearmodifiers c')
        self.assertLess(end, first_input)
        self.assertIn(
            "for event in NEXIUM_WINDOW_READY NEXIUM_GUEST_FRAMES_READY CITY_MENU_SENT",
            self.source,
        )

    def test_old_single_lookup_failure_is_removed(self):
        self.assertNotIn(
            'window="$(xdotool search --onlyvisible --name NeXium | tail -n1)"',
            self.inner,
        )



if __name__=="__main__":
    unittest.main()
