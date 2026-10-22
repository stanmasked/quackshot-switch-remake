#!/usr/bin/env python3
"""Stage208: read-only offline numeric witness audit for private Genesis reports.

Validates bounded JSON with strict fields against published numeric baselines
and historical SHA pins. It does NOT access a ROM, start libretro, reproduce an
emulation session, authenticate the report producer, or determine a hitbox.
Never print original measurement values or private paths.
"""
import argparse
import json
from pathlib import Path
import stat

from genesis_blue_roi_numeric import validate_roi
from genesis_city_same_step_triplet_probe import (
    EXPECTED_X, EXPECTED_Y, INSTRUMENTED_CORE_SHA, MARKERS, ROM_SHA,
)
from genesis_private_bundle_launcher import (
    COLOR_FRAME_FIELDS, COLOR_TRIPLET_FIELDS, _reject_public_checkout_path,
    _validate_component_report, preflight,
)

MAX_JSON_BYTES = 128 * 1024
BASE_TRIPLET_FIELDS = COLOR_TRIPLET_FIELDS - {
    "visual_component_candidates", "component_identity_unverified",
}
BRACKET_FIELDS = {
    "schema", "original_rom_executed_locally", "reference_rom_sha256",
    "instrumented_core_sha256", "three_runs_repeated_exact",
    "triplet_sample_frames", "vdp_callback_vs_post_run",
    "original_collision_plane_verified",
    "original_cpu_vdp_scanout_phase_verified",
    "original_media_or_raw_memory_exported",
    "safe_to_modify_switch_physics",
}
BRACKET_FIELDS_NESTED = {
    "schema", "status", "vdp_video_callback_phase",
    "vdp_ram_checkpoint_phase", "checks",
    "changed_hscroll_at_any_observation_boundary",
    "equal_values_prove_same_cpu_vdp_scanout_phase",
    "original_collision_or_sprite_identity_verified",
    "safe_to_modify_switch_physics",
}
BRACKET_ROW_FIELDS = {
    "libretro_frame",
    "all_plane_scroll_values_equal_at_two_observation_points",
    "plane_a_deltas_by_scanline", "plane_b_deltas_by_scanline",
}


def require(ok, reason):
    if not ok:
        raise ValueError("Private Genesis numeric receipt: " + reason)


def validate_frame_rows(rows, roi=None):
    require(type(rows) is list and len(rows) == len(MARKERS),
            "private numerical frame count differs")
    if roi is not None:
        x0, y0, width, height = validate_roi(roi)
        max_count = width * height
    else:
        x0 = y0 = 0
        width = height = 2048
        max_count = 1024 * 1024
    for step, row in zip(MARKERS, rows):
        require(type(row) is dict and set(row) == COLOR_FRAME_FIELDS and
                type(row["frame"]) is int and row["frame"] == step,
                "private numeric frames missing or extra source fields")
        for key in ("player_world_x", "player_world_y",
                    "software_camera_ram", "render_buffer_ram",
                    "vdp_plane_a_hscroll", "vdp_screen_x_candidate",
                    "blue_pixel_count"):
            require(type(row[key]) is int, "noninteger original numeric field")
        require(row["player_world_x"] == EXPECTED_X[step] and
                row["player_world_y"] == EXPECTED_Y[step] and
                0 <= row["software_camera_ram"] < 1024 and
                0 <= row["render_buffer_ram"] < 1024 and
                -1024 <= row["vdp_plane_a_hscroll"] <= 1023 and
                row["vdp_screen_x_candidate"] ==
                row["player_world_x"] + row["vdp_plane_a_hscroll"] and
                0 <= row["blue_pixel_count"] <= max_count,
                "private original landmark or VDP equation differs")
        cx = row["blue_centroid_x_milli"]
        cy = row["blue_centroid_y_milli"]
        dx = row["blue_minus_vdp_candidate_x_milli"]
        if row["blue_pixel_count"] == 0:
            require(cx is None and cy is None and dx is None,
                    "empty original color sample claims a position")
        else:
            require(all(type(n) is int for n in (cx, cy, dx)) and
                    x0 * 1000 <= cx < (x0 + width) * 1000 and
                    y0 * 1000 <= cy < (y0 + height) * 1000 and
                    dx == cx - 1000 * row["vdp_screen_x_candidate"],
                    "color centroid or VDP x delta inconsistent")


def audit_triplet(report):
    require(type(report) is dict and
            report.get("schema") ==
            "quackshot_private_genesis_ram_video_vdp_same_step_v1",
            "unknown numeric triplet report")
    has_components = "visual_component_candidates" in report
    fields = COLOR_TRIPLET_FIELDS if has_components else BASE_TRIPLET_FIELDS
    require(set(report) == fields and
            report["status"] ==
            "SAME_LIBRETRO_STEP_ONLY_NOT_ORIGINAL_SCANOUT_PHASE" and
            report["original_rom_executed_locally"] is True and
            report["independently_attested_by_report"] is False and
            report["original_media_raw_ram_vram_or_pixels_saved"] is False and
            report["independent_original_runs"] == 3 and
            report["repeated_right_numeric_and_video_identical"] is True and
            report["same_cpu_vdp_scanout_phase_verified"] is False and
            report["original_collision_hitbox_or_contact_plane_verified"] is False and
            report["safe_to_modify_switch_physics"] is False and
            report["video_callback_phase"] == "during_retro_run" and
            report["ram_and_vdp_sampling_phase"] ==
            "post_retro_run_before_next_input" and
            report["hardware_vdp_scanlines_verified"] == 120,
            "triplet schema or phase/collision disclaimer differs")
    roi = report["video_roi"]
    validate_roi(roi)
    require(type(roi) is list and
            (not has_components or roi[2] * roi[3] <= 65536),
            "connected-color report exceeded ROI budget")
    validate_frame_rows(report["frames"], roi=roi)
    if has_components:
        _validate_component_report(report, roi, True)
    return {"schema": report["schema"], "frames_checked": len(MARKERS),
            "local_provenance_independently_attested": False,
            "original_hitbox_verified": False,
            "safe_to_modify_switch_physics": False}


