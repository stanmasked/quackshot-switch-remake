#!/usr/bin/env python3
"""Stage 107: require two truly independent, physically blocked roof recoveries.

Use the existing strict Stage 91-106 artifact, input and physics checks via
collect(). Never transform a direct roof run into a claimed blocked route.
"""
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_input_latency import PROFILES
from tools.verify_nexium_repeats import collect, _metadata

EXPECTED_IDS=(4,5)

def compare_blocked(runs):
    if len(runs)!=2 or sorted(r.get("run_id") for r in runs)!=list(EXPECTED_IDS):
        raise ValueError("two DISTINCT late-roof NeXium jobs required")
    digests={r.get("nro_sha256") for r in runs}
    if len(digests)!=1 or not next(iter(digests),None):
        raise ValueError("late-roof NRO digest mismatch")
    for r in runs:
        if (r.get("input_delay_ms"),r.get("poll_interval_ms"))!=PROFILES[r["run_id"]]:
            raise ValueError("late-roof input profile mismatch")
        if (r.get("roof_path")!="ground_then_wall_recovery" or
            not r.get("wall_recovery_used") or
            not isinstance(r.get("wall_recovery_arc"),dict)):
            raise ValueError("late-roof run did not physically recover at wall")
        if len(r.get("guest_hold_frames",{}).get("B",[]))!=3:
            raise ValueError("recovery lacks exactly three guest B pulses")
        roof=r["guest_jump_arcs"]["roof"]
        rescue=r["wall_recovery_arc"]
        if (not 18<=roof["rise_px"]<=45 or roof["landing_y_px"]!=188.0 or
            not 61<=rescue["rise_px"]<=68 or
            not 600<=rescue["landing_x_px"]<=660 or
            rescue["pressed_frame"]<=roof["landing_frame"]):
            raise ValueError("false blocked roof or impossible recovery flight")
    clearance=[r["best_clear_frames"] for r in runs]
    if max(clearance)-min(clearance)>120:
        raise ValueError("unstable recovered CITY TEST completion")
    def spread(values):
        return round(max(values)-min(values),3)
    for name,values,limit in (
        ("roof blocked height",[r["guest_jump_arcs"]["roof"]["rise_px"] for r in runs],15),
        ("wall recovery height",[r["wall_recovery_arc"]["rise_px"] for r in runs],2),
        ("wall recovery landing",[r["wall_recovery_arc"]["landing_x_px"] for r in runs],30)):
        if spread(values)>limit:
            raise ValueError(f"unstable {name} between late-roof runs")
    return {
        "status":"PASS","repetitions":2,
        "roof_path_counts":{"ground_then_wall_recovery":2},
        "finish_ticks":[r["best_clear_frames"] for r in sorted(runs,key=lambda r:r["run_id"])],
        "finish_spread_frames":spread(clearance),
        "roof_blocked_rise_spread_px":spread([r["guest_jump_arcs"]["roof"]["rise_px"] for r in runs]),
        "recovery_rise_spread_px":spread([r["wall_recovery_arc"]["rise_px"] for r in runs]),
        "same_nro_sha256":next(iter(digests)),
        "source":"two separate NeXium late-roof virtual-controller jobs",
    }

def collect_blocked(root,run_id):
    directory=root/f"nexium-blocked-evidence-run-{run_id}"
    if not directory.is_dir():
        raise ValueError(f"missing independent late-roof run {run_id}")
    metadata=_metadata(directory/"metadata.txt")
    if metadata.get("ci_roof_late")!="1":
        raise ValueError("late-roof guest metadata missing")
    actions=(directory/"input-injection.txt").read_text(encoding="utf-8")
    if actions.count("ROOF_INPUT_STRATEGY=late target_min=398000 target_max=416000")!=1:
        raise ValueError("host did not request late roof controller timing")
    marks=[line for line in actions.splitlines() if line.startswith("ROOF_LAUNCH_GUEST ")]
    if len(marks)!=1:
        raise ValueError("missing single real late roof host anchor")
    import re
    m=re.fullmatch(r"ROOF_LAUNCH_GUEST frame=\d+ x_milli=(\d+)",marks[0])
    if not m or not 398000<=int(m.group(1))<=416000:
        raise ValueError("host roof launch not inside late target window")
    # Existing collect checks all original guest CSVs, authentic B/Y pulses,
    # complete roof and recovery arcs, checksum, edge timing and full clear.
    return collect(root,run_id) if (directory.name==f"nexium-emulation-evidence-run-{run_id}") else _collect_alias(root,run_id,directory)

def _collect_alias(root,run_id,directory):
    """Reuse the established evidence validator on the same physical files."""
    import tempfile
    # Symlink only the *directory*, not CSV contents. collect() validates
    # the run ID, hashes, guest source, all per-run markers and real traces.
    with tempfile.TemporaryDirectory() as temp:
        link=Path(temp)/f"nexium-emulation-evidence-run-{run_id}"
        link.symlink_to(directory.resolve(),target_is_directory=True)
        return collect(Path(temp),run_id)

def main(argv):
    if len(argv)!=3:
        raise SystemExit("usage: verify_blocked_roof_repeats.py <evidence-root> <summary.txt>")
    dest=Path(argv[2])
    try:
        root=Path(argv[1])
        expected={f"nexium-blocked-evidence-run-{i}" for i in EXPECTED_IDS}
        actual={p.name for p in root.iterdir() if p.is_dir()}
        if actual!=expected:
            raise ValueError("two independent late-roof evidence directories mandatory")
        result=compare_blocked([collect_blocked(root,i) for i in EXPECTED_IDS])
    except (OSError,ValueError,TypeError,KeyError) as exc:
        dest.write_text("FAIL: independent blocked roof: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: independent blocked roof: "+str(exc))
    line="NEXIUM_TWO_BLOCKED_ROOF_RECOVERIES_PASS "+json.dumps(result,sort_keys=True)+"\n"
    dest.write_text(line,encoding="utf-8")
    print(line,end="")

if __name__=="__main__":
    main(sys.argv)
