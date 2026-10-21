#!/usr/bin/env python3
"""Stage 92: verify live NeXium controller from checkpoint through crate and spring.

All samples are written by the running Switch NRO. The Stage 88
synthetic QsInput self-test is never used to satisfy this proof.
"""
import csv
import json
from pathlib import Path
import sys

FIELDS=["seq","frame","attempt","x_milli","y_milli","vy_milli","grounded",
        "input_bits","checkpoint","crate","spring_flash","heart",
        "ledge","cleared","mode","source"]

def verify(rows, obstacles=None):
    if not 65 <= len(rows) <= 320:
        raise ValueError("missing long live post-checkpoint trace")
    parsed=[]
    for i,line in enumerate(rows):
        try:
            d={k:int(line[k]) for k in FIELDS if k!="source"}
        except (ValueError,TypeError,KeyError) as exc:
            raise ValueError("invalid numerical post-checkpoint data") from exc
        if (d["seq"]!=i or line.get("source")!="live_controller_qs_step" or
            d["mode"]!=1 or d["checkpoint"]!=1 or d["cleared"]!=0 or
            d["crate"] not in (0,1) or d["heart"] not in (0,1) or
            d["ledge"] not in (0,1) or d["grounded"] not in (0,1) or
            not 0 <= d["input_bits"] < 128 or
            not 0 <= d["x_milli"] <= 1280000 or
            not -300000 <= d["y_milli"] <= 500000 or
            not 0 <= d["spring_flash"] <= 12):
            raise ValueError("bad live input provenance or invalid world state")
        if parsed and (d["frame"]<=parsed[-1]["frame"] or
                       d["frame"]-parsed[-1]["frame"]>4 or
                       d["attempt"]<parsed[-1]["attempt"] or
                       d["attempt"]-parsed[-1]["attempt"]>4 or
                       d["x_milli"]+8000 < parsed[-1]["x_milli"]):
            raise ValueError("discontinuous guest route, respawn or teleport")
        parsed.append(d)
    first=parsed[0]
    if (first["crate"]!=1 or first["spring_flash"]!=0 or
        first["heart"]!=0 or
        not 628000 <= first["x_milli"] <= 660000 or
        not 350 <= first["attempt"] <= 490):
        raise ValueError("route not continued from actual live checkpoint")
    if parsed[-1]["attempt"]-first["attempt"]<200:
        raise ValueError("not enough post-checkpoint movement time")
    # Stage 91 and Stage 92 are two CSV witnesses from the SAME real
    # qs_step() iteration in the visible emulator.
    if obstacles is not None:
        matching=[o for o in obstacles if
            o.get("source")=="live_controller_qs_step" and
            int(o["frame"])==first["frame"] and
            int(o["attempt"])==first["attempt"] and
            int(o["x_milli"])==first["x_milli"] and
            int(o["input_bits"])==first["input_bits"] and
            int(o["checkpoint"])==1]
        if not matching:
            raise ValueError("post-checkpoint journal not linked to Stage 91 live route")
    shots=[r for j,r in enumerate(parsed) if j and
           r["input_bits"]&8 and not (parsed[j-1]["input_bits"]&8) and
           r["crate"]==1 and 895000<=r["x_milli"]<=958000]
    broken=[r for r in parsed if r["crate"]==0]
    spring=[r for r in parsed if r["spring_flash"]>0]
    if not shots or not broken or not spring:
        raise ValueError("real Y shot, actual destroyed crate or spring bounce missing")
    fire,break_row,bounce=shots[0],broken[0],spring[0]
    if not (first["attempt"]<fire["attempt"]<
            break_row["attempt"]<bounce["attempt"] and
            915000<=break_row["x_milli"]<=1050000 and
            1120000<=bounce["x_milli"]<=1180000 and
            bounce["vy_milli"]<0 and
            bounce["spring_flash"]>=7):
        raise ValueError("crate-to-spring causal geometry not verified")
    if any(r["crate"]==1 for r in parsed if r["attempt"]>break_row["attempt"]):
        raise ValueError("crate respawned within the same controlled attempt")
    return {
        "sample_count":len(parsed),
        "checkpoint_tick":first["attempt"],
        "shot_tick":fire["attempt"],
        "crate_broken_tick":break_row["attempt"],
        "spring_tick":bounce["attempt"],
        "spring_x_px":round(bounce["x_milli"]/1000,3),
        "spring_vy_px_per_tick":round(bounce["vy_milli"]/1000,3),
        "source":"live NeXium virtual pad, verified against checkpoint journal"
    }

def load(path,header=None):
    with open(path,encoding="utf-8",newline="") as f:
        reader=csv.DictReader(f)
        if header is not None and reader.fieldnames!=header:
            raise ValueError("wrong guest post-checkpoint header")
        return list(reader)

def main(args):
    if len(args)!=4:
        raise SystemExit("usage: verify_postcheck.py <postcheck.csv> <obstacle.csv> <summary.txt>")
    output=Path(args[3])
    try:
        d=verify(load(args[1],FIELDS),load(args[2]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        output.write_text("FAIL: "+str(exc)+"\n")
        raise SystemExit("FAIL: "+str(exc))
    line="LIVE_POSTCHECK_GUEST_PASS "+json.dumps(d,sort_keys=True)+"\n"
    output.write_text(line)
    print(line,end="")

if __name__=="__main__":
    main(sys.argv)
