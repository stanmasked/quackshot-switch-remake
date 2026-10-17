import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_city_obstacle_probe as probe

READS = {
    "idle":0, "right":450, "right_repeat":450,
    "near_wall_jump":480, "early_jump":480, "wall_reverse":360,
}


def synthetic_trials():
    """Artificial numeric fixture: NO original ROM program, RAM or assets."""
    def point(x, y, vx=1, vy=0):
        return {
            "x":x,"x_fraction":0,"y":y,"y_fraction":0,
            "vx_int":vx,"vy_int":vy,
            "sprite_ram_x_candidate":max(0,x-16),
            "sprite_ram_y_candidate":max(0,y-43),
        }
    trials = {}
    for name in probe.RUNS:
        track = {}
        for f in range(probe.FIRST, probe.LAST_EXCLUSIVE):
            if name == "idle":
                row = point(61,176,0)
            else:
                x = min(498, 347 + int((f-2250)*1.625))
                y = 176 if f < 2281 else (177 if f < 2297 else 208)
                vx = int(x < 498)
                if f == 2343:
                    x, vx = 497, 1  # contact resolved at frame 2344
                if name == "near_wall_jump":
                    if f >= 2310:
                        x = 445+int((f-2310)*1.62)
                        y = 143 if f <= 2350 else 176
                    vx = 1
                elif name == "early_jump":
                    if f >= 2280 and f < 2370:
                        y = 111 if f < 2325 else 201
                    if f >= 2370:
                        x = 499
                        y = 208
                        vx = 0
                elif name == "wall_reverse":
                    if f >= 2360:
                        x = max(397,498-int((f-2360)*1.62))
                        vx = -1 if x>397 else 0
                if name in ("right","right_repeat") and f >= 2344:
                    x,y,vx = 498,208,0
                row = point(x,y,vx)
            track[f] = row
        trials[name] = track
    # Exact discrete reference events; independently constructed fixtures.
    for name in ("right","right_repeat","near_wall_jump","early_jump","wall_reverse"):
        trials[name][2250] = dict(trials["right"][2250])
    for name in ("right","right_repeat","near_wall_jump","early_jump","wall_reverse"):
        if name in ("right","right_repeat","wall_reverse"):
            trials[name][2281]["y"] = 177
            trials[name][2297]["y"] = 208
    # Wall reverse retained identical history up to 2360.
    for f in range(2250,2360):
        trials["wall_reverse"][f] = dict(trials["right"][f])
    return trials


class GenesisCityObstacleTests(unittest.TestCase):
    def test_original_free_synthetic_fixture_accepts_grounded_events(self):
        trials = synthetic_trials()
        result = probe.validate(trials, READS)
        self.assertEqual(result["ledge"]["vertical_floor_separation_pixels"], 32)
        self.assertEqual(result["ledge"]["first_lower_y_frame"], 2281)
        self.assertEqual(result["ledge"]["first_lower_floor_frame"], 2297)
        self.assertEqual(result["wall"]["first_zero_horizontal_velocity_frame"], 2344)
        self.assertGreater(result["timing"]["near_wall_jump_x_at_frame_2500"], 690-1)

    def test_numeric_ram_decoder_endianness(self):
        fake = bytearray(65536)
        fake[0xA2D0:0xA2D2] = (498).to_bytes(2,"little")
        fake[0xA2D4:0xA2D6] = (208).to_bytes(2,"little")
        fake[0xA2D8:0xA2DA] = (-1).to_bytes(2,"little",signed=True)
        out = probe.decode(bytes(fake))
        self.assertEqual(out["x"],498)
        self.assertEqual(out["y"],208)
        self.assertEqual(out["vx_int"],-1)

    def test_wrong_ram_length_rejected(self):
        with self.assertRaisesRegex(ValueError, "64 KiB"):
            probe.decode(bytes(100))

    def test_nondeterministic_replay_rejected(self):
        trials = synthetic_trials()
        trials["right_repeat"][2400]["x"] = 499
        with self.assertRaisesRegex(ValueError, "diverged"):
            probe.validate(trials, READS)

    def test_missing_input_callbacks_rejected(self):
        wrong = dict(READS, near_wall_jump=0)
        with self.assertRaisesRegex(ValueError, "input"):
            probe.validate(synthetic_trials(), wrong)

    def test_missing_frame_rejected(self):
        trials = synthetic_trials()
        del trials["right"][2280]
        with self.assertRaisesRegex(ValueError, "frame"):
            probe.validate(trials, READS)

    def test_wall_not_present_is_rejected(self):
        trials = synthetic_trials()
        for f in range(2344,2511):
            trials["right"][f]["x"] = 498+(f-2344)
        trials["right_repeat"] = {f:dict(v) for f,v in trials["right"].items()}
        with self.assertRaisesRegex(ValueError, "unexpectedly penetrated"):
            probe.validate(trials, READS)

    def test_jump_not_crossing_wall_rejected(self):
        trials = synthetic_trials()
        for f in range(2350,2511):
            trials["near_wall_jump"][f]["x"] = 498
        with self.assertRaisesRegex(ValueError, "bypass"):
            probe.validate(trials, READS)

    def test_early_jump_crossing_wall_rejected(self):
        trials = synthetic_trials()
        trials["early_jump"][2400]["x"] = 520
        with self.assertRaisesRegex(ValueError, "Early jump"):
            probe.validate(trials, READS)

    def test_reverse_not_freed_rejected(self):
        trials = synthetic_trials()
        trials["wall_reverse"][2390]["x"] = 498
        with self.assertRaisesRegex(ValueError, "Reverse"):
            probe.validate(trials, READS)

    def test_public_witness_has_sha_and_no_original_payload(self):
        witness = Path(__file__).resolve().parents[1]/"research/observations/user_rom_city_obstacles_2026-10-18.json"
        if not witness.exists():
            return
        data = json.loads(witness.read_text())
        self.assertEqual(data["reference_sha256"],
                         "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34")
        self.assertEqual(data["metrics"]["wall"]["stopped_world_xy"]["x"],498)
        self.assertNotIn("video_frames",data)
        self.assertNotIn("raw_ram",data)


if __name__ == "__main__":
    unittest.main()
