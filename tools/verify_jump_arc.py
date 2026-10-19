#!/usr/bin/env python3
"""Stage 98: independently prove two COMPLETE live guest jump arcs.

Physical height, airborne ascent and landing are read from qs_step() game
telemetry on the emulated Switch SD. Host Xdotool B press markers alone
cannot satisfy this verifier; simulated Stage 88 QsInput cannot either.
"""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.verify_guest_button_pulses import verify_pulses
from tools.verify_guest_edge_response import read_obstacles

LANDING={"pit": (182000, 193000, 365000, 415000),
         "roof": (123500, 125000, 415000, 475000)}


def verify_jump_arcs(host_text, samples):
    pulses=verify_pulses(host_text, samples)
    main=pulses["pulses"]["B"][:2]
    if len(main)!=2:
        raise ValueError("both live B jump pulses required")
    rows=[{k:int(s[k]) for k in ("frame", "attempt", "x_milli", "y_milli",
                                 "grounded", "input_bits", "checkpoint")}
          for s in samples]
    # Assert guest chronology independently, rather than relying solely on
    # the earlier button-edge verifier (which also checks this invariant).
    if any(b["frame"]<=a["frame"] or b["frame"]-a["frame"]>4 or
           b["attempt"]<=a["attempt"] for a,b in zip(rows,rows[1:])):
        raise ValueError("discontinuous jump guest timeline")
    result={}
    preceding_landing=-1
    for name,pulse in zip(("pit","roof"), main):
        start=pulse["pressed_frame"]
        stop=pulse["released_frame"]
        matches=[i for i,r in enumerate(rows) if r["frame"]==start]
        if len(matches)!=1:
            raise ValueError("B press not uniquely aligned to NRO frame")
        index=matches[0]
        departure=rows[index]
        if (departure["grounded"]!=0 or not departure["input_bits"]&4 or
            not 182000<=departure["y_milli"]<=187500):
            raise ValueError("button B did not initiate grounded-to-airborne jump")
        # Find the earliest grounded sample after real button release.
        landing=None
        for row in rows[index+1:]:
            if row["frame"]-start>85:
                break
            if row["frame"]>stop and row["grounded"]==1:
                landing=row
                break
        if landing is None:
            raise ValueError(f"{name} never landed after releasing B")
        ylo,yhi,xlo,xhi=LANDING[name]
        if not (ylo<=landing["y_milli"]<=yhi and
                xlo<=landing["x_milli"]<=xhi and
                landing["frame"]>preceding_landing):
            raise ValueError(f"{name} grounded landing not on expected level geometry")
        preceding_landing=landing["frame"]
        segment=[row for row in rows[index:] if start<=row["frame"]<landing["frame"]]
        if len(segment)<8 or any(r["grounded"]!=0 for r in segment):
            raise ValueError("jump arc incomplete or prematurely grounded")
        apex=min(segment, key=lambda r:r["y_milli"])
        apex_index=segment.index(apex)
        ascent=sum(b["y_milli"]<a["y_milli"] for a,b in
                   zip(segment[:apex_index],segment[1:apex_index+1]))
        # On the roof the surface is only ~0.25px lower than the apex;
        # a SINGLE sampled downward step plus the landing is sufficient.
        descent=sum(b["y_milli"]>a["y_milli"] for a,b in
                    zip(segment[apex_index:],segment[apex_index+1:]))
        if (not 15<=apex["frame"]-start<=40 or ascent<4 or
            (name=="pit" and descent<3) or
            (name=="roof" and not (descent>=1 or
              landing["y_milli"]>apex["y_milli"]))):
            raise ValueError("missing genuine ascent, apex or descent")
        height_milli=188000-apex["y_milli"]
        if not 61000<=height_milli<=68000:
            raise ValueError("jump height does not match full guest physics arc")
        if (landing["frame"]-start<24 or landing["frame"]-start>76 or
            landing["x_milli"]<=departure["x_milli"]+12000):
            raise ValueError("jump flight time or horizontal movement invalid")
        for a,b in zip(segment,segment[1:]):
            if (b["frame"]-a["frame"]>4 or
                b["x_milli"]+8000<a["x_milli"] or
                b["x_milli"]-a["x_milli"]>10000):
                raise ValueError("jump arc hides dropped frames or teleport")
        result[name]={
            "pressed_frame":start,"released_frame":stop,
            "apex_frame":apex["frame"],"apex_y_px":round(apex["y_milli"]/1000,3),
            "rise_px":round(height_milli/1000,3),
            "landing_frame":landing["frame"],
            "landing_x_px":round(landing["x_milli"]/1000,3),
            "landing_y_px":round(landing["y_milli"]/1000,3),
            "flight_frames":landing["frame"]-start,
            "ascent_samples":ascent,"descent_samples":descent,
        }
    return {"arcs":result,
            "source":"two live NeXium B pulse arcs observed in NRO qs_step; not scripted QsInput",
            "units":"guest simulation frames and guest pixels"}


def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_jump_arc.py <input-injection.txt> <guest-obstacle.csv> <summary.txt>")
    out=Path(argv[3])
    try:
        report=verify_jump_arcs(Path(argv[1]).read_text(encoding="utf-8"),
                                read_obstacles(argv[2]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        out.write_text("FAIL: guest jump arcs: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: guest jump arcs: "+str(exc))
    message="NEXIUM_LIVE_JUMP_ARCS_PASS "+json.dumps(report,sort_keys=True)+"\n"
    out.write_text(message,encoding="utf-8")
    print(message,end="")


if __name__=="__main__":
    main(sys.argv)
