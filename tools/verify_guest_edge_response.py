#!/usr/bin/env python3
"""Stage 96: corroborate host virtual-button dispatch with guest qs_step input.

The host reads a guest frame before requesting a key. The guest later
writes input_bits AFTER read_input(pad)->qs_step(). Their difference is a
frame-domain *observation-window upper estimate*, not synchronized
host-to-guest milliseconds, emulation FPS, or a hardware latency claim.
"""
import csv
import json
import pathlib
import re
import sys

EVENTS=(
    ("pit","PIT_LAUNCH_GUEST",4,276000,305000),
    ("roof","ROOF_LAUNCH_GUEST",4,382000,416000),
    ("crate","CRATE_FIRE_GUEST",8,902000,946000),
)
LAUNCH=re.compile(r"^(PIT_LAUNCH_GUEST|ROOF_LAUNCH_GUEST|CRATE_FIRE_GUEST) frame=(\d+) x_milli=(\d+)$")
COLUMNS=("seq","frame","attempt","x_milli","y_milli","grounded",
         "input_bits","checkpoint","mode","source")


def verify_edge_responses(host_text, samples):
    launches={}
    for line in host_text.splitlines():
        match=LAUNCH.fullmatch(line)
        if match:
            name,frame,x=match.groups()
            if name in launches:
                raise ValueError("duplicate host action anchor")
            launches[name]=(int(frame),int(x))
    if set(launches)!={item[1] for item in EVENTS}:
        raise ValueError("missing/duplicated host action anchor")
    if len(samples)<180 or len(samples)>350:
        raise ValueError("guest obstacle timeline missing or shortened")
    rows=[]
    for i,s in enumerate(samples):
        try:
            r={key:int(s[key]) for key in COLUMNS if key!="source"}
        except (ValueError,TypeError,KeyError) as exc:
            raise ValueError("invalid guest input field") from exc
        if (r["seq"]!=i or s.get("source")!="live_controller_qs_step" or
            r["mode"]!=1 or not 0<=r["input_bits"]<128):
            raise ValueError("untrusted guest controller timeline")
        if rows and (r["frame"]<=rows[-1]["frame"] or
                     r["frame"]-rows[-1]["frame"]>4 or
                     r["attempt"]<=rows[-1]["attempt"]):
            raise ValueError("discontinuous input frame observations")
        rows.append(r)
    # Stage 122: the delayed 25ms/90ms probe starts the PIT button
    # earlier. Match the real host profile marker, then require the
    # authentic guest first B in the ORIGINAL 275..309px range.
    profile=re.findall(
        r"^INPUT_TIMING_PROFILE profile=(\d+) edge_delay_ms=(\d+) poll_ms=(\d+)$",
        host_text,re.M)
    if len(profile)>1:
        raise ValueError("duplicate host input timing profile")
    early=(len(profile)==1 and profile[0]==("2","25","90"))
    details={}
    preceding_frame=-1
    response_frame=-1
    for name,marker,bit,xlow,xhigh in EVENTS:
        if name=="pit" and early:
            xlow,xhigh=269000,276000
        start,x=launches[marker]
        if start<=preceding_frame:
            raise ValueError("host button actions reordered")
        preceding_frame=start
        matches=[(index,r) for index,r in enumerate(rows) if
                 r["frame"]==start and r["x_milli"]==x]
        if len(matches)!=1:
            raise ValueError(f"host {name} frame/position not guest-authenticated")
        before_idx,before=matches[0]
        if (not xlow<=x<=xhigh or before["input_bits"]&bit or
            (bit==4 and before["grounded"]!=1)):
            raise ValueError(f"{name} pressed before legitimate grounded release")
        edge=None
        for i in range(before_idx+1,len(rows)):
            current=rows[i]
            if current["frame"]-start>24:
                break
            if current["input_bits"]&bit:
                previous=rows[i-1]
                if previous["input_bits"]&bit:
                    raise ValueError("controller edge not rising")
                edge=current
                break
        if edge is None:
            raise ValueError(f"{name} button never observed inside NRO")
        if edge["frame"]<=response_frame:
            raise ValueError("NRO edge witnesses out of order")
        response_frame=edge["frame"]
        if bit==4 and (edge["y_milli"]>=before["y_milli"]-1000 or
                       edge["grounded"]!=0):
            raise ValueError(f"{name} B edge did not change the player world")
        if name=="crate" and (edge["x_milli"]>=970000 or
                               edge["grounded"]!=1):
            raise ValueError("Y input not verified near standing crate")
        delay=edge["frame"]-start
        if not 1<=delay<=24:
            raise ValueError(f"{name} input recognition outside guest frame window")
        details[name]={
          "last_host_observed_guest_frame":start,
          "nro_first_button_frame":edge["frame"],
          "guest_frame_window":delay,
          "nro_stage_tick":edge["attempt"],
          "nro_input_bits":edge["input_bits"],
        }
    return {
      "events":details,
      "worst_guest_frame_window":max(v["guest_frame_window"] for v in details.values()),
      "source":"host pre-send guest-frame anchor + NRO qs_step input_bits",
      "interpretation":"frame-domain observation window; NOT synchronized real-time latency"
    }


def read_obstacles(path):
    with open(path,encoding="utf-8",newline="") as stream:
        reader=csv.DictReader(stream)
        if tuple(reader.fieldnames or ())!=COLUMNS:
            raise ValueError("incorrect NRO obstacle CSV schema")
        return list(reader)


def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_guest_edge_response.py <input-injection.txt> <guest-obstacle.csv> <summary.txt>")
    dest=pathlib.Path(argv[3])
    try:
        result=verify_edge_responses(
            pathlib.Path(argv[1]).read_text(encoding="utf-8"),
            read_obstacles(argv[2]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        dest.write_text("FAIL: guest input edge response: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: guest input edge response: "+str(exc))
    output="NEXIUM_GUEST_INPUT_EDGES_PASS "+json.dumps(result,sort_keys=True)+"\n"
    dest.write_text(output,encoding="utf-8")
    print(output,end="")


if __name__=="__main__":
    main(sys.argv)
