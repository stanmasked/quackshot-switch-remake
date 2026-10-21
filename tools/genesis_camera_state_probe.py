#!/usr/bin/env python3
"""Private-ROM Genesis first-city camera state probe; exports numbers only.

No game ROM, framebuffer, raw RAM, audio, sprite, or savestate is serialized.
The 0x8524 RAM candidate is NOT a decoded VDP horizontal scroll register.
"""
import argparse
import hashlib
import json
from pathlib import Path

from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY

START = 2069
END_EXCLUSIVE = 2451
FRAMES = 2550
SCENARIOS = {
    "idle": [],
    "right": [(2070, 2520, "RIGHT")],
    "right_repeat": [(2070, 2520, "RIGHT")],
    "reverse": [(2070, 2360, "RIGHT"), (2360, 2430, "LEFT")],
    "jump": [(2070, 2510, "RIGHT"), (2310, 2350, "A")],
}
RAM_FIELDS = {
    "world_x": 0xA2D0,
    "world_y": 0xA2D4,
    "scroll_candidate": 0x8524,
    "scroll_mirror_candidate": 0xFEC6,
    "secondary_scroll_candidate": 0x852C,
    "sprite_object_world_x": 0x8F58,
}


def decode(ram):
    if len(ram) != 65536:
        raise ValueError("Expected 64 KiB private Genesis work RAM")
    return {
        name: int.from_bytes(ram[off:off + 2], "little")
        for name, off in RAM_FIELDS.items()
    }


def validate(sets, actual_pressed_reads):
    if set(sets) != set(SCENARIOS):
        raise ValueError("Missing independent camera input scenarios")
    for name, frames in sets.items():
        if set(frames) != set(range(START, END_EXCLUSIVE)):
            raise ValueError(f"{name}: incomplete or excessive RAM sampling window")
    if sets["right"] != sets["right_repeat"]:
        raise ValueError("Right-run reference camera replay was not deterministic")
    if {name: actual_pressed_reads[name] for name in SCENARIOS} != {
        "idle": 0, "right": 450, "right_repeat": 450,
        "reverse": 360, "jump": 480,
    }:
        raise ValueError("Controller input callback counts do not match scenarios")

    idle, right, reverse, jump = (
        sets[name] for name in ("idle", "right", "reverse", "jump")
    )
    c = "scroll_candidate"
    if any(s["world_x"] != 61 or s[c] != 0 for s in idle.values()):
        raise ValueError("Idle camera or city spawn changed unexpectedly")
    if not (
        right[2123]["world_x"] == 141 and right[2123][c] == 0 and
        right[2124]["world_x"] == 142 and right[2124][c] == 1
    ):
        raise ValueError("First right-tracking scroll frame was not 2124")

    right_anchors = [right[f]["world_x"] - right[f][c]
                     for f in range(2180, 2345)]
    if not all(140 <= a <= 142 for a in right_anchors):
        raise ValueError("Observed rightward camera offset left measured 140..142 band")
    if any(right[f]["world_x"] != 498 or right[f][c] != 358
           for f in range(2350, END_EXCLUSIVE)):
        raise ValueError("Camera continued scrolling with character blocked at wall")

    if not (
        reverse[2360]["world_x"] == 498 and reverse[2360][c] == 358 and
        reverse[2374][c] == 358 and
        reverse[2375]["world_x"] == 480 and reverse[2375][c] == 357 and
        reverse[2390]["world_x"] == 455 and reverse[2390][c] == 333 and
        reverse[2430]["world_x"] == 397 and reverse[2430][c] == 273
    ):
        raise ValueError("Direction-dependent left tracking / hold time differs")
    left_anchors = [reverse[f]["world_x"] - reverse[f][c]
                    for f in range(2380, 2431)]
    if not all(122 <= a <= 124 for a in left_anchors):
        raise ValueError("Observed leftward camera offset left measured 122..124 band")

    if not all(140 <= jump[f]["world_x"] - jump[f][c] <= 142
               for f in range(2310, 2351)):
        raise ValueError("Right camera anchor changed during jumping experiment")

    discrepancies = {
        name: [f for f, r in frames.items()
               if r["scroll_candidate"] != r["scroll_mirror_candidate"]]
        for name, frames in sets.items()
    }
    expected_discrepancies = {
        "idle": [], "right": [2203, 2281], "right_repeat": [2203, 2281],
        "reverse": [2203, 2281], "jump": [2203, 2281, 2393],
    }
    if discrepancies != expected_discrepancies:
        raise ValueError("Secondary mirrored scroll candidate differs unexpectedly")

    keyframes = (2069, 2123, 2124, 2180, 2220, 2280, 2340,
                 2350, 2360, 2374, 2375, 2380, 2390, 2400, 2430, 2450)
    keys = {
        name: [
            {"frame": f,
             "world_x": frames[f]["world_x"],
             "camera_RAM_candidate": frames[f][c],
             "camera_secondary_candidate": frames[f]["secondary_scroll_candidate"]}
            for f in keyframes
        ]
        for name, frames in sets.items() if name != "right_repeat"
    }
    return {
        "first_scroll_frame_right": 2124,
        "camera_right_anchor_world_minus_scroll": {"minimum": 140, "maximum": 142},
        "camera_left_anchor_world_minus_scroll": {"minimum": 122, "maximum": 124},
        "first_camera_step_left_after_reverse": 2375,
        "wall_stop_camera_candidate": 358,
        "wall_stop_world_x": 498,
        "scroll_RAM_candidate": "0x8524",
        "mirror_candidate_not_identical": "0xFEC6",
        "mirror_difference_frames": discrepancies,
        "secondary_candidate": "0x852C",
        "keyframes": keys,
        "interpretation_limits": [
            "0x8524 is a strongly correlated software RAM scroll-state candidate, NOT a directly observed VDP HScroll register",
            "Camera may have separate directional follow bands; values have integer rounding and frame pacing",
            "No claim that 0x8F58 is display-space sprite X; it stores world-associated object coordinates",
            "Only first playable city level, one source hash and pinned emulator core were checked",
        ],
    }


