#!/usr/bin/env python3
"""Stage282: audit private numeric collision reports without exposing raw game data.

Requires two deterministic positive replays and a distinct no-contact control.
Does not infer a gameplay identity, prove exact engine hitboxes, or change physics.
"""
import argparse
import json
import re
from pathlib import Path

REPORT_SCHEMA = "private_genesis_libretro_numeric_probe_v1"
PROFILE_TRACE_SCHEMA = "quackshot_private_collision_profile_trace_v1"
PROFILE_SCHEMA = "quackshot_private_collision_profile_numeric_v1"
ACCESS_SCHEMA = "quackshot_private_m68k_cpu_access_trace_v2"
TARGET_SLOT = "0xFF9000"
TARGET_TYPE = 3
TARGET_PROFILE = 0x3D
EVENT_ADDRESS = 0xFFEE09
EVENT_PC = 0x00B866
DEFAULT_FRAME = 2353


def _check(condition, why):
    if not condition:
        raise ValueError(why)


def _rectangle(rect):
    _check(type(rect) is dict, "invalid collision rectangle")
    keys = ("x_min", "x_max", "y_min", "y_max")
    _check(all(type(rect.get(k)) is int for k in keys), "non-integer collision rectangle")
    _check(rect["x_min"] <= rect["x_max"] and rect["y_min"] <= rect["y_max"],
           "reversed collision rectangle")
    return {key: rect[key] for key in keys}


def _intersection(first, second):
    bounds = {
        "x_min": max(first["x_min"], second["x_min"]),
        "x_max": min(first["x_max"], second["x_max"]),
        "y_min": max(first["y_min"], second["y_min"]),
        "y_max": min(first["y_max"], second["y_max"]),
    }
    if bounds["x_min"] > bounds["x_max"] or bounds["y_min"] > bounds["y_max"]:
        return None
    return bounds


def _validate_schedule(ranges, frames):
    _check(type(ranges) is list, "missing input schedule")
    accepted = {"B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",
                "A", "X", "L", "R"}
    normalized = []
    for entry in ranges:
        _check(type(entry) is dict, "invalid input range")
        start, stop, button = (entry.get("first_frame"), entry.get("end_exclusive"),
                               entry.get("button"))
        _check(type(start) is int and type(stop) is int and
               0 <= start < stop <= frames and button in accepted,
               "invalid input schedule interval")
        normalized.append((start, stop, button))
    _check(len(set(normalized)) == len(normalized), "duplicate input range")
    return sorted(normalized)


def _require_privacy_false(row):
    for key in ("raw_rom_exported", "raw_ram_exported", "frame_pixels_exported",
                "audio_exported", "collision_profile_bytes_exported"):
        if key in row:
            _check(row[key] is False, "unsafe private data export flag")


def _validate_run(report, frame, expected_sha256):
    _check(type(report) is dict and report.get("schema") == REPORT_SCHEMA,
           "wrong probe report schema")
    rom_sha = report.get("reference_rom_sha256")
    _check(type(rom_sha) is str and rom_sha.lower() == expected_sha256.lower(),
           "different original ROM revision")
    count = report.get("frames_requested")
    _check(type(count) is int and frame < count <= 10000, "invalid run frame count")
    schedule = _validate_schedule(report.get("input_ranges"), count)
    _check(report.get("ram_checkpoint_phase") == "post_retro_run_before_next_input",
           "incompatible sampling phase")
    trace = report.get("collision_profile_trace")
    _check(type(trace) is dict and trace.get("schema") == PROFILE_TRACE_SCHEMA and
           trace.get("status") == "COMPLETE" and
           trace.get("sampling_phase") == "post_retro_run_before_next_input",
           "invalid collision profile trace")
    _require_privacy_false(trace)
    frames = trace.get("frames")
    _check(type(frames) is list and 0 < len(frames) <= 24 and
           all(type(row) is dict and type(row.get("frame")) is int for row in frames),
           "invalid bounded collision samples")
    requested = trace.get("frame_indices_requested")
    observed = [row["frame"] for row in frames]
    _check(type(requested) is list and requested == sorted(observed) and
           len(set(observed)) == len(observed) and frame in observed,
           "requested and captured frame mismatch")
    sample = next(row for row in frames if row["frame"] == frame)
    _check(sample.get("schema") == PROFILE_SCHEMA and sample.get("frame") == frame,
           "wrong collision snapshot schema")
    _require_privacy_false(sample)
    player = _rectangle(sample.get("player_collision_rectangle"))
    objects = sample.get("objects")
    _check(type(objects) is list and len(objects) == 2 and
           [obj.get("cpu_address") for obj in objects if type(obj) is dict] ==
           ["0xFF9000", "0xFF9040"], "unexpected collision object slots")
    obj = objects[0]
    _check(obj.get("object_type") == TARGET_TYPE and
           obj.get("collision_profile_index") == TARGET_PROFILE,
           "target object type/profile mismatch")
    bounds = _rectangle(obj.get("collision_bounds"))
    actual = _intersection(player, bounds)
    _check(obj.get("overlaps_player_collision_rectangle") is (actual is not None),
           "reported overlap contradicts computed rectangles")
    _check(obj.get("intersection_with_player") == actual,
           "reported intersection contradicts computed rectangles")
    event_hex = sample.get("global_event_ee09")
    _check(type(event_hex) is str and re.fullmatch(r"0x[0-9A-Fa-f]{2}", event_hex) is not None,
           "invalid global collision event")
    event_value = int(event_hex, 16)
    cpu = report.get("cpu_access_trace")
    _check(type(cpu) is dict and cpu.get("schema") == ACCESS_SCHEMA and
           cpu.get("status") == "COMPLETE" and cpu.get("overflow_count") == 0,
           "missing complete CPU access trace v2")
    _require_privacy_false(cpu)
    window = cpu.get("frame_window_inclusive")
    _check(type(window) is list and len(window) == 2 and
           all(type(x) is int for x in window) and window[0] <= frame <= window[1] and
           0 <= window[1] - window[0] < 128, "CPU trace does not cover contact frame")
    events = cpu.get("events")
    _check(type(events) is list and cpu.get("event_count") == len(events),
           "incomplete CPU access event list")
    _check(any(r.get("start_inclusive") == "0xEE08" and
               r.get("end_exclusive") == "0xEE0B"
               for r in cpu.get("write_watch_ranges", []) if type(r) is dict),
           "CPU writer watch range missing")
    writes = []
    for e in events:
        _check(type(e) is dict and type(e.get("frame")) is int and
               window[0] <= e["frame"] <= window[1], "invalid CPU access event")
        if (e["frame"] == frame and e.get("cpu_address") == EVENT_ADDRESS and
                e.get("access_kind") == "W"):
            _check(e.get("width_bytes") == 1 and type(e.get("write_value")) is int and
                   0 <= e["write_value"] <= 255, "invalid collision writer width/value")
            writes.append((e.get("instruction_pc"), e["write_value"]))
    return {"frame": frame, "schedule": schedule, "frames_requested": count,
            "core": report.get("core"), "sample": sample, "overlap": actual is not None,
            "event": event_value, "writes": writes}


