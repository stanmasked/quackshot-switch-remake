"""Stage 90: reject fake or abbreviated guest controller transcripts."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.verify_live_timeline import validate_timeline


def sample_fixture():
    events = []
    milestones = [
        ("ENTER", 217, 48, 40000, 188000, 0),
        ("JUMP_RISE", 295, 4, 40000, 185000, 0),
        ("MOVE_RIGHT", 398, 2, 40000, 188000, 0),
        ("REVERSE_LEFT", 523, 1, 150000, 188000, 0),
        ("FIRE_INPUT", 606, 8, 60000, 188000, 0),
        ("SHOT_ACTIVE", 607, 0, 60000, 188000, 1),
        ("RETURN_MAP", 746, 64, 40000, 188000, 0),
    ]
    for name, frame, mask, x, y, shots in milestones:
        events.append(dict(event=name, frame=str(frame), input_bits=str(mask),
                           x_milli=str(x), y_milli=str(y), shots=str(shots),
                           source="sdl_controller_observed"))
    samples = []
    for frame in range(220, 746, 5):
        if 395 <= frame < 500:
            mask, x = 2, 40000 + int((frame - 395) * 1500)
        elif 500 <= frame < 580:
            mask, x = 1, 197500 - int((frame - 500) * 1750)
        else:
            mask, x = 0, 60000
        if frame == 605:
            mask = 8
        y = 124000 if 300 <= frame <= 335 else 188000
        sample = dict(seq=str(len(samples)), frame=str(frame),
                      ticks_ms=str(frame * 17), stage="0",
                      input_bits=str(mask), x_milli=str(x),
                      y_milli=str(y), vy_milli="0",
                      grounded="0" if y < 188000 else "1",
                      active_shots="1" if frame in (610, 615) else "0",
                      attached_shots="0", stunned_enemies="0")
        samples.append(sample)
    return samples, events


class TimelineWitnessTests(unittest.TestCase):
    def setUp(self):
        self.motion, self.events = sample_fixture()

    def test_good_sustained_input(self):
        out = validate_timeline(self.motion, self.events)
        self.assertGreater(out["right_travel_px"], 15)
        self.assertGreater(out["left_travel_px"], 8)
        self.assertEqual(out["new_shot_delay_frames"], 1)

    def test_synthetic_route_source_rejected(self):
        self.events[0]["source"] = "qs_step_simulated_inputs"
        with self.assertRaisesRegex(ValueError, "scripted route"):
            validate_timeline(self.motion, self.events)

    def test_short_right_input_rejected(self):
        for row in self.motion:
            if int(row["input_bits"]) == 2 and int(row["frame"]) > 415:
                row["input_bits"] = "0"
        with self.assertRaisesRegex(ValueError, "right held-input"):
            validate_timeline(self.motion, self.events)

    def test_no_reverse_rejected(self):
        for row in self.motion:
            if row["input_bits"] == "1":
                row["x_milli"] = "120000"
        with self.assertRaisesRegex(ValueError, "reverse movement"):
            validate_timeline(self.motion, self.events)

    def test_missing_jump_rejected(self):
        for row in self.motion:
            row["y_milli"] = "188000"
        with self.assertRaisesRegex(ValueError, "jump arc"):
            validate_timeline(self.motion, self.events)

    def test_replayed_nonmonotone_frame_rejected(self):
        self.motion[30]["frame"] = self.motion[29]["frame"]
        with self.assertRaisesRegex(ValueError, "nonmonotone"):
            validate_timeline(self.motion, self.events)

    def test_motion_gap_rejected(self):
        # Keep seqs sequential so the failure comes specifically from the gap.
        self.motion = [r for r in self.motion if not 550 <= int(r["frame"]) <= 590]
        for i, row in enumerate(self.motion):
            row["seq"] = str(i)
        with self.assertRaisesRegex(ValueError, "unobserved gap"):
            validate_timeline(self.motion, self.events)

    def test_missing_new_shot_rejected(self):
        for row in self.motion:
            row["active_shots"] = "0"
        with self.assertRaisesRegex(ValueError, "shot absent"):
            validate_timeline(self.motion, self.events)

    def test_modified_stage_rejected(self):
        self.motion[8]["stage"] = "1"
        with self.assertRaisesRegex(ValueError, "sequence/stage"):
            validate_timeline(self.motion, self.events)

    def test_missing_event_rejected(self):
        self.events.pop(5)
        with self.assertRaisesRegex(ValueError, "milestones"):
            validate_timeline(self.motion, self.events)


if __name__ == "__main__":
    unittest.main()
