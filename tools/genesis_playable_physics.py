#!/usr/bin/env python3
"""Measure first confirmed player-controlled QuackShot physics locally.

Do not upload a private ROM, screenshots, audio, or RAM dumps. This tool
exports bounded numeric coordinates and observed button effects only.
It is pinned to the user's reference ROM by an explicit SHA-256 check.
"""
import argparse
import hashlib
import json
from pathlib import Path

from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core

WORLD_X = 0xA2D0
WORLD_Y = 0xA2D4
DISPLAY_X = 0x8F58
DISPLAY_Y = 0x8F54
VELOCITY_X = 0xA2D8
VELOCITY_Y = 0xA2DC
PLUNGER_SLOT = 0x9C00

# Boot/title/menu/intro/level. START pulses stop before player is ready.
DEFAULT_ENTRY = ",".join(
    ["450:462:START", "680:692:START", "850:862:START"] +
    [f"{f}:{f+5}:START" for f in range(1100, 1400, 45)]
)


def decode_player_state(ram):
    """Observed 16-bit *little-endian* player state from the 64-KiB RAM."""
    if len(ram) != 65536:
        raise ValueError("Expected exactly 65536 bytes of emulated Mega Drive work RAM")
    u = lambda off: int.from_bytes(ram[off:off + 2], "little", signed=False)
    s = lambda off: int.from_bytes(ram[off:off + 2], "little", signed=True)
    return {
        "x": u(WORLD_X), "y": u(WORLD_Y),
        "screen_x": u(DISPLAY_X), "screen_y": u(DISPLAY_Y),
        "vx_int": s(VELOCITY_X), "vy_int": s(VELOCITY_Y),
        "plunger_slot_active": int(ram[PLUNGER_SLOT] != 0),
    }


def analyze_observations(observations, button_start, stop_frame):
    """Fail closed if inputs have no measurable causal effects."""
    required = ("idle", "repeat_idle", "right", "left", "jump", "fire")
    if any(name not in observations for name in required):
        raise ValueError("Missing independent control or action run")
    idle = observations["idle"]
    if idle != observations["repeat_idle"]:
        raise ValueError("Two idle runs are not deterministic")
    first = button_start - 1
    if any(first not in series or stop_frame not in series
           for series in observations.values()):
        raise ValueError("Missing first/last frame in RAM observation window")
    baseline = idle[first]
    if any(idle[t]["x"] != baseline["x"] or idle[t]["y"] != baseline["y"]
           for t in range(first, stop_frame + 1)):
        raise ValueError("Idle player did not remain at stable world coordinates")
    if any(observations[name][first] != baseline
           for name in ("right", "left", "jump", "fire")):
        raise ValueError("Experimental runs differ before controls are activated")

    right = observations["right"]
    left = observations["left"]
    jump = observations["jump"]
    fire = observations["fire"]
    if not (right[button_start+90]["x"] > baseline["x"] + 100 and
            left[button_start+50]["x"] < baseline["x"] - 15 and
            all(jump[t]["x"] == baseline["x"]
                for t in range(button_start, button_start+55)) and
            fire[button_start]["x"] == baseline["x"]):
        raise ValueError("Direction/jump input did not match world-X behavior")
    steady_start = button_start + 20
    steady_end = button_start + 90
    if steady_end > stop_frame:
        raise ValueError("Observation window too short for horizontal velocity")
    xrate = (right[steady_end]["x"] - right[steady_start]["x"]) / (steady_end - steady_start)
    if not (0.5 < xrate < 5):
        raise ValueError("Observed horizontal movement slope out of safe bounds")

    rising = [(t, jump[t]["y"]) for t in range(button_start, stop_frame+1)]
    apex_y = min(y for t, y in rising)
    apex_frame = next(t for t, y in rising if y == apex_y)
    land = next((t for t, y in rising if t > apex_frame and y == baseline["y"]), None)
    if not (15 <= baseline["y"]-apex_y <= 120 and land is not None and
            land > apex_frame and all(right[t]["y"] == baseline["y"]
                                     for t in range(button_start, steady_end))):
        raise ValueError("Jump arc/ground-level control not confirmed")
    first_shot = next((t for t in range(button_start, stop_frame+1)
                       if fire[t]["plunger_slot_active"] and
                       not idle[t]["plunger_slot_active"]), None)
    if first_shot is None or first_shot > button_start + 8:
        raise ValueError("No independently observed active plunger slot after fire")

    return {
        "initial_position_world_pixels": {"x": baseline["x"], "y": baseline["y"]},
        "movement": {
            "right_world_x_at_frame_plus_20": right[steady_start]["x"],
            "right_world_x_at_frame_plus_90": right[steady_end]["x"],
            "measured_right_px_per_frame": round(xrate, 6),
            "left_minimum_world_x": min(left[t]["x"]
                                        for t in range(button_start, stop_frame+1)),
        },
        "jump": {
            "initial_world_y": baseline["y"], "apex_world_y": apex_y,
            "apex_frame": apex_frame, "peak_rise_world_pixels": baseline["y"]-apex_y,
            "landing_frame": land, "total_airborne_frames": land-button_start,
        },
        "fire": {"first_active_plunger_slot_frame": first_shot},
    }


