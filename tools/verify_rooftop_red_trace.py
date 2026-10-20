#!/usr/bin/env python3
"""Stage154: require real CITY pickup followed by a red ROOFTOP guest shot.

Only NRO-generated per-frame input+state evidence is accepted here. The host
runner attests emulator integrity, real virtual pad events, and NRO identity.
"""
import csv
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import (
    COLUMNS, SOURCE, TARGET_X_MILLI, TARGET_W_MILLI,
    SHOT_WIDTH_MILLI, read_trace,
)
from tools.verify_rooftop_reentry_trace import EDGE_COLUMNS, EDGE_SOURCE, read_edges

CITY_COLUMNS = ("seq", "frame", "mode", "stage", "x_milli", "y_milli",
                "grounded", "input_bits", "red_before", "red_after",
                "weapon", "pickup_before", "pickup_after", "source")
CITY_SOURCE = "live_city_red_pad_qs_step"
FIRE = 8
JUMP = 4
RIGHT = 2


class RedGuestError(ValueError):
    pass


def read_city(path):
    with pathlib.Path(path).open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if tuple(reader.fieldnames or ()) != CITY_COLUMNS:
            raise RedGuestError("invalid CITY red pickup CSV header")
        return list(reader)


def parsed(rows, columns, source):
    out = []
    for row in rows:
        if not isinstance(row, dict) or set(row) != set(columns):
            raise RedGuestError("missing/duplicated guest columns")
        if row.get("source") != source:
            raise RedGuestError("non-guest sample provenance")
        try:
            out.append({k: int(v) for k, v in row.items() if k != "source"})
        except (ValueError, TypeError) as exc:
            raise RedGuestError("invalid numeric guest sample") from exc
    return out


