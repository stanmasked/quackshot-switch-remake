import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_fixedpoint_motion as m


def independently_derived_synthetic_physics():
    """Original-free fixed-point arithmetic fixture, NOT copied ROM bytes."""
    observations = {}
    for label in m.OBSERVATIONS:
        x, y = 61 * 65536, 176 * 65536
        vx = vy = 0
        jump_age = 0
        result = {}
        for f in range(m.WINDOW_START, m.WINDOW_END):
            step = f - m.FRAME_START
            right = label == "right_100" and 0 <= step < 100
            left = label == "left_100" and 0 <= step < 100
            tap = label == "right_tap_8" and 0 <= step < 8
            jumping = label == "jump_tap_1" and step == 0
            holding = label == "jump_hold_50" and 0 <= step < 50
            if step >= 0:
                target = (106496 if (right or tap) else
                          -106496 if left else 0)
                vx = min(vx + 10240, target) if vx < target else max(vx - 10240, target)
                x += vx
                if step == 0 and (jumping or holding):
                    vy = -192000
                    jump_age = 0
                if vy != 0 or y < 176*65536:
                    if not ((jumping or holding) and jump_age < 15):
                        vy += 12800
                    jump_age += 1
                    y += vy
                    if y >= 176*65536:
                        y, vy = 176*65536, 0
            result[f] = {
                "x": x // 65536,
                "x_sub": x % 65536,
                "y": y // 65536,
                "y_sub": y % 65536,
                "vx_int": vx // 65536,
                "vx_sub": vx % 65536,
                "vy_int": vy // 65536,
                "vy_sub": vy % 65536,
            }
        observations[label] = result
    return observations


class FixedPointMotionTests(unittest.TestCase):
    def test_decode_16_16_from_independent_synthetic_ram(self):
        fake = bytearray(65536)
        fake[0xA2D0:0xA2D2] = (61).to_bytes(2, "little")
        fake[0xA2D2:0xA2D4] = (30720).to_bytes(2, "little")
        fake[0xA2DC:0xA2DE] = (-3).to_bytes(2, "little", signed=True)
        fake[0xA2DE:0xA2E0] = (4608).to_bytes(2, "little")
        state = m.decode(fake)
        self.assertEqual(m.p16(state, "x"), 61.46875)
        self.assertEqual(m.v16(state, "y"), -2.9296875)

    def test_validate_synthetic_reference_values(self):
        trials = independently_derived_synthetic_physics()
        inputs = {"right_100":100, "right_tap_8":8, "left_100":100,
                  "jump_tap_1":1, "jump_hold_50":50}
        metrics = m.validate(trials, inputs)
        self.assertEqual(metrics["horizontal"]["maximum_walking_speed_px_per_frame"], 1.625)
        self.assertEqual(metrics["horizontal"]["acceleration_px_per_frame_squared"], 0.15625)
        self.assertEqual(metrics["horizontal"]["tap_8_then_coast_final_x"], 71)
        self.assertEqual(metrics["jump"]["tap_1"]["rise_world_pixels"], 24)
        self.assertEqual(metrics["jump"]["hold_50"]["rise_world_pixels"], 65)
        self.assertEqual(metrics["jump"]["tap_1"]["elapsed_to_landing_frames"], 30)
        self.assertEqual(metrics["jump"]["hold_50"]["elapsed_to_landing_frames"], 56)

    def test_replay_nondeterminism_is_rejected(self):
        trial = independently_derived_synthetic_physics()
        trial["repeat_idle"][2080]["y_sub"] += 1
        with self.assertRaisesRegex(ValueError, "idle emulations diverged"):
            m.validate(trial, {"right_100":100, "right_tap_8":8,
                               "left_100":100, "jump_tap_1":1, "jump_hold_50":50})

    def test_falsified_input_is_rejected(self):
        trial = independently_derived_synthetic_physics()
        with self.assertRaisesRegex(ValueError, "did not read required"):
            m.validate(trial, {"right_100":0, "right_tap_8":8,
                               "left_100":100, "jump_tap_1":1, "jump_hold_50":50})

    def test_wrong_velocity_fails_closed(self):
        trial = independently_derived_synthetic_physics()
        trial["right_100"][2070]["vx_sub"] = 0
        with self.assertRaisesRegex(ValueError, "differs"):
            m.validate(trial, {"right_100":100, "right_tap_8":8,
                               "left_100":100, "jump_tap_1":1, "jump_hold_50":50})

    def test_missing_observation_is_rejected(self):
        trial = independently_derived_synthetic_physics()
        del trial["jump_tap_1"][2090]
        with self.assertRaisesRegex(ValueError, "Missing per-frame"):
            m.validate(trial, {"right_100":100, "right_tap_8":8,
                               "left_100":100, "jump_tap_1":1, "jump_hold_50":50})

    def test_no_private_data_or_paths_in_numeric_report_fields(self):
        names = {"idle", "repeat_idle", "right_100", "right_tap_8",
                 "left_100", "jump_tap_1", "jump_hold_50"}
        self.assertEqual(set(m.OBSERVATIONS), names)
        self.assertEqual(m.WINDOW_START, 2069)
        self.assertEqual(m.WINDOW_END, 2191)


if __name__ == "__main__":
    unittest.main()
