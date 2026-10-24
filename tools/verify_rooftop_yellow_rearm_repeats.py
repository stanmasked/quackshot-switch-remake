#!/usr/bin/env python3
"""Compare two independently driven NeXium red/reentry/red guest sessions."""
import json
import pathlib
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_yellow_rearm import verify_rearm
from tools.verify_rooftop_red_to_yellow import read_weapon
from tools.verify_rooftop_red_trace import read_city
from tools.verify_rooftop_guest_trace import read_trace
from tools.verify_rooftop_reentry_trace import read_edges
PIN="d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180"
EVENTS=(
"CITY_MENU_SENT","CITY_RED_JUMP_APPROACH","CITY_RED_JUMP_SENT",
"CITY_RED_PICKUP_GUEST","CITY_EXIT_GUEST","CITY_EXIT_SENT","ROOFTOP_MENU_SENT",
"ROOFTOP_RED_UNLOCKED_GUEST","ROOFTOP_RED_APPROACH_GUEST","ROOFTOP_RED_FIRE_SENT",
"ROOFTOP_RED_SWITCH_OVERLAP_GUEST","ROOFTOP_RED_BRIDGE_CLOSED_GUEST",
"ROOFTOP_EXIT_GUEST","ROOFTOP_SECOND_ENTRY_SENT","ROOFTOP_REENTRY_RED_GUEST",
"ROOFTOP_SECOND_APPROACH_GUEST","ROOFTOP_SECOND_RED_FIRE_SENT",
"ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST","ROOFTOP_SECOND_RED_BRIDGE_CLOSED_GUEST",
"ROOFTOP_NINTENDO_X_SENT","ROOFTOP_YELLOW_SELECTED_GUEST",
"ROOFTOP_YELLOW_FIRE_SENT","ROOFTOP_YELLOW_FIRE_GUEST",
"ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST","ROOFTOP_YELLOW_OPEN_STABLE_GUEST",
"ROOFTOP_YELLOW_OPEN_EXIT_GUEST","ROOFTOP_YELLOW_RESET_ENTRY_SENT",
"ROOFTOP_YELLOW_RESET_ENTRY_GUEST","ROOFTOP_YELLOW_RESET_CLOSED_GUEST",
"ROOFTOP_YELLOW_REARM_APPROACH_GUEST","ROOFTOP_YELLOW_REARM_FIRE_SENT",
"ROOFTOP_YELLOW_REARM_FIRE_GUEST","ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST",
"ROOFTOP_YELLOW_REARM_STABLE_GUEST")

class SecondRedRepeatError(ValueError): pass

def verify_two(root):
    root=pathlib.Path(root)
    reports=[]
    hashes=set()
    for repeat in (1,2):
        folder=root/("rooftop-yellow-rearm-evidence-"+str(repeat))
        if not folder.is_dir(): raise SecondRedRepeatError("missing independent guest folder")
        data=(folder/"metadata.txt").read_text(encoding="utf-8").splitlines()
        if any(line.count("=")!=1 for line in data):
            raise SecondRedRepeatError("malformed per-run metadata")
        info=dict(line.split("=",1) for line in data)
        sha=info.get("nro_sha256","")
        if (info.get("profile")!=str(repeat) or
            info.get("source")!="real_virtual_controller_rooftop_yellow_rearm_guest" or
            info.get("runner_exit")!="0" or len(sha)!=64 or
            any(x not in "0123456789abcdef" for x in sha)):
            raise SecondRedRepeatError("wrong profile/provenance/unfinished run")
        if info.get("emulator_sha256_expected")!=PIN or "OK" not in (folder/"checksum.txt").read_text(encoding="utf-8"):
            raise SecondRedRepeatError("unverified emulator checksum")
        if (folder/"result.txt").read_text(encoding="utf-8").strip()!=(
            "PASS: real second YELLOW bridge opening after authentic reset and reentry"):
            raise SecondRedRepeatError("guest virtual-controller sequence failed")
        host=(folder/"input-injection.txt").read_text(encoding="utf-8")
        offsets=[host.find(ev) for ev in EVENTS]
        if min(offsets)<0 or len(set(offsets))!=len(offsets) or offsets!=sorted(offsets):
            raise SecondRedRepeatError("virtual-controller actions absent/out-of-order")
        city=read_city(folder/"guest-city-red.csv")
        roof=read_trace(folder/"guest-rooftop.csv")
        edges=read_edges(folder/"guest-transitions.csv")
        out=verify_rearm(city,roof,edges,read_weapon(folder/"guest-weapon.csv"),host)
        expected=json.loads((folder/"rooftop-yellow-rearm-summary.json").read_text(encoding="utf-8"))
        if out!=expected:
            raise SecondRedRepeatError("unbound guest summary")
        reports.append(out)
        hashes.add(sha)
    if len(hashes)!=1:
        raise SecondRedRepeatError("different guest NRO binaries")
    if (abs(reports[0]["first_red_frame"]-reports[1]["first_red_frame"])>180 or
        abs(reports[0]["second_red_frame"]-reports[1]["second_red_frame"])>210 or
        abs(reports[0]["yellow_switch_open_frame"]-reports[1]["yellow_switch_open_frame"])>230 or
        abs(reports[0]["yellow_reset_reentry_frame"]-reports[1]["yellow_reset_reentry_frame"])>260 or
        abs(reports[0]["yellow_rearm_open_frame"]-reports[1]["yellow_rearm_open_frame"])>300):
        raise SecondRedRepeatError("replay timing spread exceeds bounded tolerance")
    return {"status":"PASS","profiles":[1,2],"nro_sha256":next(iter(hashes)),
            "runs":reports}

def main(argv):
    if len(argv)!=3: raise SystemExit("usage: verify_rooftop_second_red_repeats.py ROOT OUTPUT.json")
    try:
        r=verify_two(argv[1])
    except (OSError,ValueError) as e:
        raise SystemExit("FAIL: real yellow bridge rearm guests: "+str(e)) from e
    pathlib.Path(argv[2]).write_text(json.dumps(r,sort_keys=True)+"\n",encoding="utf-8")
    print("NEXIUM_TWO_YELLOW_REARM_GUESTS_PASS",json.dumps(r,sort_keys=True))

if __name__=="__main__":main(sys.argv)
