#!/usr/bin/env python3
"""Stage202: run the *real* instrumented Genesis Plus GX hook on an invented ROM.

The ROM must be the CI-authored 68000 idle loop. This smoke check deliberately
REFUSES any original QuackShot ROM. It exercises the actual ctypes video
callback -> HScroll hook -> end-of-step path. Reports only PASS/FAIL.
"""
import argparse
from pathlib import Path

from genesis_libretro_probe import run_core, safe_rom, setup_core
from genesis_vdp_scroll_probe import SCANLINES, bind_vdp_probe, vdp_number_snapshot

SAMPLES = (0, 1, 30, 60, 89)
ROI = (0, 0, 16, 16)
EXPECTED_SYNTHETIC_HEADER = b"OPENAI TEST ONLY NO GAME ASSETS"


def require(ok, reason):
    if not ok:
        raise ValueError("ROM-free VDP video callback smoke: " + reason)


def require_synthetic_rom(data):
    require(type(data) is bytes and len(data) == 128 * 1024,
            "fixture must be exactly 128KiB, not an original game")
    require(data[0x120:0x150].startswith(EXPECTED_SYNTHETIC_HEADER) and
            data[0x200:0x202] == b"\x60\xfe" and
            data[0x100:0x104] == b"SEGA",
            "fixture not the CI-authored synthetic endless loop")
    return None


def numeric_scroll(hook):
    rows = [vdp_number_snapshot(hook, line) for line in SCANLINES]
    return tuple(
        [r["plane_A_HScroll_11bit_signed"] for r in rows] +
        [r["plane_B_HScroll_11bit_signed"] for r in rows]
    )


def run_once(core, metadata, rom_bytes, rom_sha, rom_path, hook):
    after = {}
    callback_order = []
    def on_video(frame):
        callback_order.append(("video", frame))
        return numeric_scroll(hook)
    def on_ram(frame, ram):
        if frame in SAMPLES:
            require(len(ram) == 65536, "unexpected synthetic RAM size")
            after[frame] = numeric_scroll(hook)
            callback_order.append(("after", frame))
    result = run_core(
        core, metadata, rom_bytes, rom_sha, 90, [], private_rom_path=rom_path,
        video_stride=1, ram_window=(0, 90), checkpoint_sink=on_ram,
        video_roi=ROI, video_roi_frames=SAMPLES,
        video_roi_vdp_hook=on_video)
    require(result["frames_video_callbacks"] == 90 and
            result["frames_without_video_callback"] == 0 and
            result["frames_with_multiple_video_callbacks"] == 0 and
            result["video_callbacks_outside_retro_run"] == 0 and
            result["ram_checkpoint_phase"] == "post_retro_run_before_next_input" and
            result["video_callback_phase"] == "during_retro_run",
            "real core did not preserve callback phase/cadence")
    video = result["video_roi_vdp_numeric"]
    visual = result["video_roi_blue_numeric"]
    require(type(video) is list and type(visual) is list and
            [x["frame"] for x in video] == list(SAMPLES) and
            [x["frame"] for x in visual] == list(SAMPLES) and
            set(after) == set(SAMPLES), "real callback sideband frames incomplete")
    for row in video:
        require(set(row) == {"frame", "plane_scroll"} and
                type(row["plane_scroll"]) is list and
                len(row["plane_scroll"]) == 8 and
                all(type(v) is int and -1024 <= v <= 1023
                    for v in row["plane_scroll"]),
                "callback sideband contains nonnumeric/unsafe data")
    expected_order = [record for f in SAMPLES
                      for record in (("video", f), ("after", f))]
    require(callback_order == expected_order,
            "real callback VDP and post-run RAM phase ordering differs")
    require(len(result["video_checkpoints"]) == 90,
            "real synthetic video CRC cadence changed")
    return {
        "video": [(x["frame"], tuple(x["plane_scroll"])) for x in video],
        "after": [(f, after[f]) for f in SAMPLES],
        "blue": [(x["frame"], x["blue_pixel_count"],
                  x["blue_centroid_x_milli"], x["blue_centroid_y_milli"])
                 for x in visual],
        "video_crc": [(x["frame"], x["crc32"]) for x in
                      result["video_checkpoints"]],
    }


def verify_repeatability(core, metadata, data, sha, rompath):
    require_synthetic_rom(data)
    hook = bind_vdp_probe(core)
    first = run_once(core, metadata, data, sha, rompath, hook)
    second = run_once(core, metadata, data, sha, rompath, hook)
    require(first == second,
            "two independent synthetic real-core runs disagree")
    return {
        "real_instrumented_core_used": True,
        "source_game_rom_used": False,
        "independent_synthetic_runs": 2,
        "selected_video_callback_samples": len(SAMPLES),
        "video_callbacks_each_run": 90,
        "numerical_video_and_vdp_repeatability": True,
        "original_cpu_vdp_scanout_phase_verified": False,
        "original_game_hitbox_verified": False,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--core", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    args = p.parse_args()
    try:
        data, sha = safe_rom(args.rom, None)
        require_synthetic_rom(data)
        core, metadata = setup_core(args.core)
        result = verify_repeatability(core, metadata, data, sha, args.rom)
        require(result["numerical_video_and_vdp_repeatability"] and
                not result["source_game_rom_used"],
                "synthetic smoke result not secure")
        print("GENESIS_STAGE202_REAL_CORE_VDP_CALLBACK_SYNTHETIC_PASS")
    except (OSError, RuntimeError, ValueError, KeyError, TypeError):
        p.exit(1, "GENESIS_STAGE202_REAL_CORE_VDP_CALLBACK_SYNTHETIC_FAIL\n")


if __name__ == "__main__":
    main()
