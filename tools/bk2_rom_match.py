#!/usr/bin/env python3
"""Private BK2/ROM digest candidate check; never output ROM bytes or hashes.

A 32-character value labelled SHA1 in some legacy movies could be an MD5
rather than a SHA1; a match is only a *candidate* for emulation replay.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from bk2_reference_audit import BK2ValidationError, audit

MAX_ROM_BYTES = 16 * 1024 * 1024


def check_rom(movie_path: Path, rom_path: Path) -> dict:
    """Compare declared movie digest to local ROM, without disclosing digests."""
    summary = audit(Path(movie_path))
    claimed = summary["rom_hash_claim"]
    if re.fullmatch(r"[a-fA-F0-9]{32}", claimed):
        algo = "md5_candidate_for_32_digit_SHA1_field"
        digest_fn = hashlib.md5  # legacy identity check only; not secure signing
    elif re.fullmatch(r"[a-fA-F0-9]{40}", claimed):
        algo = "sha1"
        digest_fn = hashlib.sha1
    else:
        algo = None
        digest_fn = None

    try:
        with Path(rom_path).open("rb") as stream:
            rom = stream.read(MAX_ROM_BYTES + 1)
    except OSError as exc:
        raise BK2ValidationError("Cannot read local ROM") from exc
    if len(rom) < 512 or len(rom) > MAX_ROM_BYTES:
        raise BK2ValidationError("Local ROM size outside supported bounded range")

    match = digest_fn(rom).hexdigest().casefold() == claimed.casefold() if digest_fn else None
    status = ("candidate_hash_match" if match is True else
              "candidate_hash_mismatch" if match is False else
              "unverifiable_movie_hash")
    return {
        "schema": "QuackShot-BK2-private-ROM-digest-check-v1",
        "movie_frame_count": summary["frame_count"],
        "declared_hash_label": "SHA1",
        "candidate_hash_algorithm": algo,
        "candidate_digest_matches_local_rom": match,
        "status": status,
        "rom_size_bytes": len(rom),
        "rom_hash_or_bytes_exported": False,
        "replay_compatibility_verified": False,
        "notes": (
            "MD5 is a legacy candidate identity comparison only; collisions exist."
            " Matching hashes do not establish emulator timing or input replay synchronization."
        ),
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bk2", type=Path, help="private, local BizHawk movie")
    parser.add_argument("rom", type=Path, help="private, local original Genesis ROM")
    parser.add_argument("--out", type=Path, help="optional local report JSON path")
    args = parser.parse_args(argv)
    try:
        report = check_rom(args.bk2, args.rom)
    except BK2ValidationError as exc:
        print(f"BK2_ROM_MATCH_FAIL: {exc}", file=sys.stderr)
        return 1
    data = json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.write_text(data, encoding="utf-8")
    else:
        sys.stdout.write(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
