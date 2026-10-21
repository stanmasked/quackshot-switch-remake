#!/usr/bin/env python3
"""Audit private BizHawk Genesis BK2 input; output aggregate metadata only.

No ROM, full frame-by-frame timeline, graphics, or sound is exported.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

BUTTONS = ("Up", "Down", "Left", "Right", "A", "B", "C", "Start", "X", "Y", "Z", "Mode")
SYMBOLS = "UDLRABCSXYZM"
LOG_KEY = ("LogKey:#Power|Reset|#P1 Up|" + "|".join(f"P1 {b}" for b in BUTTONS[1:])
           + "|#P2 Up|" + "|".join(f"P2 {b}" for b in BUTTONS[1:]) + "|")
REQUIRED = frozenset(("Header.txt", "SyncSettings.json", "Input Log.txt"))
MAX_UNCOMPRESSED_BYTES = 15_000_000
MAX_ARCHIVE_BYTES = 16_000_000
MAX_FRAMES = 500_000

class BK2ValidationError(ValueError):
    """BK2 archive is invalid or has an unsupported input layout."""

def audit(path: Path) -> dict:
    path = Path(path)
    try:
        archive_size = path.stat().st_size
    except OSError as exc:
        raise BK2ValidationError("Cannot inspect BK2 file") from exc
    if archive_size > MAX_ARCHIVE_BYTES:
        raise BK2ValidationError("BK2 compressed archive exceeds safe size bound")
    try:
        with zipfile.ZipFile(path) as archive:
            info = archive.infolist()
            names = [x.filename for x in info]
            starts_from_core_state = "CoreState" in names
            if len(set(names)) != len(names):
                raise BK2ValidationError("Duplicate members are not permitted")
            if not REQUIRED.issubset(names):
                raise BK2ValidationError("BK2 is missing required input metadata")
            if sum(x.file_size for x in info) > MAX_UNCOMPRESSED_BYTES:
                raise BK2ValidationError("BK2 data exceeds safe size bound")
            header_bytes = archive.read("Header.txt")
            settings_bytes = archive.read("SyncSettings.json")
            movie_bytes = archive.read("Input Log.txt")
    except (OSError, zipfile.BadZipFile, RuntimeError, EOFError) as exc:
        raise BK2ValidationError("Cannot open BK2 ZIP archive") from exc
    try:
        head_text = header_bytes.decode("utf-8-sig")
        raw_text = movie_bytes.decode("utf-8-sig")
        sync = json.loads(settings_bytes.decode("utf-8-sig"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise BK2ValidationError("Invalid UTF-8 or sync JSON") from exc
    header = {}
    for line in head_text.splitlines():
        if line.strip():
            if " " not in line:
                raise BK2ValidationError("Invalid BK2 header line")
            key, value = line.split(" ", 1)
            if key in header:
                raise BK2ValidationError("Duplicate BK2 header key")
            header[key] = value.strip()
    if header.get("Platform") != "GEN":
        raise BK2ValidationError("Only BizHawk GEN movies are supported")
    if not isinstance(sync, dict):
        raise BK2ValidationError("Invalid sync settings structure")
    lines = raw_text.splitlines()
    if len(lines) < 4 or lines[0] != "[Input]" or lines[1] != LOG_KEY or lines[-1] != "[/Input]":
        raise BK2ValidationError("Input markers/Genesis controller LogKey differ")
    frame_count = len(lines) - 3
    if frame_count < 1 or frame_count > MAX_FRAMES:
        raise BK2ValidationError("Frame count outside bounded range")
    counts = Counter()
    rising = Counter()
    prev_mask = 0
    distinct = set()
    starts = []
    idle = p2_used = system_used = up_down = left_right = 0
    first_active = None
    rle_segments = 0
    last_state = None
    for index, line in enumerate(lines[2:-1]):
        chunks = line.split("|")
        if len(chunks) != 5 or chunks[0] != "" or chunks[4] != "":
            raise BK2ValidationError(f"Malformed input row at frame {index}")
        system, p1, p2 = chunks[1:4]
        if len(system) != 2 or any(ch not in ".PR" for ch in system):
            raise BK2ValidationError(f"Invalid power/reset data at frame {index}")
        for player in (p1, p2):
            if len(player) != 12 or any(ch not in (".", SYMBOLS[j]) for j, ch in enumerate(player)):
                raise BK2ValidationError(f"Invalid controller bit layout at frame {index}")
        mask = sum(1 << j for j, ch in enumerate(p1) if ch != ".")
        distinct.add(mask)
        for j, b in enumerate(BUTTONS):
            bit = 1 << j
            if mask & bit:
                counts[b] += 1
            if mask & bit and not prev_mask & bit:
                rising[b] += 1
        if mask & 128 and not prev_mask & 128:
            starts.append(index)
        if (mask & 1) and (mask & 2):
            up_down += 1
        if (mask & 4) and (mask & 8):
            left_right += 1
        if system != "..":
            system_used += 1
        if p2 != "." * 12:
            p2_used += 1
        if system == ".." and mask == 0 and p2 == "." * 12:
            idle += 1
        elif first_active is None:
            first_active = index
        state = (system, p1, p2)
        if state != last_state:
            rle_segments += 1
            last_state = state
        prev_mask = mask
    claim = header.get("SHA1", "")
    return {
        "schema": "QuackShot-BK2-aggregate-v1",
        "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "core_state_in_movie": starts_from_core_state,
        "movie_start_kind": "savestate" if starts_from_core_state else "power_on_or_reset",
        "game_name_claim": header.get("GameName", ""),
        "platform": header["Platform"],
        "bizhawk_version_claim": header.get("MovieVersion", ""),
        "emulator_version_claim": header.get("emuVersion", ""),
        "core_claim": header.get("Core", ""),
        "author_claim": header.get("Author", ""),
        "rerecord_count_claim": header.get("rerecordCount", ""),
        "rom_hash_label_in_movie": "SHA1",
        "rom_hash_claim": claim,
        "rom_sha1_is_40_hex": bool(re.fullmatch(r"[0-9A-Fa-f]{40}", claim)),
        "sync_settings": sync,
        "frame_count": frame_count,
        "duration_seconds_if_60fps": round(frame_count / 60, 3),
        "fps_verified": False,
        "controller1_held_frames": {b: counts[b] for b in BUTTONS},
        "controller1_press_edges": {b: rising[b] for b in BUTTONS},
        "controller1_unique_states": len(distinct),
        "controller2_active_frames": p2_used,
        "system_active_frames": system_used,
        "completely_idle_frames": idle,
        "opposing_up_down_frames": up_down,
        "opposing_left_right_frames": left_right,
        "start_press_count": len(starts),
        "first_nonidle_frame": first_active,
        "rle_segment_count": rle_segments,
        "limitations": [
            "Movie is controller input only; it contains no actor positions or hitboxes.",
            "Start input is not proof of a level transition.",
            "ROM revision, core synchronization and FPS are unverified without private replay.",
            "This tool does not publish or output source BK2 frames or ROM data.",
        ],
    }

def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bk2", type=Path, help="local private BK2 file")
    parser.add_argument("--out", type=Path, help="optional local aggregate JSON path")
    args = parser.parse_args(argv)
    try:
        result = audit(args.bk2)
    except BK2ValidationError as exc:
        print(f"BK2_AUDIT_FAIL: {exc}", file=sys.stderr)
        return 1
    data = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.out:
        args.out.write_text(data, encoding="utf-8")
    else:
        sys.stdout.write(data)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
