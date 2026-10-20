#!/usr/bin/env python3
"""Stage156: two real RED guest shots separated by ROOFTOP->MAP->ROOFTOP.

Uses unmodified guest-origin CSVs. Never infers gameplay from host assertions.
"""
import json
import pathlib
import re
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_red_trace import read_city, verify_red, parsed, RedGuestError
from tools.verify_rooftop_guest_trace import (
    read_trace, COLUMNS, SOURCE, TARGET_X_MILLI, TARGET_W_MILLI, SHOT_WIDTH_MILLI
)
from tools.verify_rooftop_reentry_trace import (
    read_edges, EDGE_COLUMNS, EDGE_SOURCE
)

class SecondRedGuestError(ValueError):
    pass

def verify_second_red(city_rows, roof_rows, edge_rows):
    if len(edge_rows) != 4:
        raise SecondRedGuestError("expected CITY exit, first entry, ROOF exit, second entry")
    edges = parsed(edge_rows, EDGE_COLUMNS, EDGE_SOURCE)
    for i,e in enumerate(edges):
        if e["seq"] != i or e["frame"] < 1 or (i and e["frame"] <= edges[i-1]["frame"]):
            raise SecondRedGuestError("invalid four-edge frame/sequence")
        if e["red_unlocked"] != 1 or e["weapon"] != 1 or e["bridge_cells"] != 0 or e["bridge_after"] != 0:
            raise SecondRedGuestError("lost red or opened bridge on stage boundary")
    leave, reenter = edges[2], edges[3]
    if (leave["old_mode"],leave["old_stage"],leave["mode"],leave["stage"]) != (1,2,0,2) or not leave["input_bits"] & 64:
        raise SecondRedGuestError("no real START exit from first ROOFTOP")
    if (reenter["old_mode"],reenter["mode"],reenter["stage"]) != (0,1,2) or not reenter["input_bits"] & 32:
        raise SecondRedGuestError("no real Nintendo A ROOFTOP reentry")
    if leave["bridge_before"] or reenter["bridge_before"]:
        raise SecondRedGuestError("bridge not closed across second map visit")
    if not edges[1]["frame"] + 80 < leave["frame"] < reenter["frame"] - 4:
        raise SecondRedGuestError("not enough first ROOFTOP frames or map gap")
    # Split at the true transition. No host-defined arbitrary sequence split.
    first = [r for r in roof_rows if int(r["frame"]) < leave["frame"]]
    between = [r for r in roof_rows if leave["frame"] <= int(r["frame"]) <= reenter["frame"]]
    second = [r for r in roof_rows if int(r["frame"]) > reenter["frame"]]
    if between or not first or not second:
        raise SecondRedGuestError("logged ROOFTOP data during map or missing visit")
    if int(first[0]["frame"]) != edges[1]["frame"]+1 or int(first[-1]["frame"]) != leave["frame"]-1:
        raise SecondRedGuestError("first visit missing contiguous beginning/end")
    if int(second[0]["frame"]) != reenter["frame"]+1:
        raise SecondRedGuestError("second visit not attached to actual map reentry")
    if len(first)+len(second) != len(roof_rows) or len(first)+len(second) > 1400:
        raise SecondRedGuestError("ROOFTOP trace exceeds original bounded guest logger")
    if not 85 <= len(second) <= 1100:
        raise SecondRedGuestError("no complete second ROOFTOP firing sequence")
    # Stage154's full authenticated acquisition/shot/closed bridge proof
    # remains in force on the first visit, including exact pad input.
    try:
        first_report = verify_red(city_rows,first,edge_rows[:2])
    except (RedGuestError, ValueError) as e:
        raise SecondRedGuestError("first genuine red shot invalid: "+str(e)) from e
    roof = parsed(second,COLUMNS,SOURCE)
    fire_edges, flights=[],[]
    for i,p in enumerate(roof):
        if (p["seq"] != len(first)+i or p["frame"] != roof[0]["frame"]+i or
            p["mode"] != 1 or p["stage"] != 2 or not 0 <= p["input_bits"] <= 127):
            raise SecondRedGuestError("discontinuous second-visit NRO input frames")
        if p["bridge_before"] or p["bridge_after"] or p["bridge_cells"]:
            raise SecondRedGuestError("RED opened real bridge on second visit")
        if i and abs(p["x_milli"]-roof[i-1]["x_milli"]) > 12000:
            raise SecondRedGuestError("second-visit hero teleported")
        if i and p["input_bits"] & 8 and not roof[i-1]["input_bits"] & 8:
            fire_edges.append(i)
        if p["shot_active_before"]:
            if p["shot_red_before"] != 1 or not 0 < p["shot_vx_before_milli"] <= 12000:
                raise SecondRedGuestError("non-red/nonforward second projectile")
            if not 0 <= p["shot_x_before_milli"] <= 1400000:
                raise SecondRedGuestError("impossible red projectile position")
            flights.append((i,p["shot_x_before_milli"],p["shot_vx_before_milli"]))
        elif (p["shot_red_before"] or p["shot_x_before_milli"] != -1 or
              p["shot_vx_before_milli"] != 0):
            raise SecondRedGuestError("invalid inactive projectile trace")
    if len(fire_edges) != 1 or len(flights) < 5:
        raise SecondRedGuestError("not exactly one live second FIRE+red flight")
    if flights[0][0] <= fire_edges[0] or flights[0][1] >= TARGET_X_MILLI:
        raise SecondRedGuestError("second red shot begins after target or before FIRE")
    for (i,x,v),(j,y,_) in zip(flights,flights[1:]):
        if j!=i+1 or y<=x or abs(y-x-v)>2500:
            raise SecondRedGuestError("second shot flight discontinuous")
    hits=[i for i,x,v in flights if
          x+v<TARGET_X_MILLI+TARGET_W_MILLI and x+v+SHOT_WIDTH_MILLI>TARGET_X_MILLI]
    if not hits or hits[0] <= fire_edges[0]+2:
        raise SecondRedGuestError("second red shot never crossed actual switch")
    if roof[-1]["frame"] - roof[hits[0]]["frame"] < 30:
        raise SecondRedGuestError("no post-hit closed bridge observation")
    return {"status":"PASS","first_red_frame":first_report["red_switch_overlap_frame"],
            "first_pickup_frame":first_report["pickup_frame"],
            "first_rooftop_entry_frame":edges[1]["frame"],
            "first_exit_frame":leave["frame"],"second_rooftop_entry_frame":reenter["frame"],
            "second_red_frame":roof[hits[0]]["frame"],
            "second_red_x_milli":roof[hits[0]]["shot_x_before_milli"],
            "second_red_flight_frames":len(flights),
            "second_post_hit_frames":roof[-1]["frame"]-roof[hits[0]]["frame"],
            "bridge_solid_cells":0}

