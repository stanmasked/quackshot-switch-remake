#!/usr/bin/env python3
"""Verify an NRO-created, explicitly simulated-QsInput full-route witness."""
import csv
import pathlib
import sys

FIELDS = ["seq", "event", "tick", "attempt", "x_milli", "y_milli", "detail", "source"]
EVENTS = ["SPAWN", "CHECKPOINT", "CRATE_FIRE", "CRATE_BROKEN",
          "SPRING", "HEART", "LEDGE", "CLEAR", "REENTRY"]

def verify(path):
    with open(path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise ValueError("unexpected guest route CSV header")
        rows = list(reader)
    if len(rows) != len(EVENTS):
        raise ValueError("missing or extra route milestones")
    if [r["event"] for r in rows] != EVENTS:
        raise ValueError("route event order is invalid")
    numbers = []
    for i, row in enumerate(rows):
        if row["source"] != "qs_step_simulated_inputs":
            raise ValueError("provenance must state synthetic QsInput")
        vals = [int(row[k]) for k in ("seq","tick","attempt","x_milli","y_milli","detail")]
        if vals[0] != i or vals[1] < 0 or vals[1] > 1700 or vals[2] < 0:
            raise ValueError("invalid sequence/tick/attempt")
        if not (0 <= vals[3] <= 1280000 and -300000 <= vals[4] <= 500000):
            raise ValueError("out-of-bounds hero coordinates")
        numbers.append(vals)
    ticks = [n[1] for n in numbers]
    if ticks != sorted(ticks) or len(set(ticks[:8])) != 8:
        raise ValueError("nonmonotone route milestones")
    spawn,checkpoint,fire,broken,spring,heart,ledge,clear,reentry = numbers
    if spawn[1:] != [0,0,40000,188000,3]:
        raise ValueError("route did not originate at live stage spawn")
    if checkpoint[5] != checkpoint[2] or checkpoint[2] <= 0:
        raise ValueError("checkpoint split not validated")
    if not 628000 <= checkpoint[3] <= 660000:
        raise ValueError("checkpoint activation at unexpected coordinate")
    if fire[5] != 1 or broken[5] != 0 or spring[5] != 12:
        raise ValueError("crate or spring witnesses invalid")
    if heart[5] != 1 or ledge[5] != 1:
        raise ValueError("air reward / ledge witnesses invalid")
    if not (1130000 <= spring[3] <= 1180000 and
            1140000 <= heart[3] <= 1220000 and
            1160000 <= ledge[3] <= 1220000 and
            ledge[4] == 124000):
        raise ValueError("spring-heart-ledge route geometry impossible")
    if clear[5] < 1 or clear[5] > 1700 or clear[3] < 1216000:
        raise ValueError("goal reached without valid best-time evidence")
    if reentry[2] != 0 or reentry[3] != 40000 or reentry[5] != clear[5]:
        raise ValueError("new attempt failed to reset or retain best time")
    if reentry[1] != clear[1]:
        raise ValueError("invalid reentry step index")
    return {"milestones":len(rows),"clear_simulated_tick":clear[1],
            "best_attempt_frames":clear[5],
            "checkpoint_tick":checkpoint[1],
            "spring_tick":spring[1],
            "air_heart_tick":heart[1],
            "ledge_tick":ledge[1],
            "source":"NRO-side simulated QsInput (not a Joy-Con route)"}

def main():
    if len(sys.argv)!=3:
        raise SystemExit("usage: verify_guest_route.py <guest-route.csv> <summary.txt>")
    import json
    output=pathlib.Path(sys.argv[2])
    try:
        result=verify(sys.argv[1])
    except (OSError,ValueError,KeyError,TypeError) as exc:
        output.write_text("FAIL: guest route witness: "+str(exc)+"\n")
        raise SystemExit("FAIL: guest route witness: "+str(exc))
    output.write_text("GUEST_ROUTE_SIMULATED_PASS "+json.dumps(result,sort_keys=True)+"\n")
    print(output.read_text(),end="")

if __name__=="__main__":
    main()
