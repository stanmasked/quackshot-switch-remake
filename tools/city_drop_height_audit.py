#!/usr/bin/env python3
"""Analyze synthetic CITY drop heights without fitting original physics.

The original benchmark supplies one relative fall-to-land interval.
Changing our own authored geometry cannot prove original-map parity.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

from city_event_gap import original_numbers
from reference_evidence_integrity import read_inputs, validate

PATTERN = re.compile(
    r"STAGE186_DROP_ROW drop_px=(\d+) edge=(\d+) "
    r"unsupported=(\d+) moved=(\d+) moved_quarter=(\d+) "
    r"moved_one=(\d+) land=(\d+) fall_x=(\d+\.\d{5}) "
    r"land_x=(\d+\.\d{5})"
)
FOOTER = ("STAGE186_DROP_SWEEP_PASS rows=20 heights_px=16,32,48,64 "
          "edges=23..27 event_thresholds=ungrounded,0.01,0.25,1.0 "
          "original_assets=none")
HEIGHTS = (16, 32, 48, 64)
EDGES = range(23, 28)


def require(condition, text):
    if not condition:
        raise ValueError("CITY drop audit: " + text)


def parse_rows(output):
    rows = {}
    footer_seen = 0
    for raw in output.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line == FOOTER:
            footer_seen += 1
            continue
        m = PATTERN.fullmatch(line)
        require(m is not None, "unexpected or malformed output")
        height, edge, unsupported, moved, quarter, one, land = (
            int(v) for v in m.groups()[:7]
        )
        fall_x, land_x = (float(v) for v in m.groups()[7:])
        require(height in HEIGHTS and edge in EDGES, "unexpected tile case")
        require((height, edge) not in rows, "duplicated tile case")
        require(0 <= unsupported <= moved <= quarter <= one < land < 350,
                "invalid chronological event sequence")
        require(0.0 < fall_x < land_x, "invalid world coordinate sequence")
        rows[height, edge] = {
            "drop_height_px": height,
            "ledge_column": edge,
            "unsupported": unsupported,
            "fall_0p01": moved,
            "fall_0p25": quarter,
            "fall_1px": one,
            "landing": land,
            "fall_to_land_frames": land - moved,
            "unsupported_to_land_frames": land - unsupported,
            "threshold_shifts_frames": {
                "0p01_from_unsupported": moved - unsupported,
                "0p25_from_0p01": quarter - moved,
                "1px_from_0p01": one - moved,
            },
            "fall_x": fall_x,
            "land_x": land_x,
        }
    require(footer_seen == 1, "one and only one successful footer required")
    require(set(rows) == {(h, e) for h in HEIGHTS for e in EDGES},
            "incomplete 20-scenario matrix")
    return rows


def summarize(rows, original):
    require(set(rows) == {(h, e) for h in HEIGHTS for e in EDGES},
            "matrix is incomplete or contains foreign samples")
    benchmark = original["land"] - original["fall"]
    require(benchmark == 16, "original numeric observation has changed")
    height_groups = []
    for h in HEIGHTS:
        intervals = {rows[h, e]["fall_to_land_frames"] for e in EDGES}
        require(len(intervals) == 1,
                "changing horizontal ledge position altered vertical interval")
        timing = next(iter(intervals))
        for e in EDGES:
            for before_h in HEIGHTS:
                if before_h < h:
                    prev = rows[before_h, e]
                    curr = rows[h, e]
                    require(curr["unsupported"] == prev["unsupported"] and
                            curr["fall_0p01"] == prev["fall_0p01"],
                            "drop height changed ledge departure time")
                    require(curr["fall_to_land_frames"] >
                            prev["fall_to_land_frames"],
                            "higher drop did not delay landing")
        height_groups.append({
            "drop_height_px": h,
            "synthetic_fall_to_land_frames": timing,
            "difference_to_original_32px_interval_frames":
                timing - benchmark if h == 32 else None,
        })
    baseline = rows[32, 25]
    registration_intervals = {
        "first_unsupported": (baseline["unsupported_to_land_frames"]),
        "first_y_gt_0p01": baseline["fall_to_land_frames"],
        "first_y_gt_0p25": baseline["landing"] - baseline["fall_0p25"],
        "first_y_gt_1px": baseline["landing"] - baseline["fall_1px"],
    }
    registration_residuals = {
        key: frames - benchmark
        for key, frames in registration_intervals.items()
    }
    return {
        "schema": "quackshot_city_drop_height_sensitivity_v1",
        "baseline_32px_threshold_comparison": {
            "ledge_column": 25,
            "source_numeric_original_interval_frames": benchmark,
            "synthetic_intervals_frames": registration_intervals,
            "synthetic_minus_original_frames": registration_residuals,
            "warning": "Threshold matching is not proof of original event semantics",
        },
        "status": "SYNTHETIC_DROP_EXPERIMENT_NOT_ROM_PARITY",
        "original_rom_was_run": False,
        "original_assets_in_report": False,
        "physics_changed": False,
        "source_geometry": "authored_tiles_not_original_map",
        "original_observed_drop_height_px": 32,
        "original_observed_fall_to_land_frames": benchmark,
        "height_summaries": height_groups,
        "rows": [rows[h, e] for h in HEIGHTS for e in EDGES],
        "limits": [
            "Only one previously recorded original 32px drop timing",
            "Original collision box, tile alignment, event sampling remain unknown",
            "Changing synthetic geometry does not calibrate original physics",
            "Different event definitions can change the reported event frame",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        observed, source = read_inputs(root)
        validate(observed, source)
        original = original_numbers(observed)
        exe = args.binary.resolve()
        require(exe.is_file(), "synthetic CTest executable not found")
        if args.out:
            protected = {exe, (root / "src/qs_game.c").resolve()}
            protected.update((root / p).resolve() for p in (
                "research/observations/user_rom_fixedpoint_motion_2026-10-18.json",
                "research/observations/user_rom_city_obstacles_2026-10-18.json",
                "research/observations/user_rom_camera_state_2026-10-18.json"
            ))
            require(args.out.resolve() not in protected,
                    "refusing to overwrite game core or numeric evidence")
        result = subprocess.run([str(exe)], capture_output=True,
                                text=True, timeout=30, check=True)
        report = summarize(parse_rows(result.stdout), original)
        contents = json.dumps(report, sort_keys=True, indent=2) + "\n"
        if args.out:
            args.out.write_text(contents, encoding="utf-8")
        else:
            print(contents, end="")
        print("STAGE186_DROP_AUDIT_PASS: independent 20-case vertical"
              " sensitivity; original ROM not executed")
    except (ValueError, OSError, KeyError, TypeError,
            subprocess.SubprocessError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
