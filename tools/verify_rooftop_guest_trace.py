#!/usr/bin/env python3
"""Validate independent guest-origin ROOFTOP switch/bridge telemetry.

This is a *parser*, not a guest run. It is intentionally fail-closed.
A valid-looking CSV alone does not establish that a controller was real:
the emulator job must separately attest the NRO SHA, input injection,
exact profile and original guest-produced SD-side file provenance.
"""
import csv
import pathlib
import sys

COLUMNS=(
    "seq","frame","mode","stage","x_milli","y_milli","grounded",
    "input_bits","shot_x_before_milli","shot_vx_before_milli",
    "shot_red_before","shot_active_before","shot_active_after",
    "bridge_before","bridge_after","bridge_cells","source"
)
SOURCE="live_rooftop_pad_qs_step"
FIRE_MASK=8
RIGHT_MASK=2
TARGET_X_MILLI=704000
TARGET_W_MILLI=12000
SHOT_WIDTH_MILLI=7000
BRIDGE_FIRST_X_MILLI=52*16*1000
BRIDGE_END_X_MILLI=55*16*1000

class RooftopTraceError(ValueError):
    pass

def read_trace(filename):
    with pathlib.Path(filename).open("r",encoding="utf-8",newline="") as stream:
        reader=csv.DictReader(stream)
        if tuple(reader.fieldnames or ())!=COLUMNS:
            raise RooftopTraceError("unrecognized ROOFTOP guest CSV header")
        return [dict(row) for row in reader]

