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
            allowed = {
                "roi", "pixel_format", "blue_pixel_count",
                "blue_centroid_x_milli", "blue_centroid_y_milli",
                "blue_bbox_xyxy_exclusive", "source_collision_plane_measured"
            }
            require(type(visual) is dict and set(visual) == allowed and
                    visual["roi"] == list(roi) and
                    visual["source_collision_plane_measured"] is False and
                    visual["pixel_format"] in
                    ("0RGB1555", "XRGB8888", "RGB565") and
                    type(visual["blue_pixel_count"]) is int and
                    0 <= visual["blue_pixel_count"] <= roi[2] * roi[3],
                    "unexpected original payload in numeric visual ROI")
            count = visual["blue_pixel_count"]
            cx, cy = (visual["blue_centroid_x_milli"],
                      visual["blue_centroid_y_milli"])
            bbox = visual["blue_bbox_xyxy_exclusive"]
            if count == 0:
                require(cx is None and cy is None and bbox is None,
                        "empty blue ROI cannot claim a location")
            else:
                require(type(cx) is int and type(cy) is int and
                        roi[0] * 1000 <= cx < (roi[0] + roi[2]) * 1000 and
                        roi[1] * 1000 <= cy < (roi[1] + roi[3]) * 1000,
                        "numeric centroid outside selected ROI")
                require(type(bbox) is list and len(bbox) == 4 and
                        all(type(v) is int for v in bbox) and
                        roi[0] <= bbox[0] < bbox[2] <= roi[0] + roi[2] and
                        roi[1] <= bbox[1] < bbox[3] <= roi[1] + roi[3],
                        "numeric bounding box outside selected ROI")
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


COMPONENT_KEYS = {
    "schema", "roi", "pixel_format", "connectivity",
    "min_component_pixels", "blue_pixel_count",
    "all_blue_component_count", "eligible_component_count",
    "reported_component_count", "largest_component_dominates_by_2x",
    "components", "selected_component_is_proven_player",
    "original_sprite_hitbox_or_contact_verified",
    "safe_to_modify_switch_physics",
}
COMPONENT_ITEM_KEYS = {
    "blue_pixels", "blue_centroid_x_milli", "blue_centroid_y_milli",
    "blue_bbox_xyxy_exclusive", "touches_roi_boundary",
}


def validate_component_sample(sample, roi, blue_total):
    """Prevent RGB, RAM, VDP or other raw data from entering private output."""
    require(type(sample) is dict and set(sample) == COMPONENT_KEYS and
            sample["schema"] == "quackshot_genesis_blue_components_numeric_v1" and
            sample["roi"] == list(roi) and
            sample["pixel_format"] in ("0RGB1555", "XRGB8888", "RGB565") and
            sample["connectivity"] == 4 and
            sample["min_component_pixels"] == 5 and
            type(sample["blue_pixel_count"]) is int and
            sample["blue_pixel_count"] == blue_total and
            all(sample[k] is False for k in (
                "selected_component_is_proven_player",
                "original_sprite_hitbox_or_contact_verified",
                "safe_to_modify_switch_physics")),
            "component evidence provenance or privacy mismatch")
    total = sample["all_blue_component_count"]
    eligible = sample["eligible_component_count"]
    selected = sample["reported_component_count"]
    require(all(type(n) is int for n in (total, eligible, selected)) and
            0 <= selected <= eligible <= total <= blue_total and
            selected <= 4 and
            type(sample["largest_component_dominates_by_2x"]) is bool and
            type(sample["components"]) is list and
            len(sample["components"]) == selected,
            "component counts inconsistent")
    areas = []
    for component in sample["components"]:
        require(type(component) is dict and
                set(component) == COMPONENT_ITEM_KEYS and
                type(component["blue_pixels"]) is int and
                component["blue_pixels"] >= 5 and
                type(component["touches_roi_boundary"]) is bool and
                type(component["blue_centroid_x_milli"]) is int and
                type(component["blue_centroid_y_milli"]) is int and
                type(component["blue_bbox_xyxy_exclusive"]) is list and
                len(component["blue_bbox_xyxy_exclusive"]) == 4 and
                all(type(v) is int for v in
                    component["blue_bbox_xyxy_exclusive"]),
                "component contains invalid or forbidden fields")
        xmin, ymin, xmax, ymax = component["blue_bbox_xyxy_exclusive"]
        require(roi[0] <= xmin < xmax <= roi[0] + roi[2] and
                roi[1] <= ymin < ymax <= roi[1] + roi[3] and
                xmin * 1000 <= component["blue_centroid_x_milli"] <
                xmax * 1000 and
                ymin * 1000 <= component["blue_centroid_y_milli"] <
                ymax * 1000,
                "component numeric bounds outside ROI")
        areas.append(component["blue_pixels"])
    require(areas == sorted(areas, reverse=True) and
            sum(areas) <= blue_total,
            "component pixel areas impossible or out of order")
    return sample


def measure(core, metadata, rompath, rom_bytes, sha, core_sha, roi, *,
            verify_inputs, components=False):
    require(callable(verify_inputs), "pinned input guard is required")
    require(type(components) is bool, "component selection flag must be boolean")
    validate_roi(roi)
    intro = parse_script(DEFAULT_ENTRY)
    traces, read_counts, digests, candidate_runs = {}, {}, {}, {}
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
                video_roi=roi, video_roi_frames=MARKERS,
                video_roi_components=components)
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
        if components:
            rows = out.get("video_roi_components_numeric")
            require(type(rows) is list and len(rows) == len(MARKERS),
                    "missing or repeated component callback")
            candidates = {}
            for row in rows:
                require(type(row) is dict and "frame" in row and
                        type(row["frame"]) is int and
                        row["frame"] in MARKERS and
                        row["frame"] not in candidates,
                        "invalid original component sample frame")
                frame = row["frame"]
                data = {k: v for k, v in row.items() if k != "frame"}
                candidates[frame] = validate_component_sample(
                    data, roi, visual[frame]["blue_pixel_count"])
            require(set(candidates) == set(MARKERS),
                    "missing original component marker")
            candidate_runs[name] = candidates
    result = summarize(traces, read_counts, digests, roi, sha, core_sha)
    if components:
        require(candidate_runs["right"] == candidate_runs["right_repeat"],
                "original visual components not reproducible")
        result["visual_component_candidates"] = [
            {"frame": frame, **candidate_runs["right"][frame]}
            for frame in MARKERS
        ]
        result["component_identity_unverified"] = True
        result["original_collision_plane_verified"] = False
        result["original_sprite_identity_proven_from_blue_pixels"] = False
        result["safe_to_modify_switch_physics"] = False
    return result


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
    p.add_argument("--component-candidates", action="store_true",
                   help="Private numeric 4-connected color groups, not hitbox data")
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
                         verify_inputs=guard,
                         components=args.component_candidates)
        guard()
        private_write(report, args.output, root)
    except OSError:
        p.exit(1, "Private Genesis visual ROI: I/O failed (paths redacted)\n")
    except (ValueError, RuntimeError, KeyError, TypeError) as error:
        p.exit(1, "Private Genesis visual ROI: " + str(error) + "\n")


if __name__ == "__main__":
    main()
