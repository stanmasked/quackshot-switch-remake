#!/usr/bin/env python3
"""Stage276: authenticate real NRO left-facing yellow/red horizontal shots."""
import json
import pathlib
import sys

from verify_city_horizontal_guest import (
    GOLD, NUMERIC, PIXELS, NUMERIC_SOURCE, PIXEL_SOURCE, HorizontalError, load
)

RED_PIXEL = 0xffe64a4e
RED_FIELDS = ("seq","frame","mode","stage","x_milli","y_milli","grounded",
              "input_bits","red_before","red_after","weapon","pickup_before",
              "pickup_after","source")
RED_SOURCE = "live_city_red_pad_qs_step"
RED_VARIANTS = {"left-yellow": (0,-1), "left-red": (1,-1)}

def variant_spec(variant):
    if variant not in RED_VARIANTS:
        raise HorizontalError("unknown variant")
    return RED_VARIANTS[variant]

def verify_numeric_rows(rows, variant):
    expected_red, direction = variant_spec(variant)
    # CITY's red pickup is on the raised platform; a red shot therefore
    # launches at y=149, while the yellow control fires from the floor y=197.
    expected_y = 149000 if expected_red else 197000
    if not 90 <= len(rows) <= 320:
        raise HorizontalError("not enough consecutive real guest frames")
    data=[]
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(NUMERIC) or row["source"]!=NUMERIC_SOURCE:
            raise HorizontalError("invalid source or numeric schema")
        try:
            q={key:int(row[key]) for key in NUMERIC if key!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise HorizontalError("non-numeric guest field") from exc
        if (q["seq"]!=i or q["mode"]!=1 or q["stage"]!=0 or
            q["frame"]<1 or q["attempt"]<1 or
            any(q[key] not in (0,1) for key in
                ("fire","up","grounded","pending","active","attached","red")) or q["up"]):
            raise HorizontalError("wrong scene, frame or input state")
        if data and (q["frame"]!=data[-1]["frame"]+1 or
                     q["attempt"]!=data[-1]["attempt"]+1):
            raise HorizontalError("guest frames are not consecutive")
        if q["active"]:
            if (q["red"]!=expected_red or q["attached"] or
                q["vx_milli"]!=5500*direction or q["y_milli"]!=expected_y or
                not 0<=q["x_milli"]<=1280000):
                raise HorizontalError("wrong projectile color, direction or height")
        elif (q["x_milli"]!=-1 or q["y_milli"]!=-1 or q["vx_milli"]!=0 or
              q["attached"] or q["red"]):
            raise HorizontalError("inactive projectile retains live fields")
        data.append(q)
    edges=[i for i,p in enumerate(data) if p["fire"] and
           (i==0 or not data[i-1]["fire"])]
    if len(edges)!=1 or edges[0]<4 or edges[0]>len(data)-28:
        raise HorizontalError("missing or duplicated physical FIRE rising edge")
    fire=edges[0]
    if (not data[fire]["grounded"] or not data[fire]["pending"] or
        data[fire]["active"] or data[fire+1]["pending"] or
        not data[fire+1]["active"] or data[fire+1]["red"]!=expected_red):
        raise HorizontalError("actual one-tick delayed shot not proven")
    if any(p["active"] or p["pending"] for p in data[:fire]):
        raise HorizontalError("projectile existed before physical FIRE")
    first=fire+1
    start_x=data[first]["x_milli"]
    for j in range(15):
        p=data[first+j]
        expected_x=start_x+5500*direction*j
        if (not p["active"] or abs(p["x_milli"]-expected_x)>1 or
            p["y_milli"]!=expected_y or p["vx_milli"]!=5500*direction or
            p["red"]!=expected_red):
            raise HorizontalError("leftward world trajectory is not continuous")
    if any(p["fire"] and p["pending"] for p in data[fire+1:]):
        raise HorizontalError("unexpected second queued shot")
    return {"status":"PASS","variant":variant,"source":NUMERIC_SOURCE,
            "frames":len(data),"fire_frame":data[fire]["frame"],
            "shot_frame":data[first]["frame"],"observed_positions":15,
            "start_x_milli":start_x,"y_milli":expected_y,
            "velocity_milli_per_tick":5500*direction,
            "red":expected_red,"scope":"original_free_reference_city_NRO_pad_game_core"}

def verify_pixel_rows(numeric_rows, pixel_rows, variant):
    expected_red, direction=variant_spec(variant)
    numeric=verify_numeric_rows(numeric_rows,variant)
    if not 3<=len(pixel_rows)<=18:
        raise HorizontalError("insufficient actual SDL framebuffer readbacks")
    by_frame={int(row["frame"]):row for row in numeric_rows}
    first=numeric["shot_frame"]
    expected_pixel=RED_PIXEL if expected_red else GOLD
    ages=[]; last_frame=0
    for i,row in enumerate(pixel_rows):
        if not isinstance(row,dict) or set(row)!=set(PIXELS) or row["source"]!=PIXEL_SOURCE:
            raise HorizontalError("forged raster source or schema")
        try:
            p={key:int(row[key]) for key in PIXELS if key!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise HorizontalError("non-numeric raster field") from exc
        age=p["frame"]-first
        guest=by_frame.get(p["frame"])
        if (p["seq"]!=i or p["frame"]<=last_frame or not 0<=age<=26 or
            p["readback_ok"]!=1 or p["baseline_ok"]!=1 or p["restored_ok"]!=1):
            raise HorizontalError("invalid raster timing/readback/restoration")
        if (not guest or guest["source"]!=NUMERIC_SOURCE or
            int(guest["active"])!=1 or int(guest["attached"])!=0 or
            int(guest["red"])!=expected_red or
            int(guest["vx_milli"])!=5500*direction or
            p["x_milli"]!=int(guest["x_milli"]) or
            p["y_milli"]!=int(guest["y_milli"])):
            raise HorizontalError("SDL pixel is not aligned to real target shot")
        if p["render_argb"]!=expected_pixel or p["bg_argb"]==expected_pixel:
            raise HorizontalError("same-frame SDL negative control did not isolate expected shot color")
        ages.append(age); last_frame=p["frame"]
    if ages[0]>6 or ages[-1]<14:
        raise HorizontalError("raster evidence lacks early/late horizontal flight")
    return {"status":"PASS","variant":variant,"source":PIXEL_SOURCE,
            "samples":len(ages),"ages":ages,"negative_controls":len(ages),
            "restored_frames":len(ages),"shot_frame":first,
            "expected_pixel_argb":expected_pixel,
            "scope":"actual_NRO_SDL_pixels_not_original_ROM_or_collision"}

def verify_red_rows(rows, numeric, variant):
    expected_red, _=variant_spec(variant)
    if not 60<=len(rows)<=500:
        raise HorizontalError("missing real CITY pickup/input witness")
    data=[]
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(RED_FIELDS) or row["source"]!=RED_SOURCE:
            raise HorizontalError("invalid red-pickup source or schema")
        try:
            q={key:int(row[key]) for key in RED_FIELDS if key!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise HorizontalError("non-numeric red-pickup field") from exc
        if (q["seq"]!=i or q["mode"]!=1 or q["stage"]!=0 or q["frame"]<1 or
            q["grounded"] not in (0,1) or q["red_before"] not in (0,1) or
            q["red_after"] not in (0,1) or q["weapon"] not in (0,1) or
            q["pickup_before"] not in (0,1) or q["pickup_after"] not in (0,1) or
            q["input_bits"]<0):
            raise HorizontalError("invalid red-pickup state")
        if data and (q["frame"]!=data[-1]["frame"]+1):
            raise HorizontalError("red-pickup frames are not consecutive")
        data.append(q)
    fire_frame=numeric["fire_frame"]
    fire_rows=[q for q in data if q["frame"]==fire_frame]
    if len(fire_rows)!=1 or not (fire_rows[0]["input_bits"]&8):
        raise HorizontalError("physical FIRE not present in real input witness")
    fire=fire_rows[0]
    if expected_red:
        transitions=[q for q in data if q["red_before"]==0 and q["red_after"]==1 and
                     q["pickup_before"]==1 and q["pickup_after"]==0 and q["weapon"]==1]
        if len(transitions)!=1:
            raise HorizontalError("red weapon was not unlocked by the real pickup")
        unlock=transitions[0]
        if unlock["frame"]>=fire_frame:
            raise HorizontalError("red shot predates actual pickup")
        if sum(1 for q in data if q["frame"]<unlock["frame"] and
               (q["input_bits"]&6)==6)<5:
            raise HorizontalError("real right+jump approach to pickup not witnessed")
        if not any(q["frame"]>unlock["frame"] and q["frame"]<fire_frame and
                   (q["input_bits"]&1) for q in data):
            raise HorizontalError("left-facing controller input not witnessed after pickup")
        if fire["red_after"]!=1 or fire["weapon"]!=1:
            raise HorizontalError("red weapon not active at actual FIRE frame")
        return {"status":"PASS","pickup_frame":unlock["frame"],
                "fire_frame":fire_frame,"red_unlocked":1,"weapon":1,
                "approach_frames":sum(1 for q in data if q["frame"]<unlock["frame"] and
                                      (q["input_bits"]&6)==6),
                "left_input_seen":True,"source":RED_SOURCE}
    if any(q["red_after"] for q in data) or any(q["weapon"] for q in data):
        raise HorizontalError("yellow scenario unexpectedly unlocked red weapon")
    if fire["red_after"]!=0 or fire["weapon"]!=0 or fire["pickup_after"]!=1:
        raise HorizontalError("yellow shot did not retain the locked-red control state")
    if not any(q["frame"]<fire_frame and (q["input_bits"]&2) for q in data):
        raise HorizontalError("rightward approach before left shot not witnessed")
    if not any(q["frame"]<fire_frame and (q["input_bits"]&1) for q in data):
        raise HorizontalError("left-facing controller input not witnessed")
    return {"status":"PASS","fire_frame":fire_frame,"red_unlocked":0,
            "weapon":0,"pickup_remains_active":True,"source":RED_SOURCE}

def verify_variant_rows(numeric_rows, pixel_rows, red_rows, variant):
    numeric=verify_numeric_rows(numeric_rows,variant)
    pixels=verify_pixel_rows(numeric_rows,pixel_rows,variant)
    red=verify_red_rows(red_rows,numeric,variant)
    return {"numeric":numeric,"raster":pixels,"red_acquisition":red,
            "status":"PASS","variant":variant}

def verify_variant_files(numeric_path,raster_path,red_path,variant):
    return verify_variant_rows(load(numeric_path,NUMERIC),load(raster_path,PIXELS),
                               load(red_path,RED_FIELDS),variant)

def main(argv):
    if len(argv)!=6:
        raise SystemExit("usage: verify_city_horizontal_variant.py NUMERIC_CSV RASTER_CSV RED_CSV VARIANT SUMMARY_JSON")
    try:
        result=verify_variant_files(argv[1],argv[2],argv[3],argv[4])
    except (OSError,ValueError,TypeError,KeyError) as exc:
        raise SystemExit("FAIL: CITY horizontal variant: "+str(exc)) from exc
    pathlib.Path(argv[5]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("CITY_HORIZONTAL_VARIANT_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