def measure(core, metadata, local_rom, data, sha, entry, button_start=2070,
            frames=2320, stop_frame=2220):
    if not 2 <= button_start < stop_frame < frames <= 10000:
        raise ValueError("Invalid frame range")
    window = (button_start-1, stop_frame+1)
    baseline = parse_script(entry)
    if any(end > button_start for _, end, _ in baseline):
        raise ValueError("Boot/title/menu input must stop before measured action")
    scenarios = {
        "idle": [], "repeat_idle": [],
        "right": [(button_start, button_start+120, "RIGHT")],
        "left": [(button_start, button_start+120, "LEFT")],
        "jump": [(button_start, button_start+50, "A")],
        "fire": [(button_start, button_start+50, "B")],
    }
    observed = {}
    input_counts = {}
    video_checksums = {}
    for label, extra in scenarios.items():
        decoded = {}
        def checkpoint(frame, ram):
            if window[0] <= frame < window[1]:
                decoded[frame] = decode_player_state(ram)
        report = run_core(
            core, metadata, data, sha, frames, baseline + extra,
            private_rom_path=local_rom, video_stride=60, checkpoint_sink=checkpoint,
            ram_window=window)
        if report["frames_video_callbacks"] != frames:
            raise RuntimeError("Emulator did not generate all requested frames")
        observed[label] = decoded
        input_counts[label] = report["input_nonzero_reads"]
        video_checksums[label] = [
            (v["frame"], v["crc32"]) for v in report["video_checkpoints"]
        ]
    if video_checksums["idle"] != video_checksums["repeat_idle"]:
        raise ValueError("Idle rendered frames differ across repetitions")
    for label, number in (("right", 120), ("left", 120), ("jump", 50), ("fire", 50)):
        if input_counts[label] - input_counts["idle"] != number:
            raise ValueError("Emulator did not observe expected active controller states")
    metrics = analyze_observations(observed, button_start, stop_frame)
    return {
        "schema": "quackshot_genesis_playable_physics_v1",
        "evidence_type": "real_private_genesis_emulation_controlled_numeric_RAM_correlations",
        "source_rom_sha256": sha,
        "source_rom_size_bytes": len(data),
        "emulator": metadata["library_name"] + " " + metadata["library_version"],
        "frame_count_per_run": frames,
        "independent_emu_runs": len(scenarios),
        "control_frame": button_start,
        "entry_sequence": [
            {"first_frame": f, "end_exclusive": e, "button": b}
            for f,e,b in baseline
        ],
        "state_word_endianness": "little",
        "candidate_memory_offsets": {
            "world_x": "0xA2D0", "world_y": "0xA2D4",
            "screen_x": "0x8F58", "screen_y": "0x8F54",
            "horizontal_velocity_integer": "0xA2D8",
            "vertical_velocity_integer": "0xA2DC",
            "plunger_slot_activity": "0x9C00",
        },
        "input_read_deltas": {
            label: input_counts[label]-input_counts["idle"]
            for label in ("right", "left", "jump", "fire")
        },
        "replayed_baseline_is_identical": True,
        "metrics": metrics,
        "limits": [
            "Only the first tested playable city-level state, not every QuackShot level",
            "World coordinate words and animation screen positions can differ due to camera and sprite anchors",
            "One measured input combination, fixed Genesis Plus GX configuration",
            "No original ROM bytes, RAM dumps, pixels, sounds or original-game assets included",
            "Do not equate this directly with Switch prototype physics without coordinate and timing alignment"
        ]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--entry", default=DEFAULT_ENTRY)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        core, metadata = setup_core(args.core)
        data, sha = safe_rom(args.rom, args.expected_sha256)
        report = measure(core, metadata, args.rom, data, sha, args.entry)
        report["compiled_core_sha256"] = hashlib.sha256(args.core.read_bytes()).hexdigest()
        value = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(value, encoding="utf-8")
        else:
            print(value, end="")
    except (OSError, RuntimeError, ValueError) as exc:
        parser.exit(1, "Private Genesis physics probe: " + str(exc) + "\n")


if __name__ == "__main__":
    main()
