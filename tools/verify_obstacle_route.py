#!/usr/bin/env python3
"""Stage 91: real NeXium virtual-controller pit and checkpoint witness."""
import csv
import json
from pathlib import Path
import sys

FIELDS=["seq","frame","attempt","x_milli","y_milli","grounded","input_bits",
        "checkpoint","mode","source"]

def verify(samples):
    if not 90 <= len(samples) <= 350:
        raise ValueError("missing sustained second-stage guest telemetry")
    rows=[]
    for i,s in enumerate(samples):
        try:
            r={k:int(s[k]) for k in FIELDS if k!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise ValueError("bad numerical telemetry") from exc
        if (r["seq"]!=i or s.get("source")!="live_controller_qs_step" or
            r["mode"]!=1 or not 0<=r["x_milli"]<=1280000 or
            not -300000<=r["y_milli"]<=500000 or
            r["grounded"] not in (0,1) or r["checkpoint"] not in (0,1)):
            raise ValueError("forged source, invalid mode, or corrupt world state")
        if rows and (r["frame"]<=rows[-1]["frame"] or
                     r["attempt"]<rows[-1]["attempt"] or
                     r["frame"]-rows[-1]["frame"]>4):
            raise ValueError("nonmonotone or dropped guest simulation ticks")
        rows.append(r)
    if rows[0]["x_milli"]!=40000 or rows[0]["y_milli"]!=188000:
        raise ValueError("controller route did not start at spawn")
    if rows[0]["checkpoint"]:
        raise ValueError("checkpoint was preactivated")
    pit=[r for r in rows if (r["input_bits"]&6)==6 and
         275000<=r["x_milli"]<=309000]
    roof=[r for r in rows if (r["input_bits"]&6)==6 and
          390000<=r["x_milli"]<=437000]
    crossing=[r for r in rows if r["x_milli"]>=368000]
    wall=[r for r in rows if r["x_milli"]>=560000]
    saved=[r for r in rows if r["checkpoint"]==1]
    if not pit or not roof or not crossing or not wall or not saved:
        raise ValueError("real B/pit, B/roof, wall or checkpoint event missing")
    pit_t,roof_t=pit[0]["attempt"],roof[0]["attempt"]
    cross_t,wall_t,saved_t=crossing[0]["attempt"],wall[0]["attempt"],saved[0]["attempt"]
    if not (0<pit_t<cross_t<roof_t<wall_t<=saved_t):
        raise ValueError("physical obstacle chronology broken")
    if not 628000 <= saved[0]["x_milli"] <= 660000:
        raise ValueError("checkpoint activated outside expected landmark")
    if (not any(r["grounded"]==0 and 320000<=r["x_milli"]<368000 for r in rows)
        or not any(r["input_bits"]&2 for r in rows[:30])):
        raise ValueError("not enough airborne pit traversal / right held input")
    if any(b["x_milli"]+8000<a["x_milli"] for a,b in zip(rows,rows[1:])):
        raise ValueError("respawn or teleport detected")
    return {"sample_count":len(rows),"pit_jump_tick":pit_t,
            "pit_cross_tick":cross_t,"roof_jump_tick":roof_t,
            "wall_cross_tick":wall_t,"checkpoint_tick":saved_t,
            "checkpoint_x_px":round(saved[0]["x_milli"]/1000,3),
            "source":"live NeXium virtual controller (not QsInput self-test)"}

def main(args):
    if len(args)!=3:
        raise SystemExit("usage: verify_obstacle_route.py <obstacle.csv> <summary.txt>")
    out=Path(args[2])
    try:
        with open(args[1],encoding="utf-8",newline="") as f:
            reader=csv.DictReader(f)
            if reader.fieldnames!=FIELDS:
                raise ValueError("incorrect guest obstacle header")
            samples=list(reader)
        result=verify(samples)
    except (ValueError,KeyError,TypeError,OSError) as exc:
        out.write_text("FAIL: "+str(exc)+"\n")
        raise SystemExit("FAIL: "+str(exc))
    msg="LIVE_OBSTACLE_GUEST_PASS "+json.dumps(result,sort_keys=True)+"\n"
    out.write_text(msg);print(msg,end="")

if __name__=="__main__":
    main(sys.argv)
