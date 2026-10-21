#!/usr/bin/env python3
"""Fail-closed ROOFTOP no-FIRE body-touch proof from NRO-written frames.

This only checks guest evidence. The separate NeXium runner must attest
the input driver, pinned emulator digest and exact NRO binary identity.
"""
from tools.verify_rooftop_guest_trace import COLUMNS, SOURCE, RooftopTraceError

class TouchError(RooftopTraceError):
    pass

def verify_touch(rows):
    if not 100 <= len(rows) <= 1400:
        raise TouchError("missing full ROOFTOP no-fire guest trajectory")
    last=None
    touch_frames=[]
    max_x=0
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(COLUMNS):
            raise TouchError("malformed guest row")
        if row["source"]!=SOURCE:
            raise TouchError("untrusted trace source")
        try:
            p={name:int(row[name]) for name in COLUMNS if name!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise TouchError("invalid numeric guest field") from exc
        if p["seq"]!=i or p["frame"]<=0 or p["stage"]!=2 or p["mode"]!=1:
            raise TouchError("wrong sequence, guest scene or mode")
        if last and p["frame"]!=last["frame"]+1:
            raise TouchError("missing/duplicated real gameplay frame")
        if any(p[k] not in (0,1) for k in
               ("grounded","bridge_before","bridge_after",
                "shot_red_before","shot_active_before")):
            raise TouchError("corrupt game-state boolean")
        if p["bridge_before"] or p["bridge_after"] or p["bridge_cells"]:
            raise TouchError("bridge activated by unarmed hero contact")
        if (p["input_bits"]<0 or p["input_bits"]>255 or
            p["input_bits"] & 8):
            raise TouchError("a FIRE input occurred during no-fire test")
        if (p["shot_active_before"] or p["shot_active_after"] or
            p["shot_x_before_milli"]!=-1 or
            p["shot_vx_before_milli"] or p["shot_red_before"]):
            raise TouchError("unexpected live projectile during no-fire touch")
        x=p["x_milli"]; y=p["y_milli"]
        if not 0<=x<=1600000 or not -150000<=y<=250000:
            raise TouchError("invalid player world coordinates")
        if last and abs(x-last["x_milli"])>7000:
            raise TouchError("hero warped or restarted during no-fire test")
        if i==0 and not 25000<=x<=60000:
            raise TouchError("game did not start at ordinary stage spawn")
        max_x=max(max_x,x)
        # Actual AABB contact with X=704..716, Y=192..204.
        if (p["grounded"] and 693000<=x<716000 and
            y<204000 and y+20000>192000):
            touch_frames.append(p["frame"])
        last=p
    if max_x<698000 or len(touch_frames)<14:
        raise TouchError("no sustained actual grounded collision with switch")
    if touch_frames[-1]-touch_frames[0]<13:
        raise TouchError("insufficient continuous time touching switch")
    return dict(status="PASS",frames=len(rows),touch_frame=touch_frames[0],
                contact_samples=len(touch_frames),max_hero_x_milli=max_x,
                bridge_solid_cells=0,projectile_samples=0)
