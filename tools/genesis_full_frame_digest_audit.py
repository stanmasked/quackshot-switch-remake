#!/usr/bin/env python3
"""Stage217: offline audit of transient full-frame SHA-256 digests.

The input contains frame digests and numeric callback metadata only. It does
not contain ROM bytes, raw RAM, framebuffer bytes, or screenshots. Matching
cryptographic digests strongly support byte equality but do not prove sprite
identity, collision geometry, or the cause of a repeated image.
"""
import argparse
import hashlib
import json
import os
import re
import stat
from pathlib import Path

from genesis_city_same_step_triplet_probe import INSTRUMENTED_CORE_SHA, ROM_SHA

SCHEMA = "quackshot_private_genesis_full_frame_digest_runs_v1"
RUN_NAMES = ("near_wall_jump", "near_wall_jump_repeat", "right_control")
FIRST_FRAME, LAST_FRAME = 2350, 2410
EXPECTED_FRAMES = list(range(FIRST_FRAME, LAST_FRAME + 1))
FRAMES_REQUESTED = 2600
FRAME_SIZE = [320, 224]
PIXEL_FORMAT = 2
JUMP_INPUTS = [
    {"first_frame": 450, "end_exclusive": 462, "button": "START"},
    {"first_frame": 680, "end_exclusive": 692, "button": "START"},
    {"first_frame": 850, "end_exclusive": 862, "button": "START"},
    {"first_frame": 1100, "end_exclusive": 1105, "button": "START"},
    {"first_frame": 1145, "end_exclusive": 1150, "button": "START"},
    {"first_frame": 1190, "end_exclusive": 1195, "button": "START"},
    {"first_frame": 1235, "end_exclusive": 1240, "button": "START"},
    {"first_frame": 1280, "end_exclusive": 1285, "button": "START"},
    {"first_frame": 1325, "end_exclusive": 1330, "button": "START"},
    {"first_frame": 1370, "end_exclusive": 1375, "button": "START"},
    {"first_frame": 2070, "end_exclusive": 2510, "button": "RIGHT"},
    {"first_frame": 2310, "end_exclusive": 2350, "button": "A"},
]
CONTROL_INPUTS = JUMP_INPUTS[:10] + [
    {"first_frame": 2070, "end_exclusive": 2520, "button": "RIGHT"},
]
TOP_FIELDS = {
    "schema", "stage", "status", "reference_rom_sha256",
    "research_core_sha256", "frame_window_inclusive",
    "frames_requested_per_run", "frame_size", "video_callback_phase",
    "runs", "original_media_exported", "raw_frame_pixels_exported",
    "raw_ram_exported", "full_frame_bytes_saved", "player_identity_verified",
    "collision_geometry_verified", "safe_to_modify_switch_physics",
}
RUN_FIELDS = {
    "input_ranges", "input_nonzero_reads", "frames_requested",
    "frames_video_callbacks", "frames_without_video_callback",
    "frames_with_multiple_video_callbacks", "video_callback_phase",
    "video_digest_frames_requested", "video_frame_digests", "video_crc32",
}
DIGEST_FIELDS = {
    "frame", "size", "pitch", "pixel_format", "byte_count", "sha256",
    "visible_byte_count", "visible_sha256",
}
CRC_FIELDS = {"frame", "crc32"}
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
CRC_RE = re.compile(r"^[0-9A-F]{8}$")
MAX_REPORT_BYTES = 512 * 1024


def require(ok, message):
    if not ok:
        raise ValueError("Stage217 full-frame digest audit: " + message)


def _contiguous_runs(values):
    """Return longest contiguous run of equal digest values."""
    if not values:
        return None
    best_start = current_start = values[0][0]
    best_end = current_end = values[0][0]
    best_value = current_value = values[0][1]
    for frame, value in values[1:]:
        if frame == current_end + 1 and value == current_value:
            current_end = frame
        else:
            if current_end - current_start > best_end - best_start:
                best_start, best_end, best_value = current_start, current_end, current_value
            current_start = current_end = frame
            current_value = value
    if current_end - current_start > best_end - best_start:
        best_start, best_end, best_value = current_start, current_end, current_value
    return {
        "first_frame": best_start,
        "last_frame": best_end,
        "length": best_end - best_start + 1,
        "digest": best_value,
    }


