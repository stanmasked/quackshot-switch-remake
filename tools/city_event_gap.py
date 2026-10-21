#!/usr/bin/env python3
"""ROM-free original-vs-synthetic CITY landmark gap report.

Run the EXISTING Stage170 synthetic executable and compare its event marker
with numeric-only reference evidence already in the public repository.
This never launches an original ROM, decodes game assets, or asserts parity.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

from reference_evidence_integrity import read_inputs, validate

MARKER = "STAGE170_LANDMARK_SEQUENCE_PASS"
FIELDS = re.compile(
    r"^STAGE170_LANDMARK_SEQUENCE_PASS "
    r"synthetic_drop=(?P<fall>\d+) "
    r"synthetic_landing=(?P<land>\d+) "
    r"synthetic_wall=(?P<wall>\d+) "
    r"relative_original_deltas=\[(?P<fall_delta>[+-]?\d+),"
    r"(?P<land_delta>[+-]?\d+),(?P<wall_delta>[+-]?\d+)\] "
    r"synthetic_reverse_lag=(?P<reverse_lag>\d+) "
    r"original_reverse_lag=(?P<original_reverse_lag>\d+) "
    r"synthetic_reverse_x30=(?P<reverse_x30>[+-]?\d+(?:\.\d+)?) "
    r"original_reverse_x30=(?P<original_reverse_x30>[+-]?\d+) "
    r"synthetic_reverse_x70=(?P<reverse_x70>[+-]?\d+(?:\.\d+)?) "
    r"original_reverse_x70=(?P<original_reverse_x70>[+-]?\d+)$"
)


def require(test, what):
    if not test:
        raise ValueError("CITY event gap: " + what)


def parse_marker(output):
    """Reject duplicate, partial, or semantically invalid result lines."""
    lines = [line.strip() for line in output.splitlines()
             if line.strip().startswith(MARKER)]
    require(len(lines) == 1, "expected exactly one Stage170 landmark marker")
    match = FIELDS.fullmatch(lines[0])
    require(match is not None, "malformed Stage170 marker")
    values = {
        k: float(v) if k in ("reverse_x30", "reverse_x70") else int(v)
        for k, v in match.groupdict().items()
    }
    require(0 <= values["fall"] < values["land"] < values["wall"] < 320,
            "synthetic landmark chronology")
    require(1 <= values["reverse_lag"] <= 70,
            "synthetic reverse-camera lag")
    require(values["reverse_x70"] < values["reverse_x30"],
            "synthetic reversal must move left")
    return values


def original_numbers(observations):
    """Use existing evidence field values, not copied timing literals."""
    m, o, c = (observations[name]
               for name in ("motion", "obstacles", "camera"))
    ledge, wall, cam = o["ledge"], o["wall"], c["metrics"]
    ctrl = m["first_city_control_frame"]
    scenario = next(s for s in o["independent_trials"]
                    if s["name"] == "reverse_from_wall")
    reversed_input = re.search(r"LEFT \[(\d+),(\d+)\)",
                               scenario["input"])
    require(reversed_input is not None, "missing measured reverse input range")
    reverse_start, reverse_end = map(int, reversed_input.groups())
    require(reverse_start < reverse_end, "invalid reverse interval")
    require(reverse_start <= cam["first_camera_step_left_after_reverse"] <
            reverse_end, "left camera event outside reverse input window")
    require(ledge["world_y_upper_floor"] + ledge["drop_pixels"] ==
            ledge["lower_floor_world_xy"][1], "world Y/drop inconsistency")
    original = {
        "control_frame": ctrl,
        "fall": ledge["first_falling_frame"] - ctrl,
        "land": ledge["first_lower_floor_frame"] - ctrl,
        "wall": wall["horizontal_velocity_zero_first_frame"] - ctrl,
        "reverse_camera_lag": (cam["first_camera_step_left_after_reverse"]
                               - reverse_start),
        "reverse_x30": wall["reverse_x_at_2390"],
        "reverse_x70": wall["reverse_x_at_2430"],
    }
    require(0 < original["fall"] < original["land"] < original["wall"],
            "invalid measured original causal order")
    require(original["reverse_camera_lag"] > 0,
            "measured reverse-camera lag not positive")
    return original


def build_report(marker, original, evidence_report):
    """Separate a common clock-origin shift from causal timing residuals."""
    for event in ("fall", "land", "wall"):
        require(marker[event + "_delta"] == marker[event] - original[event],
                event + " marker delta contradicts source JSON")
    original_marker_fields = {
        "reverse_lag": "reverse_camera_lag",
        "reverse_x30": "reverse_x30",
        "reverse_x70": "reverse_x70",
    }
    for marker_field, evidence_field in original_marker_fields.items():
        require(marker["original_" + marker_field] == original[evidence_field],
                marker_field + " original marker contradicts source JSON")

    original_intervals = {
        "fall_to_land": original["land"] - original["fall"],
        "land_to_wall": original["wall"] - original["land"],
        "fall_to_wall": original["wall"] - original["fall"],
    }
    synthetic_intervals = {
        "fall_to_land": marker["land"] - marker["fall"],
        "land_to_wall": marker["wall"] - marker["land"],
        "fall_to_wall": marker["wall"] - marker["fall"],
    }
    residuals = {name: synthetic_intervals[name] - original_intervals[name]
                 for name in original_intervals}
    raw_deltas = {
        event: marker[event] - original[event]
        for event in ("fall", "land", "wall")
    }
    origin_shift = raw_deltas["fall"]
    # Align ONLY the shared falling event; never fit independent offsets
    # for landing/wall or falsely report original-game parity.
    shifted_deltas = {
        event: raw_deltas[event] - origin_shift
        for event in ("fall", "land", "wall")
    }
    require(shifted_deltas["land"] == residuals["fall_to_land"],
            "time-alignment interval identity")
    require(shifted_deltas["wall"] == residuals["fall_to_wall"],
            "time-alignment wall identity")
    return {
        "schema": "quackshot_city_event_gap_diagnostics_v1",
        "status": "DIAGNOSTIC_ONLY_NOT_ROM_PARITY",
        "original_rom_was_run": False,
        "synthetic_game_was_run": True,
        "original_assets_in_report": False,
        "source_rom_sha256": evidence_report["rom_sha256"],
        "scroll_is_candidate_not_hardware_register": True,
        "original_relative_frames": {name: original[name]
                                     for name in ("fall", "land", "wall")},
        "synthetic_relative_frames": {name: marker[name]
                                      for name in ("fall", "land", "wall")},
        "raw_frame_differences": raw_deltas,
        "origin_alignment": {
            "align_on": "first_fall",
            "synthetic_minus_original_fall_frames": origin_shift,
            "event_residuals_after_single_alignment": shifted_deltas,
        },
        "causal_intervals_frames": {
            "original": original_intervals,
            "synthetic": synthetic_intervals,
            "synthetic_minus_original": residuals,
        },
        "reverse_camera_lag_frames": {
            "original": original["reverse_camera_lag"],
            "synthetic": marker["reverse_lag"],
            "synthetic_minus_original": (marker["reverse_lag"] -
                                          original["reverse_camera_lag"]),
        },
        "reverse_world_x": {
            name: {
                "original": original[name],
                "synthetic": marker[name],
                "synthetic_minus_original": round(
                    marker[name] - original[name], 3),
            }
            for name in ("reverse_x30", "reverse_x70")
        },
        "limitations": [
            "One original first-city observation and an unrelated authored tile map",
            "Camera values are RAM-candidate correlations, not recovered VDP registers",
            "No matching frame origin, hitbox, source map, or original event replay",
            "Intervals quantify gaps; they do not validate authentic gameplay parity",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--binary", type=Path, required=True,
                        help="Previously built qs_city_landmark_timeline_tests")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        observations, source = read_inputs(root)
        evidence = validate(observations, source)
        original = original_numbers(observations)
        binary = args.binary.resolve()
        require(binary.is_file(), "synthetic landmark executable missing")
        if args.out:
            protected = {(root / "src/qs_game.c").resolve()}
            protected.update((root / p).resolve()
                             for p in ("research/observations/user_rom_fixedpoint_motion_2026-10-18.json",
                                       "research/observations/user_rom_city_obstacles_2026-10-18.json",
                                       "research/observations/user_rom_camera_state_2026-10-18.json"))
            require(args.out.resolve() not in protected and
                    args.out.resolve() != binary, "refusing to overwrite evidence or executable")
        execution = subprocess.run([str(binary)], capture_output=True,
                                   text=True, check=True, timeout=30)
        report = build_report(parse_marker(execution.stdout), original, evidence)
        serialized = json.dumps(report, sort_keys=True, indent=2) + "\n"
        if args.out:
            args.out.write_text(serialized, encoding="utf-8")
        else:
            print(serialized, end="")
        print("STAGE184_EVENT_GAP_REPORT_PASS: diagnostic intervals, "
              "single-origin alignment, original ROM not executed")
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