def verify_red(city_rows, roof_rows, edge_rows):
    if not 45 <= len(city_rows) <= 500:
        raise RedGuestError("no complete CITY input/pickup route")
    city = parsed(city_rows, CITY_COLUMNS, CITY_SOURCE)
    jumps = []
    flips = []
    ground_before = 0
    for i, p in enumerate(city):
        if (p["seq"] != i or p["mode"] != 1 or p["stage"] != 0 or
            p["frame"] <= 0 or (i > 0 and p["frame"] != city[i-1]["frame"]+1)):
            raise RedGuestError("bad CITY sequence/scene")
        if any(p[k] not in (0,1) for k in ("grounded","red_before","red_after",
                                           "pickup_before","pickup_after")):
            raise RedGuestError("noncanonical CITY pickup state")
        if p["weapon"] not in (0,1) or p["input_bits"] & ~127:
            raise RedGuestError("invalid city weapon/input")
        if not 20000 <= p["x_milli"] <= 300000 or not 0 <= p["y_milli"] <= 230000:
            raise RedGuestError("implausible CITY pickup route")
        if i and abs(p["x_milli"]-city[i-1]["x_milli"]) > 12000:
            raise RedGuestError("CITY hero teleported")
        if i and p["red_before"] != city[i-1]["red_after"]:
            raise RedGuestError("unlock changed outside CITY simulation")
        if i and p["pickup_before"] != city[i-1]["pickup_after"]:
            raise RedGuestError("pickup changed outside CITY simulation")
        if i and (p["input_bits"] & JUMP) and not (city[i-1]["input_bits"] & JUMP):
            jumps.append(i)
        if p["grounded"] and not p["red_after"]:
            ground_before += 1
        if not p["red_before"] and p["red_after"]:
            flips.append(i)
            if (p["weapon"]!=1 or p["pickup_before"]!=1 or
                p["pickup_after"]!=0 or not
                (p["x_milli"] < 189000 and p["x_milli"]+12000 > 179000 and
                 p["y_milli"] < 150000 and p["y_milli"]+20000 > 140000)):
                raise RedGuestError("unlock not earned at actual pickup overlap")
        elif p["red_before"] > p["red_after"] or (
             p["pickup_before"] < p["pickup_after"]):
            raise RedGuestError("illegitimate CITY reset or regenerated pickup")
    if (city[0]["red_before"] != 0 or city[0]["pickup_before"] != 1 or
        len(flips) != 1 or not jumps or not ground_before):
        raise RedGuestError("no real red pickup after a jump from CITY spawn")
    if not jumps[0] < flips[0] or city[-1]["red_after"] != 1:
        raise RedGuestError("jump did not precede persistent red unlock")
    if city[flips[0]]["frame"] <= city[0]["frame"]+10:
        raise RedGuestError("instant/unrealistic unlock")
    if city[-1]["weapon"] != 1:
        raise RedGuestError("red weapon not selected after CITY pickup")
    if not 65 <= len(roof_rows) <= 1400:
        raise RedGuestError("no sufficiently long ROOFTOP guest red trajectory")
    roof=parsed(roof_rows,COLUMNS,SOURCE)
    red_flight=[]
    fire_edges=[]
    all_x=[]
    last_shot=None
    for i,p in enumerate(roof):
        if (p["seq"]!=i or p["frame"]<=city[-1]["frame"] or
            (i>0 and p["frame"]!=roof[i-1]["frame"]+1) or
            p["mode"]!=1 or p["stage"]!=2):
            raise RedGuestError("interchanged or discontinuous ROOFTOP frames")
        if not (0<=p["input_bits"]<=127) or not 0<=p["x_milli"]<=850000:
            raise RedGuestError("invalid ROOFTOP input/position")
        if i and abs(p["x_milli"]-roof[i-1]["x_milli"])>12000:
            raise RedGuestError("ROOFTOP hero teleported")
        if any(p[k] not in (0,1) for k in
               ("bridge_before","bridge_after","shot_red_before","shot_active_before")):
            raise RedGuestError("malformed red shot/bridge boolean")
        if (p["bridge_before"] or p["bridge_after"] or p["bridge_cells"]):
            raise RedGuestError("red projectile activated physical bridge")
        if p["shot_active_before"]==0 and (
            p["shot_x_before_milli"]!=-1 or p["shot_vx_before_milli"]!=0 or
            p["shot_red_before"]!=0):
            raise RedGuestError("fake inactive red shot")
        if not 0<=p["shot_active_after"]<=8:
            raise RedGuestError("invalid after-shot count")
        if i and p["input_bits"]&FIRE and not roof[i-1]["input_bits"]&FIRE:
            fire_edges.append(i)
        if p["shot_active_before"]:
            if not p["shot_red_before"] or not 0 < p["shot_vx_before_milli"] <= 12000:
                raise RedGuestError("yellow/reversed projectile in red-only trial")
            if not 0<=p["shot_x_before_milli"]<=1400000:
                raise RedGuestError("invalid red projectile coordinate")
            red_flight.append((i,p["shot_x_before_milli"],p["shot_vx_before_milli"]))
    if not fire_edges or not 5 <= len(red_flight):
        raise RedGuestError("no real red FIRE and projectile flight")
    first = red_flight[0][0]
    if first<=fire_edges[0] or red_flight[0][1] >= TARGET_X_MILLI:
        raise RedGuestError("red shot started beyond switch or before FIRE")
    # This stage intentionally fires one real red shot and observes it.
    if len(fire_edges)!=1:
        raise RedGuestError("multiple FIRE edges contaminate single-red trial")
    for (i,x,vx),(j,y,_) in zip(red_flight,red_flight[1:]):
        if j!=i+1 or abs((y-x)-vx)>2500 or y<=x:
            raise RedGuestError("missing/red-shot teleport between frames")
    overlaps=[i for i,x,v in red_flight if
              x+v<TARGET_X_MILLI+TARGET_W_MILLI and
              x+v+SHOT_WIDTH_MILLI>TARGET_X_MILLI]
    if not overlaps:
        raise RedGuestError("red projectile never genuinely overlapped switch")
    hit_idx=overlaps[0]
    if hit_idx < fire_edges[0]+3 or roof[-1]["frame"]<roof[hit_idx]["frame"]+30:
        raise RedGuestError("no adequate observation of closed bridge after red overlap")
    for p in roof[hit_idx:]:
        if p["bridge_after"]!=0 or p["bridge_cells"]!=0:
            raise RedGuestError("bridge changed after red projectile overlap")
    if len(edge_rows)!=2:
        raise RedGuestError("missing real CITY exit or ambiguous ROOFTOP entry")
    leave,e=parsed(edge_rows,EDGE_COLUMNS,EDGE_SOURCE)
    if (leave["seq"]!=0 or leave["old_mode"]!=1 or leave["old_stage"]!=0 or
        leave["mode"]!=0 or leave["stage"]!=0 or
        not (leave["input_bits"] & 64) or
        leave["red_unlocked"]!=1 or leave["weapon"]!=1 or
        leave["bridge_before"]!=0 or leave["bridge_after"]!=0 or
        not city[-1]["frame"] < leave["frame"] <= city[-1]["frame"]+4):
        raise RedGuestError("no live CITY-to-MAP exit after red pickup")
    if (e["seq"]!=1 or e["old_mode"]!=0 or e["mode"]!=1 or e["stage"]!=2 or
        not (e["input_bits"] & 32) or (e["input_bits"] & 64) or
        e["red_unlocked"]!=1 or e["weapon"]!=1 or e["bridge_before"]!=0 or
        e["bridge_after"]!=0 or e["bridge_cells"]!=0 or
        e["frame"]!=roof[0]["frame"]-1 or e["frame"]<=leave["frame"]+3):
        raise RedGuestError("red unlock or authentic ROOFTOP entry lost across MAP")
    return {"status":"PASS","pickup_frame":city[flips[0]]["frame"],
            "pickup_x_milli":city[flips[0]]["x_milli"],"rooftop_entry_frame":e["frame"],
            "red_flight_frames":len(red_flight),"red_switch_overlap_frame":roof[hit_idx]["frame"],
            "post_hit_observation_frames":roof[-1]["frame"]-roof[hit_idx]["frame"],
            "bridge_solid_cells":0}


def main(argv):
    if len(argv)!=5:
        raise SystemExit("usage: verify_rooftop_red_trace.py CITY.csv ROOF.csv EDGES.csv SUMMARY.json")
    try:
        result=verify_red(read_city(argv[1]),read_trace(argv[2]),read_edges(argv[3]))
    except (OSError,ValueError) as exc:
        raise SystemExit("FAIL: red guest switch proof: "+str(exc)) from exc
    pathlib.Path(argv[4]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("ROOFTOP_GUEST_RED_SWITCH_NO_BRIDGE_PASS",json.dumps(result,sort_keys=True))


if __name__=="__main__":
    main(sys.argv)
