#!/usr/bin/env python3
"""Fail-closed comparison of two independent NeXium ROOFTOP guest sessions."""
import json
import pathlib
import sys
from tools.verify_rooftop_guest_trace import verify, read_trace

class RooftopRepeatError(ValueError):
    pass

def verify_two(root):
    base=pathlib.Path(root)
    reports=[]
    nro_hashes=set()
    for profile in (1,2):
        directory=base/("rooftop-guest-evidence-"+str(profile))
        lines=(directory/"metadata.txt").read_text(encoding="utf-8").splitlines()
        metadata=dict(line.split("=",1) for line in lines if "=" in line)
        sha=metadata.get("nro_sha256","")
        if metadata.get("profile")!=str(profile):
            raise RooftopRepeatError("missing/duplicate NeXium profile")
        if metadata.get("source")!="real_virtual_controller_rooftop_guest":
            raise RooftopRepeatError("unexpected host input provenance")
        if len(sha)!=64 or not all(c in "0123456789abcdef" for c in sha):
            raise RooftopRepeatError("invalid NRO SHA256")
        if metadata.get("runner_exit")!="0":
            raise RooftopRepeatError("incomplete host/controller session")
        if metadata.get("emulator_sha256_expected")!="c933fbbcf8d1b011de8d4a79c9d8eb55c0678a305947af9388ddb40ce5960a66":
            raise RooftopRepeatError("emulator digest changed")
        if "OK" not in (directory/"checksum.txt").read_text(encoding="utf-8"):
            raise RooftopRepeatError("downloaded emulator integrity not proved")
        if not (directory/"result.txt").read_text(encoding="utf-8").startswith("PASS: real virtual-controller"):
            raise RooftopRepeatError("host route failed")
        witness=(directory/"input-injection.txt").read_text(encoding="utf-8")
        positions=[
            witness.find(event) for event in
            ("ROOFTOP_MENU_SENT","ROOFTOP_APPROACH_GUEST",
             "ROOFTOP_FIRE_SENT","ROOFTOP_BRIDGE_OPEN_GUEST",
             "ROOFTOP_BRIDGE_CROSSED_GUEST")
        ]
        if any(p<0 for p in positions) or positions!=sorted(positions):
            raise RooftopRepeatError("missing or out-of-order physical input witness")
        result=verify(read_trace(directory/"guest-rooftop.csv"))
        if result!=json.loads((directory/"rooftop-summary.json").read_text(encoding="utf-8")):
            raise RooftopRepeatError("generated summary not bound to guest CSV")
        reports.append(result)
        nro_hashes.add(sha)
    if len(nro_hashes)!=1:
        raise RooftopRepeatError("two guest jobs executed different NRO images")
    spread=abs(reports[0]["trigger_frame"]-reports[1]["trigger_frame"])
    if spread>180:
        raise RooftopRepeatError("trigger-frame spread exceeded 180 frames")
    return dict(status="PASS",profiles=[1,2],nro_sha256=next(iter(nro_hashes)),
                trigger_frame_spread=spread,runs=reports)

def main(argv):
    if len(argv)!=3:
        raise SystemExit("usage: verify_rooftop_repeats.py ROOT SUMMARY.json")
    try:
        report=verify_two(argv[1])
    except (OSError,ValueError) as exc:
        raise SystemExit("FAIL: real rooftop repeat: "+str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(report,sort_keys=True)+"\n")
    print("NEXIUM_TWO_REAL_ROOFTOP_BRIDGES_PASS",json.dumps(report,sort_keys=True))
if __name__=="__main__":
    main(sys.argv)
