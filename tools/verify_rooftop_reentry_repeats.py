#!/usr/bin/env python3
"""Check two independent real NeXium reentry runs and their guest evidence."""
import json
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import read_trace
from tools.verify_rooftop_reentry_trace import read_edges, verify_reentry

PIN = "8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642"
EVENTS = (
    "ROOFTOP_MENU_SENT", "ROOFTOP_APPROACH_GUEST",
    "ROOFTOP_FIRE_SENT", "ROOFTOP_BRIDGE_OPEN_GUEST",
    "ROOFTOP_BRIDGE_CROSSED_GUEST", "ROOFTOP_EXIT_SENT",
    "ROOFTOP_REENTRY_SENT", "ROOFTOP_REENTRY_CLOSED_GUEST",
    "ROOFTOP_SECOND_VISIT_SAMPLED",
)


class ReentryRepeatError(ValueError):
    pass


def verify_two(root):
    root = pathlib.Path(root)
    reports, fingerprints = [], set()
    for profile in (1, 2):
        folder = root / ("rooftop-reentry-evidence-" + str(profile))
        if not folder.is_dir():
            raise ReentryRepeatError("missing guest run")
        data = (folder / "metadata.txt").read_text(encoding="utf-8").splitlines()
        if any("=" not in item for item in data):
            raise ReentryRepeatError("malformed metadata")
        meta = dict(item.split("=", 1) for item in data)
        sha = meta.get("nro_sha256", "")
        if (meta.get("profile") != str(profile) or
                meta.get("source") != "real_virtual_controller_rooftop_reentry_guest" or
                meta.get("runner_exit") != "0"):
            raise ReentryRepeatError("wrong guest profile or incomplete run")
        if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
            raise ReentryRepeatError("bad NRO fingerprint")
        if meta.get("emulator_sha256_expected") != PIN or (
                "OK" not in (folder / "checksum.txt").read_text(encoding="utf-8")):
            raise ReentryRepeatError("NeXium release integrity not proven")
        if (folder / "result.txt").read_text(encoding="utf-8").strip() != (
                "PASS: real virtual-controller rooftop bridge rebuilt closed on reentry"):
            raise ReentryRepeatError("guest route not completed")
        witness = (folder / "input-injection.txt").read_text(encoding="utf-8")
        offsets = [witness.find(event) for event in EVENTS]
        if min(offsets) < 0 or offsets != sorted(offsets) or len(set(offsets)) != len(offsets):
            raise ReentryRepeatError("missing/reordered virtual-controller path")
        actual = verify_reentry(
            read_trace(folder / "guest-rooftop.csv"),
            read_edges(folder / "guest-transitions.csv"),
        )
        saved = json.loads((folder / "rooftop-reentry-summary.json").read_text(encoding="utf-8"))
        if actual != saved:
            raise ReentryRepeatError("summary not bound to guest-generated files")
        reports.append(actual)
        fingerprints.add(sha)
    if len(fingerprints) != 1:
        raise ReentryRepeatError("two sessions used different NROs")
    delta = abs(reports[0]["reentry_frame"] - reports[1]["reentry_frame"])
    if delta > 180:
        raise ReentryRepeatError("reentry timing spread too large")
    return {"status": "PASS", "profiles": [1, 2], "nro_sha256": next(iter(fingerprints)),
            "reentry_frame_spread": delta, "runs": reports}


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: verify_rooftop_reentry_repeats.py ROOT OUT.json")
    try:
        report = verify_two(argv[1])
    except (OSError, ValueError) as exc:
        raise SystemExit("FAIL: repeated live ROOFTOP reentry: " + str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(report, sort_keys=True) + "\n", encoding="utf-8")
    print("NEXIUM_TWO_ROOFTOP_REENTRIES_CLOSED_PASS", json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main(sys.argv)
