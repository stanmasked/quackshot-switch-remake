#!/usr/bin/env python3
"""Strict numeric-only witness of real Switch pad -> CITY upward projectile.

The trace is written by the NRO after qs_step(), not created by the host.
This proves a bounded actor timeline, NOT raster output or collisions.
"""
import csv
import json
import pathlib
import sys

COLUMNS=("seq","frame","attempt","mode","stage","up","fire",
         "grounded","active","age","visible","x","y","legacy_shots","source")
SOURCE="live_city_up_pad_qs_step"

class WitnessError(ValueError):
    pass

def verify(rows):
    if not 55 <= len(rows) <= 320:
        raise WitnessError("missing bounded consecutive CITY guest frames")
    numeric=[]
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(COLUMNS) or row["source"]!=SOURCE:
            raise WitnessError("invalid source or CSV schema")
        try:
            n={k:int(row[k]) for k in COLUMNS if k!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise WitnessError("non-numeric guest record") from exc
        if (n["seq"]!=i or n["frame"]<=0 or n["attempt"]<=0 or
            n["mode"]!=1 or n["stage"]!=0 or
            any(n[k] not in (0,1) for k in ("up","fire","grounded","active","visible")) or
            n["legacy_shots"]!=0):
            raise WitnessError("untrusted frame/scene/input/legacy shot")
        if numeric and (n["frame"]!=numeric[-1]["frame"]+1 or
                        n["attempt"]!=numeric[-1]["attempt"]+1):
            raise WitnessError("dropped or duplicate frame in NRO witness")
        numeric.append(n)
    presses=[i for i,r in enumerate(numeric)
             if r["up"] and r["fire"] and (i==0 or not numeric[i-1]["fire"])]
    if len(presses)!=1:
        raise WitnessError("expected one real UP+FIRE rising edge")
    start=presses[0]
    if start < 3 or start+24 >= len(numeric) or not numeric[start]["grounded"]:
        raise WitnessError("unsupported launch/bounded lifecycle")
    for r in numeric[:start]:
        if r["active"] or r["visible"] or r["x"]!=-1 or r["y"]!=-1:
            raise WitnessError("projectile appeared before UP+FIRE")
    x0=numeric[start+3]["x"]
    y0=numeric[start+3]["y"]
    if not (0<=x0<=1600 and -250<=y0<=250):
        raise WitnessError("invalid launch coordinates")
    for offset in range(23):
        r=numeric[start+offset]
        if r["active"]!=1 or r["age"]!=offset-3:
            raise WitnessError("missing or invalid bounded projectile age")
        if offset<3:
            if r["visible"] or r["x"]!=-1 or r["y"]!=-1:
                raise WitnessError("shot appeared before three-frame delay")
        else:
            age=offset-3
            dy=-((127*age-age*age+15)//16)
            if r["visible"]!=1 or r["x"]!=x0 or r["y"]!=y0+dy:
                raise WitnessError("visible trajectory differs from fitted original bounds")
    for r in numeric[start+23:]:
        if r["active"] or r["visible"] or r["x"]!=-1 or r["y"]!=-1:
            raise WitnessError("projectile persisted beyond 20 measured frames")
    return {"status":"PASS","source":SOURCE,"frames":len(numeric),
            "shot_frame":numeric[start]["frame"],"first_visible_frame":numeric[start+3]["frame"],
            "last_visible_frame":numeric[start+22]["frame"],
            "disappeared_frame":numeric[start+23]["frame"],
            "visible_positions":20,"launch_x":x0,"launch_y":y0,
            "legacy_projectile_samples":0}

def read_csv(path):
    with pathlib.Path(path).open(newline="",encoding="utf-8") as handle:
        return list(csv.DictReader(handle))

def main(argv):
    if len(argv)!=3:
        raise SystemExit("usage: verify_city_up_guest.py GUEST_CSV SUMMARY_JSON")
    try:
        result=verify(read_csv(argv[1]))
    except (OSError,WitnessError) as exc:
        raise SystemExit("FAIL: real CITY upward witness: "+str(exc)) from exc
    pathlib.Path(argv[2]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("CITY_UP_GUEST_NUMERIC_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
