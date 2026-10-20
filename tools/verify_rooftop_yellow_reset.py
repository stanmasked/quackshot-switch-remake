#!/usr/bin/env python3
"""Stage158 — guest proof that the yellow-open bridge resets on real map reentry.

The complete Stage157 RED RED YELLOW guest acceptance is rerun on the first
two visits, then the third visit is checked using immutable NRO scene edges,
physical terrain cells, selected weapon and per-frame controller telemetry.
"""
import json
import pathlib
import re
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_red_to_yellow import (
    verify_red_to_yellow, read_weapon, weapon_values, RedToYellowError
)
from tools.verify_rooftop_red_trace import read_city, parsed
from tools.verify_rooftop_guest_trace import read_trace, COLUMNS, SOURCE
from tools.verify_rooftop_reentry_trace import read_edges, EDGE_COLUMNS, EDGE_SOURCE

class YellowResetError(ValueError):
    pass

def verify_yellow_reset(city_rows, roof_rows, edge_rows, weapon_rows, host):
    if len(edge_rows)!=6:
        raise YellowResetError("expected CITY exit, two red visits, yellow-open exit and yellow reset reentry")
    edges=parsed(edge_rows,EDGE_COLUMNS,EDGE_SOURCE)
    for i,e in enumerate(edges):
        if e["seq"]!=i or not e["frame"]>0 or (i and e["frame"]<=edges[i-1]["frame"]):
            raise YellowResetError("guest scene transition sequence/frame invalid")
    leave,enter=edges[4:6]
    if ((leave["old_mode"],leave["old_stage"],leave["mode"],leave["stage"])!=(1,2,0,2)
        or not leave["input_bits"]&64
        or leave["bridge_before"]!=1 or leave["bridge_after"]!=1 or leave["bridge_cells"]!=6):
        raise YellowResetError("real START did not leave an already-open bridge for map")
    if ((enter["old_mode"],enter["mode"],enter["stage"])!=(0,1,2)
        or not enter["input_bits"]&32
        or enter["bridge_before"]!=1 or enter["bridge_after"]!=0 or enter["bridge_cells"]!=0):
        raise YellowResetError("Nintendo A did not reset yellow-open bridge on reentry")
    for e in (leave,enter):
        if e["red_unlocked"]!=1 or e["weapon"]!=0 or e["ruins_relic_earned"]!=edges[3]["ruins_relic_earned"]:
            raise YellowResetError("yellow weapon or earned quest progress lost across map")
    if leave["frame"]-edges[3]["frame"]<90 or enter["frame"]-leave["frame"]<3:
        raise YellowResetError("no genuine gameplay or map gap around third visit")
    allroof=parsed(roof_rows,COLUMNS,SOURCE)
    allweapon=weapon_values(weapon_rows)
    if len(allroof)!=len(allweapon) or not 120 <= len(allroof) <= 1400:
        raise YellowResetError("weapon samples not paired with full guest-rooftop log")
    for a,b in zip(allroof,allweapon):
        if a["seq"]!=b["seq"] or a["frame"]!=b["frame"]:
            raise YellowResetError("weapon frame/seq mismatches NRO ROOFTOP frame")
    first_count=0
    for r in allroof:
        if r["frame"]<leave["frame"]:first_count+=1
        else:break
    first=allroof[:first_count]
    third=allroof[first_count:]
    if not first or not third or len(third)<100:
        raise YellowResetError("missing 100 real frames in third rooftop visit")
    if (first[-1]["frame"]!=leave["frame"]-1 or third[0]["frame"]!=enter["frame"]+1
        or len(first)+len(third)!=len(allroof)):
        raise YellowResetError("rooftop trace crossing scene edges or missing boundary frames")
    if any(leave["frame"]<=r["frame"]<=enter["frame"] for r in allroof):
        raise YellowResetError("NRO emitted ROOFTOP frames during MAP")
    # Stage157 acceptance rechecked in full, not replaced by host messages.
    try:
        previous=verify_red_to_yellow(
            city_rows,roof_rows[:first_count],edge_rows[:4],
            weapon_rows[:first_count],host)
    except (ValueError,RedToYellowError) as exc:
        raise YellowResetError("preceding authentic RED RED YELLOW visit failed: "+str(exc)) from exc
    if previous["bridge_solid_cells"]!=6:
        raise YellowResetError("previous visit did not actually open bridge")
    for i,(r,w) in enumerate(zip(third,allweapon[first_count:])):
        if (r["seq"]!=first_count+i or r["frame"]!=third[0]["frame"]+i or
            r["mode"]!=1 or r["stage"]!=2 or r["bridge_before"]!=0 or
            r["bridge_after"]!=0 or r["bridge_cells"]!=0):
            raise YellowResetError("bridge was not closed for every real third-visit frame")
        if (r["shot_active_before"]!=0 or r["shot_active_after"]!=0 or
            r["shot_red_before"]!=0 or r["shot_x_before_milli"]!=-1 or
            r["shot_vx_before_milli"]!=0 or r["input_bits"]&8):
            raise YellowResetError("stale shot survived stage boundary or unauthenticated new FIRE")
        if (w["weapon_before"]!=0 or w["weapon_after"]!=0 or w["red_unlocked"]!=1 or
            w["change_pressed"]!=0):
            raise YellowResetError("yellow weapon changed during observed third visit")
    def exact_event(label,pattern):
        lines=[l for l in host.splitlines() if l.startswith(label+" ")]
        if len(lines)!=1:raise YellowResetError("missing or duplicated controller evidence "+label)
        m=re.fullmatch(pattern,lines[0])
        if not m:raise YellowResetError("nonnumeric controller evidence "+label)
        return tuple(map(int,m.groups()))
    x,=exact_event("ROOFTOP_YELLOW_OPEN_EXIT_GUEST",r"ROOFTOP_YELLOW_OPEN_EXIT_GUEST frame=(\d+)")
    y,=exact_event("ROOFTOP_YELLOW_RESET_ENTRY_GUEST",r"ROOFTOP_YELLOW_RESET_ENTRY_GUEST frame=(\d+)")
    z,=exact_event("ROOFTOP_YELLOW_RESET_CLOSED_GUEST",r"ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=(\d+)")
    if (x!=leave["frame"] or y!=enter["frame"] or z!=third[-1]["frame"]):
        raise YellowResetError("controller events not tied to real NRO scene or terrain frames")
    return {**previous,"status":"PASS","yellow_open_exit_frame":leave["frame"],
            "yellow_reset_reentry_frame":enter["frame"],
            "third_visit_closed_frames":len(third),"final_rooftop_frame":third[-1]["frame"],
            "bridge_solid_cells_after_reentry":0,"selected_weapon_after_reentry":0}

def main(argv):
    if len(argv)!=7:
        raise SystemExit("usage: verify_rooftop_yellow_reset.py CITY.csv ROOF.csv EDGES.csv WEAPON.csv INPUT.txt OUT.json")
    report=verify_yellow_reset(read_city(argv[1]),read_trace(argv[2]),
        read_edges(argv[3]),read_weapon(argv[4]),pathlib.Path(argv[5]).read_text(encoding="utf-8"))
    pathlib.Path(argv[6]).write_text(json.dumps(report,sort_keys=True)+"\n",encoding="utf-8")
    print("NEXIUM_REAL_YELLOW_BRIDGE_RESET_PASS",json.dumps(report,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