def _validate_run(name, run):
    require(type(run) is dict and set(run) == RUN_FIELDS,
            name + ": unexpected or missing run fields")
    expected_inputs = CONTROL_INPUTS if name == "right_control" else JUMP_INPUTS
    require(run["input_ranges"] == expected_inputs,
            name + ": scripted input ranges differ from the pinned experiment")
    expected_reads = 521 if name == "right_control" else 551
    require(type(run["input_nonzero_reads"]) is int and
            run["input_nonzero_reads"] == expected_reads,
            name + ": nonzero input-read count differs")
    require(run["frames_requested"] == FRAMES_REQUESTED and
            run["frames_video_callbacks"] == FRAMES_REQUESTED and
            run["frames_without_video_callback"] == 0 and
            run["frames_with_multiple_video_callbacks"] == 0 and
            run["video_callback_phase"] == "during_retro_run",
            name + ": video callback cadence or phase differs")
    require(run["video_digest_frames_requested"] == EXPECTED_FRAMES,
            name + ": requested digest frame sequence differs")
    digests = run["video_frame_digests"]
    crcs = run["video_crc32"]
    require(type(digests) is list and len(digests) == len(EXPECTED_FRAMES),
            name + ": full-frame digest count differs")
    require(type(crcs) is list and len(crcs) == len(EXPECTED_FRAMES),
            name + ": CRC32 frame count differs")
    for frame, digest, crc in zip(EXPECTED_FRAMES, digests, crcs):
        require(type(digest) is dict and set(digest) == DIGEST_FIELDS and
                type(digest["frame"]) is int and digest["frame"] == frame,
                name + ": digest row schema or frame sequence differs")
        require(digest["size"] == FRAME_SIZE and
                type(digest["pitch"]) is int and
                FRAME_SIZE[0] * 2 <= digest["pitch"] <= 16384 and
                digest["pixel_format"] == PIXEL_FORMAT and
                digest["byte_count"] == digest["pitch"] * FRAME_SIZE[1] and
                digest["visible_byte_count"] ==
                FRAME_SIZE[0] * FRAME_SIZE[1] * 2,
                name + ": frame geometry or byte counts differ")
        require(type(digest["sha256"]) is str and SHA_RE.fullmatch(digest["sha256"]) and
                type(digest["visible_sha256"]) is str and
                SHA_RE.fullmatch(digest["visible_sha256"]),
                name + ": malformed SHA-256 digest")
        require(type(crc) is dict and set(crc) == CRC_FIELDS and
                type(crc["frame"]) is int and crc["frame"] == frame and
                type(crc["crc32"]) is str and CRC_RE.fullmatch(crc["crc32"]),
                name + ": malformed CRC32 row")
    return digests, crcs


