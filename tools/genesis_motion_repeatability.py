#!/usr/bin/env python3
"""Cold-boot, numeric-only repeatability audit of private Genesis motion.

The original ROM and the libretro core remain on the trusted local computer.
Only fixed-point field digests, observed input counts and summary metrics leave
this process; no RAM snapshots, gameplay frames, sound or input logs are saved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from genesis_fixedpoint_motion import (
    FRAME_START, OBSERVATIONS, WINDOW_END, WINDOW_START, decode, validate,
)
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY

FRAMES = 2320
FIELDS = ("x", "x_sub", "y", "y_sub", "vx_int", "vx_sub", "vy_int", "vy_sub")
SCENARIOS = {
    **OBSERVATIONS,
    "reverse_right20_left20": [(2070, 2090, "RIGHT"), (2090, 2110, "LEFT")],
    "jump_hold_8": [(2070, 2078, "A")],
}


def numeric_digest(series):
    """Hash only eight decoded numeric fields per frame; never raw RAM bytes."""
    digest = hashlib.sha256(b"QuackShot/16.16/numeric/v1\0")
    for frame in range(WINDOW_START, WINDOW_END):
        if frame not in series:
            raise ValueError(f"Missing frame {frame} for numeric digest")
        digest.update(struct.pack(">I", frame))
        state = series[frame]
        if set(state) != set(FIELDS):
            raise ValueError(f"Unexpected numeric fields at frame {frame}")
        for field in FIELDS:
            value = state[field]
            if type(value) is not int or not -32768 <= value <= 65535:
                raise ValueError(f"Invalid 16-bit numeric field {field} at frame {frame}")
            digest.update(struct.pack(">i", value))
    return digest.hexdigest()


def validate_repetitions(samples):
    """Fail closed on divergence, absent frames, or fabricated input reads."""
    if not 2 <= len(samples) <= 4:
        raise ValueError("Repeat count must be in 2..4")
    digests = []
    reference_metrics = None
    baseline = None
    for index, sample in enumerate(samples):
        traces, reads = sample["traces"], sample["reads"]
        if set(traces) != set(SCENARIOS) or set(reads) != set(SCENARIOS):
            raise ValueError(f"Run {index + 1} missing or unexpected scenarios")
        deltas = {name: reads[name] - reads["idle"]
                  for name in SCENARIOS if name not in ("idle", "repeat_idle")}
        if reads["idle"] != reads["repeat_idle"]:
            raise ValueError(f"Run {index + 1} idle input reads disagree")
        for name, schedule in SCENARIOS.items():
            expected = sum(stop - start for start, stop, _ in schedule)
            if name not in ("idle", "repeat_idle") and deltas[name] != expected:
                raise ValueError(f"Run {index + 1} {name}: input read count differs")
        metrics = validate(traces, deltas)
        if reference_metrics is None:
            reference_metrics = metrics
        elif metrics != reference_metrics:
            raise ValueError(f"Run {index + 1}: original motion metrics diverged")
        current = {name: numeric_digest(traces[name]) for name in SCENARIOS}
        digests.append(current)
        if baseline is not None:
            for name in SCENARIOS:
                if current[name] != digests[0][name]:
                    for frame in range(WINDOW_START, WINDOW_END):
                        if traces[name][frame] != baseline[name][frame]:
                            differences = [key for key in FIELDS
                                           if traces[name][frame][key] != baseline[name][frame][key]]
                            raise ValueError(
                                f"Run {index + 1} diverged: {name} frame {frame}, fields {','.join(differences)}")
                    raise ValueError(f"Run {index + 1} {name}: digest differs")
        else:
            baseline = traces
    return {
        "scenario_digests": digests[0],
        "metrics": reference_metrics,
        "scenario_count": len(SCENARIOS),
        "cold_boots": len(samples) * len(SCENARIOS),
        "repeats_per_scenario": len(samples),
        "frames_per_cold_boot": FRAMES,
        "observed_frames_per_scenario": WINDOW_END - WINDOW_START,
        "all_numeric_traces_identical": True,
    }


def capture(core, meta, rom_path, rom_data, rom_sha):
    intro = parse_script(DEFAULT_ENTRY)
    traces, reads = {}, {}
    for name, schedule in SCENARIOS.items():
        states = {}

        def checkpoint(frame, private_ram):
            if WINDOW_START <= frame < WINDOW_END:
                states[frame] = decode(private_ram)

        result = run_core(core, meta, rom_data, rom_sha, FRAMES,
                          intro + schedule, private_rom_path=rom_path,
                          checkpoint_sink=checkpoint,
                          ram_window=(WINDOW_START, WINDOW_END))
        if result["frames_video_callbacks"] != FRAMES:
            raise RuntimeError(f"{name}: emulator frame count differs")
        traces[name] = states
        reads[name] = result["input_nonzero_reads"]
    return {"traces": traces, "reads": reads}


def measure(core_path, rom_path, expected_sha, repeats):
    if not 2 <= repeats <= 4:
        raise ValueError("--repeats must be between 2 and 4")
    core_sha = hashlib.sha256(core_path.read_bytes()).hexdigest()
    core, meta = setup_core(core_path)
    rom_data, rom_sha = safe_rom(rom_path, expected_sha)
    summary = validate_repetitions([
        capture(core, meta, rom_path, rom_data, rom_sha) for _ in range(repeats)
    ])
    if hashlib.sha256(core_path.read_bytes()).hexdigest() != core_sha:
        raise RuntimeError("Libretro core changed during measurement")
    return {
        "schema": "quackshot_genesis_motion_repeatability_v1",
        "evidence_type": "private_cold_boot_replay_numeric_only",
        "reference_rom_sha256": rom_sha,
        "core_binary_sha256": core_sha,
        "emulator_core": meta["library_name"] + " " + meta["library_version"],
        "first_action_frame": FRAME_START,
        "window": [WINDOW_START, WINDOW_END],
        **summary,
        "limits": [
            "Hashes describe only eight decoded motion fields, not all game state",
            "No proof of frame-exact BK2 playback or physics on other maps",
            "New reversal and eight-frame jump cases check repeatability, not reference equivalence",
            "No ROM, RAM dumps, captured images, audio, or raw input logs exported",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.output and args.output.resolve() in (args.core.resolve(), args.rom.resolve()):
            raise ValueError("Output would overwrite a private input")
        result = measure(args.core, args.rom, args.expected_sha256, args.repeats)
        report = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(report, encoding="utf-8")
        else:
            print(report, end="")
    except (ValueError, OSError, RuntimeError, KeyError, TypeError) as error:
        parser.exit(1, f"Genesis motion repeatability: {error}\n")


if __name__ == "__main__":
    main()
