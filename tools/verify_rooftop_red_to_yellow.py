#!/usr/bin/env python3
"""Stage157: guest RED, RED after reentry, then actual YELLOW switch activation.

No host-defined game state is trusted. Separate NRO weapon log proves
physical Nintendo X caused red->yellow. ROOF log proves projectile color
and six real collision cells. Stage156 validates both preceding red shots.
"""
import csv
import json
import pathlib
import re
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_second_red_trace import verify_second_red,verify_host_transcript
from tools.verify_rooftop_red_trace import read_city,parsed
from tools.verify_rooftop_guest_trace import (
    read_trace,COLUMNS,SOURCE,TARGET_X_MILLI,TARGET_W_MILLI,SHOT_WIDTH_MILLI
)
from tools.verify_rooftop_reentry_trace import read_edges

WEAPON_COLUMNS=("seq","frame","mode","stage","change_pressed",
                "weapon_before","weapon_after","red_unlocked","source")
WEAPON_SOURCE="live_rooftop_weapon_pad_qs_step"

class RedToYellowError(ValueError): pass

def read_weapon(path):
    with pathlib.Path(path).open(newline="",encoding="utf-8") as f:
        r=csv.DictReader(f)
        if tuple(r.fieldnames or ())!=WEAPON_COLUMNS:
            raise RedToYellowError("unrecognized NRO weapon telemetry header")
        return list(r)

def weapon_values(rows):
    if not rows: raise RedToYellowError("missing guest weapon log")
    results=[]
    for row in rows:
        if not isinstance(row,dict) or set(row)!=set(WEAPON_COLUMNS) or row.get("source")!=WEAPON_SOURCE:
            raise RedToYellowError("untrusted weapon telemetry")
        try:
            p={k:int(row[k]) for k in WEAPON_COLUMNS if k!="source"}
        except (ValueError,TypeError,KeyError) as exc:
            raise RedToYellowError("nonnumeric weapon guest frame") from exc
        if (p["mode"]!=1 or p["stage"]!=2 or
            p["change_pressed"] not in (0,1) or p["red_unlocked"]!=1 or
            p["weapon_before"] not in (0,1) or p["weapon_after"] not in (0,1)):
            raise RedToYellowError("invalid weapon state or original scene")
        results.append(p)
    return results

