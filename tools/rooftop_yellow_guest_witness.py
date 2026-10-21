#!/usr/bin/env python3
"""Read-only numeric poll for real NRO X-change, yellow FIRE and bridge opening."""
import csv
import pathlib
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_red_to_yellow import WEAPON_COLUMNS,WEAPON_SOURCE
from tools.verify_rooftop_guest_trace import COLUMNS,SOURCE

def witness(roof_path,weapon_path,kind,after):
    with pathlib.Path(roof_path).open(newline="",encoding="utf-8") as fd:
        rows=list(csv.DictReader(fd))
    with pathlib.Path(weapon_path).open(newline="",encoding="utf-8") as fd:
        wr=list(csv.DictReader(fd))
    if not rows or not wr:
        return None
    # The actual NRO writes/flushes ROOF before WEAPON on each tick.
    # Concurrent host polling often catches ROOF one row ahead, so
    # exact whole-file length equality hides a genuine earlier event.
    # Scan ONLY the common paired prefix; the full post-run verifier
    # still demands exact complete-file matching, contiguous seq/frame
    # and the complete original controller/shot/bridge timeline.
    if tuple(rows[0])!=COLUMNS or tuple(wr[0])!=WEAPON_COLUMNS:
        return None
    prev_bits=0
    try:
        for row,w in zip(rows,wr):
            if row["source"]!=SOURCE or w["source"]!=WEAPON_SOURCE: return None
            frame=int(row["frame"])
            if frame!=int(w["frame"]) or int(row["seq"])!=int(w["seq"]):
                return None
            bits=int(row["input_bits"])
            if frame>after:
                if (kind=="change" and int(w["change_pressed"])==1 and
                    int(w["weapon_before"])==1 and int(w["weapon_after"])==0):
                    return frame
                if (kind=="fire" and bits&8 and not prev_bits&8 and
                    int(w["weapon_after"])==0):
                    return frame
                if (kind=="bridge" and int(row["bridge_before"])==0 and
                    int(row["bridge_after"])==1 and int(row["bridge_cells"])==6 and
                    int(row["shot_active_before"])==1 and int(row["shot_red_before"])==0):
                    return frame
            prev_bits=bits
    except (KeyError,ValueError,TypeError):
        return None
    return None

def main(argv):
    if len(argv)!=5 or argv[3] not in ("change","fire","bridge"):
        raise SystemExit("usage: rooftop_yellow_guest_witness.py ROOF.csv WEAPON.csv change|fire|bridge AFTER_FRAME")
    try: after=int(argv[4])
    except ValueError:raise SystemExit("AFTER_FRAME must be integer")
    try: result=witness(argv[1],argv[2],argv[3],after)
    except (OSError,UnicodeError):result=None
    if result is None:return 1
    print(result)
    return 0

if __name__=="__main__":sys.exit(main(sys.argv))
