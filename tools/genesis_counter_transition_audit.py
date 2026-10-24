#!/usr/bin/env python3
"""Synthetic 68000 counter sequence audit; not original-game parity."""
import argparse
import json
from pathlib import Path

STATUS = "SYNTHETIC_COUNTER_DIAGNOSTIC_NOT_GAME_PARITY"
PRIVACY = ("raw_rom_exported", "raw_ram_exported",
           "instruction_bytes_exported", "frame_pixels_exported")
COUNT = 0xFFA2E6
STATE = 0xFFA2C8
POINTER = 0xFFA2C0
INIT, STEP, BRANCH, RESET = 0x64E4, 0x6540, 0x6544, 0x6552


class AuditError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise AuditError(message)


def number(value, lo=0, hi=0xFFFFFFFF):
    require(type(value) is int and lo <= value <= hi, "invalid numeric field")
    return value


def extract(report, key, schema):
    obj = report.get(key)
    require(isinstance(obj, dict) and obj.get("schema") == schema, "schema mismatch")
    require(obj.get("status") == "COMPLETE" and obj.get("overflow_count") == 0,
            "incomplete trace or overflow")
    require(all(obj.get(flag) is False for flag in PRIVACY), "privacy flag missing")
    window = obj.get("frame_window_inclusive")
    require(isinstance(window, list) and len(window) == 2, "invalid window")
    first, last = number(window[0]), number(window[1])
    require(first <= last and last - first < 128, "window too large")
    events = obj.get("events")
    require(isinstance(events, list) and len(events) <= 65536, "invalid events")
    require(number(obj.get("event_count")) == len(events), "event count mismatch")
    previous = first
    for event in events:
        require(isinstance(event, dict), "malformed event")
        frame = number(event.get("frame"), first, last)
        require(frame >= previous, "events out of chronological order")
        previous = frame
        number(event.get("instruction_pc"), 0, 0xFFFFFF)
        if key == "cpu_access_trace":
            kind = event.get("access_kind")
            keys = {"frame", "instruction_pc", "cpu_address", "ram_offset",
                    "width_bytes", "access_kind", "access_value"}
            if kind == "W":
                keys.add("write_value")
            require(kind in ("R", "W") and set(event) == keys, "invalid access fields")
            address = number(event["cpu_address"], 0, 0xFFFFFF)
            require(number(event["ram_offset"], 0, 65535) == (address & 65535),
                    "RAM offset mismatch")
            number(event["width_bytes"], 1, 4)
            value = number(event["access_value"])
            if kind == "W":
                require(event["write_value"] == value, "write value mismatch")
        else:
            require(set(event) == {"frame", "instruction_pc", "a0_address",
                                  "a1_address", "ccr_low5"}, "invalid PC fields")
            number(event["a0_address"], 0, 0xFFFFFF)
            number(event["a1_address"], 0, 0xFFFFFF)
            number(event["ccr_low5"], 0, 31)
    return (first, last), events


def audit(report, negative=False):
    require(isinstance(report, dict), "report must be an object")
    wa, access = extract(report, "cpu_access_trace",
                         "quackshot_private_m68k_cpu_access_trace_v2")
    wp, pcs = extract(report, "cpu_control_flow_trace",
                      "quackshot_private_m68k_control_flow_trace_v2")
    require(wa == wp, "trace windows differ")
    require(report["cpu_control_flow_trace"].get("object_pointer_filter") ==
            "A0 low 24 bits equal 0xFFA2C0", "unexpected object filter")
    counter = [e for e in access if e["cpu_address"] == COUNT]
    resets = [e for e in access if e["cpu_address"] == STATE and
              e["instruction_pc"] == RESET and e["access_kind"] == "W"]
    relevant_pcs = [e for e in pcs if e["instruction_pc"] in
                    (INIT, STEP, BRANCH, RESET)]
    if negative:
        require(not counter and not resets and not relevant_pcs,
                "negative control contains counter events")
        return {"status": STATUS, "steps": 0, "mode": "ABSENT",
                "original_rom_was_run": False, "gameplay_semantics_verified": False}
    require(len(counter) == 33 and len(resets) == 1, "incomplete countdown or reset")
    initial = counter[0]
    require((initial["instruction_pc"], initial["access_kind"],
             initial["width_bytes"], initial["access_value"]) ==
            (INIT, "W", 2, 16), "incorrect initialization")
    start = initial["frame"]
    require(start + 16 <= wa[1], "countdown outside time window")
    for i in range(16):
        r, w = counter[1+2*i:3+2*i]
        frame = start + i + 1
        require((r["frame"], r["instruction_pc"], r["access_kind"],
                 r["width_bytes"], r["access_value"]) ==
                (frame, STEP, "R", 2, 16-i), "counter read mismatch")
        require((w["frame"], w["instruction_pc"], w["access_kind"],
                 w["width_bytes"], w["access_value"]) ==
                (frame, STEP, "W", 2, 15-i), "counter write mismatch")
    groups = {pc: [e for e in relevant_pcs if e["instruction_pc"] == pc]
              for pc in (INIT, STEP, BRANCH, RESET)}
    require([len(groups[pc]) for pc in (INIT, STEP, BRANCH, RESET)] ==
            [1, 16, 16, 1], "incomplete PC branch")
    require(groups[INIT][0]["frame"] == start, "initializer PC absent")
    for i in range(16):
        frame = start + i + 1
        require(groups[STEP][i]["frame"] == frame and
                groups[BRANCH][i]["frame"] == frame, "branch timing mismatch")
        require((groups[BRANCH][i]["ccr_low5"] & 4) ==
                (4 if i == 15 else 0), "zero-flag branch mismatch")
    require(groups[RESET][0]["frame"] == start+16, "zero path early")
    require((resets[0]["frame"], resets[0]["width_bytes"],
             resets[0]["access_value"]) == (start+16, 1, 0),
            "state reset not observed")
    require(all(e["a0_address"] == POINTER for e in relevant_pcs),
            "wrong object pointer")
    return {"status": STATUS, "mode": "COUNTDOWN_VERIFIED", "steps": 16,
            "initial_frame": start, "terminal_frame": start+16,
            "zero_branch_observed": True, "state_reset_observed": True,
            "original_rom_was_run": False, "gameplay_semantics_verified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--negative-control", action="store_true")
    args = parser.parse_args()
    if not args.report.is_file() or args.report.stat().st_size > 4000000:
        parser.error("input missing or oversized")
    try:
        result = audit(json.loads(args.report.read_text(encoding="utf-8")),
                       negative=args.negative_control)
    except (ValueError, UnicodeError) as error:
        parser.error(str(error))
    args.out.write_text(json.dumps(result, sort_keys=True)+"\n", encoding="utf-8")
    print("STAGE280_ROM_FREE_COUNTER_AUDIT_PASS")


if __name__ == "__main__":
    main()
