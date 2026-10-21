#!/usr/bin/env python3
"""Validate input-to-world witnesses generated inside the running guest NRO."""
import csv
import json
import pathlib
import sys

COLUMNS=["seq","frame","event","mode","stage","controller","input_bits",
         "x_milli","y_milli","shots","detail","source"]
EVENTS=["ENTER","JUMP_RISE","MOVE_RIGHT","REVERSE_LEFT",
        "FIRE_INPUT","SHOT_ACTIVE","RETURN_MAP"]
MASKS=[48,4,2,1,8,0,64]

def load(path):
    with open(path,encoding="utf-8",newline="") as f:
        return list(csv.DictReader(f))

def check(live_file,runtime_file,motion_file):
    with open(live_file,encoding="utf-8",newline="") as f:
        reader=csv.DictReader(f)
        if reader.fieldnames != COLUMNS:
            raise ValueError("unexpected guest live-controller CSV schema")
        live=list(reader)
    if len(live)!=7 or [x["event"] for x in live]!=EVENTS:
        raise ValueError("missing/out-of-order real controller events")
    numfields=["seq","frame","mode","stage","controller","input_bits",
               "x_milli","y_milli","shots","detail"]
    v=[]
    for i,row in enumerate(live):
        d={name:int(row[name]) for name in numfields}
        if (row["source"]!="sdl_controller_observed" or d["seq"]!=i
            or d["controller"]!=1 or d["frame"]<1 or
            d["frame"]>6000 or d["shots"]<0 or d["shots"]>8 or
            not 0<=d["x_milli"]<=1280000 or
            not -300000<=d["y_milli"]<=500000):
            raise ValueError("invalid live guest data/provenance")
        if (d["input_bits"] & MASKS[i])!=MASKS[i]:
            raise ValueError("observed event lacks expected live controller button")
        if i<6 and (d["mode"]!=1 or d["stage"]!=0):
            raise ValueError("live movement event outside city stage")
        v.append(d)
    frames=[q["frame"] for q in v]
    if frames!=sorted(frames) or len(set(frames))!=7:
        raise ValueError("duplicate or nonmonotonic input frames")
    if not 1 <= frames[5]-frames[4] <= 4:
        raise ValueError("shot activation not causally close to Y edge")
    if (v[5]["detail"]!=v[5]["shots"] or v[5]["shots"]<1):
        raise ValueError("new shot count witness invalid")
    if (v[0]["detail"]!=1 or v[0]["x_milli"]!=40000 or
        v[0]["y_milli"]!=188000 or
        v[1]["y_milli"]>=v[0]["y_milli"]-1000 or
        v[2]["x_milli"]<=v[0]["x_milli"] or
        v[3]["detail"]-v[3]["x_milli"]<8000 or
        v[4]["detail"]!=1 or v[5]["shots"]<1 or
        v[6]["mode"]!=0 or v[6]["detail"]!=0):
        raise ValueError("actual world movement, shot or menu return not proven")
    runtime=load(runtime_file)
    transitions=[row for row in runtime if int(row["event"])==7]
    if (len(transitions)<2 or
        [int(x["frames"]) for x in transitions[:2]]!=[frames[0],frames[-1]] or
        [int(x["mode"]) for x in transitions[:2]]!=[1,0] or
        any(int(x["controllers"])!=1 for x in transitions[:2])):
        raise ValueError("runtime stage changes contradict live controller file")
    motion=load(motion_file)
    if not motion:
        raise ValueError("numeric motion evidence is missing")
    for idx in (1,2,3,4):
        frame,mask=frames[idx],MASKS[idx]
        candidates=[row for row in motion if
          abs(int(row["frame"])-frame)<=5 and
          int(row["input_bits"])&mask==mask]
        if not candidates:
            raise ValueError(f"numeric motion trace cannot corroborate {EVENTS[idx]}")
    if not any(abs(int(row["frame"])-frames[5])<=5 and
               int(row["active_shots"])>=v[5]["shots"] for row in motion):
        raise ValueError("independent numeric trace cannot corroborate new shot")
    return {"milestones":len(v),"entry_frame":frames[0],
            "jump_frame":frames[1],"right_frame":frames[2],
            "left_reversal_frame":frames[3],"fire_frame":frames[4],
            "shot_frame":frames[5],"exit_frame":frames[6],
            "left_reversal_px":(v[3]["detail"]-v[3]["x_milli"])/1000,
            "source":"observed SDL controller + qs_step, not scripted route"}

def main(args):
    if len(args)!=5:
        raise SystemExit("usage: verify_live_control.py <live.csv> <runtime.csv> <motion.csv> <summary.txt>")
    summary=pathlib.Path(args[4])
    try:
        data=check(*args[1:4])
    except (OSError,ValueError,KeyError,TypeError) as exc:
        summary.write_text("FAIL: "+str(exc)+"\n")
        raise SystemExit("FAIL: "+str(exc))
    msg="LIVE_CONTROLLER_GUEST_PASS "+json.dumps(data,sort_keys=True)+"\n"
    summary.write_text(msg)
    print(msg,end="")

if __name__=="__main__":
    main(sys.argv)
