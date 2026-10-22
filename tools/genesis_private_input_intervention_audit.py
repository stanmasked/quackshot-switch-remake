#!/usr/bin/env python3
"""Stage210: offline audit of private original-game idle/RIGHT color interventions.

Checks only bounded numeric JSON from two ROIs. It does not load a ROM,
emulate the game, inspect pixels, identify Donald, attest the producing process,
or infer a source-game hitbox/contact plane.
"""
import argparse
from pathlib import Path

from genesis_city_same_step_triplet_probe import (
    EXPECTED_X, INSTRUMENTED_CORE_SHA, MARKERS, ROM_SHA,
)
from genesis_offline_numeric_report_audit import read_private_report
from genesis_private_bundle_launcher import preflight

SCHEMA = "quackshot_private_original_input_color_intervention_v1"
ROI = {
    "left": (40, 120, 32, 60),
    "right": (123, 120, 32, 60),
}
TOP_FIELDS = {
    "schema", "original_rom_executed_locally", "reference_rom_sha256",
    "research_core_sha256", "roi", "runs",
    "same_right_replay_exact", "independent_identity_or_hitbox_proven",
    "safe_to_modify_switch_physics",
}
RUN_NAMES = {"idle", "right", "right_repeat"}
ROW_FIELDS = {
    "frame", "world_x", "world_y", "camera", "render_buffer",
    "color_blue_pixel_count", "connected_color_groups",
    "largest_color_group",
}
COMPONENT_FIELDS = {
    "blue_bbox_xyxy_exclusive", "blue_centroid_x_milli",
    "blue_centroid_y_milli", "blue_pixels", "touches_roi_boundary",
}
EXPECTED_Y_RIGHT = {2180: 176, 2220: 176, 2280: 176, 2297: 208, 2350: 208}


def require(ok, message):
    if not ok:
        raise ValueError("Private Genesis input intervention: " + message)


def _int(value, low=0, high=65535):
    return type(value) is int and low <= value <= high


def _validate_component(component, roi, total_pixels):
    require(type(component) is dict and set(component) == COMPONENT_FIELDS,
            "unexpected color component fields")
    x0, y0, width, height = roi
    box = component["blue_bbox_xyxy_exclusive"]
    require(type(box) is list and len(box) == 4 and
            all(type(v) is int for v in box),
            "invalid color component bounds")
    x1, y1, x2, y2 = box
    require(x0 <= x1 < x2 <= x0 + width and
            y0 <= y1 < y2 <= y0 + height,
            "color component outside declared ROI")
    pixels = component["blue_pixels"]
    cx, cy = (component["blue_centroid_x_milli"],
              component["blue_centroid_y_milli"])
    require(_int(pixels, 1, width * height) and pixels <= total_pixels and
            type(cx) is int and x1 * 1000 <= cx < x2 * 1000 and
            type(cy) is int and y1 * 1000 <= cy < y2 * 1000 and
            component["touches_roi_boundary"] is False,
            "invalid or edge-clipped color component")


def _validate_rows(report, role):
    roi = ROI[role]
    runs = report["runs"]
    require(type(runs) is dict and set(runs) == RUN_NAMES,
            "idle/RIGHT/repeat runs incomplete")
    for name, rows in runs.items():
        require(type(rows) is list and len(rows) == len(MARKERS),
                "numeric frame count mismatch")
        for i, (frame, row) in enumerate(zip(MARKERS, rows)):
            require(type(row) is dict and set(row) == ROW_FIELDS and
                    type(row["frame"]) is int and row["frame"] == frame,
                    "unexpected row fields or frame sequence")
            expected_x = 61 if name == "idle" else EXPECTED_X[frame]
            expected_y = 176 if name == "idle" else EXPECTED_Y_RIGHT[frame]
            require(row["world_x"] == expected_x and
                    row["world_y"] == expected_y and
                    _int(row["world_x"]) and _int(row["world_y"]) and
                    _int(row["camera"]) and _int(row["render_buffer"]),
                    "RAM/camera numeric baseline differs")
            total = row["color_blue_pixel_count"]
            groups = row["connected_color_groups"]
            require(_int(total, 0, roi[2] * roi[3]) and
                    _int(groups, 0, roi[2] * roi[3]) and
                    groups <= total,
                    "invalid color/group counts")
            component = row["largest_color_group"]
            if total == 0:
                require(groups == 0 and component is None,
                        "empty ROI claims a color component")
            else:
                require(total > 0 and groups > 0 and component is not None,
                        "nonempty ROI lacks a color component")
                _validate_component(component, roi, total)
            if name == "idle":
                require(row["camera"] == 0 and row["render_buffer"] == 0,
                        "idle run unexpectedly advanced camera/render buffer")
    require(runs["right"] == runs["right_repeat"],
            "RIGHT and RIGHT-repeat numeric rows differ")


