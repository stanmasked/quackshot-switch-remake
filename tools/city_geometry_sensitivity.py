#!/usr/bin/env python3
"""ROM-free sensitivity of event timings to independently authored city tiles.

Distinguishes geometry-induced changes from immutable physics settings.
Published original measurements are diagnostics, never an optimization
target or assertion of source-map reconstruction.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

from city_event_gap import original_numbers
from reference_evidence_integrity import read_inputs, validate

ROW = re.compile(
    r"^STAGE185_GEOMETRY_ROW edge=(\d+) wall=(\d+) fall=(\d+) "
    r"land=(\d+) stop=(\d+) fall_x=([0-9]+\.[0-9]{5}) "
    r"land_x=([0-9]+\.[0-9]{5}) stop_x=([0-9]+\.[0-9]{5})$"
)
FINISH = re.compile(
    r"^STAGE185_GEOMETRY_SWEEP_PASS rows=20 "
    r"edge_tiles=23\.\.27 wall_tiles=31\.\.34 "
    r"source_geometry=synthetic original_assets=none$"
)
EDGE_RANGE = range(23, 28)
WALL_RANGE = range(31, 35)


def check(condition, message):
    if not condition:
        raise ValueError("Stage185 geometry sensitivity: " + message)


def parse_sweep(output):
    rows = {}
    finish_count = 0
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        if FINISH.fullmatch(line):
            finish_count += 1
            continue
        match = ROW.fullmatch(line)
        check(match is not None, "invalid or unexpected sweep output")
        e, w, fall, land, stop = (int(k) for k in match.groups()[:5])
        fx, lx, sx = (float(k) for k in match.groups()[5:])
        check(e in EDGE_RANGE and w in WALL_RANGE,
              "tile column outside controlled authored geometry")
        check((e, w) not in rows, "duplicate tile combination")
        check(0 <= fall < land < stop < 350,
              "causal event chronology changed")
        check(0 < fx < lx < sx, "unexpected event world coordinates")
        rows[e, w] = {
            "ledge_tile": e,
            "wall_tile": w,
            "fall": fall,
            "land": land,
            "stop": stop,
            "fall_x": fx,
            "land_x": lx,
            "stop_x": sx,
            "fall_to_land": land - fall,
            "land_to_stop": stop - land,
        }
    check(finish_count == 1, "exactly one PASS footer required")
    expected = {(e, w) for e in EDGE_RANGE for w in WALL_RANGE}
    check(set(rows) == expected, "grid must cover all 20 tile variants")
    return rows


def analyze(rows, original):
    check(set(rows) == {(e, w) for e in EDGE_RANGE for w in WALL_RANGE},
          "missing or extra matrix rows")
    # A wall farther right cannot affect the fall/landing that happen
    # earlier on this authored course. A ledge farther right cannot
    # affect motion before that ledge is reached.
    edge_changes = []
    wall_changes = []
    for w in WALL_RANGE:
        for e in range(23, 27):
            a, b = rows[e, w], rows[e + 1, w]
            delta = {key: b[key] - a[key] for key in
                     ("fall", "land", "stop", "fall_to_land", "land_to_stop")}
            check(delta["fall"] > 0 and delta["land"] > 0,
                  "ledge shift failed to delay fall and landing")
            check(delta["stop"] >= 0,
                  "ledge shift unexpectedly advanced wall stop")
            edge_changes.append({"wall_tile": w, "from_edge": e,
                                 "to_edge": e + 1, **delta})
    for e in EDGE_RANGE:
        for w in range(31, 34):
            a, b = rows[e, w], rows[e, w + 1]
            delta = {key: b[key] - a[key] for key in
                     ("fall", "land", "stop", "fall_to_land", "land_to_stop")}
            check(delta["fall"] == 0 and delta["land"] == 0,
                  "downstream wall unexpectedly affected upstream events")
            check(delta["stop"] > 0, "farther wall must delay stop")
            wall_changes.append({"ledge_tile": e, "from_wall": w,
                                 "to_wall": w + 1, **delta})
    base = rows[25, 32]
    event_keys = {"fall": "fall", "land": "land", "stop": "wall"}
    reference = {key: original[source] for key, source in event_keys.items()}
    check(reference == {"fall": 211, "land": 227, "stop": 274},
          "source-observation values changed unexpectedly")
    raw = {key: base[key] - reference[key] for key in reference}
    anchor = raw["fall"]
    aligned = {key: raw[key] - anchor for key in raw}
    original_intervals = {
        "fall_to_land": reference["land"] - reference["fall"],
        "land_to_stop": reference["stop"] - reference["land"],
    }
    relative_gap = {
        key: base[key] - original_intervals[key]
        for key in original_intervals
    }
    check(aligned["land"] == relative_gap["fall_to_land"] and
          aligned["stop"] == sum(relative_gap.values()),
          "origin shift must preserve interval residuals")
    return {
        "schema": "quackshot_city_geometry_sensitivity_v1",
        "status": "SYNTHETIC_GEOMETRY_DIAGNOSTIC_NOT_ROM_PARITY",
        "original_rom_was_run": False,
        "original_assets_in_report": False,
        "authored_tile_grid": {"edge_columns": list(EDGE_RANGE),
                               "wall_columns": list(WALL_RANGE)},
        "physics_changed": False,
        "comparison": {
            "baseline_tile_pair": {"edge": 25, "wall": 32},
            "original_relative_event_frames": reference,
            "synthetic_relative_event_frames": {
                key: base[key] for key in reference},
            "raw_frame_differences": raw,
            "aligned_on_first_fall": {
                "common_origin_offset": anchor,
                "residuals": aligned,
            },
            "original_event_intervals": original_intervals,
            "synthetic_minus_original_interval_frames": relative_gap,
        },
        "event_rows": [rows[e, w] for e in EDGE_RANGE for w in WALL_RANGE],
        "one_tile_ledge_shifts": edge_changes,
        "one_tile_wall_shifts": wall_changes,
        "summary": {
            "tested_tile_pairs": len(rows),
            "ledge_finite_differences": len(edge_changes),
            "wall_finite_differences": len(wall_changes),
            "ledge_fall_delay_range_frames": [
                min(r["fall"] for r in edge_changes),
                max(r["fall"] for r in edge_changes),
            ],
            "wall_stop_delay_range_frames": [
                min(r["stop"] for r in wall_changes),
                max(r["stop"] for r in wall_changes),
            ],
        },
        "limitations": [
            "No original tile geometry or collision hitboxes were recovered",
            "One published first-city original numeric observation only",
            "Tile sensitivity cannot uniquely identify physics parameters",
            "Fitting original event times could conceal wrong geometry",
            "An exact numeric event match would not prove gameplay parity",
        ],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path,
                    default=Path(__file__).resolve().parents[1])
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    try:
        root = args.root.resolve()
        observation, game_source = read_inputs(root)
        validated = validate(observation, game_source)
        check(validated["original_rom_was_run_by_this_audit"] is False,
              "evidence provenance check did not certify ROM-free input")
        original = original_numbers(observation)
        binary = args.binary.resolve()
        check(binary.is_file(), "compiled synthetic geometry test missing")
        if args.out:
            protected = {(root / "src/qs_game.c").resolve(), binary}
            protected.update((root / p).resolve() for p in (
                "research/observations/user_rom_fixedpoint_motion_2026-10-18.json",
                "research/observations/user_rom_city_obstacles_2026-10-18.json",
                "research/observations/user_rom_camera_state_2026-10-18.json"))
            check(args.out.resolve() not in protected,
                  "refusing to overwrite evidence or executable")
        output = subprocess.run([str(binary)], text=True,
                                capture_output=True, check=True, timeout=30)
        result = analyze(parse_sweep(output.stdout), original)
        serialized = json.dumps(result, sort_keys=True, indent=2) + "\n"
        if args.out:
            args.out.write_text(serialized, encoding="utf-8")
        else:
            print(serialized, end="")
        print("STAGE185_GEOMETRY_SENSITIVITY_PASS: 20 authored variants, "
              "causal event deltas only; original ROM not executed")
    except (ValueError, KeyError, OSError, TypeError,
            subprocess.SubprocessError) as exc:
        ap.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