def measure(core, info, private_rom_path, rom_data, sha):
    intro = parse_script(DEFAULT_ENTRY)
    sets, counts, video_checks = {}, {}, {}
    for label, extra in SCENARIOS.items():
        snapshots = {}
        def callback(frame, ram):
            if START <= frame < END_EXCLUSIVE:
                snapshots[frame] = decode(ram)
        out = run_core(
            core, info, rom_data, sha, FRAMES, intro + extra,
            private_rom_path=private_rom_path, video_stride=60,
            ram_window=(START, END_EXCLUSIVE), checkpoint_sink=callback
        )
        if out["frames_video_callbacks"] != FRAMES:
            raise RuntimeError("Private original-ROM emulator did not complete")
        sets[label] = snapshots
        counts[label] = out["input_nonzero_reads"] - 71
        video_checks[label] = out["video_checkpoints"]
    if video_checks["right"] != video_checks["right_repeat"]:
        raise ValueError("Original rendered reference frames not deterministic")
    return {
        "schema": "quackshot_genesis_camera_state_v1",
        "evidence_type": "private_held_input_real_genesis_emulation_numeric_RAM_only",
        "reference_rom_sha256": sha,
        "emulator": info["library_name"] + " " + info["library_version"],
        "independent_runs": len(SCENARIOS),
        "frames_emulated_per_run": FRAMES,
        "ram_window": [START, END_EXCLUSIVE],
        "input_read_deltas_over_menu_entry": counts,
        "two_right_runs_reproduced_identical_ram_and_video": True,
        "metrics": validate(sets, counts),
        "copyright_boundary": "Only numeric RAM-derived measurements; no source ROM, save state, framebuffer, game assets or raw RAM exported.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        core, info = setup_core(args.core)
        data, sha = safe_rom(args.rom, args.expected_sha256)
        result = measure(core, info, args.rom, data, sha)
        result["core_binary_sha256"] = hashlib.sha256(args.core.read_bytes()).hexdigest()
        serialized = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.write_text(serialized, encoding="utf-8")
        else:
            print(serialized, end="")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Private Genesis camera-state probe: {error}\n")


if __name__ == "__main__":
    main()