def analyze_pair(left, right):
    """Validate a pair of private numeric reports without revealing measurements."""
    reports = (left, right)
    for role, report in zip(("left", "right"), reports):
        require(type(report) is dict and set(report) == TOP_FIELDS and
                report["schema"] == SCHEMA and
                report["original_rom_executed_locally"] is True and
                report["reference_rom_sha256"] == ROM_SHA and
                report["research_core_sha256"] == INSTRUMENTED_CORE_SHA and
                report["roi"] == list(ROI[role]) and
                report["same_right_replay_exact"] is True and
                report["independent_identity_or_hitbox_proven"] is False and
                report["safe_to_modify_switch_physics"] is False,
                "report schema, source labels, ROI or safety disclaimers differ")
        _validate_rows(report, role)

    for name in RUN_NAMES:
        lrows, rrows = left["runs"][name], right["runs"][name]
        for lrow, rrow in zip(lrows, rrows):
            for key in ("frame", "world_x", "world_y", "camera", "render_buffer"):
                require(lrow[key] == rrow[key],
                        "numeric gameplay state differs between ROIs")
    for i, frame in enumerate(MARKERS):
        left_idle = left["runs"]["idle"][i]
        left_right = left["runs"]["right"][i]
        right_idle = right["runs"]["idle"][i]
        right_right = right["runs"]["right"][i]
        require(left_idle["color_blue_pixel_count"] > 0 and
                left_idle["largest_color_group"] is not None and
                left_right["color_blue_pixel_count"] == 0 and
                left_right["largest_color_group"] is None,
                "left ROI does not show the expected idle-to-RIGHT transition")
        require(right_idle["color_blue_pixel_count"] == 0 and
                right_idle["largest_color_group"] is None and
                right_right["color_blue_pixel_count"] > 0 and
                right_right["largest_color_group"] is not None,
                "right ROI does not show the expected idle-to-RIGHT transition")
        # The two reported groups must lie in disjoint, role-correct ROIs.
        lcomp = left_idle["largest_color_group"]
        rcomp = right_right["largest_color_group"]
        require(lcomp["blue_bbox_xyxy_exclusive"][2] <= ROI["left"][0] + ROI["left"][2] and
                rcomp["blue_bbox_xyxy_exclusive"][0] >= ROI["right"][0],
                "color candidates not localized in the declared side regions")
    return {
        "schema": "quackshot_private_input_intervention_audit_v1",
        "tested_frames": list(MARKERS),
        "reports_checked": 2,
        "left_idle_color_present_and_right_color_absent": True,
        "right_idle_color_absent_and_right_color_present": True,
        "right_replay_identical_in_both_rois": True,
        "RAM_camera_and_render_buffer_agree_across_ROIs": True,
        "original_execution_provenance_independently_attested": False,
        "color_groups_identified_as_Donald": False,
        "original_hitbox_or_contact_plane_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--left", type=Path, required=True)
    parser.add_argument("--right", type=Path, required=True)
    args = parser.parse_args()
    try:
        preflight(args.bundle)
        paths = (args.left, args.right)
        require(len({str(p.resolve(strict=True)) for p in paths}) == 2,
                "two distinct private report paths required")
        reports = [read_private_report(args.bundle, p) for p in paths]
        analyze_pair(*reports)
        print("GENESIS_STAGE210_PRIVATE_INPUT_INTERVENTION_PASS_NOT_IDENTITY_ATTESTED")
    except (ValueError, OSError, RuntimeError, TypeError, KeyError,
            OverflowError, UnicodeError, SyntaxError):
        parser.exit(1, "GENESIS_STAGE210_PRIVATE_INPUT_INTERVENTION_REJECTED\n")


if __name__ == "__main__":
    main()
