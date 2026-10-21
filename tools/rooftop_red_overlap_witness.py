#!/usr/bin/env python3
"""Read-only host poll: find a genuine numeric red switch-overlap guest row.

The full acceptance logic remains in verify_rooftop_red_trace.py. This helper
only locates a frame to synchronize the virtual-controller runner; it MUST NOT
modify guest CSV data. Header/string rows never count as a numeric witness.
"""
import csv
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import COLUMNS, SOURCE, TARGET_X_MILLI, TARGET_W_MILLI, SHOT_WIDTH_MILLI


def overlap_in_guest(path, after_frame=0):
    try:
        with pathlib.Path(path).open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != COLUMNS:
                return None
            for row in reader:
                if row.get("source") != SOURCE:
                    continue
                try:
                    frame = int(row["frame"])
                    stage = int(row["stage"])
                    mode = int(row["mode"])
                    red = int(row["shot_red_before"])
                    active = int(row["shot_active_before"])
                    shot_x = int(row["shot_x_before_milli"])
                    velocity = int(row["shot_vx_before_milli"])
                    bridge_before = int(row["bridge_before"])
                    bridge_after = int(row["bridge_after"])
                    solid_cells = int(row["bridge_cells"])
                except (TypeError, ValueError, KeyError):
                    # A partially-written last line is not a witness.
                    continue
                nxt = shot_x + velocity
                if (frame > after_frame and mode == 1 and stage == 2 and red == active == 1
                        and 0 < velocity <= 12000 and shot_x >= 0
                        and nxt < TARGET_X_MILLI + TARGET_W_MILLI
                        and nxt + SHOT_WIDTH_MILLI > TARGET_X_MILLI
                        and bridge_before == bridge_after == solid_cells == 0):
                    return frame, shot_x
    except (OSError, UnicodeError):
        return None
    return None


def main(argv):
    if len(argv) not in (2,3):
        raise SystemExit("usage: rooftop_red_overlap_witness.py GUEST.csv [AFTER_FRAME]")
    try:
        after_frame = int(argv[2]) if len(argv)==3 else 0
    except ValueError:
        raise SystemExit('AFTER_FRAME must be an integer')
    witness = overlap_in_guest(argv[1], after_frame)
    if witness is None:
        return 1
    print(f"{witness[0]} {witness[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
