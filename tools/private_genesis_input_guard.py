#!/usr/bin/env python3
"""Stage192: local-only integrity checks for a private Genesis ROM and core.

Never exports file paths, original bytes, file stats or emulator binaries.
This is a tamper/stability check, not an independent attestation of gameplay.
"""
import hashlib
import os
from pathlib import Path
import re
import stat


def require(ok, message):
    if not ok:
        raise ValueError("Private Genesis input guard: " + message)


def file_snapshot(path, label):
    """Return only an in-process fingerprint; refuse symlinks and file swaps."""
    path = Path(path)
    try:
        before = path.lstat()
        require(stat.S_ISREG(before.st_mode),
                label + " is not a regular local file")
        h = hashlib.sha256()
        with path.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            require(stat.S_ISREG(opened.st_mode), label + " changed file type")
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                h.update(block)
            closed = os.fstat(stream.fileno())
        after = path.lstat()
        fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        values = lambda s: tuple(getattr(s, key) for key in fields)
        require(stat.S_ISREG(after.st_mode) and
                values(before) == values(opened) ==
                values(closed) == values(after),
                label + " changed while hashing")
        return (h.hexdigest(), values(after))
    except OSError:
        # OSError normally includes private path; do not copy it into logs.
        raise ValueError(
            "Private Genesis input guard: " + label +
            " cannot be read as a stable local file") from None


class PinnedOriginalInputs:
    """Fail closed before/after each run; identity never appears in the report."""

    def __init__(self, rom_path, core_path, expected_rom_sha256,
                 expected_core_sha256):
        self.rom_path = Path(rom_path)
        self.core_path = Path(core_path)
        self.expected = {
            "original ROM": expected_rom_sha256,
            "Genesis Plus GX core": expected_core_sha256,
        }
        for label, digest in self.expected.items():
            require(isinstance(digest, str) and
                    re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
                    label + " has invalid expected SHA256")
        self._initial = None

    def __call__(self):
        # Never include private paths or inode/mtime in error messages.
        found = {
            "original ROM": file_snapshot(self.rom_path, "original ROM"),
            "Genesis Plus GX core": file_snapshot(self.core_path,
                                                   "Genesis Plus GX core"),
        }
        for label, (digest, _) in found.items():
            require(digest == self.expected[label],
                    label + " SHA256 differs from pinned reference")
        if self._initial is None:
            self._initial = found
        else:
            for label in found:
                require(found[label] == self._initial[label],
                        label + " file identity/content changed between runs")
        return None
