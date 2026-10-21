#!/usr/bin/env python3
"""Stage199 ROM-free crosscheck of archived Genesis VDP HScroll vs blue pixels.

Two previously measured blue-color *centroids* are not character origins,
hitbox edges, or collision planes. Never runs a ROM or decodes frame pixels.
"""
import argparse
from decimal import Decimal, InvalidOperation
import json
from pathlib import Path

from reference_evidence_integrity import read_inputs, validate

VDP_PATH = "research/observations/user_rom_vdp_hscroll_2026-10-18.json"
FRAMES = (2220, 2400)
EXPECTED_BLUE_MILLI = {2220: 134720, 2400: 134164}
EXPECTED_BLUE_PIXEL_COUNTS = [175, 171]


def require(test, reason):
    if not test:
        raise ValueError("VDP visual origin crosscheck: " + reason)


def decimal_milli(value, label):
    require(type(value) in (int, float, Decimal),
            "non-numeric " + label)
    try:
        milli = Decimal(str(value)) * 1000
    except InvalidOperation:
        raise ValueError("VDP visual origin crosscheck: nonfinite " + label) from None
    require(milli.is_finite() and milli == milli.to_integral_value(),
            label + " cannot be represented at 0.001px")
    return int(milli)


def analyze(observations, game_source, hardware_vdp):
    prior = validate(observations, game_source)
    require(type(hardware_vdp) is dict and
            hardware_vdp.get("schema") ==
            "quackshot_megadrive_actual_vdp_hscroll_v1" and
            hardware_vdp.get("reference_rom_sha256") == prior["rom_sha256"] and
            hardware_vdp.get("original_rom_in_public_repository") is False and
            hardware_vdp.get("original_vram_ram_pixels_audio_exported") is False,
            "VDP provenance/schema does not match original numbers")
    measurement = hardware_vdp.get("measurement")
    require(type(measurement) is dict and
            measurement.get("independent_emulations") == 5 and
            measurement.get("identical_right_replays") is True and
            measurement.get("all_checked_samples_match_exactly") is True and
            measurement.get("VDP_register_11") == 0 and
            measurement.get("VDP_register_13") == 52 and
            measurement.get("HScroll_VRAM_table_base") == "0xD000",
            "VDP scroll measurement contract differs")

    heuristic = observations["obstacles"]["camera_heuristic"]
    require(type(heuristic) is dict and
            heuristic.get("world_x_at_2220") == 298 and
            heuristic.get("world_x_at_2400") == 498 and
            heuristic.get("palette_pixel_counts") == EXPECTED_BLUE_PIXEL_COUNTS and
            heuristic.get("source", "").startswith("Private rendered frames"),
            "published color-pixel measurements differ")
    blue = {
        2220: decimal_milli(
            heuristic.get("screen_palette_blue_centroid_x_at_2220"),
            "blue centroid frame 2220"),
        2400: decimal_milli(
            heuristic.get("screen_palette_blue_centroid_x_at_2400"),
            "blue centroid frame 2400"),
    }
    require(blue == EXPECTED_BLUE_MILLI,
            "original blue-centroid numeric evidence changed")
    rows = hardware_vdp.get("keyframes")
    require(type(rows) is list and
            len({r.get("frame") for r in rows if type(r) is dict}) == len(rows),
            "duplicate or malformed hardware keyframes")
    by_frame = {r["frame"]: r for r in rows}
    samples = []
    for i, frame in enumerate(FRAMES):
        require(frame in by_frame, "missing VDP hardware frame " + str(frame))
        item = by_frame[frame]
        require(type(item) is dict and
                all(type(item.get(key)) is int for key in
                    ("world_x", "ram_camera", "ram_hscroll_buffer", "plane_a", "plane_b")),
                "VDP frame contains noninteger coordinates")
        x = item["world_x"]
        scroll = item["ram_hscroll_buffer"]
        require(x == heuristic["world_x_at_" + str(frame)] and
                item["plane_a"] == -scroll and
                item["plane_b"] == -(scroll // 2) and
                0 <= scroll <= 1023 and
                0 <= item["ram_camera"] <= 1023,
                "VDP hardware or world coordinate contradicts published frame")
        # This is a screen X candidate based on layer-A scroll. It is not
        # the offset between the original collision body and the sprite.
        origin = x + item["plane_a"]
        samples.append({
            "frame": frame,
            "original_world_x_px": x,
            "vdp_plane_a_hscroll_px": item["plane_a"],
            "vdp_render_buffer_ram_px": scroll,
            "software_camera_ram_px": item["ram_camera"],
            "screen_x_candidate_px": origin,
            "historical_blue_centroid_x_milli": blue[frame],
            "historical_blue_pixels_count": EXPECTED_BLUE_PIXEL_COUNTS[i],
            "blue_minus_candidate_milli": blue[frame] - origin * 1000,
        })
    offsets = [r["blue_minus_candidate_milli"] for r in samples]
    require(offsets == [-8280, -5836],
            "published VDP-versus-color discrepancies changed")
    return {
        "schema": "quackshot_city_vdp_visual_origin_crosscheck_v1",
        "status": "HISTORICAL_COLOR_CENTROIDS_ARE_NOT_VERIFIED_COLLISION_ORIGINS",
        "rom_executed_by_this_audit": False,
        "original_pixels_or_raw_ram_in_report": False,
        "source_rom_sha256": prior["rom_sha256"],
        "historical_two_point_crosscheck": samples,
        "blue_centroid_delta_milli": blue[2400] - blue[2220],
        "vdp_based_screen_x_candidate_delta_px": (
            samples[1]["screen_x_candidate_px"] -
            samples[0]["screen_x_candidate_px"]),
        "visual_vs_screen_origin_offset_difference_milli": (
            offsets[1] - offsets[0]),
        "one_exact_constant_offset_fits_both_historical_color_means": (
            offsets[0] == offsets[1]),
        "blue_pixel_identity_or_foot_contact_verified": False,
        "original_collision_box_or_anchor_verified": False,
        "same_cpu_vdp_raster_phase_independently_proven": False,
        "safe_to_modify_switch_physics": False,
        "limitations": [
            "The centroids are historical blue-color pixel heuristics, not an identified sprite mask",
            "VDP table state is an end-of-frame sampled HScroll observation",
            "The two reports do not establish identical CPU/VDP/raster phases",
            "Two historical samples cannot determine character collision dimensions",
            "The 2.444px offset discrepancy is diagnostic, not proof of its cause",
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path,
                   default=Path(__file__).resolve().parents[1])
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    try:
        root = args.root.resolve()
        observations, source = read_inputs(root)
        source_vdp = json.loads((root / VDP_PATH).read_text(encoding="utf-8"))
        result = analyze(observations, source, source_vdp)
        encoded = json.dumps(result, ensure_ascii=False, indent=2,
                             sort_keys=True) + "\n"
        if args.out:
            protected = {(root / "src/qs_game.c").resolve(),
                         (root / VDP_PATH).resolve()}
            from reference_evidence_integrity import OBSERVATIONS
            protected.update((root / path).resolve()
                             for path in OBSERVATIONS.values())
            require(args.out.resolve() not in protected and
                    not args.out.exists(), "refusing to overwrite numeric evidence")
            args.out.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (ValueError, TypeError, KeyError, OSError,
            json.JSONDecodeError) as exc:
        p.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
