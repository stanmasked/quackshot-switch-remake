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
    # Model a fresh genuine third visit: the ROOFTOP spawn is x=40px,
    # confirmed by both Stage158 independent NeXium NRO recordings.
    # The first 140 frames are closed/idle through the reset checkpoint.
    third_start=next(i for i,r in enumerate(roof) if int(r["frame"])==1751)
    for k in range(third_start,len(roof)):
        roof[k]["x_milli"]="40000"
    base=dict(roof[-1])
    # 200 contiguous frames at 3px/tick with genuine RIGHT+DASH bits
    # move the test actor from x40 to x640 *after* the reset checkpoint.
    # This is a synthetic verifier fixture, never guest state injection.
    for i in range(200):
        r=dict(base)
        r.update(seq=str(len(roof)),frame=str(1891+i),
                 x_milli=str(40000+3000*(i+1)),input_bits="18",
                 shot_active_before="0",shot_active_after="0",
                 shot_x_before_milli="-1",shot_vx_before_milli="0",
                 shot_red_before="0",bridge_before="0",
                 bridge_after="0",bridge_cells="0")
        roof.append(r)
        weapon.append(dict(seq=r["seq"],frame=r["frame"],mode="1",stage="2",
                           change_pressed="0",weapon_before="0",weapon_after="0",
                           red_unlocked="1",source="live_rooftop_weapon_pad_qs_step"))
    base=dict(roof[-1])
    for i in range(80):
        r=dict(base)
        r.update(seq=str(len(roof)),frame=str(2091+i),
             input_bits="8" if i==12 else "0",x_milli=base["x_milli"],
             shot_active_before="0",shot_active_after="1" if i==12 else "0",
             shot_red_before="0",shot_x_before_milli="-1",shot_vx_before_milli="0",
             bridge_before="1" if i>=21 else "0",
             bridge_after="1" if i>=20 else "0",
             bridge_cells="6" if i>=20 else "0")
        if 13<=i<=20:
            r.update(shot_active_before="1",
                     shot_active_after="1" if i<20 else "0",
                     # Native shot: x640 hero + 12px muzzle + 5.5px on
                     # FIRE tick = 657500 on the following NRO sample.
                     # First contact at i=20: 696000 -> 701500, whose
                     # 7px projectile body overlaps x704000 switch.
                     shot_x_before_milli=str(657500+5500*(i-13)),
                     shot_vx_before_milli="5500")
        roof.append(r)
        weapon.append(dict(seq=r["seq"],frame=r["frame"],mode="1",stage="2",
                           change_pressed="0",weapon_before="0",weapon_after="0",
                           red_unlocked="1",source="live_rooftop_weapon_pad_qs_step"))
    host+=("ROOFTOP_YELLOW_REARM_APPROACH_GUEST frame=2097 x_milli=640000\n"
           "ROOFTOP_YELLOW_REARM_FIRE_SENT\n"
           "ROOFTOP_YELLOW_REARM_FIRE_GUEST frame=2103\n"
           "ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST frame=2111\n"
           "ROOFTOP_YELLOW_REARM_STABLE_GUEST frame=2160\n")
    return city,roof,edges,weapon,host

