"""Stage 98: prove actual rise/apex/landing, reject fake/shortened jumps."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_jump_arc import verify_jump_arcs


def fixture():
    host=("PIT_LAUNCH_GUEST frame=100 x_milli=280000\n"
          "ROOF_LAUNCH_GUEST frame=180 x_milli=390000\n"
          "CRATE_FIRE_GUEST frame=300 x_milli=912000")
    rows=[]
    for frame in range(330):
        x=280000 if frame<=100 else (
          280000+1900*(frame-100) if frame<=157 else
          390000 if frame<=180 else
          390000+1000*(frame-180) if frame<=213 else 912000)
        if 103<=frame<157:
            elapsed=frame-103
            if elapsed<=27:
                y=185070-int(61328*elapsed/27)
            else:
                y=123742+int(64258*(elapsed-27)/26)
            grounded=0
        elif 184<=frame<213:
            elapsed=frame-184
            if elapsed<=27:
                y=185070-int(61328*elapsed/27)
            else:
                y=123742+int(200*(elapsed-27))
            grounded=0
        else:
            y=124000 if 213<=frame<300 else 188000
            grounded=1
        bits=2+(4 if (103<=frame<=112 or 184<=frame<=195) else 0)+(
              8 if 304<=frame<=313 else 0)
        rows.append(dict(seq=str(frame),frame=str(frame),attempt=str(frame),
                    x_milli=str(x),y_milli=str(y),grounded=str(grounded),
                    input_bits=str(bits),checkpoint="0",mode="1",
                    source="live_controller_qs_step"))
    return host,rows




def blocked_roof_fixture():
    """Late roof B collides with overhang; genuine wall B rescues path."""
    host,rows=fixture()
    host=host.replace("CRATE_FIRE_GUEST",
                      "WALL_RECOVERY_GUEST frame=227 x_milli=548000\nCRATE_FIRE_GUEST")
    for frame in range(184,216):
        r=rows[frame]
        t=frame-184
        if frame==215:
            r["y_milli"]="188000"
            r["grounded"]="1"
        else:
            y=(185070-int(24500*t/10)) if t<=10 else (
                160570+int(27430*(t-10)/20))
            r["y_milli"]=str(y)
            r["grounded"]="0"
        r["x_milli"]=str(390000+2000*(frame-180))
    for frame in range(216,228):
        rows[frame]["x_milli"]=str(460000+int((frame-215)*88000/12))
        rows[frame]["y_milli"]="188000"
        rows[frame]["grounded"]="1"
    for frame in range(228,282):
        r=rows[frame]
        t=frame-228
        r["x_milli"]=str(548000+1600*(frame-227))
        if frame==281:
            r["y_milli"]="188000"; r["grounded"]="1"
        else:
            y=(185070-int(61328*t/27)) if t<=27 else (
               123742+int(64258*(t-27)/26))
            r["y_milli"]=str(y)
            r["grounded"]="0"
        r["input_bits"]="6" if 228<=frame<=239 else "2"
    return host,rows

class GuestArcTests(unittest.TestCase):
    def setUp(self):self.host,self.rows=fixture()
    def test_complete_pit_and_roof_arcs(self):
        r=verify_jump_arcs(self.host,self.rows)
        self.assertEqual(tuple(r["arcs"]),("pit","roof"))
        self.assertEqual(r["arcs"]["roof"]["landing_y_px"],124.0)
    def test_blocked_roof_with_real_recovery_arc(self):
        host,rows=blocked_roof_fixture()
        result=verify_jump_arcs(host,rows)
        self.assertEqual(result["roof_path"],"ground_then_wall_recovery")
        self.assertAlmostEqual(result["arcs"]["roof"]["rise_px"],27.43,places=2)
        self.assertGreater(result["wall_recovery"]["rise_px"],64)

    def test_sparse_telemetry_still_proves_blocked_roof(self):
        # Real Stage 107 run 5 reached Y≈160.656 from Y=188, but
        # three-frame guest telemetry showed just three ascent steps.
        # Downsample only the synthetic blocked ascent while retaining
        # the authenticated B press and genuine wall-recovery arc.
        host,rows=blocked_roof_fixture()
        skipped={185,186,188,189,191,192,193}
        rows=[r for r in rows if int(r["frame"]) not in skipped]
        for i,r in enumerate(rows):
            r["seq"]=str(i)
        result=verify_jump_arcs(host,rows)
        self.assertEqual(result["roof_path"],"ground_then_wall_recovery")
        self.assertEqual(result["arcs"]["roof"]["ascent_samples"],3)
        self.assertGreaterEqual(result["arcs"]["roof"]["descent_samples"],3)
    def test_sparse_blocked_roof_requires_real_wall_recovery(self):
        host,rows=blocked_roof_fixture()
        skipped={185,186,188,189,191,192,193}
        rows=[r for r in rows if int(r["frame"]) not in skipped]
        for i,r in enumerate(rows):
            r["seq"]=str(i)
        host=host.replace("WALL_RECOVERY_GUEST","UNVERIFIED_RECOVERY")
        with self.assertRaises(ValueError):
            verify_jump_arcs(host,rows)
    def test_direct_roof_rejects_extra_recovery_B(self):
        # Preserve the authenticated third B at the wall while replacing
        # only the roof flight with the already-proven direct flight.
        # This combination must not be mislabeled a direct route.
        host,rows=blocked_roof_fixture()
        _,direct_rows=fixture()
        for i in range(184,216):
            rows[i]["y_milli"]=direct_rows[i]["y_milli"]
            rows[i]["grounded"]=direct_rows[i]["grounded"]
        with self.assertRaisesRegex(ValueError,"direct roof must not"):
            verify_jump_arcs(host,rows)
    def test_blocked_roof_without_authorized_recovery(self):
        host,rows=blocked_roof_fixture()
        host=host.replace("WALL_RECOVERY_GUEST","FAKE_WALL_RECOVERY")
        with self.assertRaises(ValueError):
            verify_jump_arcs(host,rows)
    def test_fake_recovery_landing(self):
        host,rows=blocked_roof_fixture()
        rows[281]["x_milli"]="570000"
        with self.assertRaisesRegex(ValueError,"did not clear the wall|landing hides"):
            verify_jump_arcs(host,rows)
    def test_apex_missing(self):
        for row in self.rows[103:157]:row["y_milli"]="184000"
        with self.assertRaisesRegex(ValueError,"height|ascent"):
            verify_jump_arcs(self.host,self.rows)
    def test_no_ground_landing(self):
        for row in self.rows[157:184]:row["grounded"]="0"
        # The next roof host anchor must itself be grounded; absence
        # of a legitimate pit landing must fail even before arc analysis.
        with self.assertRaisesRegex(ValueError,"landed|grounded release"):
            verify_jump_arcs(self.host,self.rows)
    def test_roof_lands_on_wrong_surface(self):
        self.rows[213]["y_milli"]="188000"
        with self.assertRaisesRegex(ValueError,"landing"):
            verify_jump_arcs(self.host,self.rows)
    def test_apex_starts_with_physics_teleport(self):
        for row in self.rows[104:130]:row["y_milli"]="123742"
        with self.assertRaisesRegex(ValueError,"ascent"):
            verify_jump_arcs(self.host,self.rows)
    def test_no_horizontal_travel(self):
        for row in self.rows[103:158]:row["x_milli"]="281900"
        with self.assertRaisesRegex(ValueError,"landing"):
            verify_jump_arcs(self.host,self.rows)
    def test_airborne_position_teleport(self):
        self.rows[142]["x_milli"]="999999"
        with self.assertRaisesRegex(ValueError,"teleport"):
            verify_jump_arcs(self.host,self.rows)
    def test_airborne_vertical_teleport(self):
        self.rows[143]["y_milli"]=str(int(self.rows[143]["y_milli"])+55000)
        with self.assertRaisesRegex(ValueError,"vertical teleport"):
            verify_jump_arcs(self.host,self.rows)
    def test_launch_rejects_corrupted_previous_ground_height(self):
        # The real B edge and flight remain unchanged. Only the
        # preceding grounded guest position has been forged.
        self.rows[102]["y_milli"]="300000"
        with self.assertRaisesRegex(ValueError,"continuous grounded takeoff"):
            verify_jump_arcs(self.host,self.rows)
    def test_recovery_rejects_corrupted_previous_ground_height(self):
        host,rows=blocked_roof_fixture()
        rows[227]["y_milli"]="300000"
        with self.assertRaisesRegex(ValueError,"continuous grounded takeoff"):
            verify_jump_arcs(host,rows)
    def test_reversed_vertical_motion_during_ascent(self):
        # A 7px disturbance looks velocity-plausible, but reverses
        # the physical upward trajectory before the apex.
        self.rows[108]["y_milli"]=str(int(self.rows[108]["y_milli"])+7000)
        with self.assertRaisesRegex(ValueError,"reverses downward"):
            verify_jump_arcs(self.host,self.rows)
    def test_reversed_vertical_motion_during_descent(self):
        # Another velocity-plausible alteration makes the descending
        # hero climb again without an additional authenticated B press.
        self.rows[147]["y_milli"]=str(int(self.rows[147]["y_milli"])-8000)
        with self.assertRaisesRegex(ValueError,"reverses upward"):
            verify_jump_arcs(self.host,self.rows)
    def test_touchdown_vertical_teleport(self):
        # The last airborne sample is corrupted, but touchdown itself
        # and all recorded horizontal coordinates look legitimate.
        self.rows[212]["y_milli"]="182000"
        with self.assertRaisesRegex(ValueError,"vertical teleport"):
            verify_jump_arcs(self.host,self.rows)
    def test_input_jumps_are_required(self):
        for row in self.rows[103:113]:row["input_bits"]="2"
        with self.assertRaises(ValueError):verify_jump_arcs(self.host,self.rows)
    def test_lost_guest_frame_rejected(self):
        self.rows[130]["frame"]="160"
        with self.assertRaises(ValueError):verify_jump_arcs(self.host,self.rows)


if __name__=="__main__":unittest.main()