def audit(positive_a, positive_b, negative, frame, expected_sha256):
    _check(type(frame) is int and 0 <= frame < 10000, "invalid requested frame")
    _check(type(expected_sha256) is str and
           re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha256) is not None,
           "SHA-256 of original ROM is required")
    a, b, control = (_validate_run(row, frame, expected_sha256)
                     for row in (positive_a, positive_b, negative))
    _check(a["frames_requested"] == b["frames_requested"] == control["frames_requested"],
           "different run lengths")
    _check(a["core"] == b["core"] == control["core"], "different emulator core metadata")
    _check(a["schedule"] == b["schedule"] and a["schedule"] != control["schedule"],
           "positive replays must match and control must have different inputs")
    _check(a["sample"] == b["sample"] and a["writes"] == b["writes"],
           "positive replays are not numerically deterministic")
    _check(a["overlap"] and b["overlap"] and a["event"] != 0 and b["event"] != 0,
           "positive pair lacks overlap or event state")
    _check((EVENT_PC, a["event"]) in a["writes"],
           "positive pair lacks matching writer PC and event value")
    _check(not control["overlap"] and control["event"] == 0 and
           all(value == 0 for _, value in control["writes"]),
           "negative control shows overlap or collision event")
    return {
        "schema": "quackshot_private_collision_positive_witness_audit_v1",
        "status": "NUMERIC_WRITER_AND_OVERLAP_REPRODUCED",
        "frame": frame,
        "positive_repeat_count": 2,
        "negative_control_count": 1,
        "target_object_type": TARGET_TYPE,
        "target_profile_index": TARGET_PROFILE,
        "collision_writer_pc": f"0x{EVENT_PC:06X}",
        "collision_write_address": f"0x{EVENT_ADDRESS:06X}",
        "raw_rom_exported": False,
        "raw_ram_exported": False,
        "frame_pixels_exported": False,
        "audio_exported": False,
        "collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
        "limitations": "Numeric coincidence and CPU writer observed; full hitbox parity and gameplay object identity remain unverified.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--positive-a", type=Path, required=True)
    parser.add_argument("--positive-b", type=Path, required=True)
    parser.add_argument("--negative", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--frame", type=int, default=DEFAULT_FRAME)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        _check(len({args.positive_a.resolve(), args.positive_b.resolve(),
                    args.negative.resolve()}) == 3, "three independent files required")
        reports = [json.loads(path.read_text(encoding="utf-8"))
                   for path in (args.positive_a, args.positive_b, args.negative)]
        result = audit(*reports, args.frame, args.expected_sha256)
        payload = json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
        if args.output:
            args.output.write_text(payload, encoding="utf-8")
        else:
            print(payload, end="")
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
        parser.exit(2, "Stage282: audit incomplete or invalid; no positive collision certified.\n")


if __name__ == "__main__":
    main()