#!/usr/bin/env python3
"""Stage 90: long, measured input timeline INSIDE the NeXium guest.

The input CSV is emitted from qs_motion_emit in the visible game loop,
not from the independent Stage 88 game-core route self-test.
"""
import csv
import json
import pathlib
import sys

# The same module is imported by CTest and invoked directly from the
# repo's tools/ directory by NeXium's shell probe. Resolve the sibling
# verifier consistently in both contexts.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_live_control import check as verify_milestones

MOTION_COLUMNS = ["seq", "frame", "ticks_ms", "stage", "input_bits",
                  "x_milli", "y_milli", "vy_milli", "grounded",
                  "active_shots", "attached_shots", "stunned_enemies"]
EVENTS = ["ENTER", "JUMP_RISE", "MOVE_RIGHT", "REVERSE_LEFT",
          "FIRE_INPUT", "SHOT_ACTIVE", "RETURN_MAP"]
RIGHT, LEFT, JUMP, FIRE = 2, 1, 4, 8


def longest_run(rows, required_bit, opposite_bit):
    """Longest consecutive, genuine held-input interval; 5-frame sampling."""
    best = []
    run = []
    for row in rows:
        mask = row["input_bits"]
        if (mask & required_bit) and not (mask & opposite_bit):
            if run and row["frame"] - run[-1]["frame"] > 8:
                if len(run) > len(best):
                    best = run
                run = []
            run.append(row)
        else:
            if len(run) > len(best):
                best = run
            run = []
    return run if len(run) > len(best) else best


def validate_timeline(samples, events):
    """Pure validator for both CI evidence and hostile/mutated test fixtures."""
    if len(events) != 7 or [r.get("event") for r in events] != EVENTS:
        raise ValueError("live controller milestones absent or reordered")
    if any(r.get("source") != "sdl_controller_observed" for r in events):
        raise ValueError("scripted route mislabeled as live input")
    stages = []
    for event in events:
        try:
            stages.append({"frame": int(event["frame"]),
                           "input_bits": int(event["input_bits"]),
                           "x_milli": int(event["x_milli"]),
                           "y_milli": int(event["y_milli"]),
                           "shots": int(event["shots"])})
        except (KeyError, TypeError) as exc:
            raise ValueError(f"invalid live event: {exc}") from exc
    marks = [s["frame"] for s in stages]
    if marks != sorted(marks) or len(set(marks)) != len(marks):
        raise ValueError("nonmonotone live-event order")
    if not 70 <= marks[-1] - marks[0] <= 1800:
        raise ValueError("live interval too short/long for real controls")
    if len(samples) < 35 or len(samples) > 256:
        raise ValueError("motion CSV missing significant gameplay time")
    rows = []
    for index, sample in enumerate(samples):
        try:
            row = {k: int(sample[k]) for k in MOTION_COLUMNS}
        except (ValueError, KeyError, TypeError) as exc:
            raise ValueError(f"motion CSV field invalid: {exc}") from exc
        if row["seq"] != index or row["stage"] != 0:
            raise ValueError("motion CSV sequence/stage corrupted")
        if not (marks[0] <= row["frame"] < marks[-1] and
                -300000 <= row["y_milli"] <= 500000 and
                0 <= row["x_milli"] <= 1280000 and
                0 <= row["input_bits"] < 128 and
                0 <= row["active_shots"] <= 8):
            raise ValueError("out-of-bounds or out-of-stage motion sample")
        if rows and (row["frame"] <= rows[-1]["frame"] or
                     row["ticks_ms"] < rows[-1]["ticks_ms"]):
            raise ValueError("nonmonotone physical motion timeline")
        rows.append(row)
    max_gap = max(b["frame"] - a["frame"] for a, b in zip(rows, rows[1:]))
    if max_gap > 12:
        raise ValueError("motion timeline has an unobserved gap")
    right = longest_run(rows, RIGHT, LEFT)
    left = longest_run(rows, LEFT, RIGHT)
    if len(right) < 8 or right[-1]["frame"] - right[0]["frame"] < 35:
        raise ValueError("right held-input sequence too short")
    if len(left) < 6 or left[-1]["frame"] - left[0]["frame"] < 25:
        raise ValueError("left held-input sequence too short")
    right_travel = (right[-1]["x_milli"] - right[0]["x_milli"]) / 1000
    left_travel = (left[0]["x_milli"] - left[-1]["x_milli"]) / 1000
    if right_travel <= 15:
        raise ValueError("held right input did not produce meaningful movement")
    if left_travel <= 8:
        raise ValueError("held left input did not produce meaningful reverse movement")
    if not (stages[1]["y_milli"] <= stages[0]["y_milli"] - 1000):
        raise ValueError("jump milestone lacks upward displacement")
    jump_rise = (rows[0]["y_milli"] - min(r["y_milli"] for r in rows)) / 1000
    if not (55 < jump_rise < 70):
        raise ValueError("full held-button jump arc missing")
    fire_frame, shot_frame = marks[4], marks[5]
    if not 1 <= shot_frame - fire_frame <= 4:
        raise ValueError("new projectile appeared too late for Y press")
    if not any((r["input_bits"] & FIRE) and
               abs(r["frame"] - fire_frame) <= 1 for r in rows):
        raise ValueError("real Y input edge absent from motion CSV")
    if not any(r["active_shots"] >= stages[5]["shots"] > 0 and
               abs(r["frame"] - shot_frame) <= 5 for r in rows):
        raise ValueError("shot absent from real motion timeline")
    if right[0]["frame"] <= marks[1] or left[0]["frame"] <= marks[2] - 5:
        raise ValueError("physical control order contradicts milestones")
    return {
        "live_input_window_frames": marks[-1] - marks[0],
        "motion_samples": len(rows),
        "max_sample_frame_gap": max_gap,
        "longest_right_hold_frames": right[-1]["frame"] - right[0]["frame"],
        "longest_left_hold_frames": left[-1]["frame"] - left[0]["frame"],
        "right_travel_px": round(right_travel, 3),
        "left_travel_px": round(left_travel, 3),
        "jump_rise_px": round(jump_rise, 3),
        "new_shot_delay_frames": shot_frame - fire_frame,
        "source": "live gamepad input observed by NeXium guest, not autonomous route",
    }


def read_csv(path, expected=None):
    with open(path, encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if expected is not None and reader.fieldnames != expected:
            raise ValueError("incorrect motion CSV header")
        return list(reader)


def main(argv):
    if len(argv) != 5:
        raise SystemExit("usage: verify_live_timeline.py <motion.csv> <live.csv> <runtime.csv> <summary.txt>")
    summary = pathlib.Path(argv[4])
    try:
        # Preserve the previous strict independent input/world/runtime test.
        prior = verify_milestones(argv[2], argv[3], argv[1])
        details = validate_timeline(read_csv(argv[1], MOTION_COLUMNS),
                                    read_csv(argv[2]))
        details["milestones"] = prior["milestones"]
    except (ValueError, OSError, KeyError, TypeError) as exc:
        summary.write_text("FAIL: live-controller timeline: " + str(exc) + "\n")
        raise SystemExit("FAIL: live-controller timeline: " + str(exc))
    result = "LIVE_TIMELINE_GUEST_PASS " + json.dumps(details, sort_keys=True) + "\n"
    summary.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main(sys.argv)
