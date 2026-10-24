#!/usr/bin/env python3
"""Stage275: independently authenticate two NRO controller + SDL horizontal traces."""
import json
import pathlib
import re
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_city_horizontal_guest import (
    load,NUMERIC,PIXELS,verify_numeric,verify_pixels,HorizontalError
)

PIN="d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180"
EVENTS=("NEXIUM_WINDOW_READY","NEXIUM_GUEST_FRAMES_READY",
        "CITY_MENU_SENT","CITY_GROUNDED_GUEST",
        "CITY_HORIZONTAL_FIRE_SENT","CITY_HORIZONTAL_CAPTURE_COMPLETE")

class RepeatError(ValueError):
    pass

def meta(path):
    records={}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            raise RepeatError("invalid guest metadata")
        k,v=line.split("=",1)
        if not k or not v or k in records:
            raise RepeatError("duplicate or empty guest metadata")
        records[k]=v
    return records

def compare(root):
    reports=[]
    nros=set()
    for profile in (1,2):
        directory=pathlib.Path(root)/f"city-horizontal-evidence-{profile}"
        if not directory.is_dir():
            raise RepeatError("missing independent horizontal guest")
        m=meta(directory/"metadata.txt")
        if (m.get("profile")!=str(profile) or
            m.get("source")!="real_virtual_controller_city_horizontal_guest" or
            m.get("runner_exit")!="0" or
            m.get("emulator_sha256_expected")!=PIN or
            m.get("emulator_sha256_observed")!=PIN):
            raise RepeatError("untrusted emulator/profile/source metadata")
        nro=m.get("nro_sha256","")
        if not re.fullmatch("[0-9a-f]{64}",nro):
            raise RepeatError("missing NRO hash")
        if "OK" not in (directory/"checksum.txt").read_text(encoding="utf-8"):
            raise RepeatError("emulator download checksum absent")
        if not (directory/"result.txt").read_text(encoding="utf-8").startswith(
            "PASS: real virtual-controller horizontal FIRE"):
            raise RepeatError("guest session did not attest controller event")
        host=(directory/"input-injection.txt").read_text(encoding="utf-8")
        offsets=[host.find(name) for name in EVENTS]
        if any(x<0 for x in offsets) or offsets!=sorted(offsets):
            raise RepeatError("actual controller chronology absent")
        num=load(directory/"guest-city-horizontal.csv",NUMERIC)
        pix=load(directory/"guest-city-horizontal-raster.csv",PIXELS)
        result={"numeric":verify_numeric(num),"raster":verify_pixels(num,pix)}
        saved=json.loads((directory/"city-horizontal-summary.json").read_text(encoding="utf-8"))
        if result!=saved:
            raise RepeatError("stored summary does not match original guest CSV")
        reports.append(result)
        nros.add(nro)
    if len(nros)!=1:
        raise RepeatError("guests used different NRO binaries")
    spread=abs(reports[0]["numeric"]["fire_frame"]-reports[1]["numeric"]["fire_frame"])
    if spread>180:
        raise RepeatError("independent guest fire frame spread too large")
    return {"status":"PASS","guest_runs":2,"same_nro_sha256":next(iter(nros)),
            "emulator_sha256":PIN,"fire_frame_spread":spread,"runs":reports,
            "scope":"real_homebrew_NRO_controller_and_SDL_pixels_only"}

def main(argv):
    if len(argv)!=3:
        raise SystemExit("usage: verify_city_horizontal_repeats.py ROOT SUMMARY_JSON")
    try:
        result=compare(argv[1])
    except (OSError,ValueError,TypeError,KeyError) as exc:
        raise SystemExit("FAIL: two horizontal NRO guest checks: "+str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("TWO_CITY_HORIZONTAL_GUESTS_RASTER_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
