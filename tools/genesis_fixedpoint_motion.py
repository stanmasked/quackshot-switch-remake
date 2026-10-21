#!/usr/bin/env python3
"""Numeric-only Genesis 16.16 walking + variable jump measurement.

Run ONLY with a private, SHA-pinned user ROM on the local computer. Never
upload the ROM, screenshots, sound, emulator saves, raw RAM or code bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path

from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY

FRAME_START = 2070
WINDOW_START = FRAME_START - 1
WINDOW_END = 2191

OBSERVATIONS = {
    "idle": [],
    "repeat_idle": [],
    "right_100": [(2070, 2170, "RIGHT")],
    "right_tap_8": [(2070, 2078, "RIGHT")],
    "left_100": [(2070, 2170, "LEFT")],
    "jump_tap_1": [(2070, 2071, "A")],
    "jump_hold_50": [(2070, 2120, "A")],
}


def decode(ram):
    if len(ram) != 65536:
        raise ValueError("Expected exactly 64 KiB 68000 work RAM")
    u = lambda o: int.from_bytes(ram[o:o + 2], "little", signed=False)
    s = lambda o: int.from_bytes(ram[o:o + 2], "little", signed=True)
    return {
        "x": u(0xA2D0), "y": u(0xA2D4),
        "x_sub": u(0xA2D2), "y_sub": u(0xA2D6),
        "vx_int": s(0xA2D8), "vx_sub": u(0xA2DA),
        "vy_int": s(0xA2DC), "vy_sub": u(0xA2DE),
    }


def v16(state, axis):
    return state["v" + axis + "_int"] + state["v" + axis + "_sub"] / 65536


def p16(state, axis):
    return state[axis] + state[axis + "_sub"] / 65536


def close(actual, expected, label, tol=1/65536):
    if abs(actual - expected) > tol:
        raise ValueError(f"Measured {label} differs: {actual:.8f} vs {expected:.8f}")


def validate(traces, input_deltas):
    """Reject plausible-looking but causally inconsistent observations."""
    if traces["idle"] != traces["repeat_idle"]:
        raise ValueError("Independent idle emulations diverged")
    initial = traces["idle"][WINDOW_START]
    if (initial["x"], initial["y"]) != (61, 176):
        raise ValueError("Not at verified original-game first-city spawn")
    for label, series in traces.items():
        if not all(t in series for t in range(WINDOW_START, WINDOW_END)):
            raise ValueError(f"Missing per-frame RAM observations in {label}")
        if series[WINDOW_START] != initial:
            raise ValueError(f"{label}: different state before input")
    for label, expected in {
        "right_100": 100, "right_tap_8": 8, "left_100": 100,
        "jump_tap_1": 1, "jump_hold_50": 50,
    }.items():
        if input_deltas[label] != expected:
            raise ValueError(f"{label}: emulator did not read required buttons")

    right, tap, left = (traces[k] for k in
                        ("right_100", "right_tap_8", "left_100"))
    for t in range(11):
        frame = FRAME_START + t
        expected = min((t + 1) * (10240 / 65536), 1.625)
        close(v16(right[frame], "x"), expected, f"right velocity {frame}")
        close(v16(left[frame], "x"), -expected, f"left velocity {frame}")
    for frame in range(FRAME_START + 11, FRAME_START + 100):
        close(v16(right[frame], "x"), 1.625, "right speed cap")
    for t in range(8):
        close(v16(tap[FRAME_START + t], "x"),
              (t + 1) * (10240 / 65536), "tap acceleration")
        close(v16(tap[FRAME_START + 8 + t], "x"),
              (7-t) * (10240 / 65536), "tap braking")
    close(p16(right[2090], "x") - p16(initial, "x"),
          26.46875, "21-tick displacement")
    close(p16(right[2160], "x") - p16(initial, "x"),
          140.21875, "91-tick displacement")
    close(p16(tap[2085], "x") - p16(initial, "x"),
          10, "eight-frame input plus eight-frame coast")

    short, long = traces["jump_tap_1"], traces["jump_hold_50"]
    close(v16(short[2070], "y"), -2.9296875, "jump takeoff velocity")
    close(v16(long[2070], "y"), -2.9296875, "long jump takeoff")
    for frame in range(2070, 2085):
        close(v16(long[frame], "y"), -2.9296875, "held jump velocity")
    close(v16(short[2071], "y"), -2.734375, "released jump gravity")
    close(v16(long[2085], "y"), -2.734375, "long jump after hold ceiling")
    short_apex = min(short[f]["y"] for f in range(2070, 2150))
    long_apex = min(long[f]["y"] for f in range(2070, 2150))
    short_apex_f = min(f for f in range(2070, 2150)
                       if short[f]["y"] == short_apex)
    long_apex_f = min(f for f in range(2070, 2150)
                      if long[f]["y"] == long_apex)
    short_land = next((f for f in range(short_apex_f+1, 2191)
                       if short[f]["y"] == initial["y"]), None)
    long_land = next((f for f in range(long_apex_f+1, 2191)
                      if long[f]["y"] == initial["y"]), None)
    if (short_apex, short_apex_f, short_land) != (152, 2083, 2100):
        raise ValueError("Short-jump apex/landing differs from verified ROM")
    if (long_apex, long_apex_f, long_land) != (111, 2097, 2126):
        raise ValueError("Long-jump apex/landing differs from verified ROM")
    for label in ("idle", "right_100", "jump_tap_1", "jump_hold_50"):
        if any(traces[label][f]["x"] != initial["x"]
               for f in range(2070, 2150)) and label not in ("right_100",):
            raise ValueError("Unintended original horizontal movement")
    return {
        "horizontal": {
            "initial_x": initial["x"],
            "acceleration_px_per_frame_squared": 10240/65536,
            "maximum_walking_speed_px_per_frame": 106496/65536,
            "braking_px_per_frame_squared": 10240/65536,
            "first_speed_cap_frame": 2080,
            "world_x_after_21_frames": right[2090]["x"],
            "world_x_after_91_frames": right[2160]["x"],
            "tap_8_then_coast_final_x": tap[2085]["x"],
        },
        "jump": {
            "initial_vy_px_per_frame": -2.9296875,
            "gravity_when_released_px_per_frame_squared": 12800/65536,
            "max_hold_velocity_plateau_frames": 15,
            "tap_1": {
                "rise_world_pixels": initial["y"] - short_apex,
                "first_apex_frame": short_apex_f,
                "landing_frame": short_land,
                "elapsed_to_landing_frames": short_land - FRAME_START,
            },
            "hold_50": {
                "rise_world_pixels": initial["y"] - long_apex,
                "first_apex_frame": long_apex_f,
                "landing_frame": long_land,
                "elapsed_to_landing_frames": long_land - FRAME_START,
            },
        },
    }


def measure(core, meta, rom_path, rom_data, rom_sha):
    intro = parse_script(DEFAULT_ENTRY)
    traces, reads = {}, {}
    for name, extra in OBSERVATIONS.items():
        states = {}
        def checkpoint(frame, private_ram):
            if WINDOW_START <= frame < WINDOW_END:
                states[frame] = decode(private_ram)
        result = run_core(core, meta, rom_data, rom_sha, 2320,
                          intro + extra, private_rom_path=rom_path,
                          checkpoint_sink=checkpoint,
                          ram_window=(WINDOW_START, WINDOW_END))
        if result["frames_video_callbacks"] != 2320:
            raise RuntimeError("Original Genesis emulation failed to complete")
        traces[name] = states
        reads[name] = result["input_nonzero_reads"]
    input_deltas = {k: reads[k]-reads["idle"] for k in OBSERVATIONS
                    if k not in ("idle", "repeat_idle")}
    metrics = validate(traces, input_deltas)
    return {
        "schema": "quackshot_original_genesis_fixedpoint_motion_v1",
        "evidence_type": "private_repeated_full_emulator_observations_numeric_only",
        "rom_sha256": rom_sha,
        "emulator_core": meta["library_name"] + " " + meta["library_version"],
        "independent_runs": len(OBSERVATIONS),
        "frames_per_run": 2320,
        "action_start_frame": FRAME_START,
        "observed_ram_window": [WINDOW_START, WINDOW_END],
        "original_game_state": "First playable city stage; no modifications to original ROM",
        "real_joypad_input_reads_over_baseline": input_deltas,
        "measurement_ram_addresses": {
            "player_world_x_low_word": "0xA2D0",
            "player_world_y_low_word": "0xA2D4",
            "x_fraction": "0xA2D2",
            "y_fraction": "0xA2D6",
            "horizontal_velocity_words": ["0xA2D8", "0xA2DA"],
            "vertical_velocity_words": ["0xA2DC", "0xA2DE"],
        },
        "fixedpoint_format": "signed integer word plus unsigned 16-bit fraction / 65536",
        "baseline_replayed_identically": True,
        "metrics": metrics,
        "limitations": [
            "Measurements are at one verified first-city spawn and ROM SHA only",
            "Gravity and jump-hold behavior identified through two input windows",
            "No proof of identical physics on other terrain, under damage, while dashing",
            "Raw RAM, video, audio, save states, source ROM, sprite assets were NOT exported",
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
        rom_data, rom_sha = safe_rom(args.rom, args.expected_sha256)
        report = measure(core, meta, args.rom, rom_data, rom_sha)
        report["core_binary_sha256"] = hashlib.sha256(args.core.read_bytes()).hexdigest()
        txt = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(txt, encoding="utf-8")
        else:
            print(txt, end="")
    except (ValueError, RuntimeError, OSError) as e:
        p.exit(1, f"Genesis fixed-point motion probe: {e}\n")


if __name__ == "__main__":
    main()