def audit_bracket(report):
    require(type(report) is dict and set(report) == BRACKET_FIELDS and
            report["schema"] ==
            "quackshot_genesis_video_hscroll_same_step_bracket_v1" and
            report["original_rom_executed_locally"] is True and
            report["three_runs_repeated_exact"] is True and
            report["original_collision_plane_verified"] is False and
            report["original_cpu_vdp_scanout_phase_verified"] is False and
            report["original_media_or_raw_memory_exported"] is False and
            report["safe_to_modify_switch_physics"] is False,
            "bracket report fields or unverifiable claims differ")
    validate_frame_rows(report["triplet_sample_frames"])
    bracket = report["vdp_callback_vs_post_run"]
    require(type(bracket) is dict and set(bracket) == BRACKET_FIELDS_NESTED and
            bracket["schema"] ==
            "quackshot_private_genesis_video_vdp_bracket_v1" and
            bracket["status"] ==
            "SAME_LIBRETRO_STEP_TWO_VDP_OBSERVATIONS_NOT_CPU_SCANOUT_ATTESTATION" and
            bracket["vdp_video_callback_phase"] ==
            "inside_retro_run_video_callback" and
            bracket["vdp_ram_checkpoint_phase"] ==
            "after_retro_run_before_next_input" and
            bracket["equal_values_prove_same_cpu_vdp_scanout_phase"] is False and
            bracket["original_collision_or_sprite_identity_verified"] is False and
            bracket["safe_to_modify_switch_physics"] is False,
            "bracket VDP phase/disclaimer mismatch")
    rows = bracket["checks"]
    require(type(rows) is list and len(rows) == len(MARKERS),
            "bracket frame count inconsistent")
    changed = False
    for frame, row in zip(MARKERS, rows):
        require(type(row) is dict and set(row) == BRACKET_ROW_FIELDS and
                type(row["libretro_frame"]) is int and
                row["libretro_frame"] == frame and
                type(row["all_plane_scroll_values_equal_at_two_observation_points"])
                is bool,
                "missing or malformed VDP bracket frame")
        delta = []
        for key in ("plane_a_deltas_by_scanline",
                    "plane_b_deltas_by_scanline"):
            nums = row[key]
            require(type(nums) is list and len(nums) == 4 and
                    all(type(n) is int and -2047 <= n <= 2047 for n in nums),
                    "invalid delta from numeric VDP sample")
            delta.extend(nums)
        same = all(d == 0 for d in delta)
        require(row["all_plane_scroll_values_equal_at_two_observation_points"]
                is same, "incorrect VDP equality status")
        changed |= not same
    require(bracket["changed_hscroll_at_any_observation_boundary"] is changed,
            "overall VDP bracket difference does not match five rows")
    return {"schema": report["schema"], "frames_checked": len(MARKERS),
            "local_provenance_independently_attested": False,
            "original_hitbox_verified": False,
            "safe_to_modify_switch_physics": False}


def audit_report(report):
    require(type(report) is dict, "report is not a JSON object")
    require(report.get("reference_rom_sha256") == ROM_SHA and
            report.get("instrumented_core_sha256") == INSTRUMENTED_CORE_SHA,
            "wrong original ROM or instrumented core provenance label")
    if report.get("schema") == "quackshot_private_genesis_ram_video_vdp_same_step_v1":
        return audit_triplet(report)
    if report.get("schema") == "quackshot_genesis_video_hscroll_same_step_bracket_v1":
        return audit_bracket(report)
    raise ValueError("Private Genesis numeric receipt: unsupported schema")


def read_private_report(bundle, report_path):
    path = Path(report_path)
    root = Path(bundle)
    require(path.is_absolute() and path.suffix.lower() == ".json" and
            path.is_file() and not path.is_symlink() and
            root.resolve() not in path.resolve().parents,
            "report requires an existing private absolute JSON path")
    _reject_public_checkout_path(root, path)
    info = path.stat()
    require(stat.S_ISREG(info.st_mode) and
            stat.S_IMODE(info.st_mode) & 0o077 == 0 and
            1 <= info.st_size <= MAX_JSON_BYTES,
            "private report size or permissions unsafe")
    with path.open("rb") as stream:
        raw = stream.read(MAX_JSON_BYTES + 1)
    require(len(raw) == info.st_size and len(raw) <= MAX_JSON_BYTES,
            "private report changed while reading")
    return json.loads(raw.decode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        preflight(args.bundle)
        report = read_private_report(args.bundle, args.report)
        audit_report(report)
        print("GENESIS_STAGE208_NUMERIC_REPORT_SCHEMA_PASS_NOT_ORIGIN_ATTESTED")
    except (ValueError, OSError, RuntimeError, TypeError, KeyError,
            UnicodeError, json.JSONDecodeError, OverflowError):
        parser.exit(1, "GENESIS_STAGE208_NUMERIC_REPORT_REJECTED\n")


if __name__ == "__main__":
    main()
