#!/usr/bin/env python3
"""Stage276: independently authenticate two live NRO runs per left-facing variant."""
import json
import pathlib
import re
import sys
from verify_city_horizontal_variant import verify_variant_files, HorizontalError

PIN="d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180"

def metadata(path):
    result={}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            raise HorizontalError("malformed metadata")
        key,value=line.split("=",1)
        if not key or not value or key in result:
            raise HorizontalError("duplicate/empty metadata")
        result[key]=value
    return result

def compare(root,variant):
    if variant not in ("left-yellow","left-red"):
        raise HorizontalError("unknown variant")
    paths=sorted(pathlib.Path(root).glob(f"city-horizontal-variant-{variant}-*"))
    if len(paths)!=2:
        raise HorizontalError("two independent variant guest artifacts required")
    runs=[]; nros=set()
    required=["NEXIUM_WINDOW_READY","NEXIUM_GUEST_FRAMES_READY","CITY_MENU_SENT",
              "CITY_GROUNDED_GUEST"]
    required += (["CITY_LEFT_FACE_SENT","CITY_LEFT_YELLOW_FIRE_SENT"] if variant=="left-yellow" else
                 ["CITY_RED_PICKUP_APPROACH_SENT","CITY_RED_PICKUP_CONFIRMED",
                  "CITY_LEFT_FACE_SENT","CITY_LEFT_RED_FIRE_SENT"])
    required += ["CITY_HORIZONTAL_CAPTURE_COMPLETE"]
    for path in paths:
        meta=metadata(path/"metadata.txt")
        profile=meta.get("profile")
        if (profile not in ("1","2") or meta.get("variant")!=variant or
            meta.get("source")!="real_virtual_controller_city_horizontal_variant_guest" or
            meta.get("runner_exit")!="0" or meta.get("emulator_sha256_expected")!=PIN or
            meta.get("emulator_sha256_observed")!=PIN):
            raise HorizontalError("untrusted emulator, profile, source or variant metadata")
        nro=meta.get("nro_sha256","")
        if not re.fullmatch("[0-9a-f]{64}",nro):
            raise HorizontalError("missing NRO hash")
        if "OK" not in (path/"checksum.txt").read_text(encoding="utf-8"):
            raise HorizontalError("pinned emulator checksum missing")
        if not (path/"result.txt").read_text(encoding="utf-8").startswith(
            "PASS: real virtual-controller horizontal variant"):
            raise HorizontalError("guest session did not pass local validator")
        log=(path/"input-injection.txt").read_text(encoding="utf-8")
        positions=[log.find(event) for event in required]
        if any(pos<0 for pos in positions) or positions!=sorted(positions):
            raise HorizontalError("controller event chronology absent")
        result=verify_variant_files(path/"guest-city-horizontal.csv",
                                    path/"guest-city-horizontal-raster.csv",
                                    path/"city-red-acquire.csv",variant)
        saved=json.loads((path/"city-horizontal-variant-summary.json").read_text(encoding="utf-8"))
        if result!=saved:
            raise HorizontalError("saved variant summary differs from raw guest traces")
        runs.append(result); nros.add(nro)
    if len(nros)!=1:
        raise HorizontalError("independent guests used different NRO binaries")
    frames=[run["numeric"]["fire_frame"] for run in runs]
    spread=abs(frames[0]-frames[1])
    if spread>180:
        raise HorizontalError("independent guest fire frame spread too large")
    return {"status":"PASS","variant":variant,"guest_runs":2,
            "same_nro_sha256":next(iter(nros)),"emulator_sha256":PIN,
            "fire_frame_spread":spread,"runs":runs,
            "scope":"real_NRO_controller_and_SDL_pixels_only"}

def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_city_horizontal_variant_repeats.py ROOT VARIANT SUMMARY_JSON")
    try:
        result=compare(argv[1],argv[2])
    except (OSError,ValueError,TypeError,KeyError) as exc:
        raise SystemExit("FAIL: two horizontal variant guest checks: "+str(exc)) from exc
    pathlib.Path(argv[3]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("TWO_CITY_HORIZONTAL_VARIANTS_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
