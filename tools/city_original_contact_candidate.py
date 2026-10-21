#!/usr/bin/env python3
"""Stage197: numeric-only, explicitly unattested original contact-plane candidate.

The published Genesis Y values are PLAYER RAM coordinates, NOT independently
known collision surfaces. Without an independent numeric contact-plane witness,
the contact offset remains unknown. Does not run an original game or decode RAM.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import tempfile

from reference_evidence_integrity import read_inputs, validate
from private_genesis_report_output import private_output_path

Q16 = 65536
SCHEMA = "quackshot_private_genesis_contact_planes_numeric_v1"
SAMPLES = {("upper", 1), ("upper", 2), ("lower", 1), ("lower", 2)}
ALLOWED = {
    "schema", "status", "reference_rom_sha256",
    "genesis_core_sha256", "ram_checkpoint_phase",
    "contact_plane_method", "contains_original_media_or_ram",
    "samples",
}
SAMPLE_FIELDS = {"site", "repeat", "player_ram_y_q16",
                 "independent_contact_plane_y_q16"}


def require(value, explanation):
    if not value:
        raise ValueError("CITY original contact candidate: " + explanation)


def integer(value, name, low, high):
    require(type(value) is int and low <= value <= high,
            "invalid numeric field " + name)
    return value


def analyze(observations, source, witness=None):
    provenance = validate(observations, source)
    ledge = observations["obstacles"]["ledge"]
    upper = ledge["world_y_upper_floor"]
    lower = ledge["lower_floor_world_xy"][1]
    require(type(upper) is int and type(lower) is int and
            (upper, lower) == (176, 208) and
            ledge["drop_pixels"] == 32,
            "published PLAYER RAM Y anchors changed")

    result = {
        "schema": "quackshot_city_contact_anchor_candidate_v1",
        "status": ("EXTERNAL_CONTACT_NUMBERS_NOT_INDEPENDENTLY_ATTESTED"
                   if witness is not None else
                   "ORIGINAL_COLLISION_SURFACES_NOT_MEASURED"),
        "original_rom_executed_by_this_analyzer": False,
        "original_media_or_raw_ram_in_report": False,
        "reference_rom_sha256": provenance["rom_sha256"],
        "published_player_ram_y": {"upper": upper, "lower": lower,
                                   "delta": lower - upper},
        "independent_original_contact_planes_available": witness is not None,
        "contact_offset_candidate_q16": None,
        "contact_offset_candidate_px": None,
        "original_hitbox_size_verified": False,
        "original_anchor_independently_attested": False,
        "safe_to_modify_switch_physics": False,
        "scope": (
            "Only a proposed constant foot-contact-plane minus PLAYER RAM Y "
            "offset. Numeric inputs are untrusted external testimony; "
            "no original sprite or tile collision geometry is verified."
        ),
    }
    if witness is None:
        return result
    require(type(witness) is dict and set(witness) == ALLOWED,
            "missing/extra witness fields or forbidden payload")
    require(witness["schema"] == SCHEMA and
            witness["status"] == "PRIVATE_CONTACT_NUMERIC_UNATTESTED",
            "external contact witness schema/status differs")
    require(witness["reference_rom_sha256"] == provenance["rom_sha256"] and
            witness["genesis_core_sha256"] == provenance["genesis_core_sha256"],
            "pinned ROM or core digest differs")
    require(witness["ram_checkpoint_phase"] ==
            "post_retro_run_before_next_input",
            "RAM sampling phase differs")
    require(witness["contact_plane_method"] ==
            "independent_collision_boundary_experiment",
            "independent contact-plane method must be declared")
    require(witness["contains_original_media_or_ram"] is False,
            "original media or raw RAM must not be published")
    samples = witness["samples"]
    require(type(samples) is list and len(samples) == 4,
            "expected exactly four independent numeric samples")
    seen = {}
    for row in samples:
        require(type(row) is dict and set(row) == SAMPLE_FIELDS,
                "sample has forbidden or missing fields")
        site, repeat = row["site"], row["repeat"]
        require(type(site) is str and site in ("upper", "lower") and
                type(repeat) is int and repeat in (1, 2) and
                (site, repeat) not in seen, "duplicate or invalid sample key")
        ram = integer(row["player_ram_y_q16"], "player_ram_y_q16",
                      0, 1024 * Q16)
        plane = integer(row["independent_contact_plane_y_q16"],
                        "independent_contact_plane_y_q16", 0, 1024 * Q16)
        expected = upper if site == "upper" else lower
        require(ram // Q16 == expected,
                "private PLAYER RAM integer Y does not match pinned evidence")
        seen[site, repeat] = (ram, plane, plane - ram)
    require(set(seen) == SAMPLES, "both independent repetitions per site required")
    offsets = [seen[key][2] for key in sorted(SAMPLES)]
    require(len(set(offsets)) == 1,
            "proposed original contact offset is not repeatable/constant")
    require(seen["upper", 1][1] == seen["upper", 2][1] and
            seen["lower", 1][1] == seen["lower", 2][1],
            "contact plane differs across independent repeats")
    require(seen["lower", 1][1] - seen["upper", 1][1] ==
            32 * Q16,
            "contact plane separation differs from two original RAM levels")
    result["contact_offset_candidate_q16"] = offsets[0]
    result["contact_offset_candidate_px"] = offsets[0] / Q16
    return result


def _exclusive_private_write(serialized, output, root):
    target = private_output_path(output, root)
    tmp = None
    try:
        fd, tmp = tempfile.mkstemp(prefix=".qs-contact197-", suffix=".tmp",
                                   dir=target.parent)
        with os.fdopen(fd, "w", encoding="utf-8") as file:
            os.fchmod(file.fileno(), stat.S_IRUSR | stat.S_IWUSR)
            file.write(serialized)
            file.flush()
            os.fsync(file.fileno())
        os.link(tmp, target, follow_symlinks=False)
    except OSError:
        raise ValueError("CITY original contact candidate: "
                         "private numeric output failed") from None
    finally:
        if tmp:
            try:
                os.unlink(tmp)
            except FileNotFoundError:
                pass


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path,
                   default=Path(__file__).resolve().parents[1])
    p.add_argument("--numeric-witness", type=Path)
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    try:
        root = args.root.resolve()
        observations, source = read_inputs(root)
        witness = (json.loads(args.numeric_witness.read_text(encoding="utf-8"))
                   if args.numeric_witness else None)
        report = analyze(observations, source, witness)
        serialized = json.dumps(report, sort_keys=True, indent=2,
                                ensure_ascii=False) + "\n"
        if witness is not None:
            require(args.out is not None, "private witness requires --out")
            _exclusive_private_write(serialized, args.out, root)
        elif args.out:
            require(args.out.resolve() !=
                    (root / "src/qs_game.c").resolve() and
                    not args.out.exists(),
                    "refusing to overwrite an existing source/output file")
            args.out.write_text(serialized, encoding="utf-8")
        else:
            print(serialized, end="")
    except (ValueError, TypeError, KeyError, OSError,
            json.JSONDecodeError) as exc:
        p.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
