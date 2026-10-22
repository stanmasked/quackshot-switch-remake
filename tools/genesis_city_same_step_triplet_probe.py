#!/usr/bin/env python3
"""Stage200: private same-retro_run-index Genesis RAM, color ROI and VDP data.

RAM and VDP are sampled AFTER retro_run returns; transient video ROI is
sampled DURING its video callback. Matching step indices do not prove
CPU instruction, VBlank, scanout or collision timing equivalence.
ROM, pixels, raw RAM, VRAM and audio never leave the private process.
"""
import argparse
import json
from pathlib import Path

from genesis_blue_roi_numeric import validate_roi
from genesis_city_obstacle_probe import RUNS, TOTAL
from genesis_city_visual_roi_probe import (DEFAULT_ROI, EXPECTED_Y,
                                           MARKERS, private_write)
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY
from genesis_vdp_scroll_probe import (SCANLINES, bind_vdp_probe,
                                      validate_actual_vdp_parallax,
                                      vdp_number_snapshot)
from private_genesis_input_guard import PinnedOriginalInputs
from reference_evidence_integrity import read_inputs, validate

ROM_SHA = "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
INSTRUMENTED_CORE_SHA = "4e3381cf5bfa7ff387bbe6887e7e31453bf4c8c85b295aa66d6d07d415dde7aa"
EXPECTED_X = {2180: 233, 2220: 298, 2280: 396, 2297: 423, 2350: 498}
VDP_ARCHIVE_PATH = "research/observations/user_rom_vdp_hscroll_2026-10-18.json"


def validate_instrumented_vdp_archive(vdp):
    """Bind this SHA-pinned hook to previously observed numeric hardware data."""
    require(type(vdp) is dict and
            vdp.get("schema") == "quackshot_megadrive_actual_vdp_hscroll_v1" and
            vdp.get("reference_rom_sha256") == ROM_SHA and
            vdp.get("rom_free_instrumented_core_sha256") ==
            INSTRUMENTED_CORE_SHA and
            vdp.get("original_rom_in_public_repository") is False and
            vdp.get("original_vram_ram_pixels_audio_exported") is False,
            "original VDP archive provenance does not match pinned hook")
    measured = vdp.get("measurement")
    require(type(measured) is dict and
            measured.get("independent_emulations") == 5 and
            measured.get("identical_right_replays") is True and
            measured.get("all_checked_samples_match_exactly") is True and
            measured.get("sampled_vdp_scanlines") == list(SCANLINES) and
            measured.get("HScroll_VRAM_table_base") == "0xD000" and
            measured.get("VDP_register_11") == 0 and
            measured.get("VDP_register_13") == 52,
            "original VDP archive hardware contract differs")
    return None

FIELDS = {
    "player_world_x", "player_world_y", "camera_ram", "render_buffer_ram",
    "vdp_scanlines", "blue_visual"
}
VISUAL_FIELDS = {
    "roi", "pixel_format", "blue_pixel_count", "blue_centroid_x_milli",
    "blue_centroid_y_milli", "blue_bbox_xyxy_exclusive",
    "source_collision_plane_measured"
}
VDP_FIELDS = {
    "line", "vdp_reg11", "vdp_reg13", "hscroll_vram_base_address",
    "hscroll_scanline_mask", "plane_A_HScroll_11bit_signed",
    "plane_B_HScroll_11bit_signed", "plane_A_highbits",
    "plane_B_highbits"
}


def require(value, problem):
    if not value:
        raise ValueError("Genesis same-step triplet: " + problem)


