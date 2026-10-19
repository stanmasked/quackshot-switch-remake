#!/usr/bin/env python3
"""Stage 98: verify live NeXium jump arcs, including authenticated wall recovery.

The normal roof jump lands on Y=124. If a late roof jump meets the ceiling,
the hero instead lands on Y=188; that path is accepted ONLY when the
host/guest-authenticated third B clears the wall with a full physical jump.
No teleport, fabricated landing or merely sent host key satisfies a proof.
"""
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_guest_button_pulses import verify_pulses
from tools.verify_guest_edge_response import read_obstacles


def _measure(rows,pulse,name):
    start=pulse["pressed_frame"]
    released=pulse["released_frame"]
    matches=[i for i,r in enumerate(rows) if r["frame"]==start]
    if len(matches)!=1:
        raise ValueError(f"{name} B press not uniquely aligned to guest frame")
    index=matches[0]
    departure=rows[index]
    if (departure["grounded"]!=0 or not departure["input_bits"]&4 or
        not 182000<=departure["y_milli"]<=187500):
        raise ValueError(f"{name} did not initiate a physical airborne jump")
    landing=None
    for row in rows[index+1:]:
        if row["frame"]-start>85:
            break
        if row["frame"]>released and row["grounded"]==1:
            landing=row
            break
    if landing is None:
        raise ValueError(f"{name} never landed after releasing B")
    segment=[r for r in rows[index:] if start<=r["frame"]<landing["frame"]]
    if len(segment)<8 or any(r["grounded"]!=0 for r in segment):
        raise ValueError(f"{name} jump arc incomplete or prematurely grounded")
    apex=min(segment,key=lambda r:r["y_milli"])
    index_apex=segment.index(apex)
    ascent=sum(b["y_milli"]<a["y_milli"] for a,b in
               zip(segment[:index_apex],segment[1:index_apex+1]))
    descent=sum(b["y_milli"]>a["y_milli"] for a,b in
                zip(segment[index_apex:],segment[index_apex+1:]))
    height=188000-apex["y_milli"]
    if (ascent<4 or not 8<=apex["frame"]-start<=40 or
        not 20<=landing["frame"]-start<=80):
        raise ValueError(f"{name} ascent, apex or flight duration invalid")
    if landing["frame"]<=released:
        raise ValueError(f"{name} landing preceded button release")
    if name!="roof" and descent<3:
        raise ValueError(f"{name} missing genuine descent")
    if name=="roof" and not (descent>=1 or landing["y_milli"]>apex["y_milli"]):
        raise ValueError("roof missing descent/landing contact")
    for a,b in zip(segment,segment[1:]):
        if (b["frame"]-a["frame"]>4 or
            b["x_milli"]+8000<a["x_milli"] or
            b["x_milli"]-a["x_milli"]>10000):
            raise ValueError(f"{name} arc hides lost frames or teleport")
    return {
        "pressed_frame":start,"released_frame":released,
        "apex_frame":apex["frame"],"apex_y_px":round(apex["y_milli"]/1000,3),
        "rise_px":round(height/1000,3),
        "landing_frame":landing["frame"],
        "landing_x_px":round(landing["x_milli"]/1000,3),
        "landing_y_px":round(landing["y_milli"]/1000,3),
        "flight_frames":landing["frame"]-start,
        "ascent_samples":ascent,"descent_samples":descent
    }


def _landed(arc,xlo,xhi,ylo,yhi,hlo=61,hhi=68):
    return (xlo<=arc["landing_x_px"]<=xhi and
            ylo<=arc["landing_y_px"]<=yhi and
            hlo<=arc["rise_px"]<=hhi)


def verify_jump_arcs(host_text,samples):
    pulses=verify_pulses(host_text,samples)
    rows=[{key:int(s[key]) for key in
           ("frame","attempt","x_milli","y_milli","grounded","input_bits")}
          for s in samples]
    if any(b["frame"]<=a["frame"] or b["frame"]-a["frame"]>4 or
           b["attempt"]<=a["attempt"] for a,b in zip(rows,rows[1:])):
        raise ValueError("discontinuous jump guest timeline")
    all_B=pulses["pulses"]["B"]
    if len(all_B) not in (2,3):
        raise ValueError("missing two real B input pulses")
    pit=_measure(rows,all_B[0],"pit")
    roof=_measure(rows,all_B[1],"roof")
    if not _landed(pit,365,415,182,193):
        raise ValueError("pit grounded landing not on far bank")
    direct=_landed(roof,415,475,123.5,125)
    blocked=_landed(roof,440,495,182,193,18,45)
    recovery=None
    if len(all_B)==3:
        if pulses["wall_recovery"] is None:
            raise ValueError("unverified recovery B input")
        recovery=_measure(rows,all_B[2],"recovery")
        if not _landed(recovery,600,660,182,193):
            raise ValueError("recovery jump did not clear the wall and land")
        if not (roof["landing_frame"]<recovery["pressed_frame"] and
                recovery["landing_frame"]>recovery["apex_frame"]):
            raise ValueError("recovery jump not chronologically after roof")
    if not direct and not (blocked and recovery):
        raise ValueError("roof landing was blocked, but no physically verified wall recovery")
    if direct and not (61<=roof["rise_px"]<=68):
        raise ValueError("roof apex height outside full jump range")
    return {
        "arcs":{"pit":pit,"roof":roof},
        "roof_path":"direct_sky_ledge" if direct else "ground_then_wall_recovery",
        "wall_recovery":recovery,
        "source":"NeXium live B guest physics; ceiling block requires verified recovery",
        "units":"guest simulation frames and pixels"
    }


def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_jump_arc.py <input-injection.txt> <guest-obstacle.csv> <summary.txt>")
    dest=Path(argv[3])
    try:
        result=verify_jump_arcs(Path(argv[1]).read_text(encoding="utf-8"),
                                read_obstacles(argv[2]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        dest.write_text("FAIL: guest jump arcs: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: guest jump arcs: "+str(exc))
    line="NEXIUM_LIVE_JUMP_ARCS_PASS "+json.dumps(result,sort_keys=True)+"\n"
    dest.write_text(line,encoding="utf-8")
    print(line,end="")


if __name__=="__main__":
    main(sys.argv)