def verify_host_transcript(host,edges,report):
    def event(key,pattern):
        matches=[line for line in host.splitlines() if line.startswith(key+" ")]
        if len(matches)!=1:
            raise SecondRedGuestError("missing/duplicate host event "+key)
        m=re.fullmatch(pattern,matches[0])
        if not m:
            raise SecondRedGuestError("nonnumeric host event "+key)
        return tuple(int(z) for z in m.groups())
    old,=event("ROOFTOP_EXIT_GUEST",r"ROOFTOP_EXIT_GUEST frame=(\d+)")
    new,=event("ROOFTOP_REENTRY_RED_GUEST",r"ROOFTOP_REENTRY_RED_GUEST frame=(\d+)")
    hit,x=event("ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST",
                r"ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST frame=(\d+) x_milli=(\d+)")
    if (old != report["first_exit_frame"] or new != report["second_rooftop_entry_frame"] or
        hit != report["second_red_frame"] or x != report["second_red_x_milli"]):
        raise SecondRedGuestError("host events do not match guest transitions/projectile")
    return True

def main(argv):
    if len(argv)!=6:
        raise SystemExit("usage: verify_rooftop_second_red_trace.py CITY.csv ROOF.csv EDGES.csv INPUT.txt REPORT.json")
    result=verify_second_red(read_city(argv[1]),read_trace(argv[2]),read_edges(argv[3]))
    host=pathlib.Path(argv[4]).read_text(encoding="utf-8")
    verify_host_transcript(host, read_edges(argv[3]),result)
    pathlib.Path(argv[5]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("ROOFTOP_SECOND_REAL_RED_NO_BRIDGE_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
