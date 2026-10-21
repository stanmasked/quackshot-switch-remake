#!/usr/bin/env python3
"""Stage 93: independent, guest-generated live controller finish witness."""
import csv
import json
from pathlib import Path
import sys

FIELDS=["seq","frame","attempt","x_milli","y_milli","vy_milli","grounded",
        "input_bits","checkpoint","crate","spring_flash","heart","ledge",
        "cleared","mode","best","source"]


def verify(finish, postcheck, obstacle):
    if not 16 <= len(finish) <= 250:
        raise ValueError("missing spring-to-finish guest records")
    parsed=[]
    for i,r in enumerate(finish):
        try:
            d={k:int(r[k]) for k in FIELDS if k!="source"}
        except (ValueError,TypeError,KeyError) as exc:
            raise ValueError("non-numeric guest record") from exc
        if (r.get("source")!="live_controller_qs_step" or d["seq"]!=i or
            not 0 <= d["x_milli"] <= 1280000 or
            not -300000<=d["y_milli"]<=500000 or
            not 0<=d["input_bits"]<128 or
            d["mode"] not in (0,1) or d["cleared"] not in (0,1) or
            d["heart"] not in (0,1) or d["ledge"] not in (0,1) or
            d["grounded"] not in (0,1)):
            raise ValueError("wrong witness source or corrupt gameplay value")
        if parsed and (d["frame"]<=parsed[-1]["frame"] or
                       d["frame"]-parsed[-1]["frame"]>4):
            raise ValueError("missing or out-of-order simulation frames")
        if i<len(finish)-1 and (d["mode"]!=1 or d["cleared"]!=0 or
                               d["checkpoint"]!=1 or d["crate"]!=0 or
                               d["attempt"]<=0 or
                               (parsed and d["attempt"]<=parsed[-1]["attempt"]) or
                               (parsed and d["x_milli"]<parsed[-1]["x_milli"]-8000)):
            raise ValueError("unexpected scene reset, respawn or forged completion")
        parsed.append(d)
    start,final=parsed[0],parsed[-1]
    if (start["spring_flash"]<1 or start["heart"]!=0 or
        start["mode"]!=1 or not 1120000<=start["x_milli"]<=1170000 or
        start["vy_milli"]>=0):
        raise ValueError("spring bounce not proved by first record")
    # The two logs must be from the same real qs_step stage attempt.
    if not any(p.get("source")=="live_controller_qs_step" and
        int(p["frame"])==start["frame"] and
        int(p["attempt"])==start["attempt"] and
        int(p["x_milli"])==start["x_milli"] and
        int(p["spring_flash"])==start["spring_flash"] for p in postcheck):
        raise ValueError("spring witness not linked to post-checkpoint journal")
    if not any(o.get("source")=="live_controller_qs_step" and
        int(o["checkpoint"])==1 and int(o["attempt"])<start["attempt"]
        for o in obstacle):
        raise ValueError("missing prior live checkpoint proof")
    heart=next((r for r in parsed if r["heart"]==1 and r["mode"]==1),None)
    ledge=next((r for r in parsed if r["ledge"]==1 and r["mode"]==1 and r["grounded"]==1
        and abs(r["y_milli"]-124000)<=1),None)
    if heart is None or ledge is None:
        raise ValueError("live airborne heart or one-way ledge missing")
    if not (start["attempt"]<heart["attempt"]<ledge["attempt"] and
            1140000<=heart["x_milli"]<=1190000 and
            1164000<=ledge["x_milli"]<=1204000):
        raise ValueError("heart or ledge geometry/chronology is not causal")
    if not any((r["input_bits"]&2) and
               heart["frame"]<=r["frame"]<ledge["frame"] for r in parsed):
        raise ValueError("controller did not steer from heart to ledge")
    if not any((r["input_bits"]&2) and
               ledge["frame"]<=r["frame"]<final["frame"] for r in parsed):
        raise ValueError("controller did not steer from ledge to exit")
    if not (final["mode"]==0 and final["cleared"]==1 and
            final["best"]>=final["frame"]-start["frame"] and
            final["best"]<=1700 and final["attempt"]==0 and
            final["x_milli"]>=1216000 and
            final["frame"]>ledge["frame"]):
        raise ValueError("real guest never reached completed map state")
    return {"spring_tick":start["attempt"],
            "heart_tick":heart["attempt"],"ledge_tick":ledge["attempt"],
            "clear_frame":final["frame"],"best_clear_frames":final["best"],
            "ledge_x_px":round(ledge["x_milli"]/1000,3),
            "guest_samples":len(parsed),
            "source":"live NeXium gamepad, same second attempt; NOT simulated QsInput"}


def load(path,cols=None):
    with open(path,encoding="utf-8",newline="") as stream:
        reader=csv.DictReader(stream)
        if cols is not None and reader.fieldnames!=cols:
            raise ValueError("unexpected guest finish CSV schema")
        return list(reader)


def main(argv):
    if len(argv)!=5:
        raise SystemExit("usage: verify_finish_route.py <finish.csv> <postcheck.csv> <obstacle.csv> <summary.txt>")
    dest=Path(argv[4])
    try:
        stats=verify(load(argv[1],FIELDS),load(argv[2]),load(argv[3]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        dest.write_text("FAIL: "+str(exc)+"\n")
        raise SystemExit("FAIL: "+str(exc))
    out="LIVE_FULL_CITY_GUEST_PASS "+json.dumps(stats,sort_keys=True)+"\n"
    dest.write_text(out)
    print(out,end="")

if __name__=="__main__":
    main(sys.argv)
