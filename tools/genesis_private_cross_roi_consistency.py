#!/usr/bin/env python3
"""Stage209: compare three private Genesis numeric ROI reports without media.

This is a consistency check for nested color ROIs, never pixel/ROM recovery,
player identity, local run authentication, or contact-plane verification.
"""
import argparse
from pathlib import Path

from genesis_offline_numeric_report_audit import (
    audit_report, read_private_report,
)
from genesis_private_bundle_launcher import preflight
from genesis_city_same_step_triplet_probe import MARKERS


STABLE_NUMERIC_KEYS = (
    "frame", "player_world_x", "player_world_y", "software_camera_ram",
    "render_buffer_ram", "vdp_plane_a_hscroll", "vdp_screen_x_candidate",
)


def require(condition, message):
    if not condition:
        raise ValueError("Private numeric cross-ROI consistency: " + message)


def _bbox_inside(box, roi):
    return (roi[0] < box[0] < box[2] and
            roi[1] < box[1] < box[3]) if False else (
        roi[0] < box[0] < box[2] <= roi[0] + roi[2] and
        roi[1] < box[1] < box[3] <= roi[1] + roi[3]
    )


def _strictly_nested(outer, inner):
    return (outer[0] < inner[0] and outer[1] < inner[1] and
            inner[0] + inner[2] < outer[0] + outer[2] and
            inner[1] + inner[3] < outer[1] + outer[3])


def _identity(component):
    """Numeric appearance fingerprint; not a sprite identity."""
    return (
        component["blue_pixels"],
        component["blue_centroid_x_milli"],
        component["blue_centroid_y_milli"],
        tuple(component["blue_bbox_xyxy_exclusive"]),
    )


def _numeric_state(report):
    return tuple(tuple(frame[k] for k in STABLE_NUMERIC_KEYS)
                 for frame in report["frames"])


def analyze(wide, focus, tight):
    """Return numeric-only summary of *unattested* captured report content."""
    reports = (wide, focus, tight)
    for report in reports:
        require(type(report) is dict and
                report.get("schema") ==
                "quackshot_private_genesis_ram_video_vdp_same_step_v1" and
                report.get("visual_component_candidates") is not None,
                "each report must be a Stage207 numeric color triplet")
        # Includes SHA-label, frame count, private numeric schema, and
        # unsupported-hitbox/scanout disclaimer validation.
        audit_report(report)
    reference = {(r["reference_rom_sha256"], r["instrumented_core_sha256"])
                 for r in reports}
    require(len(reference) == 1 and
            all(r["component_identity_unverified"] is True for r in reports),
            "numeric source labels or disclaimer differ")
    w_roi, f_roi, t_roi = [r["video_roi"] for r in reports]
    require(_strictly_nested(w_roi, f_roi) and
            _strictly_nested(f_roi, t_roi),
            "ROIs must nest strictly with nonzero margin")
    states = [_numeric_state(r) for r in reports]
    require(states[0] == states[1] == states[2],
            "original RAM/camera/VDP numerical states changed across ROIs")
    results = []
    static_region = None
    for i, frame in enumerate(MARKERS):
        groups = [r["visual_component_candidates"][i] for r in reports]
        require(all(g["frame"] == frame and
                    type(g["components"]) is list and
                    g["components"] for g in groups),
                "missing candidate in original color reports")
        dominant = groups[2]["components"][0]
        candidate = _identity(dominant)
        require(dominant["touches_roi_boundary"] is False and
                all(sum(_identity(c) == candidate for c in
                        group["components"]) == 1 for group in groups),
                "tight color candidate not identical across all three ROIs")
        require(all(next(c for c in group["components"]
                         if _identity(c) == candidate)["touches_roi_boundary"]
                    is False for group in groups),
                "candidate clipped at an ROI boundary")
        wide_largest = groups[0]["components"][0]
        require(wide_largest["touches_roi_boundary"] is True and
                _identity(wide_largest) != candidate,
                "wide ROI did not contain a distinct edge-clipped region")
        # A repeated numeric color patch is not inherently background.
        if static_region is None:
            static_region = _identity(wide_largest)
        else:
            require(_identity(wide_largest) == static_region,
                    "largest wide color region changed between markers")
        require(all(_identity(wide_largest) != _identity(c)
                    for group in groups[1:] for c in group["components"]),
                "wide persistent component survived smaller ROI crop")
        results.append({
            "frame": frame,
            "ram_world_y": wide["frames"][i]["player_world_y"],
            "candidate_blue_pixels": dominant["blue_pixels"],
            "candidate_color_y_milli": dominant["blue_centroid_y_milli"],
            "large_invariant_wide_color_pixels": wide_largest["blue_pixels"],
        })
    before = next(row for row in results if row["frame"] == 2280)
    after = next(row for row in results if row["frame"] == 2297)
    return {
        "schema": "quackshot_genesis_three_roi_numeric_consistency_v1",
        "tested_libretro_frames": list(MARKERS),
        "reports_compared": len(reports),
        "numeric_ram_camera_vdp_same_across_all_reports": True,
        "narrow_color_group_exactly_matched_at_all_five_frames": True,
        "wide_large_edge_color_group_unchanged_at_all_five_frames": True,
        "large_region_absent_from_smaller_rois": True,
        "ram_y_difference_2280_to_2297": (
            after["ram_world_y"] - before["ram_world_y"]),
        "candidate_color_y_difference_milli_2280_to_2297": (
            after["candidate_color_y_milli"] -
            before["candidate_color_y_milli"]),
        "numeric_samples": results,
        "local_execution_provenance_independently_attested": False,
        "color_group_identified_as_original_player": False,
        "invariant_color_group_identified_as_background": False,
        "original_contact_plane_or_hitbox_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--wide", type=Path, required=True)
    parser.add_argument("--focus", type=Path, required=True)
    parser.add_argument("--tight", type=Path, required=True)
    args = parser.parse_args()
    try:
        # Bundled Python+numeric baselines+compiled core must match the
        # original pinned SHA before any private numeric JSON is opened.
        preflight(args.bundle)
        paths = [args.wide, args.focus, args.tight]
        require(len(set(str(p.resolve(strict=True)) for p in paths)) == 3,
                "three distinct private report paths are required")
        data = [read_private_report(args.bundle, path) for path in paths]
        output = analyze(*data)
        require(output["safe_to_modify_switch_physics"] is False,
                "invalid source-collision conclusion")
        print("GENESIS_STAGE209_PRIVATE_CROSS_ROI_NUMERIC_PASS_NOT_ORIGIN_ATTESTED")
    except (ValueError, OSError, TypeError, KeyError, RuntimeError,
            OverflowError, UnicodeError, SyntaxError):
        # Never echo original ROM path, private JSON paths, or measurements.
        parser.exit(1, "GENESIS_STAGE209_PRIVATE_CROSS_ROI_NUMERIC_REJECTED\n")


if __name__ == "__main__":
    main()
