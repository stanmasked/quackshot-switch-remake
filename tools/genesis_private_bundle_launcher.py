#!/usr/bin/env python3
"""Stage204: fail-closed offline preflight / private Genesis research launcher.

No original ROM is needed for --preflight. --run reads an existing user's
local original ROM only after validating the full Stage203 SHA manifest and
the *exact historical instrumented core*; it never copies/uploads a ROM.
No original-game numeric output is printed, only a generic completion tag.
"""
import argparse
import json
from pathlib import Path

from build_genesis_private_bundle import CORE_NAME, MANIFEST, verify
from genesis_blue_roi_numeric import validate_roi
from genesis_city_same_step_triplet_probe import (
    ROM_SHA, INSTRUMENTED_CORE_SHA, validate_instrumented_vdp_archive,
    VDP_ARCHIVE_PATH, collect as collect_triplet, private_write,
)
from genesis_city_video_vdp_bracket_probe import collect as collect_bracket
from genesis_libretro_probe import safe_rom, setup_core
from private_genesis_input_guard import PinnedOriginalInputs
from private_genesis_report_output import private_output_path
from reference_evidence_integrity import read_inputs, validate

HISTORIC_CORE_STATUS = "CORE_BINARY_MATCHES_HISTORIC_VDP_WITNESS"
MODES = ("bracket", "triplet")


def require(condition, reason):
    if not condition:
        raise ValueError("Genesis private launcher: " + reason)


def preflight(bundle_root):
    """No user ROM touched; enforce full package SHA and original provenance."""
    root = Path(bundle_root)
    require(root.is_dir() and not root.is_symlink(),
            "expected a complete nonsymlinked bundle directory")
    manifest = verify(root)
    require(manifest["schema"] == "quackshot_rom_free_genesis_research_bundle_v1" and
            manifest["status"] == HISTORIC_CORE_STATUS and
            manifest["packaged_core_matches_historical_binary"] is True and
            manifest["packaged_core_sha256"] == INSTRUMENTED_CORE_SHA and
            manifest["historical_instrumented_core_sha256"] == INSTRUMENTED_CORE_SHA and
            manifest["source_rom_sha256_reference"] == ROM_SHA and
            manifest["original_game_media_in_bundle"] is False and
            manifest["original_collision_geometry_verified"] is False and
            manifest["safe_to_modify_switch_physics"] is False,
            "bundle is not the historical SHA-pinned numerical research environment")
    prior = validate(*read_inputs(root))
    require(prior["rom_sha256"] == ROM_SHA, "reference numeric ROM provenance differs")
    vdp = json.loads((root / VDP_ARCHIVE_PATH).read_text(encoding="utf-8"))
    validate_instrumented_vdp_archive(vdp)
    require((root / MANIFEST).is_file() and (root / CORE_NAME).is_file(),
            "pinned research core or manifest absent")
    return {"bundle_sha_verified": True, "historical_binary_match": True,
            "original_game_executed_by_preflight": False,
            "safe_to_modify_switch_physics": False}


def prepare_run(bundle_root, rom, output, roi, mode):
    """Validate all local prerequisites before opening the original ROM."""
    root = Path(bundle_root)
    preflight(root)
    require(mode in MODES, "unknown bounded private experiment mode")
    validate_roi(roi)
    target = private_output_path(output, root)
    rom_path = Path(rom)
    require(rom_path.is_absolute(), "private ROM must use an absolute local path")
    require(rom_path.resolve() != (root / CORE_NAME).resolve() and
            root.resolve() not in rom_path.resolve().parents,
            "original ROM must remain outside the research bundle")
    # Reuse the Stage192 inode/content guard; reject changed/symlink media.
    guard = PinnedOriginalInputs(rom_path, root / CORE_NAME,
                                 ROM_SHA, INSTRUMENTED_CORE_SHA)
    guard()
    return guard, target


def run_private(bundle_root, rom, output, roi, mode):
    guard, target = prepare_run(bundle_root, rom, output, roi, mode)
    root = Path(bundle_root)
    core_path = root / CORE_NAME
    # Only after the full manifest and original ROM/core SHA checks pass
    # may the libretro emulator receive copyrighted ROM bytes in memory.
    core, metadata = setup_core(core_path)
    rom_bytes, found_sha = safe_rom(Path(rom), ROM_SHA)
    require(found_sha == ROM_SHA, "original source ROM changed")
    guard()
    collector = collect_bracket if mode == "bracket" else collect_triplet
    report = collector(core, metadata, Path(rom), rom_bytes, tuple(roi), guard)
    guard()
    require(type(report) is dict and
            report.get("safe_to_modify_switch_physics") is False and
            report.get("original_rom_executed_locally") is True and
            report.get("reference_rom_sha256") == ROM_SHA and
            report.get("instrumented_core_sha256") == INSTRUMENTED_CORE_SHA,
            "private report not from pinned numeric experiment")
    private_write(report, target, root)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path,
                        default=Path(__file__).resolve().parents[1])
    choices = parser.add_mutually_exclusive_group(required=True)
    choices.add_argument("--preflight", action="store_true")
    choices.add_argument("--run", action="store_true")
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mode", choices=MODES, default="bracket")
    parser.add_argument("--roi", type=int, nargs=4,
                        default=(100, 80, 85, 140))
    args = parser.parse_args()
    try:
        if args.preflight:
            require(args.rom is None and args.output is None,
                    "preflight never accepts original ROM/output arguments")
            preflight(args.bundle)
            print("GENESIS_STAGE204_PRIVATE_BUNDLE_PREFLIGHT_PASS")
        else:
            require(args.rom is not None and args.output is not None,
                    "private run needs local ROM and fresh private output")
            run_private(args.bundle, args.rom, args.output,
                        tuple(args.roi), args.mode)
            print("GENESIS_STAGE204_PRIVATE_NUMERIC_RUN_PASS")
    except (OSError, ValueError, RuntimeError, TypeError, KeyError,
            SyntaxError, json.JSONDecodeError):
        # Neither the local original path nor private numeric measurements
        # are ever echoed on error. Avoid printing exception reprs.
        parser.exit(1, "GENESIS_STAGE204_PRIVATE_BUNDLE_OR_RUN_REJECTED\n")


if __name__ == "__main__":
    main()
