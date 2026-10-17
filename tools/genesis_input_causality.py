#!/usr/bin/env python3
"""Compare actual Genesis emulator runs with matched, one-change input scripts.

Only numerical frame CRC32 / RAM CRC32 differences and input callback counts
are exported. Original ROM/RAM pixels, paths, sound, save states are private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zlib

from genesis_libretro_probe import (
    MAX_FRAMES, buttons_at, parse_script, run_core, safe_rom, setup_core,
)

LABEL = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,31}$")


def parse_variants(value):
    """Comma-separated NAME@start:end:BUTTON; exactly one extra input per run."""
    if not value:
        raise ValueError("At least one variant is required")
    result = []
    seen = set()
    for part in value.split(","):
        if part.count("@") != 1:
            raise ValueError("Use LABEL@start:end:BUTTON")
        name, script = part.split("@")
        if not LABEL.fullmatch(name) or name in seen:
            raise ValueError("Variant labels must be unique, bounded ASCII names")
        ranges = parse_script(script)
        if len(ranges) != 1:
            raise ValueError("Each variant must have exactly one input range")
        seen.add(name)
        result.append((name, script))
    if len(result) > 16:
        raise ValueError("At most 16 variants per experiment")
    return result


def compare_checkpoints(baseline, alternative):
    """Return first differing sampled frame and count; no RAM/video content."""
    if set(baseline) != set(alternative):
        raise ValueError("Cannot compare missing or differently sampled frames")
    changed = [f for f in sorted(baseline) if baseline[f] != alternative[f]]
    return {
        "sample_count": len(baseline),
        "changed_count": len(changed),
        "first_changed_frame": changed[0] if changed else None,
        "last_changed_frame": changed[-1] if changed else None,
        "sampled_frames": sorted(baseline),
    }


def summarize_run(report, ram_checksums, reference_video, reference_ram):
    images = {x["frame"]: x["crc32"] for x in report["video_checkpoints"]}
    if len(images) != len(report["video_checkpoints"]):
        raise ValueError("Duplicate video checkpoint")
    video_result = compare_checkpoints(reference_video, images)
    ram_result = compare_checkpoints(reference_ram, ram_checksums)
    video_result.pop("sampled_frames")
    ram_result.pop("sampled_frames")
    return {
        "frames_emulated": report["frames_video_callbacks"],
        "actual_joypad_nonzero_reads": report["input_nonzero_reads"],
        "video_crc_differences": video_result,
        "work_ram_crc_differences": ram_result,
    }


def study(core, meta, private_rom, data, sha, frames, base_script, variants, stride):
    if frames < 1 or frames > MAX_FRAMES or not 1 <= stride <= 600:
        raise ValueError("Invalid emulated frames or checkpoint stride")
    baseline_schedule = parse_script(base_script)
    if any(stop > frames for start, stop, _ in baseline_schedule):
        raise ValueError("Baseline extends past experiment horizon")

    def execute(schedule):
        ram = {}
        def sample(frame, private_ram):
            # Private callback contains RAM bytes temporarily; only CRC is retained.
            ram[frame] = f"{zlib.crc32(private_ram) & 0xffffffff:08X}"
        r = run_core(core, meta, data, sha, frames, schedule,
                     private_rom_path=private_rom, checkpoint_sink=sample,
                     video_stride=stride)
        if r["frames_video_callbacks"] != frames:
            raise RuntimeError("Emulator did not present requested frame count")
        return r, ram

    baseline, ref_ram = execute(baseline_schedule)
    repeat, repeat_ram = execute(baseline_schedule)
    ref_video = {x["frame"]: x["crc32"] for x in baseline["video_checkpoints"]}
    baseline_repeat = summarize_run(repeat, repeat_ram, ref_video, ref_ram)
    if (baseline_repeat["video_crc_differences"]["changed_count"] or
            baseline_repeat["work_ram_crc_differences"]["changed_count"]):
        raise RuntimeError("Identical baseline replays diverged; reject conclusions")

    findings = []
    for name, script in variants:
        schedule = parse_script(script)
        if schedule[0][1] > frames:
            raise ValueError("Variant extends past experiment horizon")
        actual, ram = execute(baseline_schedule + schedule)
        stats = summarize_run(actual, ram, ref_video, ref_ram)
        stats["name"] = name
        stats["extra_input"] = {
            "start_frame": schedule[0][0], "end_exclusive": schedule[0][1],
            "button": schedule[0][2],
        }
        stats["additional_nonzero_reads"] = (
            actual["input_nonzero_reads"] - baseline["input_nonzero_reads"]
        )
        stats["response_detected_in_sampled_state"] = bool(
            stats["video_crc_differences"]["changed_count"] or
            stats["work_ram_crc_differences"]["changed_count"]
        )
        findings.append(stats)
    return {
        "schema": "quackshot_genesis_input_causality_v1",
        "type": "actual_private_rom_emulator_runs_numeric_only",
        "source_sha256": sha,
        "emulator_core_version": meta["library_version"],
        "frames_per_run": frames,
        "checkpoints_every_frames": stride,
        "baseline_input": [
            {"start_frame": a, "end_exclusive": b, "button": btn}
            for a, b, btn in baseline_schedule
        ],
        "baseline": {
            "video_callbacks": baseline["frames_video_callbacks"],
            "joypad_nonzero_reads": baseline["input_nonzero_reads"],
            "repeat_identical": True,
            "video_checkpoints": len(ref_video),
            "ram_checkpoints": len(ref_ram),
        },
        "variants": findings,
        "interpretation_limit": (
            "An identical video/RAM CRC at sparse sampling points does not "
            "prove that input was completely ignored between those points. "
            "An observed change does not identify player coordinates or "
            "prove an interactive gameplay state. No original bytes are output."
        ),
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--core", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--expected-sha256", required=True)
    p.add_argument("--frames", type=int, default=3700)
    p.add_argument("--base", default="450:465:START")
    p.add_argument("--variants", default=(
        "right_1300@1300:1370:RIGHT,left_3200@3200:3320:LEFT,"
        "start_3200@3200:3215:START"
    ))
    p.add_argument("--stride", type=int, default=60)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    try:
        core, metadata = setup_core(args.core)
        data, sha = safe_rom(args.rom, args.expected_sha256)
        variants = parse_variants(args.variants)
        result = study(core, metadata, args.rom, data, sha,
                       args.frames, args.base, variants, args.stride)
        output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
    except (OSError, ValueError, RuntimeError) as error:
        p.exit(1, f"Causal input probe: {error}\n")


if __name__ == "__main__":
    main()
