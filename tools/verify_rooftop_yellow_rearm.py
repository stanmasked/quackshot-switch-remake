#!/usr/bin/env python3
"""Stage160: two real yellow openings across live MAP->ROOFTOP reset.

Replay the *complete* Stage158 authenticated prefix, including two RED
negatives, first YELLOW switch opening, OPEN->CLOSED map rebuild and 100+
unaltered closed frames. Then accept only another genuine NRO YELLOW
projectile flight crossing the same switch and opening six bridge cells.
"""
import json
import pathlib
import re
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_yellow_reset import verify_yellow_reset
from tools.verify_rooftop_red_to_yellow import read_weapon,weapon_values
from tools.verify_rooftop_red_trace import read_city,parsed
from tools.verify_rooftop_guest_trace import (
    read_trace,COLUMNS,SOURCE,TARGET_X_MILLI,TARGET_W_MILLI,SHOT_WIDTH_MILLI
)
from tools.verify_rooftop_reentry_trace import read_edges

class YellowRearmError(ValueError):
    pass

def verify_rearm(city_rows,roof_rows,edges,weapon_rows,host):
    rows=parsed(roof_rows,COLUMNS,SOURCE)
    weapon=weapon_values(weapon_rows)
    if len(rows)!=len(weapon) or len(rows)>1400:
        raise YellowRearmError("NRO roof/weapon logs not fully paired")
    for a,b in zip(rows,weapon):
        if a["seq"]!=b["seq"] or a["frame"]!=b["frame"]:
            raise YellowRearmError("NRO weapon frame/sequence disagreement")
    # Exactly six scene edges, and first YELLOW opens before the third entry.
    if len(edges)!=6:
        raise YellowRearmError("missing or extra guest scene edge")
    from tools.verify_rooftop_reentry_trace import EDGE_COLUMNS,EDGE_SOURCE
    parsed_edges=parsed(edges,EDGE_COLUMNS,EDGE_SOURCE)
    entry_frame=parsed_edges[5]["frame"]
    start=next((i for i,r in enumerate(rows) if r["frame"]==entry_frame+1),None)
    if start is None:
        raise YellowRearmError("third-visit ROOFTOP sample missing")
    third_fire=[i for i,r in enumerate(rows) if
       r["frame"]>entry_frame and
       (r["input_bits"]&8) and (i==0 or not(rows[i-1]["input_bits"]&8))]
    if len(third_fire)!=1:
        raise YellowRearmError("expected exactly one authentic third-visit FIRE edge")
    fire=third_fire[0]
    if fire-start<100 or fire+40>=len(rows):
        raise YellowRearmError("too little authentic closed pre-fire or open post-fire record")
    # Stage158 verifies the entire RED RED YELLOW -> map -> closed third
    # visit prefix and the read-only host reset witness, not just flags.
    try:
        previous=verify_yellow_reset(city_rows,roof_rows[:fire],
                  edges,weapon_rows[:fire],host)
    except ValueError as exc:
        raise YellowRearmError("prior fully verified Stage158 prefix rejected: "+str(exc)) from exc
    if previous["bridge_solid_cells_after_reentry"]!=0 or previous["selected_weapon_after_reentry"]!=0:
        raise YellowRearmError("Stage158 did not re-arm a closed YELLOW bridge")
    # Selected YELLOW must be maintained continuously with no CHANGE.
    for k in range(start,len(rows)):
        w=weapon[k]
        if w["weapon_before"]!=0 or w["weapon_after"]!=0 or w["red_unlocked"]!=1 or w["change_pressed"]!=0:
            raise YellowRearmError("third-visit weapon selection changed")
    # The two original Stage158 NeXium captures prove that a real
    # reentry starts at x=40 px, with no horizontal input for 130+ frames.
    # Stage160 must prove an actual *new* RIGHT+DASH approach after that
    # closed checkpoint; a fabricated pre-positioned x640 hero cannot pass.
    if not 35000<=rows[start]["x_milli"]<=50000:
        raise YellowRearmError("third ROOFTOP visit did not begin at genuine x40 spawn")
    if any((r["input_bits"]&2) for r in rows[start:start+100]):
        raise YellowRearmError("third visit moved before required closed-bridge checkpoint")
    motion=rows[start:fire]
    if not motion or rows[fire]["x_milli"]-rows[start]["x_milli"]<550000:
        raise YellowRearmError("third visit has no real long-distance switch approach")
    right=sum(1 for r in motion if r["input_bits"]&2)
    dash_right=sum(1 for r in motion if (r["input_bits"]&18)==18)
    if right<100 or dash_right<80:
        raise YellowRearmError("no authentic sustained DASH+RIGHT approach after third entry")
    # ROOFTOP runs the prototype physics profile: a normal RIGHT tick is
    # capped at 1.9px and DASH+RIGHT at 3.1px. Its guest CSV contains the
    # post-qs_step hero coordinate and the input consumed in that same tick.
    # Therefore distance must be earned *on right-input ticks*, rather than
    # from a smooth forged coordinate drift while the hero is idle.
    start_x=rows[start]["x_milli"]
    right_distance=0
    for k in range(start+1,fire+1):
        tick=rows[k]
        dx=tick["x_milli"]-rows[k-1]["x_milli"]
        bits=tick["input_bits"]
        if k<start+100 and abs(tick["x_milli"]-start_x)>1000:
            raise YellowRearmError("third-visit spawn drifted during the closed idle checkpoint")
        limit=3500 if bits&16 else 2300
        if abs(dx)>limit:
            raise YellowRearmError("third-visit hero exceeded native horizontal speed")
        if abs(dx)>1000 and ((dx>0 and (bits&3)!=2) or
                             (dx<0 and (bits&3)!=1)):
            raise YellowRearmError("third-visit horizontal motion disagrees with controller")
        if dx>0 and (bits&3)==2:
            right_distance+=dx
    if right_distance<550000:
        raise YellowRearmError("third-visit approach distance not earned by RIGHT input")
    if rows[fire]["bridge_before"]!=0 or rows[fire]["bridge_after"]!=0 or rows[fire]["bridge_cells"]!=0:
        raise YellowRearmError("bridge not closed on third FIRE")
    if rows[fire]["shot_active_before"] or rows[fire]["shot_active_after"]!=1:
        raise YellowRearmError("third FIRE did not actually spawn a fresh projectile")
    if not (rows[fire]["input_bits"]&8):
        raise YellowRearmError("missing real controller FIRE on fresh projectile")
    first_projectile=[i for i in range(fire+1,len(rows)) if rows[i]["shot_active_before"]==1]
    if len(first_projectile)<5 or first_projectile[0]!=fire+1:
        raise YellowRearmError("insufficient uninterrupted second YELLOW projectile")
    opened=[i for i in range(fire+1,len(rows)) if rows[i]["bridge_before"]==0 and
            rows[i]["bridge_after"]==1 and rows[i]["bridge_cells"]==6]
    if len(opened)!=1:
        raise YellowRearmError("expected exactly one authentic second bridge activation")
    hit=opened[0]
    if hit<fire+5 or hit+30>=len(rows):
        raise YellowRearmError("second hit too early or insufficient stable open frames")
    flight=[i for i in first_projectile if i<=hit]
    if flight!=list(range(fire+1,hit+1)):
        raise YellowRearmError("second YELLOW projectile flight skipped NRO frames")
    # shoot_as() creates a right-facing shot at hero.x + 12px and
    # update_shots() advances it by 5.5px on that SAME FIRE tick.
    # The first next-tick shot sample must therefore match the actual
    # hero location immediately before FIRE, not an arbitrary X value.
    # Millipixel logs truncate floats; allow a 0.05px precision margin.
    expected_birth_x=rows[fire-1]["x_milli"]+17500
    if abs(rows[fire+1]["shot_x_before_milli"]-expected_birth_x)>50:
        raise YellowRearmError("second yellow projectile birth not tied to real hero position")
    for i in flight:
        p=rows[i]
        # shoot() fixes YELLOW horizontal velocity at +5.5 px/tick.
        # A smooth, slower forged path must not be mistaken for the
        # unchanged physical projectile in the Nintendo Switch NRO.
        if (p["shot_red_before"]!=0 or p["shot_vx_before_milli"]!=5500 or
            not 0<=p["shot_x_before_milli"]<=1400000):
            raise YellowRearmError("wrong color/speed/coordinate of third-visit projectile")
        if i<hit and p["shot_active_after"]!=1:
            raise YellowRearmError("yellow projectile vanished before its genuine impact")
        if i>fire+1:
            previous_shot=rows[i-1]
            if abs(p["shot_x_before_milli"]-
                   previous_shot["shot_x_before_milli"]-
                   previous_shot["shot_vx_before_milli"])>2500:
                raise YellowRearmError("second yellow projectile teleported")
    impact=rows[hit]
    # The actual Stage158 NeXium traces show the projectile is consumed
    # on physical switch contact. A fake open flag with a surviving shot
    # must not qualify as a second legitimate activation.
    if impact["shot_active_before"]!=1 or impact["shot_active_after"]!=0:
        raise YellowRearmError("second YELLOW shot not consumed on switch contact")
    sx=impact["shot_x_before_milli"]+impact["shot_vx_before_milli"]
    if not (sx<TARGET_X_MILLI+TARGET_W_MILLI and
            sx+SHOT_WIDTH_MILLI>TARGET_X_MILLI):
        raise YellowRearmError("no second real YELLOW overlap with switch")
    for k in range(fire,hit):
        if rows[k]["bridge_before"] or rows[k]["bridge_after"] or rows[k]["bridge_cells"]:
            raise YellowRearmError("bridge opened before second projectile overlap")
        if k>fire:
            # An identical YELLOW shot cannot pass through the horizontal
            # switch hitbox without first activating it, then inexplicably
            # open the bridge only on a later contact frame.
            x=rows[k]["shot_x_before_milli"]+rows[k]["shot_vx_before_milli"]
            if (x<TARGET_X_MILLI+TARGET_W_MILLI and
                x+SHOT_WIDTH_MILLI>TARGET_X_MILLI):
                raise YellowRearmError("yellow projectile crossed switch before recorded first impact")
    for p in rows[hit+1:]:
        if p["bridge_before"]!=1 or p["bridge_after"]!=1 or p["bridge_cells"]!=6:
            raise YellowRearmError("reopened bridge not physically stable")
        if p["shot_red_before"]!=0 or p["shot_active_before"]!=0 or p["shot_active_after"]!=0:
            raise YellowRearmError("unexpected projectile after second YELLOW impact")
    # The Stage158 prefix validated its own rows; post-FIRE samples must
    # independently be valid NRO-stage observations. A forged stage/mode,
    # impossible controller bits or instantaneous player teleport cannot
    # substitute for a real third visit followed by real movement.
    for k in range(start,len(rows)):
        p=rows[k]
        if p["mode"]!=1 or p["stage"]!=2 or p["input_bits"]&~127:
            raise YellowRearmError("third-visit sample not genuine ROOFTOP controller telemetry")
        if (not 0<=p["x_milli"]<=1600000 or
            not -150000<=p["y_milli"]<=250000 or
            p["bridge_before"] not in (0,1) or
            p["bridge_after"] not in (0,1) or
            p["bridge_cells"]!=6*p["bridge_after"]):
            raise YellowRearmError("invalid third-visit terrain/hero coordinates")
        if p["shot_active_before"] not in (0,1) or p["shot_red_before"] not in (0,1):
            raise YellowRearmError("invalid third-visit projectile state")
        if (not p["shot_active_before"] and
            (p["shot_x_before_milli"]!=-1 or
             p["shot_vx_before_milli"]!=0 or p["shot_red_before"])):
            raise YellowRearmError("fabricated inactive projectile coordinates")
        if k>start:
            prior=rows[k-1]
            if (p["frame"]!=prior["frame"]+1 or p["seq"]!=prior["seq"]+1 or
                p["bridge_before"]!=prior["bridge_after"] or
                abs(p["x_milli"]-prior["x_milli"])>16000):
                raise YellowRearmError("third-visit timeline, bridge state or player movement discontinuity")
    def event(name,fmt):
        vals=[l for l in host.splitlines() if l.startswith(name+" ")]
        if len(vals)!=1:raise YellowRearmError("missing/duplicated host proof "+name)
        m=re.fullmatch(fmt,vals[0])
        if not m:raise YellowRearmError("bad host proof "+name)
        return tuple(map(int,m.groups()))
    approached,px=event("ROOFTOP_YELLOW_REARM_APPROACH_GUEST",
                        r"ROOFTOP_YELLOW_REARM_APPROACH_GUEST frame=(\d+) x_milli=(\d+)")
    fire_host,=event("ROOFTOP_YELLOW_REARM_FIRE_GUEST",r"ROOFTOP_YELLOW_REARM_FIRE_GUEST frame=(\d+)")
    open_host,=event("ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST",
                     r"ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST frame=(\d+)")
    stable_host,=event("ROOFTOP_YELLOW_REARM_STABLE_GUEST",
                       r"ROOFTOP_YELLOW_REARM_STABLE_GUEST frame=(\d+)")
    if (approached>=rows[fire]["frame"] or approached<entry_frame+100 or
        px<619000 or px>1400000 or
        not any(x["frame"]==approached and x["x_milli"]==px for x in rows[start:fire]) or
        fire_host!=rows[fire]["frame"] or open_host!=rows[hit]["frame"] or
        stable_host<rows[hit+30]["frame"] or stable_host>rows[-1]["frame"] or
        stable_host not in {x["frame"] for x in rows[hit+30:]}):
        raise YellowRearmError("host rearm actions not tied to real NRO shot and bridge frames")
    return {**previous,"status":"PASS","yellow_rearm_approach_frame":approached,
        "yellow_rearm_fire_frame":rows[fire]["frame"],
        "yellow_rearm_open_frame":rows[hit]["frame"],
        "yellow_rearm_open_frames":len(rows)-hit-1,
        "bridge_solid_cells_after_rearm":6}

def main(argv):
    if len(argv)!=7:
        raise SystemExit("usage: verify_rooftop_yellow_rearm.py CITY ROOF EDGES WEAPON HOST OUT.json")
    report=verify_rearm(read_city(argv[1]),read_trace(argv[2]),
                        read_edges(argv[3]),read_weapon(argv[4]),
                        pathlib.Path(argv[5]).read_text(encoding="utf-8"))
    pathlib.Path(argv[6]).write_text(json.dumps(report,sort_keys=True)+"\n",encoding="utf-8")
    print("NEXIUM_REAL_YELLOW_REARM_PASS",json.dumps(report,sort_keys=True))
if __name__=="__main__":main(sys.argv)