class YellowRearmProtocol(unittest.TestCase):
    def test_valid_red_red_yellow_reset_yellow_reopen(self):
        report=verify_rearm(*fixture())
        self.assertEqual(report["status"],"PASS")
        self.assertEqual(report["bridge_solid_cells_after_rearm"],6)
        self.assertEqual(report["yellow_rearm_fire_frame"],2103)
        self.assertEqual(report["yellow_rearm_open_frame"],2111)
    def test_real_7px_projectile_edge_overlap_is_valid(self):
        # A 2.5px right-edge overlap is a genuine physical hit, even
        # when the projectile's left-side coordinate stops before x704.
        c,r,e,w,h=fixture()
        for i in range(793,801):
            r[i]["shot_x_before_milli"]=str(int(r[i]["shot_x_before_milli"])-2000)
        report=verify_rearm(c,r,e,w,h)
        self.assertEqual(report["bridge_solid_cells_after_rearm"],6)
    def bad(self,fn):
        c,r,e,w,h=fixture()
        new=fn(c,r,e,w,h)
        if isinstance(new,str):h=new
        with self.assertRaises((ValueError,YellowRearmError)):
            verify_rearm(c,r,e,w,h)
    def test_spawning_at_prepositioned_x640_is_not_new_movement(self):
        self.bad(lambda c,r,e,w,h:r[440].update(x_milli="640000"))
    def test_no_actual_right_movement(self):
        self.bad(lambda c,r,e,w,h:[row.update(input_bits="0") for row in r[580:780]])
    def test_idle_spawn_drift_not_earned_by_right(self):
        # Within the old 16px continuity allowance, but not a real
        # stationary 100-frame bridge reset checkpoint.
        self.bad(lambda c,r,e,w,h:r[480].update(x_milli="42000"))
    def test_movement_with_unpressed_right_must_not_count(self):
        # Still leaves 140 valid DASH+RIGHT frames, so input totals
        # alone would falsely accept this 60-frame movement segment.
        self.bad(lambda c,r,e,w,h:[
            row.update(input_bits="0") for row in r[580:640]])
    def test_smooth_but_unnaturally_fast_hero_motion(self):
        # Both adjacent jumps fit the old permissive 16px threshold;
        # actual ROOFTOP DASH motion never exceeds 3.1px per NRO tick.
        self.bad(lambda c,r,e,w,h:r[660].update(
            x_milli=str(int(r[660]["x_milli"])+12000)))
    def test_missing_dash_during_approach(self):
        self.bad(lambda c,r,e,w,h:[row.update(input_bits="2") for row in r[580:780]])
    def test_right_button_used_before_reset_observation(self):
        self.bad(lambda c,r,e,w,h:r[460].update(input_bits="18"))
    def test_missing_second_fire(self):
        self.bad(lambda c,r,e,w,h:r[792].update(input_bits="0"))
    def test_rearm_weapon_red(self):
        self.bad(lambda c,r,e,w,h:w[800].update(weapon_after="1"))
    def test_rearm_change_button(self):
        self.bad(lambda c,r,e,w,h:w[797].update(change_pressed="1"))
    def test_rearm_fire_not_spawning_shot(self):
        self.bad(lambda c,r,e,w,h:r[792].update(shot_active_after="0"))
    def test_yellow_shot_red_instead(self):
        self.bad(lambda c,r,e,w,h:r[800].update(shot_red_before="1"))
    def test_smooth_yellow_projectile_misses_switch(self):
        # A continuous 5.5 px/frame flight is insufficient: the second
        # switch impact must actually overlap physical x=704..716.
        def miss(c,r,e,w,h):
            for i in range(793,801):
                r[i]["shot_x_before_milli"]=str(int(r[i]["shot_x_before_milli"])-5000)
        self.bad(miss)
    def test_yellow_shot_teleports(self):
        self.bad(lambda c,r,e,w,h:r[799].update(shot_x_before_milli="699999"))
    def test_smooth_but_wrong_yellow_projectile_velocity(self):
        # Before this guard, 4.8px/tick passed the wide 0..12px speed
        # envelope and the 2.5px continuity tolerance.
        self.bad(lambda c,r,e,w,h:[
            r[i].update(shot_vx_before_milli="4800") for i in range(793,801)])
    def test_spawn_position_must_match_real_hero_and_firing_tick(self):
        # A smooth 1px offset changes no velocity and preserves a real
        # switch intersection; only an authentic muzzle-spawn check rejects it.
        def fake_birth(c,r,e,w,h):
            for i in range(793,801):
                r[i]["shot_x_before_milli"]=str(int(r[i]["shot_x_before_milli"])-1000)
        self.bad(fake_birth)
    def test_switch_contact_cannot_precede_recorded_opening(self):
        # 1.2px drift for one intermediate frame stays within the former
        # 2.5px motion tolerance but crosses the physical 7px hitbox.
        self.bad(lambda c,r,e,w,h:r[799].update(
            shot_x_before_milli=str(int(r[799]["shot_x_before_milli"])+1200)))
    def test_projectile_vanishes_and_resurrects_midflight(self):
        self.bad(lambda c,r,e,w,h:r[797].update(shot_active_after="0"))
    def test_extra_projectile_after_second_yellow_impact(self):
        # No new FIRE edge may mint a second active projectile.
        self.bad(lambda c,r,e,w,h:r[812].update(
            shot_active_before="1",shot_x_before_milli="730000",
            shot_vx_before_milli="5500"))
    def test_player_teleport_in_third_visit(self):
        self.bad(lambda c,r,e,w,h:r[801].update(x_milli="780000"))
    def test_wrong_rooftop_stage_after_rearm(self):
        self.bad(lambda c,r,e,w,h:r[820].update(stage="0"))
    def test_out_of_range_controller_bits(self):
        self.bad(lambda c,r,e,w,h:r[820].update(input_bits="512"))
    def test_bridge_before_after_discontinuity(self):
        self.bad(lambda c,r,e,w,h:r[825].update(bridge_before="0"))
    def test_rearm_shot_missing_middle_frame(self):
        self.bad(lambda c,r,e,w,h:r[798].update(shot_active_before="0"))
    def test_bridge_opened_before_yellow(self):
        self.bad(lambda c,r,e,w,h:r[795].update(bridge_after="1",bridge_cells="6"))
    def test_second_yellow_impact_keeps_projectile_alive(self):
        # Both real Stage158 NRO guests consumed their shot on the hit tick.
        self.bad(lambda c,r,e,w,h:r[800].update(shot_active_after="1"))
    def test_bridge_did_not_open(self):
        self.bad(lambda c,r,e,w,h:r[800].update(bridge_after="0",bridge_cells="0"))
    def test_bridge_not_stable(self):
        self.bad(lambda c,r,e,w,h:r[825].update(bridge_after="0",bridge_cells="0"))
    def test_switch_not_overlapped(self):
        self.bad(lambda c,r,e,w,h:r[800].update(shot_x_before_milli="300000"))
    def test_fake_controller_fire_frame(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_FIRE_GUEST frame=2103","REARM_FIRE_GUEST frame=2102"))
    def test_fake_controller_open_frame(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_BRIDGE_OPEN_GUEST frame=2111","REARM_BRIDGE_OPEN_GUEST frame=2110"))
    def test_host_fake_approach_position(self):
        self.bad(lambda c,r,e,w,h:h.replace("x_milli=640000","x_milli=710000"))
    def test_host_too_early_stable(self):
        self.bad(lambda c,r,e,w,h:h.replace("REARM_STABLE_GUEST frame=2160","REARM_STABLE_GUEST frame=2113"))
    def test_rearm_without_100_closed_frames(self):
        self.bad(lambda c,r,e,w,h:r[480].update(bridge_cells="6"))
    def test_forged_third_scene_edge(self):
        self.bad(lambda c,r,e,w,h:e[5].update(bridge_after="1"))
    def test_unmatched_weapon_tick(self):
        self.bad(lambda c,r,e,w,h:w[805].update(frame="9999"))
    def test_old_yellow_first_impact_missing(self):
        self.bad(lambda c,r,e,w,h:r[373].update(bridge_cells="0"))
if __name__=="__main__":
    unittest.main()
