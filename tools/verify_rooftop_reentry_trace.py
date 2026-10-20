#!/usr/bin/env python3
"""Fail-closed verification of two real ROOFTOP visits separated by the map.

The scene-boundary CSV and the existing per-frame ROOFTOP CSV are written by
the guest NRO after read_input() and qs_step(). No simulated state is accepted.
"""
import csv
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import (
    COLUMNS, SOURCE, RooftopTraceError, read_trace, verify,
)

EDGE_COLUMNS = (
    "seq", "frame", "old_mode", "old_stage", "mode", "stage",
    "input_bits", "bridge_before", "bridge_after", "bridge_cells",
    "ruins_relic_earned", "red_unlocked", "weapon", "source",
)
EDGE_SOURCE = "live_rooftop_pad_scene_edge"


class ReentryError(ValueError):
    pass


def read_edges(path):
    with pathlib.Path(path).open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if tuple(reader.fieldnames or ()) != EDGE_COLUMNS:
            raise ReentryError("unrecognized guest transition header")
        return list(reader)


def _numbers(row, names, source):
    if not isinstance(row, dict) or set(row) != set(names):
        raise ReentryError("bad guest columns")
    if row["source"] != source:
        raise ReentryError("untrusted guest telemetry provenance")
    try:
        return {name: int(row[name]) for name in names if name != "source"}
    except (TypeError, ValueError, KeyError) as exc:
        raise ReentryError("invalid guest numeric value") from exc


def verify_reentry(roof_rows, edges):
    if len(edges) != 3:
        raise ReentryError("expected map->roof, roof->map, map->roof")
    edge = [_numbers(row, EDGE_COLUMNS, EDGE_SOURCE) for row in edges]
    for i, e in enumerate(edge):
        if e["seq"] != i or e["frame"] < 1:
            raise ReentryError("out-of-order transition seq/frame")
        if i and e["frame"] <= edge[i - 1]["frame"]:
            raise ReentryError("duplicate/reversed transition frame")
        if e["bridge_before"] not in (0, 1) or e["bridge_after"] not in (0, 1):
            raise ReentryError("noncanonical transition bridge state")
        if e["bridge_cells"] not in (0, 6):
            raise ReentryError("partial bridge on transition")
        if e["ruins_relic_earned"] not in (0, 1) or e["red_unlocked"] not in (0, 1):
            raise ReentryError("invalid persistent state")
        if e["weapon"] not in (0, 1):
            raise ReentryError("invalid weapon state")
    first, leave, second = edge
    if (first["old_mode"], first["mode"], first["stage"]) != (0, 1, 2):
        raise ReentryError("first visit not entered from the real map")
    if (leave["old_mode"], leave["old_stage"], leave["mode"]) != (1, 2, 0):
        raise ReentryError("first visit not exited to the real map")
    if (second["old_mode"], second["mode"], second["stage"]) != (0, 1, 2):
        raise ReentryError("second visit not selected through the real map")
    if leave["stage"] != 2 or first["bridge_before"] or first["bridge_after"] or first["bridge_cells"]:
        raise ReentryError("initial bridge not closed")
    if leave["bridge_before"] != 1 or leave["bridge_after"] != 1 or leave["bridge_cells"] != 6:
        raise ReentryError("bridge not physically open at map exit")
    if second["bridge_before"] != 1 or second["bridge_after"] != 0 or second["bridge_cells"] != 0:
        raise ReentryError("no real open->closed rebuild on reentry")
    if not first["frame"] + 30 < leave["frame"] < second["frame"] - 1:
        raise ReentryError("missing gameplay or map interval")
    for field in ("ruins_relic_earned", "red_unlocked"):
        if len({e[field] for e in edge}) != 1:
            raise ReentryError("persistent unlock/badge lost across exit")
    if len(roof_rows) < 100 or len(roof_rows) > 1400:
        raise ReentryError("missing full two-visit rooftop trace")
    parsed = [_numbers(row, COLUMNS, SOURCE) for row in roof_rows]
    for i, p in enumerate(parsed):
        if p["seq"] != i or p["frame"] <= 0 or p["mode"] != 1 or p["stage"] != 2:
            raise ReentryError("rooftop frame out of order or wrong scene")
        if i and p["frame"] <= parsed[i - 1]["frame"]:
            raise ReentryError("duplicate/reversed guest frame")
    if parsed[0]["frame"] != first["frame"] + 1:
        raise ReentryError("first rooftop frame not tied to map entry")
    before = [row for row, p in zip(roof_rows, parsed) if p["frame"] < leave["frame"]]
    after = [p for p in parsed if p["frame"] > second["frame"]]
    between = [p for p in parsed if leave["frame"] <= p["frame"] <= second["frame"]]
    if between or not before or not after:
        raise ReentryError("rooftop frames illegally recorded during map transition")
    if int(before[-1]["frame"]) != leave["frame"] - 1:
        raise ReentryError("first rooftop segment cut short")
    if after[0]["frame"] != second["frame"] + 1:
        raise ReentryError("second rooftop segment not tied to entry")
    first_report = verify(before)  # full guest yellow-shot flight AND grounded crossing
    if not 45 <= len(after) <= 400:
        raise ReentryError("insufficient closed bridge observation after reentry")
    for i, p in enumerate(after):
        if p["frame"] != after[0]["frame"] + i:
            raise ReentryError("gap in second rooftop visit")
        if p["bridge_before"] or p["bridge_after"] or p["bridge_cells"]:
            raise ReentryError("bridge remained or reopened on second visit")
        if p["shot_active_before"] or p["shot_active_after"] or p["shot_red_before"]:
            raise ReentryError("second visit contaminated by old/new projectile")
        if p["input_bits"] & 8:
            raise ReentryError("FIRE during closed-bridge observation")
        if not 25000 <= p["x_milli"] <= 90000:
            raise ReentryError("new session not spawned at starting platform")
        if i and abs(p["x_milli"] - after[i - 1]["x_milli"]) > 12000:
            raise ReentryError("hero teleported in second scene")
    if after[0]["bridge_before"] or after[0]["shot_active_before"]:
        raise ReentryError("stale stage state at start of second visit")
    return {
        "status": "PASS",
        "first_entry_frame": first["frame"],
        "first_open_frame": first_report["trigger_frame"],
        "map_exit_frame": leave["frame"],
        "reentry_frame": second["frame"],
        "second_visit_frames": len(after),
        "bridge_solid_cells_after_reentry": 0,
        "ruins_relic_earned": second["ruins_relic_earned"],
        "red_unlocked": second["red_unlocked"],
    }


def main(argv):
    if len(argv) != 4:
        raise SystemExit("usage: verify_rooftop_reentry_trace.py ROOF.csv EDGES.csv OUT.json")
    try:
        report = verify_reentry(read_trace(argv[1]), read_edges(argv[2]))
    except (OSError, ValueError, RooftopTraceError) as exc:
        raise SystemExit("FAIL: rooftop reentry guest evidence: " + str(exc)) from exc
    pathlib.Path(argv[3]).write_text(json.dumps(report, sort_keys=True) + "\n", encoding="utf-8")
    print("ROOFTOP_GUEST_REENTRY_CLOSED_PASS", json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main(sys.argv)
