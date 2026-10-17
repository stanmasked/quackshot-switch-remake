import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_playable_physics as physics
from genesis_libretro_probe import parse_script


def synthetic_observations(start=10, end=120):
    baseline = {"x": 61, "y": 176, "vx_int": 0, "vy_int": 0,
                "screen_x": 45, "screen_y": 133, "plunger_slot_active": 0}
    runs = {name: {} for name in
            ("idle", "repeat_idle", "right", "left", "jump", "fire")}
    for frame in range(start-1, end+1):
        for run in runs.values():
            run[frame] = dict(baseline)
        if frame >= start:
            runs["right"][frame]["x"] += min((frame-start)*2, 250)
            runs["left"][frame]["x"] = max(16, 61-(frame-start)*2)
            if frame <= start+60:
                runs["jump"][frame]["y"] -= int(50*(1-abs(2*(frame-start)/60-1)))
            runs["fire"][frame]["plunger_slot_active"] = int(
                start+1 <= frame <= start+25)
    return runs


class PlayablePhysicsTests(unittest.TestCase):
    def test_coordinate_byte_order_and_signed_velocity(self):
        ram = bytearray(65536)
        ram[0xA2D0:0xA2D2] = bytes((0x01, 0x02))
        ram[0xA2D4:0xA2D6] = bytes((0xB0, 0x00))
        ram[0xA2D8:0xA2DA] = bytes((0xFE, 0xFF))
        ram[0xA2DC:0xA2DE] = bytes((0xFD, 0xFF))
        ram[0x9C00] = 0x60
        state = physics.decode_player_state(bytes(ram))
        self.assertEqual(state["x"], 513)
        self.assertEqual(state["y"], 176)
        self.assertEqual(state["vx_int"], -2)
        self.assertEqual(state["vy_int"], -3)
        self.assertEqual(state["plunger_slot_active"], 1)

    def test_rejects_non_64k_ram(self):
        for n in (0, 65535, 65537):
            with self.subTest(n=n), self.assertRaisesRegex(ValueError, "65536"):
                physics.decode_player_state(bytes(n))

    def test_steady_motion_and_jump_apex(self):
        observed = synthetic_observations()
        metrics = physics.analyze_observations(observed, 10, 120)
        self.assertEqual(metrics["initial_position_world_pixels"], {"x":61, "y":176})
        self.assertEqual(metrics["movement"]["measured_right_px_per_frame"], 2.0)
        self.assertEqual(metrics["movement"]["left_minimum_world_x"], 16)
        self.assertEqual(metrics["jump"]["peak_rise_world_pixels"], 50)
        self.assertEqual(metrics["jump"]["landing_frame"], 70)
        self.assertEqual(metrics["fire"]["first_active_plunger_slot_frame"], 11)

    def test_detects_nondeterministic_control(self):
        observed = synthetic_observations()
        observed["repeat_idle"][20]["x"] = 99
        with self.assertRaisesRegex(ValueError, "deterministic"):
            physics.analyze_observations(observed, 10, 120)

    def test_refuses_mismatched_before_input(self):
        observed = synthetic_observations()
        observed["right"][9]["x"] = 62
        with self.assertRaisesRegex(ValueError, "before controls"):
            physics.analyze_observations(observed, 10, 120)

    def test_refuses_nonresponsive_direction(self):
        observed = synthetic_observations()
        observed["right"] = {k: dict(v) for k, v in observed["idle"].items()}
        with self.assertRaisesRegex(ValueError, "world-X behavior"):
            physics.analyze_observations(observed, 10, 120)

    def test_refuses_fake_jump(self):
        observed = synthetic_observations()
        observed["jump"] = {k: dict(v) for k, v in observed["idle"].items()}
        with self.assertRaisesRegex(ValueError, "Jump arc"):
            physics.analyze_observations(observed, 10, 120)

    def test_refuses_missing_plunger(self):
        observed = synthetic_observations()
        observed["fire"] = {k: dict(v) for k, v in observed["idle"].items()}
        with self.assertRaisesRegex(ValueError, "active plunger"):
            physics.analyze_observations(observed, 10, 120)

    def test_missing_frame_fails_closed(self):
        observed = synthetic_observations()
        del observed["jump"][120]
        with self.assertRaisesRegex(ValueError, "Missing first/last"):
            physics.analyze_observations(observed, 10, 120)

    def test_entry_input_stops_before_active_window(self):
        schedule = parse_script(physics.DEFAULT_ENTRY)
        self.assertEqual(schedule[:3], [
            (450, 462, "START"), (680, 692, "START"), (850, 862, "START")])
        self.assertLess(max(end for _,end,_ in schedule), 2070)

    def test_missing_variant_refused(self):
        observed = synthetic_observations()
        del observed["fire"]
        with self.assertRaisesRegex(ValueError, "Missing independent"):
            physics.analyze_observations(observed, 10, 120)


if __name__ == "__main__":
    unittest.main()