def verify_red_to_yellow(city_rows,roof_rows,edges,weapon_rows,host):
    roof=parsed(roof_rows,COLUMNS,SOURCE)
    w=weapon_values(weapon_rows)
    if len(roof)!=len(w) or len(roof)>1400:
        raise RedToYellowError("weapon file does not cover every real rooftop frame")
    for p,x in zip(roof,w):
        if x["seq"]!=p["seq"] or x["frame"]!=p["frame"]:
            raise RedToYellowError("weapon tick does not match immutable ROOF sample")
    switch=[i for i,p in enumerate(w) if p["change_pressed"]==1]
    if len(switch)!=1:
        raise RedToYellowError("missing or repeated physical change-button input")
    toggle=switch[0]
    p=w[toggle]
    if p["weapon_before"]!=1 or p["weapon_after"]!=0:
        raise RedToYellowError("real Nintendo X did not select YELLOW")
    if toggle<100 or len(roof)-toggle<40:
        raise RedToYellowError("too few authentic frames around ammo selection")
    for i,item in enumerate(w):
        wanted=1 if i<toggle else 0
        if (item["weapon_after"]!=wanted or
            (i!=toggle and item["weapon_before"]!=wanted)):
            raise RedToYellowError("unlogged weapon swap or wrong persistent color")
    red_prefix=roof_rows[:toggle]
    red_report=verify_second_red(city_rows,red_prefix,edges)
    verify_host_transcript(host,edges,red_report)
    if roof[toggle]["bridge_cells"] or roof[toggle]["bridge_after"]:
        raise RedToYellowError("bridge opened before yellow projectile")
    prev=roof[toggle-1]["input_bits"]
    fires=[]
    yellow=[]
    opened=[]
    for j in range(toggle,len(roof)):
        p=roof[j]
        if j and (p["seq"]!=roof[j-1]["seq"]+1 or p["frame"]!=roof[j-1]["frame"]+1):
            raise RedToYellowError("gap in yellow-stage NRO telemetry")
        if (p["input_bits"]&8) and not (prev&8):fires.append(j)
        prev=p["input_bits"]
        if p["shot_active_before"]:
            if p["shot_red_before"]!=0 or not 0 < p["shot_vx_before_milli"] <= 12000:
                raise RedToYellowError("not a real forward YELLOW projectile")
            yellow.append(j)
        if p["bridge_after"]==1 and p["bridge_cells"]==6 and p["bridge_before"]==0:
            opened.append(j)
        elif p["bridge_after"] not in (0,1) or p["bridge_cells"] not in (0,6):
            raise RedToYellowError("partial or invalid real bridge terrain")
    if len(fires)!=1 or len(opened)!=1 or not yellow:
        raise RedToYellowError("expected one yellow FIRE edge and one bridge-open edge")
    hit=opened[0]
    overlap=[j for j in yellow if
        roof[j]["shot_x_before_milli"]+roof[j]["shot_vx_before_milli"] <
          TARGET_X_MILLI+TARGET_W_MILLI and
        roof[j]["shot_x_before_milli"]+roof[j]["shot_vx_before_milli"]+SHOT_WIDTH_MILLI >
          TARGET_X_MILLI]
    if hit not in overlap or hit<=fires[0]+2:
        raise RedToYellowError("no real yellow projectile opening switch on impact")
    for j in range(toggle,hit):
        if roof[j]["bridge_before"] or roof[j]["bridge_after"] or roof[j]["bridge_cells"]:
            raise RedToYellowError("bridge opened before yellow impact")
    if len(roof)-hit-1 <30:
        raise RedToYellowError("too little observation after actual yellow hit")
    for p in roof[hit+1:]:
        if p["bridge_before"]!=1 or p["bridge_after"]!=1 or p["bridge_cells"]!=6:
            raise RedToYellowError("yellow bridge did not stay physically open")
    result={**red_report,
            "yellow_change_frame":roof[toggle]["frame"],
            "yellow_fire_frame":roof[fires[0]]["frame"],
            "yellow_switch_open_frame":roof[hit]["frame"],
            "post_yellow_open_frames":len(roof)-hit-1,
            "bridge_solid_cells":6,"status":"PASS"}
    def event(label,pattern):
        matches=[s for s in host.splitlines() if s.startswith(label+" ")]
        if len(matches)!=1:raise RedToYellowError("missing/duplicate host yellow event "+label)
        m=re.fullmatch(pattern,matches[0])
        if not m:raise RedToYellowError("malformed yellow host witness "+label)
        return tuple(int(n) for n in m.groups())
    changed,=event("ROOFTOP_YELLOW_SELECTED_GUEST",r"ROOFTOP_YELLOW_SELECTED_GUEST frame=(\d+)")
    shot,=event("ROOFTOP_YELLOW_FIRE_GUEST",r"ROOFTOP_YELLOW_FIRE_GUEST frame=(\d+)")
    opened_frame,=event("ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST",r"ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST frame=(\d+)")
    if (changed!=result["yellow_change_frame"] or shot!=result["yellow_fire_frame"] or
        opened_frame!=result["yellow_switch_open_frame"]):
        raise RedToYellowError("host evidence disagrees with NRO weapon/fire/bridge")
    return result

def main(argv):
    if len(argv)!=7:
        raise SystemExit("usage: verify_rooftop_red_to_yellow.py CITY ROOF EDGES WEAPON INPUT OUT.json")
    report=verify_red_to_yellow(read_city(argv[1]),read_trace(argv[2]),
                                read_edges(argv[3]),read_weapon(argv[4]),
                                pathlib.Path(argv[5]).read_text(encoding="utf-8"))
    pathlib.Path(argv[6]).write_text(json.dumps(report,sort_keys=True)+"\n",encoding="utf-8")
    print("NEXIUM_REAL_RED_TO_YELLOW_BRIDGE_PASS",json.dumps(report,sort_keys=True))
if __name__=="__main__": main(sys.argv)
