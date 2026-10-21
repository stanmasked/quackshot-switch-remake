#!/usr/bin/env python3
"""Private, read-only Genesis first-city subpixel fall event probe.

The source ROM and libretro core MUST remain local. Only a compact,
numeric event summary leaves this function: no raw RAM, frame arrays,
pixels, sound, savestates, ROM bytes or controller log is serialized.
"""
import argparse
from pathlib import Path

from genesis_city_obstacle_probe import (
    FIRST, LAST_EXCLUSIVE, RUNS, TOTAL, decode
)
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY
from private_genesis_input_guard import PinnedOriginalInputs
from private_genesis_report_output import write_private_numeric_report
from reference_evidence_integrity import read_inputs, validate

Q16 = 65536
START_WORLD_Y = 176
LOWER_WORLD_Y = 208
EXPECTED_ROM_SHA256 = (
    "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
)
OBSERVED_INTEGER_FIRST_FALL = 2281
OBSERVED_INTEGER_FIRST_LOWER_FLOOR = 2297
FRAME_RANGE = range(FIRST, LAST_EXCLUSIVE)


def require(condition, message):
    if not condition:
        raise ValueError("Private Genesis fractional fall: " + message)


def raw_y16(state):
    """Reduce decoded numeric fields to one signed-free first-city Y value."""
    require(isinstance(state, dict), "decoded state is not an object")
    y, sub = state.get("y"), state.get("y_fraction")
    require(type(y) is int and 0 <= y <= 65535, "invalid Y integer word")
    require(type(sub) is int and 0 <= sub < Q16, "invalid Y fraction word")
    return y * Q16 + sub


def first_frame_above(series, floor):
    return next((frame for frame in FRAME_RANGE
                 if raw_y16(series[frame]) > floor), None)


