#!/usr/bin/env python3
"""Stage 97: bound B/Y press+release pulses actually seen inside NeXium NRO.

Host injection markers are linked to guest qs_step input-bit rises. The guest
must then see a falling edge for each press. Any extra B/Y pulse invalidates
the evidence. A third B is permitted ONLY when authenticated against a
WALL_RECOVERY_GUEST host anchor and the physical hero's grounded state.
All durations are guest simulation frames, NOT milliseconds.
"""
import json
import pathlib
import re
import sys

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_guest_edge_response import read_obstacles, verify_edge_responses

RECOVERY=re.compile(r"^WALL_RECOVERY_GUEST frame=(\d+) x_milli=(\d+)$")
BUTTONS={"B":4,"Y":8}


def verify_pulses(host_text,samples):
    prior=verify_edge_responses(host_text,samples)
    rows=[]
    for sample in samples:
        rows.append({key:int(sample[key]) for key in
                    ("frame","attempt","x_milli","y_milli","grounded","input_bits")})
    extra=[RECOVERY.fullmatch(line) for line in host_text.splitlines()]
    extra=[match for match in extra if match]
    if len(extra)>1:
        raise ValueError("duplicate wall recovery host command")
    expected_B=[prior["events"]["pit"]["nro_first_button_frame"],
                prior["events"]["roof"]["nro_first_button_frame"]]
    extra_info=None
    if extra:
        anchor,x=(int(group) for group in extra[0].groups())
        matches=[row for row in rows if
                 row["frame"]==anchor and row["x_milli"]==x]
        if (len(matches)!=1 or not 545000<=x<=551000 or
            matches[0]["grounded"]!=1 or matches[0]["input_bits"]&4 or
            anchor<=expected_B[-1]):
            raise ValueError("wall recovery not anchored to actual stuck hero")
        candidates=[]
        for before,after in zip(rows,rows[1:]):
            if (anchor<after["frame"]<=anchor+24 and
                not before["input_bits"]&4 and after["input_bits"]&4 and
                after["y_milli"]<matches[0]["y_milli"]-1000 and
                after["grounded"]==0):
                candidates.append(after["frame"])
        if len(candidates)!=1:
            raise ValueError("missing or ambiguous recovery B in guest physics")
        extra_info={"host_frame":anchor,"guest_button_frame":candidates[0]}
        expected_B.append(candidates[0])
    expected={"B":expected_B,
              "Y":[prior["events"]["crate"]["nro_first_button_frame"]]}
    stats={}
    for name,bit in BUTTONS.items():
        rises=[];falls=[];previous=False
        for row in rows:
            current=bool(row["input_bits"]&bit)
            if current and not previous:rises.append(row["frame"])
            if previous and not current:falls.append(row["frame"])
            previous=current
        if previous:
            raise ValueError(f"stuck {name}: no final release inside guest")
        if rises!=expected[name]:
            raise ValueError(f"uncommanded or repeated {name} guest keydown")
        if len(falls)!=len(rises):
            raise ValueError(f"missing {name} guest key release")
        pulses=[]
        for i,(start,stop) in enumerate(zip(rises,falls)):
            if not start<stop or (i+1<len(rises) and stop>=rises[i+1]):
                raise ValueError(f"overlapping {name} pulses / stuck button")
            held=stop-start
            if not 7<=held<=40:
                raise ValueError(f"{name} held for abnormal guest frame duration")
            pulses.append({"pressed_frame":start,"released_frame":stop,
                           "observed_hold_frames":held})
        stats[name]=pulses
    return {"pulses":stats,"wall_recovery":extra_info,
            "guest_press_counts":{"B":len(stats["B"]),"Y":len(stats["Y"])},
            "source":"host real virtual controller + NRO press/release transitions",
            "interpretation":"sampled guest frames; not host elapsed milliseconds"}


def main(argv):
    if len(argv)!=4:
        raise SystemExit("usage: verify_guest_button_pulses.py <input-injection.txt> <guest-obstacle.csv> <summary.txt>")
    out=pathlib.Path(argv[3])
    try:
        report=verify_pulses(pathlib.Path(argv[1]).read_text(encoding="utf-8"),
                             read_obstacles(argv[2]))
    except (OSError,ValueError,TypeError,KeyError) as exc:
        out.write_text("FAIL: guest button pulses: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: guest button pulses: "+str(exc))
    line="NEXIUM_GUEST_BUTTON_PULSES_PASS "+json.dumps(report,sort_keys=True)+"\n"
    out.write_text(line,encoding="utf-8")
    print(line,end="")


if __name__=="__main__":
    main(sys.argv)
