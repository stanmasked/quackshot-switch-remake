#!/usr/bin/env python3
"""Stage206: ROM-free *compiled Genesis Plus GX* blue-component smoke test.

The only program permitted here is the already-published CI-authored
Motorola 68000 infinite loop, never the user's original QuackShot ROM.
No original game graphics, pixel bytes, RAM, or audio are exported.
"""
import argparse
import hashlib
from pathlib import Path

from ci_genesis_vdp_video_callback_smoke import (
    require_synthetic_rom, SAMPLES, ROI,
)
from genesis_libretro_probe import run_core, safe_rom, setup_core
from genesis_city_visual_roi_probe import validate_component_sample

EXPECTED_CI_ROM_SHA256 = "816e5478f34c213612760fae8f724e1919e633ce0cd8bdbaa03ce13658acad00"


def require_stage206_fixture(data):
    """The full 128KiB loop must match the deterministic workflow bytes."""
    require_synthetic_rom(data)
    require(hashlib.sha256(data).hexdigest() == EXPECTED_CI_ROM_SHA256,
            "CI-authored synthetic ROM checksum changed")
    return None


ALLOWED_SAMPLE_KEYS = {
    "frame", "schema", "roi", "pixel_format", "connectivity",
    "min_component_pixels", "blue_pixel_count",
    "all_blue_component_count", "eligible_component_count",
    "reported_component_count", "largest_component_dominates_by_2x",
    "components", "selected_component_is_proven_player",
    "original_sprite_hitbox_or_contact_verified",
    "safe_to_modify_switch_physics",
}


def require(value, reason):
    if not value:
        raise ValueError("ROM-free compiled blue-components smoke: " + reason)


def verify_numeric_report(result):
    """Never serialize transient RGB or raw memory into a research report."""
    require(type(result) is dict and
            result.get("frames_video_callbacks") == 90 and
            result.get("frames_without_video_callback") == 0 and
            result.get("frames_with_multiple_video_callbacks") == 0 and
            result.get("video_callbacks_outside_retro_run") == 0 and
            result.get("video_callback_phase") == "during_retro_run",
            "video cadence or callback phase differs")
    visual = result.get("video_roi_blue_numeric")
    rows = result.get("video_roi_components_numeric")
    require(type(visual) is list and type(rows) is list and
            len(visual) == len(rows) == len(SAMPLES) and
            [x.get("frame") for x in visual] == list(SAMPLES) and
            [x.get("frame") for x in rows] == list(SAMPLES),
            "wrong callback frame set or duplicate")
    numeric = []
    for old, component in zip(visual, rows):
        require(type(component) is dict and
                set(component) == ALLOWED_SAMPLE_KEYS and
                component["frame"] == old["frame"] and
                type(old.get("blue_pixel_count")) is int,
                "extra or corrupted component data")
        sample = {key: value for key, value in component.items()
                  if key != "frame"}
        validate_component_sample(sample, ROI, old["blue_pixel_count"])
        numeric.append((
            component["frame"], component["blue_pixel_count"],
            component["all_blue_component_count"],
            component["eligible_component_count"],
            component["reported_component_count"],
            tuple((c["blue_pixels"], c["blue_centroid_x_milli"],
                   c["blue_centroid_y_milli"],
                   tuple(c["blue_bbox_xyxy_exclusive"]),
                   c["touches_roi_boundary"])
                  for c in component["components"]),
        ))
    checksums = result.get("video_checkpoints")
    require(type(checksums) is list and len(checksums) == 90 and
            all(type(x) is dict and
                type(x.get("frame")) is int and
                type(x.get("crc32")) is str
                for x in checksums),
            "missing synthetic video CRC history")
    return {
        "components": tuple(numeric),
        "video_crc": tuple((x["frame"], x["crc32"]) for x in checksums),
    }


def repeatable_synthetic_runs(core, metadata, rom, sha, rompath):
    """Run real compiled core twice on artificial bytes, compare only numbers."""
    require_stage206_fixture(rom)
    snapshots = []
    for _ in range(2):
        sample = run_core(
            core, metadata, rom, sha, 90, [], private_rom_path=rompath,
            video_stride=1, video_roi=ROI,
            video_roi_frames=SAMPLES, video_roi_components=True)
        snapshots.append(verify_numeric_report(sample))
    require(snapshots[0] == snapshots[1],
            "two compiled synthetic video and color traces disagree")
    return {
        "real_compiled_libretro_core_tested": True,
        "source_game_rom_used": False,
        "original_sprite_or_hitbox_identified": False,
        "original_collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
        "synthetic_independent_runs": 2,
        "selected_color_samples": len(SAMPLES),
        "repeatable_numeric_components": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    args = parser.parse_args()
    try:
        # Reject any non-synthetic ROM *before* loading the libretro library.
        rom, digest = safe_rom(args.rom, None)
        require_stage206_fixture(rom)
        core, meta = setup_core(args.core)
        report = repeatable_synthetic_runs(core, meta, rom, digest, args.rom)
        require(report["repeatable_numeric_components"] and
                not report["source_game_rom_used"] and
                not report["safe_to_modify_switch_physics"],
                "unsafe original-game claim")
        print("GENESIS_STAGE206_REAL_CORE_BLUE_COMPONENTS_SYNTHETIC_PASS")
    except (OSError, RuntimeError, ValueError, TypeError, KeyError,
            OverflowError):
        parser.exit(1, "GENESIS_STAGE206_REAL_CORE_BLUE_COMPONENTS_SYNTHETIC_FAIL\n")


if __name__ == "__main__":
    main()
