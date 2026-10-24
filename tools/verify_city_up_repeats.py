#!/usr/bin/env python3
"""Fail-closed comparison of two genuine NeXium UP+FIRE NRO guest captures."""
import json
import pathlib
import re
import sys

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_city_up_guest import verify,read_csv,WitnessError

PIN="8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642"
EVENTS=("NEXIUM_WINDOW_READY","NEXIUM_GUEST_FRAMES_READY",
        "CITY_MENU_SENT","CITY_GROUNDED_GUEST","CITY_UP_FIRE_SENT",
        "CITY_UP_CAPTURE_COMPLETE")

class RepeatError(ValueError):
    pass

def metadata(path):
    result={}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            raise RepeatError("malformed metadata")
        key,value=line.split("=",1)
        if not key or not value or key in result:
            raise RepeatError("invalid or duplicate metadata")
        result[key]=value
    return result

def compare(root):
    reports=[]
    signatures=set()
    for number in (1,2):
        directory=pathlib.Path(root)/("city-up-evidence-"+str(number))
        if not directory.is_dir():
            raise RepeatError("missing independently archived NeXium guest")
        data=metadata(directory/"metadata.txt")
        if (data.get("profile")!=str(number) or
            data.get("source")!="real_virtual_controller_city_up_guest" or
            data.get("runner_exit")!="0" or
            data.get("emulator_sha256_expected")!=PIN or
            data.get("emulator_sha256_observed")!=PIN):
            raise RepeatError("unverified guest runner/profile/emulator")
        nro=data.get("nro_sha256","")
        if not re.fullmatch(r"[0-9a-f]{64}",nro):
            raise RepeatError("invalid NRO SHA-256")
        if "OK" not in (directory/"checksum.txt").read_text(encoding="utf-8"):
            raise RepeatError("unverified emulator archive checksum")
        if not (directory/"result.txt").read_text(encoding="utf-8").startswith("PASS: real virtual-controller UP+FIRE"):
            raise RepeatError("no authenticated guest witness")
        host=(directory/"input-injection.txt").read_text(encoding="utf-8")
        positions=[host.find(event) for event in EVENTS]
        if any(pos<0 for pos in positions) or positions!=sorted(positions):
            raise RepeatError("invalid chronological virtual-controller evidence")
        result=verify(read_csv(directory/"guest-city-up.csv"))
        saved=json.loads((directory/"city-up-summary.json").read_text(encoding="utf-8"))
        if result!=saved:
            raise RepeatError("guest numeric summary differs from original CSV")
        reports.append(result)
        signatures.add(nro)
    if len(signatures)!=1:
        raise RepeatError("two guest runs did not execute identical NRO")
    if abs(reports[0]["shot_frame"]-reports[1]["shot_frame"])>180:
        raise RepeatError("unreasonable launch scheduling spread")
    return {"status":"PASS","guest_runs":2,"same_nro_sha256":next(iter(signatures)),
            "emulator_sha256":PIN,"launch_frame_spread":abs(reports[0]["shot_frame"]-reports[1]["shot_frame"]),
            "runs":reports,"scope":"numeric guest actor only; no raster/collision claims"}

def main(argv):
    if len(argv)!=3:
        raise SystemExit("usage: verify_city_up_repeats.py ROOT OUTPUT_JSON")
    try:
        result=compare(argv[1])
    except (OSError,ValueError,KeyError,TypeError,WitnessError) as exc:
        raise SystemExit("FAIL: independent CITY UP guest proof: "+str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("TWO_CITY_UP_GUESTS_NUMERIC_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