def validate_triplet(traces, reads, video_digests, roi, *, rom_sha, core_sha):
    """Fail closed without claiming original hardware phase alignment."""
    validate_roi(roi)
    require(rom_sha == ROM_SHA and core_sha == INSTRUMENTED_CORE_SHA,
            "wrong pinned original ROM or instrumented research core")
    require(type(traces) is dict and
            set(traces) == {"idle", "right", "right_repeat"} and
            type(reads) is dict and set(reads) == set(traces) and
            type(video_digests) is dict and set(video_digests) == set(traces),
            "missing or extra independent run")
    require(all(type(reads[n]) is int for n in reads) and
            reads["right"] - reads["idle"] == 450 and
            reads["right_repeat"] - reads["idle"] == 450,
            "controller callback reads differ")
    require(video_digests["right"] == video_digests["right_repeat"],
            "right-run video digests are not reproducible")
    vdp_scenarios = {}
    for name, frames in traces.items():
        require(type(frames) is dict and set(frames) == set(MARKERS),
                "missing/extra same-step marker")
        vdp_scenarios[name] = {}
        for frame, sample in frames.items():
            require(type(sample) is dict and set(sample) == FIELDS,
                    "missing/extra private numeric sample fields")
            for key in ("player_world_x", "player_world_y",
                        "camera_ram", "render_buffer_ram"):
                require(type(sample[key]) is int and 0 <= sample[key] < 1024,
                        "invalid original RAM coordinate")
            visual = sample["blue_visual"]
            require(type(visual) is dict and set(visual) == VISUAL_FIELDS,
                    "video result contains forbidden fields or missing values")
            require(type(visual["roi"]) is list and visual["roi"] == list(roi) and
                    type(visual["pixel_format"]) is str and
                    visual["pixel_format"] in
                    ("0RGB1555", "XRGB8888", "RGB565") and
                    visual["source_collision_plane_measured"] is False,
                    "ROI/video provenance differs")
            count = visual["blue_pixel_count"]
            require(type(count) is int and 0 <= count <= roi[2] * roi[3],
                    "invalid blue-pixel count")
            cx, cy, box = (visual["blue_centroid_x_milli"],
                           visual["blue_centroid_y_milli"],
                           visual["blue_bbox_xyxy_exclusive"])
            if count == 0:
                require(cx is None and cy is None and box is None,
                        "empty blue ROI must not claim a position")
            else:
                require(type(cx) is int and type(cy) is int and
                        roi[0] * 1000 <= cx < (roi[0] + roi[2]) * 1000 and
                        roi[1] * 1000 <= cy < (roi[1] + roi[3]) * 1000 and
                        type(box) is list and len(box) == 4 and
                        all(type(k) is int for k in box) and
                        roi[0] <= box[0] < box[2] <= roi[0] + roi[2] and
                        roi[1] <= box[1] < box[3] <= roi[1] + roi[3],
                        "blue ROI centroid/bounds inconsistent")
            vdp_rows = sample["vdp_scanlines"]
            require(type(vdp_rows) is list and len(vdp_rows) == len(SCANLINES),
                    "missing/extra VDP hardware scanline")
            for row in vdp_rows:
                require(type(row) is dict and set(row) == VDP_FIELDS and
                        all(type(v) is int for v in row.values()),
                        "VDP sample contains forbidden or noninteger data")
            vdp_scenarios[name][frame] = {
                "render_buffer_RAM_0x852c": sample["render_buffer_ram"],
                "vdp_scanlines": vdp_rows
            }
    require(traces["right"] == traces["right_repeat"],
            "right-run RAM/video/VDP triplets not reproducible")
    require(all(traces["idle"][f]["player_world_y"] == 176 for f in MARKERS),
            "original idle Y has changed")
    require(all(traces["right"][f]["player_world_y"] == EXPECTED_Y[f] and
                traces["right"][f]["player_world_x"] == EXPECTED_X[f]
                for f in MARKERS), "original integer world landmarks differ")
    vdp_proof = validate_actual_vdp_parallax(vdp_scenarios)
    require(vdp_proof["frames_verified"] == 3 * len(MARKERS),
            "incomplete original numeric VDP scanlines")
    right = traces["right"]
    numeric = []
    for frame in MARKERS:
        sample = right[frame]
        plane = sample["vdp_scanlines"][0]["plane_A_HScroll_11bit_signed"]
        candidate_screen_x = sample["player_world_x"] + plane
        visual = sample["blue_visual"]
        numeric.append({
            "frame": frame,
            "player_world_x": sample["player_world_x"],
            "player_world_y": sample["player_world_y"],
            "software_camera_ram": sample["camera_ram"],
            "render_buffer_ram": sample["render_buffer_ram"],
            "vdp_plane_a_hscroll": plane,
            "vdp_screen_x_candidate": candidate_screen_x,
            "blue_pixel_count": visual["blue_pixel_count"],
            "blue_centroid_x_milli": visual["blue_centroid_x_milli"],
            "blue_centroid_y_milli": visual["blue_centroid_y_milli"],
            "blue_minus_vdp_candidate_x_milli": (
                visual["blue_centroid_x_milli"] - candidate_screen_x * 1000
                if visual["blue_centroid_x_milli"] is not None else None
            ),
        })
    return {
        "schema": "quackshot_private_genesis_ram_video_vdp_same_step_v1",
        "status": "SAME_LIBRETRO_STEP_ONLY_NOT_ORIGINAL_SCANOUT_PHASE",
        "original_rom_executed_locally": True,
        "independently_attested_by_report": False,
        "original_media_raw_ram_vram_or_pixels_saved": False,
        "reference_rom_sha256": rom_sha,
        "instrumented_core_sha256": core_sha,
        "independent_original_runs": 3,
        "repeated_right_numeric_and_video_identical": True,
        "video_roi": list(roi),
        "frames": numeric,
        "hardware_vdp_scanlines_verified": vdp_proof["individual_vdp_plane_word_comparisons"],
        "video_callback_phase": "during_retro_run",
        "ram_and_vdp_sampling_phase": "post_retro_run_before_next_input",
        "same_cpu_vdp_scanout_phase_verified": False,
        "original_collision_hitbox_or_contact_plane_verified": False,
        "safe_to_modify_switch_physics": False,
        "interpretation": (
            "Three measurements share a libretro frame index only. "
            "No original instruction/VBlank/scanout phase correspondence "
            "or color-mask identity has been independently established."
        )
    }


