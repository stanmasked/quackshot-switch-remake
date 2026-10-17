#!/usr/bin/env python3
"""Numeric-only repeatable original QuackShot city ledge/wall causality.

Uses only a LOCAL private ROM with required SHA-256. Never saves or uploads
copyrighted ROM bytes, raw RAM, video frames, audio or savestates.
"""
import argparse
import hashlib
import json
from pathlib import Path

from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY

FIRST = 2250
LAST_EXCLUSIVE = 2511
TOTAL = 2600

RUNS = {
    "idle": [],
    "right": [(2070, 2520, "RIGHT")],
    "right_repeat": [(2070, 2520, "RIGHT")],
    "near_wall_jump": [(2070, 2510, "RIGHT"), (2310, 2350, "A")],
    "early_jump": [(2070, 2510, "RIGHT"), (2275, 2315, "A")],
    "wall_reverse": [(2070, 2360, "RIGHT"), (2360, 2430, "LEFT")],
}


def decode(ram):
    if len(ram) != 65536:
        raise ValueError("Expected exactly 64 KiB original Genesis work RAM")
    u16 = lambda off: int.from_bytes(ram[off:off+2], "little")
    s16 = lambda off: int.from_bytes(ram[off:off+2], "little", signed=True)
    return {
        "x": u16(0xA2D0), "x_fraction": u16(0xA2D2),
        "y": u16(0xA2D4), "y_fraction": u16(0xA2D6),
        "vx_int": s16(0xA2D8), "vy_int": s16(0xA2DC),
        "sprite_ram_x_candidate": u16(0x8F58),
        "sprite_ram_y_candidate": u16(0x8F54),
    }


def first_frame(series, predicate, start, stop):
    return next((f for f in range(start, stop) if predicate(series[f])), None)


def validate(traces, reads):
    if set(traces) != set(RUNS):
        raise ValueError("Missing one of six independent original game runs")
    for name, series in traces.items():
        if set(series) != set(range(FIRST, LAST_EXCLUSIVE)):
            raise ValueError(f"{name}: missing or extra frame checkpoints")
    if traces["right"] != traces["right_repeat"]:
        raise ValueError("Repeated rightward original-game trace diverged")
    for name, expected in {
        "idle": 0, "right": 450, "right_repeat": 450,
        "near_wall_jump": 480, "early_jump": 480, "wall_reverse": 360,
    }.items():
        if reads[name] != expected:
            raise ValueError(f"{name}: input not actually read by emulator")
    idle = traces["idle"]
    right = traces["right"]
    near = traces["near_wall_jump"]
    early = traces["early_jump"]
    reverse = traces["wall_reverse"]

    # All independent actions share the exact same pre-jump history.
    for name in ("near_wall_jump", "early_jump", "wall_reverse"):
        if traces[name][FIRST] != right[FIRST]:
            raise ValueError(f"{name}: source game state differs before action")
    if any(idle[f]["x"] != 61 or idle[f]["y"] != 176
           for f in range(FIRST, LAST_EXCLUSIVE)):
        raise ValueError("Idle reference city state is not stable")

    first_drop = first_frame(right, lambda r: r["y"] > 176, 2260, 2320)
    first_lower_floor = first_frame(right, lambda r: r["y"] == 208, 2260, 2320)
    first_wall_stop = first_frame(
        right, lambda r: r["x"] >= 498 and r["vx_int"] == 0, 2300, 2420
    )
    if (first_drop, first_lower_floor, first_wall_stop) != (2281, 2297, 2344):
        raise ValueError(
            f"First city ledge/floor/wall timing differs from ROM witness: "
            f"{(first_drop,first_lower_floor,first_wall_stop)}"
        )
    if (right[2344]["x"], right[2344]["y"]) != (498, 208):
        raise ValueError("Observed wall state no longer matches verified ROM")
    if any(right[f]["x"] != 498 or right[f]["y"] != 208
           for f in range(2344, LAST_EXCLUSIVE)):
        raise ValueError("Continuous right input unexpectedly penetrated city wall")

    if not (near[2350]["x"] > 500 and
            near[2500]["x"] >= 690 and
            min(near[f]["y"] for f in range(2310, 2351)) <= 143):
        raise ValueError("Correctly timed jump did not bypass original wall")
    if not (max(early[f]["x"] for f in range(2350, LAST_EXCLUSIVE)) <= 500 and
            early[2400]["y"] == 208):
        raise ValueError("Early jump should land before original wall")
    if not (reverse[2360]["x"] == 498 and
            reverse[2390]["x"] < 460 and
            reverse[2430]["x"] <= 398):
        raise ValueError("Reverse input must free original player from wall")

    return {
        "starting_world_xy": {"x": 61, "y": 176},
        "ledge": {
            "last_high_floor_y": 176,
            "first_lower_y_frame": first_drop,
            "lower_floor_world_y": 208,
            "first_lower_floor_frame": first_lower_floor,
            "vertical_floor_separation_pixels": 32,
        },
        "wall": {
            "first_zero_horizontal_velocity_frame": first_wall_stop,
            "stopped_world_xy": {"x":498,"y":208},
            "held_right_does_not_cross": True,
            "release_by_reverse_left": True,
            "reverse_x_at_frame_2390": reverse[2390]["x"],
        },
        "timing": {
            "early_jump_start_frame": 2275,
            "early_jump_stopped_x": early[2400]["x"],
            "near_wall_jump_start_frame": 2310,
            "near_wall_jump_x_at_frame_2350": near[2350]["x"],
            "near_wall_jump_x_at_frame_2500": near[2500]["x"],
            "near_wall_jump_crossed_world_x_500": True,
        },
        "sprite_RAM_caveat": (
            "0x8F58/0x8F54 are object/sprite-related RAM candidates, "
            "not independently proven viewport pixel coordinates or "
            "the hardware scroll register."
        ),
    }