def verify(rows):
    if not 30<=len(rows)<=1400:
        raise RooftopTraceError("ROOFTOP requires 30..1400 contiguous guest ticks")
    parsed=[]
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(COLUMNS):
            raise RooftopTraceError("missing or duplicated CSV fields")
        if row["source"]!=SOURCE:
            raise RooftopTraceError("non-guest or untrusted sample source")
        try:
            p={k:int(row[k]) for k in COLUMNS if k!="source"}
        except (ValueError,TypeError,KeyError) as exc:
            raise RooftopTraceError("nonnumeric guest world-state field") from exc
        if (p["seq"]!=i or p["frame"]<=0 or
            (i>0 and p["frame"]!=parsed[-1]["frame"]+1) or
            p["mode"]!=1 or p["stage"]!=2):
            raise RooftopTraceError("missing/interchanged tick, stage or sequence")
        for bit in ("grounded","shot_red_before","shot_active_before",
                    "bridge_before","bridge_after"):
            if p[bit] not in (0,1):
                raise RooftopTraceError("corrupt boolean "+bit)
        if p["bridge_cells"] not in (0,6):
            raise RooftopTraceError("partial/fabricated bridge collision")
        if p["bridge_cells"]!=6*p["bridge_after"]:
            raise RooftopTraceError("bridge flag diverges from physical terrain")
        if p["x_milli"]<0 or p["x_milli"]>1600*1000 or not -150000<=p["y_milli"]<=250000:
            raise RooftopTraceError("impossible hero world location")
        if p["shot_active_before"]==0 and (
            p["shot_x_before_milli"]!=-1 or p["shot_vx_before_milli"]!=0
            or p["shot_red_before"]!=0):
            raise RooftopTraceError("invented projectile without active slot")
        if p["shot_active_before"]==1 and not 0<=p["shot_x_before_milli"]<=1600000:
            raise RooftopTraceError("invalid active projectile coordinate")
        if p["shot_active_after"]<0 or p["shot_active_after"]>8:
            raise RooftopTraceError("invalid post-step shot count")
        if i>0 and p["bridge_before"]!=parsed[-1]["bridge_after"]:
            raise RooftopTraceError("bridge state changed outside qs_step")
        if i>0 and abs(p["x_milli"]-parsed[-1]["x_milli"])>12000:
            raise RooftopTraceError("guest hero teleported between adjacent simulation frames")
        parsed.append(p)
    if parsed[0]["bridge_before"] or parsed[0]["bridge_after"]:
        raise RooftopTraceError("bridge was already open before input")
    flips=[i for i,p in enumerate(parsed) if p["bridge_before"]==0 and p["bridge_after"]==1]
    if len(flips)!=1 or parsed[-1]["bridge_after"]!=1:
        raise RooftopTraceError("missing, repeated or reverted bridge trigger")
    trigger=flips[0]
    row=parsed[trigger]
    if (not row["shot_active_before"] or row["shot_red_before"] or
        row["shot_vx_before_milli"]<=0 or row["shot_active_after"]!=0):
        raise RooftopTraceError("bridge appeared without consumed live yellow shot")
    next_x=row["shot_x_before_milli"]+row["shot_vx_before_milli"]
    if not (next_x<TARGET_X_MILLI+TARGET_W_MILLI and
            next_x+SHOT_WIDTH_MILLI>TARGET_X_MILLI):
        raise RooftopTraceError("no physically intersecting projectile at flip")
    if any(p["bridge_after"] for p in parsed[:trigger]):
        raise RooftopTraceError("bridge opened before authenticated projectile")
    # A real FIRE keydown must precede an independently moving projectile.
    edges=[i for i in range(1,trigger)
           if parsed[i]["input_bits"]&FIRE_MASK and
           not parsed[i-1]["input_bits"]&FIRE_MASK]
    if not edges:
        raise RooftopTraceError("no live FIRE press preceding shot movement")
    flight=[(i,p["shot_x_before_milli"]) for i,p in enumerate(parsed[:trigger+1])
            if p["shot_active_before"] and not p["shot_red_before"]
            and p["shot_vx_before_milli"]>0]
    if len(flight)<3 or flight[-1][0]!=trigger:
        raise RooftopTraceError("projectile lacks continuous guest flight evidence")
    for (i,a),(j,b) in zip(flight,flight[1:]):
        if (j!=i+1 or not 1<=b-a<=12000 or
            abs((b-a)-parsed[i]["shot_vx_before_milli"])>2500):
            raise RooftopTraceError("teleported/stale or discontinuous projectile")
    if flight[0][0]<=edges[0]:
        raise RooftopTraceError("bullet position precedes real FIRE input")
    # Crossing must be seen in actual grounded, right-moving hero ticks.
    ground_walk=[p for p in parsed[trigger+1:]
                 if p["grounded"] and p["input_bits"]&RIGHT_MASK]
    if (len(ground_walk)<12 or
        not any(p["x_milli"]<=BRIDGE_FIRST_X_MILLI+12000 for p in ground_walk) or
        not any(p["x_milli"]>=BRIDGE_END_X_MILLI+12000 for p in ground_walk)):
        raise RooftopTraceError("no authenticated rightward grounded bridge crossing")
    for a,b in zip(ground_walk,ground_walk[1:]):
        if b["x_milli"]<a["x_milli"]-2000 or b["x_milli"]-a["x_milli"]>12000:
            raise RooftopTraceError("hero teleported while crossing bridge")
    return {"status":"PASS","ticks":len(parsed),"trigger_frame":row["frame"],
            "shot_flight_samples":len(flight),
            "first_grounded_cross_x_milli":ground_walk[0]["x_milli"],
            "last_grounded_cross_x_milli":ground_walk[-1]["x_milli"]}

def main(argv):
    if len(argv)!=2:
        raise SystemExit("usage: verify_rooftop_guest_trace.py <guest-rooftop.csv>")
    try:
        result=verify(read_trace(argv[1]))
    except (RooftopTraceError,OSError) as exc:
        raise SystemExit("FAIL: ROOFTOP guest trace: "+str(exc)) from exc
    print("ROOFTOP_GUEST_TRACE_PASS",result)

if __name__=="__main__":
    main(sys.argv)
