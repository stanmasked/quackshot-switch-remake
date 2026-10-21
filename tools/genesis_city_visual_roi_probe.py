#!/usr/bin/env python3
"""Stage198 private Genesis color-ROI versus RAM Y numeric witness.

This is a visual *heuristic*, not a source-game hitbox or contact plane.
The original ROM/core remain local; no pixels, audio or raw RAM are saved.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import tempfile

from genesis_city_obstacle_probe import RUNS, TOTAL
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY
from genesis_blue_roi_numeric import validate_roi
from private_genesis_input_guard import PinnedOriginalInputs
from private_genesis_report_output import private_output_path
from reference_evidence_integrity import read_inputs, validate

EXPECTED_ROM = "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
MARKERS = (2180, 2220, 2280, 2297, 2350)
EXPECTED_Y = {2180: 176, 2220: 176, 2280: 176, 2297: 208, 2350: 208}
DEFAULT_ROI = (100, 80, 85, 140)


def require(value, reason):
    if not value:
        raise ValueError("Private Genesis visual ROI: " + reason)


def summarize(traces, read_counts, video_digests, roi, sha, core_sha):
    """Validate independent repeats using only integer Y and blue ROI stats."""
    require(set(traces) == {"idle", "right", "right_repeat"},
            "missing original-run numerical summaries")
    require(set(read_counts) == set(traces) and
            read_counts["right"] - read_counts["idle"] == 450 and
            read_counts["right_repeat"] - read_counts["idle"] == 450,
            "scripted controller reads are not 450")
    require(video_digests["right"] == video_digests["right_repeat"],
            "independent video digests differ")
    for name, frame_map in traces.items():
        require(set(frame_map) == set(MARKERS),
                name + " contains missing/extra observation frames")
        for frame, metric in frame_map.items():
            require(type(metric) is dict and
                    set(metric) == {"player_ram_y", "visual"},
                    "unexpected original payload in numeric frames")
            require(type(metric["player_ram_y"]) is int and
                    0 <= metric["player_ram_y"] <= 65535,
                    "invalid original RAM-Y number")
            visual = metric["visual"]
            require(type(visual) is dict and
                    visual.get("roi") == list(roi) and
                    visual.get("source_collision_plane_measured") is False and
                    type(visual.get("blue_pixel_count")) is int and
                    0 <= visual["blue_pixel_count"] <= roi[2] * roi[3],
                    "invalid visual ROI numbers or collision claim")
    require(traces["right"] == traces["right_repeat"],
            "two original right-run visual/RAM observations diverged")
    require(all(traces["right"][frame]["player_ram_y"] == y
                for frame, y in EXPECTED_Y.items()),
            "original city integer Y landmarks disagree with published data")
    return {
        "schema": "quackshot_private_genesis_visual_roi_numeric_v1",
        "status": "PRIVATE_BLUE_PIXEL_ROI_HEURISTIC_NOT_COLLISION_MEASUREMENT",
        "original_rom_executed_locally": True,
        "independently_attested_by_this_report": False,
        "original_pixels_or_raw_ram_saved": False,
        "reference_sha256": sha,
        "core_binary_sha256": core_sha,
        "independent_original_runs": 3,
        "repeated_right_video_and_numeric_identical": True,
        "ram_sampling_phase": "post_retro_run_before_next_input",
        "video_sampling_phase": "during_retro_run",
        "roi": list(roi),
        "sample_frames": [
            {"frame": frame, **traces["right"][frame]}
            for frame in MARKERS
        ],
        "original_collision_plane_verified": False,
        "original_sprite_identity_proven_from_blue_pixels": False,
        "safe_to_modify_switch_physics": False,
        "caveat": (
            "A blue-color centroid may include scenery, animation or palette "
            "changes; the RAM Y and screen ROI do not share a verified camera "
            "origin, and no hitbox or foot contact was observed."
        ),
    }


def measure(core, metadata, rompath, rom_bytes, sha, core_sha, roi, *,
            verify_inputs):
    require(callable(verify_inputs), "pinned input guard is required")
    validate_roi(roi)
    intro = parse_script(DEFAULT_ENTRY)
    traces, read_counts, digests = {}, {}, {}
    for name in ("idle", "right", "right_repeat"):
        ram_y = {}

        def on_ram(frame, ram):
            if frame in MARKERS:
                require(len(ram) == 65536,
                        "unexpected Genesis system RAM size")
                ram_y[frame] = int.from_bytes(ram[0xA2D4:0xA2D6],
                                             "little")
        verify_inputs()
        try:
            out = run_core(
                core, metadata, rom_bytes, sha, TOTAL, intro + RUNS[name],
                private_rom_path=rompath, video_stride=60,
                ram_window=(MARKERS[0], MARKERS[-1] + 1),
                checkpoint_sink=on_ram,
                video_roi=roi, video_roi_frames=MARKERS)
        finally:
            verify_inputs()
        require(out["frames_video_callbacks"] == TOTAL and
                all(out.get(k) == 0 for k in (
                    "frames_without_video_callback",
                    "frames_with_multiple_video_callbacks",
                    "video_callbacks_outside_retro_run")),
                "libretro video callback cadence differs")
        require(out.get("ram_checkpoint_phase") ==
                "post_retro_run_before_next_input",
                "unexpected original RAM sampling phase")
        read_counts[name] = out["input_nonzero_reads"]
        digests[name] = out["video_checkpoints"]
        visual = {entry["frame"]: {
            key: value for key, value in entry.items() if key != "frame"
        } for entry in out["video_roi_blue_numeric"]}
        require(set(ram_y) == set(MARKERS) and set(visual) == set(MARKERS),
                "missing numeric RAM or visual frame")
        traces[name] = {frame: {
            "player_ram_y": ram_y[frame], "visual": visual[frame]}
            for frame in MARKERS}
    return summarize(traces, read_counts, digests, roi, sha, core_sha)


def private_write(report, destination, root):
    target = private_output_path(destination, root)
    text = json.dumps(report, sort_keys=True, indent=2,
                      ensure_ascii=False) + "\n"
    tmp = None
    try:
        fd, tmp = tempfile.mkstemp(prefix=".qs-visual198-", suffix=".tmp",
                                   dir=target.parent)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            os.fchmod(f.fileno(), stat.S_IRUSR | stat.S_IWUSR)
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.link(tmp, target, follow_symlinks=False)
    except OSError:
        raise ValueError("Private Genesis visual ROI: private output failed") from None
    finally:
        if tmp:
            try:
                os.unlink(tmp)
            except FileNotFoundError:
                pass


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--core", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--roi", type=int, nargs=4, default=DEFAULT_ROI,
                   metavar=("X", "Y", "WIDTH", "HEIGHT"))
    args = p.parse_args()
    try:
        root = Path(__file__).resolve().parents[1]
        roi = tuple(args.roi)
        validate_roi(roi)
        provenance = validate(*read_inputs(root))
        guard = PinnedOriginalInputs(args.rom, args.core,
                                     EXPECTED_ROM,
                                     provenance["genesis_core_sha256"])
        guard()
        core, metadata = setup_core(args.core)
        rom_data, sha = safe_rom(args.rom, EXPECTED_ROM)
        guard()
        report = measure(core, metadata, args.rom, rom_data, sha,
                         provenance["genesis_core_sha256"], roi,
                         verify_inputs=guard)
        guard()
        private_write(report, args.output, root)
    except OSError:
        p.exit(1, "Private Genesis visual ROI: I/O failed (paths redacted)\n")
    except (ValueError, RuntimeError, KeyError, TypeError) as error:
        p.exit(1, "Private Genesis visual ROI: " + str(error) + "\n")


if __name__ == "__main__":
    main()