def collect(core, metadata, rom_path, rom_data, roi, verify_inputs):
    """Take independent private observations; return no ROM/RAM/pixel arrays."""
    hook = bind_vdp_probe(core)
    intro = parse_script(DEFAULT_ENTRY)
    traces, reads, video_digests = {}, {}, {}
    for name in ("idle", "right", "right_repeat"):
        snapshots = {}
        def on_private_ram(frame, ram):
            if frame not in MARKERS:
                return
            require(len(ram) == 65536, "unexpected private Genesis RAM size")
            get_word = lambda off: int.from_bytes(ram[off:off+2], "little")
            snapshots[frame] = {
                "player_world_x": get_word(0xA2D0),
                "player_world_y": get_word(0xA2D4),
                "camera_ram": get_word(0x8524),
                "render_buffer_ram": get_word(0x852C),
                "vdp_scanlines": [
                    vdp_number_snapshot(hook, line) for line in SCANLINES
                ],
            }
        verify_inputs()
        try:
            result = run_core(
                core, metadata, rom_data, ROM_SHA, TOTAL,
                intro + RUNS[name], private_rom_path=rom_path,
                ram_window=(MARKERS[0], MARKERS[-1]+1),
                checkpoint_sink=on_private_ram, video_stride=60,
                video_roi=roi, video_roi_frames=MARKERS)
        finally:
            verify_inputs()
        require(result.get("frames_video_callbacks") == TOTAL and
                result.get("frames_without_video_callback") == 0 and
                result.get("frames_with_multiple_video_callbacks") == 0 and
                result.get("video_callbacks_outside_retro_run") == 0 and
                result.get("ram_checkpoint_phase") ==
                "post_retro_run_before_next_input" and
                result.get("video_callback_phase") == "during_retro_run",
                "missing/misordered libretro callback phase")
        visual = result.get("video_roi_blue_numeric")
        require(type(visual) is list and len(visual) == len(MARKERS) and
                all(type(row) is dict and type(row.get("frame")) is int
                    for row in visual), "video ROI callbacks incomplete")
        for row in visual:
            frame = row["frame"]
            require(frame in snapshots and "blue_visual" not in snapshots[frame],
                    "missing/duplicate private RAM/VDP video frame")
            snapshots[frame]["blue_visual"] = {
                key: value for key, value in row.items() if key != "frame"
            }
        traces[name] = snapshots
        reads[name] = result["input_nonzero_reads"]
        video_digests[name] = result["video_checkpoints"]
    return validate_triplet(
        traces, reads, video_digests, roi,
        rom_sha=ROM_SHA, core_sha=INSTRUMENTED_CORE_SHA)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--roi", type=int, nargs=4, default=DEFAULT_ROI,
                        metavar=("X", "Y", "WIDTH", "HEIGHT"))
    args = parser.parse_args()
    try:
        root = Path(__file__).resolve().parents[1]
        roi = tuple(args.roi)
        validate_roi(roi)
        prior = validate(*read_inputs(root))
        require(prior["rom_sha256"] == ROM_SHA,
                "source ROM numeric provenance mismatch")
        # The special VDP-capable binary has a different SHA from the
        # ordinary Genesis Plus GX binary used by Stage188.
        archived_vdp = json.loads((root / VDP_ARCHIVE_PATH).read_text(
            encoding="utf-8"))
        validate_instrumented_vdp_archive(archived_vdp)
        guard = PinnedOriginalInputs(
            args.rom, args.core, ROM_SHA, INSTRUMENTED_CORE_SHA)
        guard()
        core, metadata = setup_core(args.core)
        raw_rom, sha = safe_rom(args.rom, ROM_SHA)
        require(sha == ROM_SHA, "private original source SHA mismatch")
        guard()
        report = collect(core, metadata, args.rom, raw_rom, roi, guard)
        guard()
        private_write(report, args.output, root)
    except OSError:
        parser.exit(1, "Private Genesis same-step probe: local I/O error "
                       "(paths redacted)\n")
    except (ValueError, RuntimeError, KeyError, TypeError) as exc:
        parser.exit(1, "Private Genesis same-step probe: " + str(exc) + "\n")


if __name__ == "__main__":
    main()
