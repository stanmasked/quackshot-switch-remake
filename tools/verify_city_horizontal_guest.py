#!/usr/bin/env python3
"""Stage275: fail-closed real NRO horizontal yellow/right shot proof.

The horizontal physics numbers below are CURRENT ORIGINAL-FREE PROTOTYPE
parameters, not exact recovered Mega Drive physics or original sprite data.
"""
import csv
import json
import pathlib
import sys

NUMERIC=("seq","frame","attempt","mode","stage","fire","up","grounded",
         "pending","active","attached","red","x_milli","y_milli",
         "vx_milli","source")
PIXELS=("seq","frame","x_milli","y_milli","render_argb","bg_argb",
        "readback_ok","baseline_ok","restored_ok","source")
NUMERIC_SOURCE="live_city_horizontal_pad_qs_step"
PIXEL_SOURCE="live_city_horizontal_sdl_readback"
GOLD=0xffffe16c

class HorizontalError(ValueError):
    pass

def load(path,expected):
    with pathlib.Path(path).open(encoding="utf-8",newline="") as f:
        rdr=csv.DictReader(f)
        if tuple(rdr.fieldnames or ())!=expected:
            raise HorizontalError("CSV schema changed or malformed")
        return list(rdr)

def verify_numeric(rows):
    if not 90<=len(rows)<=320:
        raise HorizontalError("not enough consecutive real guest frames")
    data=[]
    for i,row in enumerate(rows):
        if not isinstance(row,dict) or set(row)!=set(NUMERIC) or row["source"]!=NUMERIC_SOURCE:
            raise HorizontalError("invalid source or numeric schema")
        try:
            q={key:int(row[key]) for key in NUMERIC if key!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise HorizontalError("non-numeric field") from exc
        if (q["seq"]!=i or q["mode"]!=1 or q["stage"]!=0 or
            q["frame"]<1 or q["attempt"]<1 or
            any(q[key] not in (0,1) for key in
                ("fire","up","grounded","pending","active","attached","red")) or
            q["up"] or q["red"] or q["attached"]):
            raise HorizontalError("wrong game scene, weapon or actor state")
        if data and (q["frame"]!=data[-1]["frame"]+1 or
                     q["attempt"]!=data[-1]["attempt"]+1):
            raise HorizontalError("not consecutive game simulation ticks")
        if q["active"]==0:
            if q["x_milli"]!=-1 or q["y_milli"]!=-1 or q["vx_milli"]!=0:
                raise HorizontalError("invisible shot carries coordinates")
        elif q["vx_milli"]!=5500 or q["y_milli"]!=197000:
            raise HorizontalError("unexpected prototype direction/height")
        data.append(q)
    edges=[i for i,p in enumerate(data) if p["fire"] and (i==0 or not data[i-1]["fire"])]
    if len(edges)!=1 or edges[0]<4 or edges[0]>len(data)-28:
        raise HorizontalError("missing or duplicated physical FIRE rising edge")
    fire=edges[0]
    if (not data[fire]["grounded"] or not data[fire]["pending"] or
        data[fire]["active"] or data[fire+1]["pending"] or
        not data[fire+1]["active"]):
        raise HorizontalError("actual one-tick delayed shot not proven")
    if any(p["active"] or p["pending"] for p in data[:fire]):
        raise HorizontalError("projectile existed before real controller FIRE")
    first=fire+1
    if data[first]["x_milli"]!=57500:
        raise HorizontalError("prototype spawn world x differs from stationary start")
    for j in range(15):
        p=data[first+j]
        # Actual NRO uses single-precision float accumulation; casting
        # 73.99999f to integer milli-pixels yields 73999, not 74000.
        # Allow at most ONE milli-pixel truncation error, never a full
        # pixel or a missed/misordered simulation tick.
        if not p["active"] or abs(p["x_milli"]-(57500+5500*j))>1:
            raise HorizontalError("horizontal world trajectory not continuous")
    if any(p["fire"] and p["pending"] for p in data[fire+1:]):
        raise HorizontalError("unexpected second queued shot")
    return {"status":"PASS","source":NUMERIC_SOURCE,"frames":len(data),
            "fire_frame":data[fire]["frame"],
            "shot_frame":data[first]["frame"],
            "observed_positions":15,"start_x_milli":57500,
            "y_milli":197000,"velocity_milli_per_tick":5500,
            "scope":"original_free_reference_city_NRO_pad_game_core"}

def verify_pixels(numeric_rows,pixel_rows):
    result=verify_numeric(numeric_rows)
    if not 3<=len(pixel_rows)<=18:
        raise HorizontalError("missing sufficient actual SDL readbacks")
    by_frame={int(r["frame"]):r for r in numeric_rows}
    first=result["shot_frame"]
    ages=[]
    last_frame=0
    for index,row in enumerate(pixel_rows):
        if not isinstance(row,dict) or set(row)!=set(PIXELS) or row["source"]!=PIXEL_SOURCE:
            raise HorizontalError("forged pixel source or schema")
        try:
            p={key:int(row[key]) for key in PIXELS if key!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise HorizontalError("non-numeric framebuffer field") from exc
        age=p["frame"]-first
        if (p["seq"]!=index or p["frame"]<=last_frame or
            not 0<=age<=26 or p["readback_ok"]!=1 or
            p["baseline_ok"]!=1 or p["restored_ok"]!=1):
            raise HorizontalError("invalid real SDL sample timing/restore")
        guest=by_frame.get(p["frame"])
        if (not guest or guest["source"]!=NUMERIC_SOURCE or
            int(guest["active"])!=1 or int(guest["attached"])!=0 or
            int(guest["red"])!=0 or
            p["x_milli"]!=int(guest["x_milli"]) or
            p["y_milli"]!=int(guest["y_milli"])):
            raise HorizontalError("SDL sample not aligned to true NRO game shot")
        if p["render_argb"]!=GOLD or p["bg_argb"]==GOLD:
            raise HorizontalError("same-frame SDL negative control did not isolate shot")
        ages.append(age)
        last_frame=p["frame"]
    if ages[0]>6 or ages[-1]<14:
        raise HorizontalError("renderer evidence lacks early/late horizontal flight")
    return {"status":"PASS","source":PIXEL_SOURCE,"samples":len(ages),
            "ages":ages,"negative_controls":len(ages),
            "restored_frames":len(ages),"shot_frame":first,
            "scope":"actual_NRO_SDL_pixels_not_original_ROM_or_collision"}

def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_city_horizontal_guest.py NUMERIC_CSV RASTER_CSV SUMMARY_JSON")
    try:
        num=load(argv[1],NUMERIC)
        pix=load(argv[2],PIXELS)
        result={"numeric":verify_numeric(num),"raster":verify_pixels(num,pix)}
    except (OSError,ValueError,TypeError) as exc:
        raise SystemExit("FAIL: CITY horizontal guest: "+str(exc)) from exc
    pathlib.Path(argv[3]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("CITY_HORIZONTAL_GUEST_RASTER_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