def audit_report(report):
    """Validate a bounded digest report and return a small numeric-only verdict."""
    require(type(report) is dict and set(report) == TOP_FIELDS and
            report["schema"] == SCHEMA and report["stage"] == 217 and
            report["status"] == "NUMERIC_DIGESTS_ONLY",
            "top-level schema or stage marker differs")
    require(report["reference_rom_sha256"] == ROM_SHA and
            report["research_core_sha256"] == INSTRUMENTED_CORE_SHA,
            "pinned source ROM/core labels differ")
    require(report["frame_window_inclusive"] == [FIRST_FRAME, LAST_FRAME] and
            report["frames_requested_per_run"] == FRAMES_REQUESTED and
            report["frame_size"] == FRAME_SIZE and
            report["video_callback_phase"] == "during_retro_run",
            "frame window or callback metadata differs")
    for key in ("original_media_exported", "raw_frame_pixels_exported",
                "raw_ram_exported", "full_frame_bytes_saved",
                "player_identity_verified", "collision_geometry_verified",
                "safe_to_modify_switch_physics"):
        require(report[key] is False, "unsupported data-export or physics claim")
    runs = report["runs"]
    require(type(runs) is dict and set(runs) == set(RUN_NAMES),
            "required jump/repeat/control runs missing")
    checked = {name: _validate_run(name, runs[name]) for name in RUN_NAMES}
    jump, repeat, control = (checked[n] for n in RUN_NAMES)
    jump_digest, jump_crc = jump
    repeat_digest, repeat_crc = repeat
    control_digest, control_crc = control

    require(jump_digest == repeat_digest and jump_crc == repeat_crc,
            "independent jump repeat differs at full-frame digest or CRC32 level")
    jump_by_frame = {x["frame"]: x for x in jump_digest}
    control_by_frame = {x["frame"]: x for x in control_digest}
    full_diff = sum(jump_by_frame[f]["sha256"] != control_by_frame[f]["sha256"]
                    for f in EXPECTED_FRAMES)
    visible_diff = sum(jump_by_frame[f]["visible_sha256"] !=
                       control_by_frame[f]["visible_sha256"]
                       for f in EXPECTED_FRAMES)

    crc_by_frame = {x["frame"]: x["crc32"] for x in jump_crc}
    plateau_frames = list(range(2364, 2385))
    require(all(crc_by_frame[f] == "7BF0AAE9" for f in plateau_frames),
            "pinned 21-frame CRC32 plateau differs")
    require(crc_by_frame[2363] == "95731692" and
            crc_by_frame[2385] == "A7AB14AA",
            "CRC32 plateau boundary anchors differ")
    full_runs = _contiguous_runs([(f, jump_by_frame[f]["sha256"])
                                  for f in EXPECTED_FRAMES])
    visible_runs = _contiguous_runs([(f, jump_by_frame[f]["visible_sha256"])
                                     for f in EXPECTED_FRAMES])
    require(full_runs["first_frame"] == 2364 and
            full_runs["last_frame"] == 2384 and full_runs["length"] == 21,
            "longest full-buffer SHA-256 plateau differs")
    require(visible_runs["first_frame"] == 2364 and
            visible_runs["last_frame"] == 2384 and visible_runs["length"] == 21,
            "longest visible-pixel SHA-256 plateau differs")
    require(jump_by_frame[2363]["visible_sha256"] !=
            jump_by_frame[2364]["visible_sha256"] and
            jump_by_frame[2384]["visible_sha256"] !=
            jump_by_frame[2385]["visible_sha256"],
            "visible-pixel digest plateau boundary is not distinct")
    require(full_diff == len(EXPECTED_FRAMES) and
            visible_diff == len(EXPECTED_FRAMES),
            "jump/control output difference count differs")

    return {
        "schema": "quackshot_stage217_full_frame_digest_audit_result_v1",
        "status": "PASS_REPEAT_SHA256_AND_21_FRAME_PLATEAU_CONFIRMED",
        "frames_checked_per_run": len(EXPECTED_FRAMES),
        "jump_repeat_full_buffer_sha256_exact": True,
        "jump_repeat_visible_pixel_sha256_exact": True,
        "jump_repeat_crc32_exact": True,
        "jump_vs_right_control_full_buffer_digest_differences": full_diff,
        "jump_vs_right_control_visible_pixel_digest_differences": visible_diff,
        "longest_full_buffer_sha256_run": full_runs,
        "longest_visible_pixel_sha256_run": visible_runs,
        "crc32_plateau": {
            "first_frame": 2364, "last_frame": 2384, "length": 21,
            "crc32": "7BF0AAE9",
        },
        "raw_frame_pixels_saved": False,
        "raw_ram_saved": False,
        "player_identity_verified": False,
        "collision_geometry_verified": False,
        "plateau_cause_verified": False,
        "safe_to_modify_switch_physics": False,
        "interpretation": (
            "Matching full-buffer and visible-pixel SHA-256 digests strongly support "
            "identical callback output over frames 2364-2384. The digest does not "
            "identify Donald, establish wall geometry, or determine why the image repeats."
        ),
    }


def read_private_report(path):
    """Read a private report only from a regular owner-only JSON file."""
    path = Path(path)
    repo_root = Path(__file__).resolve().parents[1]
    require(path.is_absolute() and path.suffix.lower() == ".json" and
            path.is_file() and not path.is_symlink(),
            "report must be an existing absolute private JSON path")
    require(repo_root.resolve() not in path.resolve().parents,
            "private report must remain outside the public checkout")
    info = path.stat()
    require(stat.S_ISREG(info.st_mode) and stat.S_IMODE(info.st_mode) & 0o077 == 0 and
            1 <= info.st_size <= MAX_REPORT_BYTES,
            "report size or permissions are unsafe")
    raw = path.read_bytes()
    require(len(raw) == info.st_size and len(raw) <= MAX_REPORT_BYTES,
            "report changed while reading")
    return json.loads(raw.decode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True,
                        help="Private owner-only JSON report; never a ROM or framebuffer")
    parser.add_argument("--output", type=Path,
                        help="Optional private owner-only output path")
    args = parser.parse_args()
    try:
        result = audit_report(read_private_report(args.report))
        if args.output:
            path = args.output
            repo_root = Path(__file__).resolve().parents[1]
            require(path.is_absolute() and path.suffix.lower() == ".json" and
                    not path.exists() and not path.is_symlink() and
                    repo_root.resolve() not in path.resolve().parents,
                    "output must be a new absolute JSON path outside the public checkout")
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                         stat.S_IRUSR | stat.S_IWUSR)
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump(result, stream, ensure_ascii=False, indent=2, sort_keys=True)
                stream.write("\n")
        print("GENESIS_STAGE217_FULL_FRAME_DIGEST_AUDIT_PASS")
    except (ValueError, OSError, TypeError, KeyError, UnicodeError,
            json.JSONDecodeError, OverflowError):
        parser.exit(1, "GENESIS_STAGE217_FULL_FRAME_DIGEST_AUDIT_REJECTED\n")


if __name__ == "__main__":
    main()