def analyze(right, right_repeat, idle, input_deltas):
    """Summarize causal Y thresholds; never return raw samples."""
    expected = set(FRAME_RANGE)
    require(set(right) == expected and set(right_repeat) == expected and
            set(idle) == expected, "missing or extra private sample frames")
    require(right == right_repeat, "repeated original-game run diverged")
    require(input_deltas.get("right") == 450 and
            input_deltas.get("right_repeat") == 450 and
            input_deltas.get("idle") == 0,
            "original joypad callbacks did not match the scripted run")

    upper = START_WORLD_Y * Q16
    lower = LOWER_WORLD_Y * Q16
    require(all(raw_y16(idle[f]) == upper for f in FRAME_RANGE),
            "idle first-city baseline shifted")
    require(right[2280]["y"] == START_WORLD_Y and
            right[2281]["y"] == START_WORLD_Y + 1 and
            right[OBSERVED_INTEGER_FIRST_LOWER_FLOOR]["y"] ==
            LOWER_WORLD_Y, "published original integer landmarks changed")

    first_positive = first_frame_above(right, upper)
    quarter = first_frame_above(right, upper + Q16 // 4)
    one = first_frame_above(right, upper + Q16)
    integer = next((f for f in FRAME_RANGE
                    if right[f]["y"] > START_WORLD_Y), None)
    lower_integer = next((f for f in FRAME_RANGE
                          if right[f]["y"] == LOWER_WORLD_Y), None)
    lower_exact = next((f for f in FRAME_RANGE
                        if raw_y16(right[f]) == lower), None)
    require(all(x is not None for x in (
        first_positive, quarter, one, integer, lower_integer)),
        "could not establish fall and floor thresholds")
    require(FIRST <= first_positive <= quarter <= integer <= one <
            lower_integer, "inconsistent fractional-event chronology")
    require(integer == OBSERVED_INTEGER_FIRST_FALL and
            lower_integer == OBSERVED_INTEGER_FIRST_LOWER_FLOOR,
            "integer baseline no longer matches independent evidence")
    require(first_positive >= 2260,
            "unexpected Y motion before ledge approach")
    require(lower_exact is None or lower_exact >= lower_integer,
            "exact lower-floor state precedes integer contact")
    require(all(raw_y16(right[f]) <= lower for f in range(2260, 2300)),
            "unexpected penetration below the lower floor")

    # A single integer Y=176 frame may still have a nonzero 16.16
    # fraction. Publishing the scalar alone does not publish RAM bytes.
    fractional_at_2280 = right[2280]["y_fraction"]
    return {
        "schema": "quackshot_genesis_fractional_fall_event_v1",
        "status": "NUMERIC_SERIES_ANALYSIS_UNVERIFIED",
        "original_rom_executed_locally": False,
        "raw_ram_saved": False,
        "raw_frames_saved": False,
        "rom_or_emulator_payload_in_report": False,
        "world_y_integer_upper": START_WORLD_Y,
        "world_y_integer_lower": LOWER_WORLD_Y,
        "first_y_positive_subpixel_frame": first_positive,
        "first_y_above_quarter_pixel_frame": quarter,
        "first_y_above_one_pixel_frame": one,
        "first_integer_y_increase_frame": integer,
        "first_integer_lower_floor_frame": lower_integer,
        "first_exact_16_16_lower_floor_frame": lower_exact,
        "fractional_y_units_at_integer_176_frame_2280":
            fractional_at_2280,
        "fall_duration_by_threshold_frames": {
            "positive_subpixel_to_integer_floor":
                lower_integer - first_positive,
            "over_quarter_to_integer_floor": lower_integer - quarter,
            "over_one_to_integer_floor": lower_integer - one,
            "integer_change_to_integer_floor": lower_integer - integer,
        },
        "frame_label_caveat": (
            "libretro checkpoint callback labels are NOT proof of the "
            "original 68000 update phase, display scanout phase, or "
            "the moment of a rendered sprite collision."
        ),
        "scope": [
            "One SHA-pinned original first-city RIGHT run, repeated identically",
            "Only integer/fraction numeric thresholds; original collision shape unknown",
            "No claim of per-frame parity with independently authored Switch tiles",
        ],
    }


def measure(core, metadata, private_rom_path, rom_bytes, verified_sha, *,
            verify_inputs):
    require(verified_sha == EXPECTED_ROM_SHA256,
            "ROM SHA differs from the observed original version")
    require(callable(verify_inputs), "private input guard must be callable")
    intro = parse_script(DEFAULT_ENTRY)
    traces, videos, read_counts = {}, {}, {}
    for name in ("idle", "right", "right_repeat"):
        series = {}

        def on_private_ram(frame, ram):
            if FIRST <= frame < LAST_EXCLUSIVE:
                series[frame] = decode(ram)

        # Check both local files around *every* independent emulation run.
        # Do this again after a failed run, not just a successful return.
        verify_inputs()
        try:
            result = run_core(
                core, metadata, rom_bytes, verified_sha, TOTAL,
                intro + RUNS[name], private_rom_path=private_rom_path,
                ram_window=(FIRST, LAST_EXCLUSIVE),
                checkpoint_sink=on_private_ram, video_stride=60
            )
        finally:
            verify_inputs()
        require(result["frames_video_callbacks"] == TOTAL,
                "original emulator stopped early")
        require(result.get("ram_checkpoint_phase") ==
                "post_retro_run_before_next_input" and
                result.get("video_callback_phase") == "during_retro_run",
                "emulator checkpoint phase contract is absent or incompatible")
        require(result.get("frames_without_video_callback") == 0 and
                result.get("frames_with_multiple_video_callbacks") == 0 and
                result.get("video_callbacks_outside_retro_run") == 0,
                "emulator video callback cadence is not one per retro_run")
        traces[name] = series
        videos[name] = result["video_checkpoints"]
        read_counts[name] = result["input_nonzero_reads"]
    require(videos["right"] == videos["right_repeat"],
            "repeated original framebuffer digests diverged")
    deltas = {
        name: read_counts[name] - read_counts["idle"]
        for name in ("idle", "right", "right_repeat")
    }
    summary = analyze(traces["right"], traces["right_repeat"],
                      traces["idle"], deltas)
    summary["reference_sha256"] = verified_sha
    summary["status"] = "PRIVATE_ORIGINAL_EMULATION_NUMERIC_ONLY"
    summary["original_rom_executed_locally"] = True
    summary["emulator"] = (
        metadata["library_name"] + " " + metadata["library_version"]
    )
    summary["independent_original_runs"] = 3
    summary["ram_checkpoint_phase"] = "post_retro_run_before_next_input"
    summary["video_callback_phase"] = "during_retro_run"
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--core", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--expected-sha256",
                    default=EXPECTED_ROM_SHA256)
    ap.add_argument("--output", type=Path, required=True,
                    help="Absolute private .json path outside the repository")
    args = ap.parse_args()
    try:
        require(args.expected_sha256 == EXPECTED_ROM_SHA256,
                "expected original ROM SHA must stay pinned")
        # Report output is never streamed to stdout or a public work tree.
        # The writer rejects existing files, symlinks and relative paths.
        # Pin against existing numeric-only evidence *before loading* the core.
        # This does not grant independent provenance to exported JSON.
        provenance = validate(*read_inputs(Path(__file__).resolve().parents[1]))
        guard = PinnedOriginalInputs(
            args.rom, args.core, EXPECTED_ROM_SHA256,
            provenance["genesis_core_sha256"])
        guard()
        core, metadata = setup_core(args.core)
        rom_bytes, sha = safe_rom(args.rom, args.expected_sha256)
        guard()
        report = measure(core, metadata, args.rom, rom_bytes, sha,
                         verify_inputs=guard)
        guard()
        report["core_binary_sha256"] = provenance["genesis_core_sha256"]
        # Fail-closed Stage189 schema/provenance/phase verification precedes
        # writing; no invalid or partial numeric report is exported.
        write_private_numeric_report(
            report, args.output, Path(__file__).resolve().parents[1])
    except OSError:
        ap.exit(1, "Private fractional fall probe failed: local I/O error "
                   "(private paths redacted)\n")
    except (ValueError, RuntimeError, KeyError) as exc:
        ap.exit(1, "Private fractional fall probe failed: " + str(exc) + "\n")


if __name__ == "__main__":
    main()
