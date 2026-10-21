#!/usr/bin/env python3
"""ROM-free source-of-truth audit for measured QuackShot first-city numbers.

Compares previously published numeric-only reference observations against each
other and against explicitly opt-in prototype constants. This is NOT a new
original-ROM playthrough or a verification of original collision code.
"""
import argparse
import json
import re
from pathlib import Path

OBSERVATIONS = {
    "motion": "research/observations/user_rom_fixedpoint_motion_2026-10-18.json",
    "obstacles": "research/observations/user_rom_city_obstacles_2026-10-18.json",
    "camera": "research/observations/user_rom_camera_state_2026-10-18.json",
}
EXPECTED_ROM_SHA256 = "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
MACROS = {
    "QS_CITY_ACCEL": "10240",
    "QS_CITY_MAX_WALK": "106496",
    "QS_CITY_GRAVITY": "12800",
    "QS_CITY_JUMP_IMPULSE": "-192000",
    "QS_CITY_MAX_JUMP_HOLD": "15",
}


def require(condition, message):
    if not condition:
        raise ValueError("Reference evidence inconsistency: " + message)


def read_inputs(root):
    return ({name: json.loads((root / path).read_text(encoding="utf-8"))
             for name, path in OBSERVATIONS.items()},
            (root / "src/qs_game.c").read_text(encoding="utf-8"))


