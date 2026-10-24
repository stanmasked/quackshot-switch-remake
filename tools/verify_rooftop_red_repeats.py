#!/usr/bin/env python3
"""Check two independent real NeXium red-shot negative runs and their guest evidence."""
import json
import pathlib
import re
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import read_trace
from tools.verify_rooftop_red_trace import read_city, verify_red
from tools.verify_rooftop_reentry_trace import read_edges

PIN = "d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180"
EVENTS = (
    "CITY_MENU_SENT", "CITY_RED_JUMP_APPROACH", "CITY_RED_JUMP_SENT",
    "CITY_RED_PICKUP_GUEST", "CITY_EXIT_GUEST", "CITY_EXIT_SENT",
    "ROOFTOP_MENU_SENT",
    "ROOFTOP_RED_UNLOCKED_GUEST", "ROOFTOP_RED_APPROACH_GUEST",
    "ROOFTOP_RED_FIRE_SENT", "ROOFTOP_RED_SWITCH_OVERLAP_GUEST",
    "ROOFTOP_RED_BRIDGE_CLOSED_GUEST",
)


class RedRepeatError(ValueError):
    pass


def verify_host_event_frames(witness, edge_rows, roof_rows, result):
    """Bind host synchronization messages to numerical guest-owned CSV frames.

    These host messages are never treated as a substitute for the independent
    stage-transition and shot-collision verifiers. Every asserted event must
    match an actual immutable guest row in the same captured session.
    """
    def only_event(prefix, pattern):
        matched = [line for line in witness.splitlines()
                   if line.startswith(prefix + " ")]
        if len(matched) != 1:
            raise RedRepeatError("missing or duplicated host sync event: " + prefix)
        found = re.fullmatch(pattern, matched[0])
        if not found:
            raise RedRepeatError("malformed numeric host sync event: " + prefix)
        return tuple(map(int, found.groups()))

    exit_frame, attempt = only_event(
        "CITY_EXIT_GUEST", r"CITY_EXIT_GUEST frame=(\d+) attempt=(\d+)")
    hit_frame, hit_x = only_event(
        "ROOFTOP_RED_SWITCH_OVERLAP_GUEST",
        r"ROOFTOP_RED_SWITCH_OVERLAP_GUEST frame=(\d+) x_milli=(\d+)")
    if attempt not in (1, 2, 3):
        raise RedRepeatError("host exit attempt outside controller retry budget")
    if not edge_rows or exit_frame != int(edge_rows[0]["frame"]):
        raise RedRepeatError("host CITY exit not witnessed by guest transition")
    if hit_frame != result["red_switch_overlap_frame"]:
        raise RedRepeatError("host red-overlap frame differs from guest verification")
    shot_rows = [r for r in roof_rows if int(r["frame"]) == hit_frame]
    if len(shot_rows) != 1 or hit_x != int(shot_rows[0]["shot_x_before_milli"]):
        raise RedRepeatError("host red-overlap coordinate differs from real NRO")
    return {"exit_frame": exit_frame, "hit_frame": hit_frame}


def verify_two(root):
    root = pathlib.Path(root)
    reports, fingerprints = [], set()
    for profile in (1, 2):
        folder = root / ("rooftop-red-evidence-" + str(profile))
        if not folder.is_dir():
            raise RedRepeatError("missing guest run")
        data = (folder / "metadata.txt").read_text(encoding="utf-8").splitlines()
        if any("=" not in item for item in data):
            raise RedRepeatError("malformed metadata")
        meta = dict(item.split("=", 1) for item in data)
        sha = meta.get("nro_sha256", "")
        if (meta.get("profile") != str(profile) or
                meta.get("source") != "real_virtual_controller_rooftop_red_guest" or
                meta.get("runner_exit") != "0"):
            raise RedRepeatError("wrong guest profile or incomplete run")
        if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
            raise RedRepeatError("bad NRO fingerprint")
        if meta.get("emulator_sha256_expected") != PIN or (
                "OK" not in (folder / "checksum.txt").read_text(encoding="utf-8")):
            raise RedRepeatError("NeXium release integrity not proven")
        if (folder / "result.txt").read_text(encoding="utf-8").strip() != (
                "PASS: real CITY pickup and red ROOFTOP guest shot kept bridge closed"):
            raise RedRepeatError("guest route not completed")
        witness = (folder / "input-injection.txt").read_text(encoding="utf-8")
        offsets = [witness.find(event) for event in EVENTS]
        if min(offsets) < 0 or offsets != sorted(offsets) or len(set(offsets)) != len(offsets):
            raise RedRepeatError("missing/reordered virtual-controller path")
        city_rows = read_city(folder / "guest-city-red.csv")
        roof_rows = read_trace(folder / "guest-rooftop.csv")
        edge_rows = read_edges(folder / "guest-transitions.csv")
        actual = verify_red(city_rows, roof_rows, edge_rows)
        verify_host_event_frames(witness, edge_rows, roof_rows, actual)
        saved = json.loads((folder / "rooftop-red-summary.json").read_text(encoding="utf-8"))
        if actual != saved:
            raise RedRepeatError("summary not bound to guest-generated files")
        reports.append(actual)
        fingerprints.add(sha)
    if len(fingerprints) != 1:
        raise RedRepeatError("two sessions used different NROs")
    delta = abs(reports[0]["red_switch_overlap_frame"] - reports[1]["red_switch_overlap_frame"])
    if delta > 180:
        raise RedRepeatError("red-overlap timing spread too large")
    return {"status": "PASS", "profiles": [1, 2], "nro_sha256": next(iter(fingerprints)),
            "red_switch_overlap_frame_spread": delta, "runs": reports}


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: verify_rooftop_red_repeats.py ROOT OUT.json")
    try:
        report = verify_two(argv[1])
    except (OSError, ValueError) as exc:
        raise SystemExit("FAIL: repeated live ROOFTOP red shots: " + str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(report, sort_keys=True) + "\n", encoding="utf-8")
    print("NEXIUM_TWO_RED_SHOTS_NO_BRIDGE_PASS", json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main(sys.argv)
