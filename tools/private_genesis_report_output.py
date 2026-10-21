#!/usr/bin/env python3
"""Stage193: validate a numeric-only original-emulation report before private output.

ROM-free library. Does not execute any emulator, inspect game media, or
independently attest to the origin of a caller-supplied JSON object.
"""
import json
import os
from pathlib import Path
import stat
import tempfile

from city_fractional_witness_comparison import compare
from reference_evidence_integrity import read_inputs


def require(ok, message):
    if not ok:
        raise ValueError("Private Genesis report: " + message)


def validate_numeric_report(report, repository_root):
    """Run Stage189's strict schema/provenance/phase validator before export."""
    root = Path(repository_root)
    observations, source = read_inputs(root)
    outcome = compare(observations, source, numeric_witness=report)
    require(outcome["status"] ==
            "USER_NUMERIC_WITNESS_NOT_INDEPENDENTLY_ATTESTED",
            "numeric witness comparison did not accept the report")
    require(outcome["original_rom_executed_by_this_comparison"] is False,
            "unexpected attestation claim")
    return json.dumps(report, indent=2, sort_keys=True,
                      ensure_ascii=False) + "\n"


def _is_within(child, parent):
    return child == parent or parent in child.parents


def private_output_path(destination, repository_root):
    """Require a fresh absolute .json file outside the public source tree."""
    target = Path(destination)
    require(target.is_absolute(), "output path must be absolute")
    require(target.suffix.lower() == ".json", "output must be a .json file")
    root = Path(repository_root).resolve()
    # Guard both the lexical path and the resolved path against symlinks.
    raw = Path(os.path.abspath(target))
    resolved = target.resolve(strict=False)
    require(not _is_within(raw, root) and
            not _is_within(resolved, root),
            "refusing to write a report inside the public repository")
    require(target.parent.is_dir() and
            not target.parent.is_symlink(),
            "private destination directory is missing or a symlink")
    require(not target.exists() and not target.is_symlink(),
            "private report destination must not already exist")
    return target


def write_private_numeric_report(report, destination, repository_root):
    """Validate first, then write atomically and exclusively with mode 0600.

    Hard-linking a complete temporary file avoids replacement of an existing
    report, including an existing symlink. Public Git trees are disallowed.
    """
    serialized = validate_numeric_report(report, repository_root)
    target = private_output_path(destination, repository_root)
    temporary = None
    try:
        fd, temporary = tempfile.mkstemp(prefix=".qs-stage193-", suffix=".tmp",
                                        dir=target.parent)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            os.fchmod(stream.fileno(), stat.S_IRUSR | stat.S_IWUSR)
            stream.write(serialized)
            stream.flush()
            os.fsync(stream.fileno())
        # Atomic exclusive publication: unlike os.replace, never overwrite.
        os.link(temporary, target, follow_symlinks=False)
        return None
    except OSError:
        # OSError strings may expose the user's private directory paths.
        raise ValueError("Private Genesis report: secure output failed") from None
    finally:
        if temporary is not None:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