def validate(observations, game_source):
    require(set(observations) == set(OBSERVATIONS), "evidence files missing")
    m = observations["motion"]
    o = observations["obstacles"]
    c = observations["camera"]
    require(m["schema"] == "quackshot_original_genesis_fixedpoint_motion_v1",
            "motion schema")
    require(o["schema"] == "quackshot_genesis_city_obstacles_v1", "obstacle schema")
    require(c["schema"] == "quackshot_genesis_camera_state_v1", "camera schema")
    for identifier in (m["reference_rom_sha256"], o["reference_sha256"],
                       c["reference_rom_sha256"]):
        require(identifier == EXPECTED_ROM_SHA256, "ROM provenance differs")
    core_ids = (m["emulator_sha256"], o["genesis_plus_gx_core_sha256"],
                c["emulator_core_sha256"])
    require(len(set(core_ids)) == 1 and re.fullmatch(r"[0-9a-f]{64}", core_ids[0]),
            "Genesis Plus GX binary provenance differs")
    require(m["identical_pre_input_world_coordinate"] == {"x": 61, "y": 176},
            "first city spawn differs")
    require(m["first_city_control_frame"] == 2070, "control frame differs")
    require(m["raw_rom_ram_audio_video_included"] is False,
            "reference motion contains unexpected original assets declaration")

    values = m["conclusions"]
    expected = {
        "horizontal_acceleration_16_16_units": 10240,
        "horizontal_max_speed_16_16_units": 106496,
        "horizontal_braking_16_16_units": 10240,
        "jump_takeoff_vy_px_per_frame": -192000/65536,
        "vertical_gravity_px_per_frame_squared": 12800/65536,
        "jump_upward_velocity_hold_limit_frames": 15,
    }
    for key, value in expected.items():
        require(type(values[key]) in (float, int) and values[key] == value,
                "measured fixed-point constant " + key)
    require(m["scenarios"][1]["world_x_at_2090"] == 87 and
            m["scenarios"][1]["world_x_at_2160"] == 201,
            "walking numerical anchors differ")
    require(m["scenarios"][2]["final_world_x"] == 71,
            "eight-frame braking observation differs")
    for scenario, rise, landing in ((m["scenarios"][4], 24, 2100),
                                    (m["scenarios"][5], 65, 2126)):
        require(scenario["peak_rise_world_pixels"] == rise and
                scenario["landing_frame"] == landing,
                "jump landmarks differ")

    ledge, wall, metrics = o["ledge"], o["wall"], c["metrics"]
    require(o["original_content_in_repository"] is False,
            "obstacle report original-assets declaration")
    require(ledge["world_x_at_frame_2280"] == 396 and
            ledge["lower_floor_world_xy"] == [423, 208] and
            ledge["world_y_upper_floor"] == 176 and
            ledge["drop_pixels"] == 32 and
            ledge["first_lower_floor_frame"] > ledge["first_falling_frame"],
            "measured ledge markers differ")
    require(wall["world_xy"] == [498, 208] and
            wall["horizontal_velocity_zero_first_frame"] == 2344 and
            wall["reverse_x_at_2390"] == 455 and
            wall["reverse_x_at_2430"] == 397,
            "measured wall/reverse markers differ")
    require(metrics["wall_stop_world_x"] == wall["world_xy"][0] and
            metrics["wall_stop_camera_candidate"] == 358,
            "wall and camera reports disagree")
    require(metrics["first_scroll_frame_right"] == 2124 and
            metrics["first_camera_step_left_after_reverse"] == 2375,
            "camera latch frames differ")
    require(metrics["camera_right_anchor_world_minus_scroll"] == {"minimum": 140, "maximum": 142},
            "right camera tracking anchor differs")
    require(metrics["camera_left_anchor_world_minus_scroll"] == {"minimum": 122, "maximum": 124},
            "left camera tracking anchor differs")
    require(metrics["scroll_RAM_candidate"] == "0x8524",
            "scroll-state candidate differs")
    for side, low, high, start in (("right", 140, 142, 2124),
                                   ("reverse", 122, 124, 2374)):
        frames = metrics["keyframes"][side]
        recent = [f for f in frames if f["frame"] >= start]
        require(recent, side + " camera evidence empty")
        for frame in recent:
            delta = frame["world_x"] - frame["camera_RAM_candidate"]
            require(low <= delta <= high, side + " camera keyframe contradicts anchor")

    # Match the *active* constants rather than a historic approximating
    # equation in documents/reference_city_envelope_v1.json.
    for symbol, numerator in MACROS.items():
        if symbol == "QS_CITY_MAX_JUMP_HOLD":
            pattern = r"^\s*#define\s+" + symbol + r"\s+15\s*$"
        else:
            pattern = (r"^\s*#define\s+" + symbol + r"\s+\(" +
                       numerator + r"\.0f\s*/\s*65536\.0f\)\s*$")
        require(bool(re.search(pattern, game_source, re.MULTILINE)),
                "active city physics constant " + symbol)

    return {
        "schema": "quackshot_reference_evidence_integrity_v1",
        "status": "CONSISTENT",
        "original_rom_was_run_by_this_audit": False,
        "scope": "previously measured first-city fixed-point values, ledge, wall, and candidate scroll state",
        "rom_sha256": EXPECTED_ROM_SHA256,
        "genesis_core_sha256": core_ids[0],
        "measured_walk_speed_16_16": expected["horizontal_max_speed_16_16_units"],
        "measured_horizontal_acceleration_16_16": expected["horizontal_acceleration_16_16_units"],
        "measured_jump_impulse_16_16": -192000,
        "measured_gravity_16_16": 12800,
        "measured_short_and_long_landing_frames": [2100, 2126],
        "measured_ledge_drop_pixels": ledge["drop_pixels"],
        "measured_wall_x": wall["world_xy"][0],
        "scroll_state_ram_candidate": metrics["scroll_RAM_candidate"],
        "not_proven": [
            "No frame-exact ROM replay was performed by this audit",
            "No direct verification of camera hardware register or original tile geometry",
            "Original first-city coordinate origin differs from authored Switch prototype",
            "The historical exponential model is not the currently active opt-in physics",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        if args.out:
            originals = { (args.root / p).resolve() for p in OBSERVATIONS.values() }
            originals.add((args.root / "src/qs_game.c").resolve())
            require(args.out.resolve() not in originals,
                    "refusing to overwrite reference inputs")
        report = validate(*read_inputs(args.root))
        result = json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
        if args.out:
            args.out.write_text(result, encoding="utf-8")
        else:
            print(result, end="")
    except (ValueError, KeyError, TypeError, OSError, json.JSONDecodeError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