def measure(core, meta, rompath, rom, sha):
    base = parse_script(DEFAULT_ENTRY)
    states = {}
    video = {}
    inputs = {}
    baseline_reads = None
    for name, more in RUNS.items():
        samples = {}
        def cb(frame, ram):
            if FIRST <= frame < LAST_EXCLUSIVE:
                samples[frame] = decode(ram)
        out = run_core(
            core, meta, rom, sha, TOTAL, base + more,
            private_rom_path=rompath, ram_window=(FIRST, LAST_EXCLUSIVE),
            checkpoint_sink=cb, video_stride=60
        )
        if out["frames_video_callbacks"] != TOTAL:
            raise RuntimeError("Original emulator did not execute requested frames")
        states[name] = samples
        video[name] = out["video_checkpoints"]
        inputs[name] = out["input_nonzero_reads"]
        if name == "idle":
            baseline_reads = inputs[name]
    if video["right"] != video["right_repeat"]:
        raise ValueError("Repeated rightward video output diverged")
    deltas = {name: inputs[name]-baseline_reads for name in RUNS}
    result = validate(states, deltas)
    return {
        "schema": "quackshot_original_city_obstacle_probe_v1",
        "type": "causal_private_original_megadrive_gameplay_numeric_only",
        "reference_sha256": sha,
        "emulator": meta["library_name"]+" "+meta["library_version"],
        "frames_per_run": TOTAL,
        "independent_runs": len(RUNS),
        "ram_sampling_window_inclusive_exclusive": [FIRST, LAST_EXCLUSIVE],
        "baseline_entry": [
            {"first_frame": a,"end_exclusive":b,"button":key}
            for a,b,key in base
        ],
        "input_read_deltas": deltas,
        "repeat_right_numeric_and_video_identical": True,
        "metrics": result,
        "scope": [
            "One first-city controllable stage, not all original maps.",
            "The wall is evidenced by held-right freeze plus jump/reverse causal releases; physical tile hitbox not disassembled.",
            "Camera scroll amount not yet identified as a specific hardware/RAM register.",
            "No original pixels, ROM bytes, audio, raw RAM or save states exported."
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--core", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--expected-sha256", required=True)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    try:
        core, meta = setup_core(args.core)
        data, sha = safe_rom(args.rom, args.expected_sha256)
        out = measure(core, meta, args.rom, data, sha)
        out["compiled_core_sha256"] = hashlib.sha256(args.core.read_bytes()).hexdigest()
        result = json.dumps(out, ensure_ascii=False, indent=2)+"\n"
        if args.output:
            args.output.write_text(result, encoding="utf-8")
        else:
            print(result, end="")
    except (OSError, ValueError, RuntimeError) as e:
        p.exit(1, f"Original city collision probe failed: {e}\n")


if __name__ == "__main__":
    main()
