#!/usr/bin/env python3
"""Independent NRO SDL pixel readback audit, joined to real pad numeric frames.

No screenshots or copyrighted assets are used. Fails closed on lost,
misaligned or substituted render samples. Does not prove collision physics.
"""
import csv
import json
import pathlib
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_city_up_guest import read_csv as read_numeric, verify as verify_numeric

COLUMNS=("seq","frame","age","shot_x","shot_y","gold_argb","cyan_argb",
         "readback_ok","scale_x_milli","scale_y_milli","source")
SOURCE="live_city_up_sdl_pixel_readback"
GOLD=0xffffe16c
CYAN=0xff70a7dd

class RasterError(ValueError):
    pass

def read_raster(path):
    with pathlib.Path(path).open(encoding="utf-8",newline="") as stream:
        return list(csv.DictReader(stream))

def verify_pair(numeric_rows,raster_rows):
    numerical=verify_numeric(numeric_rows)
    if not 5 <= len(raster_rows) <= 48:
        raise RasterError("too few or too many real SDL pixel samples")
    frames={int(row["frame"]):row for row in numeric_rows}
    ages=[]
    last_frame=0
    last_age=-1
    for seq,row in enumerate(raster_rows):
        if not isinstance(row,dict) or set(row)!=set(COLUMNS) or row["source"]!=SOURCE:
            raise RasterError("untrusted renderer sample or schema")
        try:
            numbers={name:int(row[name]) for name in COLUMNS if name!="source"}
        except (TypeError,ValueError,KeyError) as exc:
            raise RasterError("non-numeric SDL readback sample") from exc
        n=numbers
        if (n["seq"]!=seq or n["frame"]<=last_frame or
            n["age"]<=last_age or not 0<=n["age"]<20 or
            n["readback_ok"]!=1 or
            not 500<=n["scale_x_milli"]<=16000 or
            not 500<=n["scale_y_milli"]<=16000):
            raise RasterError("invalid age, sequence, scale or failed real pixel readback")
        guest=frames.get(n["frame"])
        if (guest is None or guest["source"]!="live_city_up_pad_qs_step" or
            int(guest["visible"])!=1 or int(guest["age"])!=n["age"] or
            int(guest["x"])!=n["shot_x"] or int(guest["y"])!=n["shot_y"]):
            raise RasterError("SDL pixel result not tied to matching guest frame/position")
        if n["gold_argb"]!=GOLD or n["cyan_argb"]!=CYAN:
            raise RasterError("actual SDL framebuffer lacked gold/cyan shot pixels")
        ages.append(n["age"])
        last_frame=n["frame"]
        last_age=n["age"]
    if ages[0]>5 or ages[-1]<12 or len(set(ages))<5:
        raise RasterError("renderer did not witness early and late trajectory")
    return {"status":"PASS","source":SOURCE,
            "raster_samples":len(ages),"first_age":ages[0],
            "last_age":ages[-1],"rendered_ages":ages,
            "numeric_guest_frames":numerical["frames"],
            "gold_argb":GOLD,"cyan_argb":CYAN,
            "scope":"SDL_NRO_framebuffer_pixels_not_external_display_or_collision"}

def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_city_up_raster.py GUEST_NUMERIC_CSV GUEST_RASTER_CSV SUMMARY_JSON")
    try:
        result=verify_pair(read_numeric(argv[1]),read_raster(argv[2]))
    except (OSError,ValueError,TypeError) as exc:
        raise SystemExit("FAIL: genuine CITY UP SDL raster: "+str(exc)) from exc
    pathlib.Path(argv[3]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("CITY_UP_GUEST_SDL_RASTER_PASS",json.dumps(result,sort_keys=True))

if __name__=="__main__":
    main(sys.argv)
