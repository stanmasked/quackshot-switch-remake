#!/usr/bin/env python3
"""Stage203: deterministic public-numeric private-Genesis research bundle.

Builds a transitive Python dependency closure for Stage200/201 probes,
includes only published NUMERIC references and authored C99 physics, and
never bundles original game files. The binary SHA is reported separately:
a successful compilation is NOT proof of equality to the historic
instrumented-core SHA used in original-ROM observations.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import shutil

from genesis_city_same_step_triplet_probe import INSTRUMENTED_CORE_SHA, ROM_SHA
from reference_evidence_integrity import OBSERVATIONS, read_inputs, validate

SCHEMA = "quackshot_rom_free_genesis_research_bundle_v1"
MANIFEST = "private-research-manifest.json"
CORE_NAME = "genesis_plus_gx_libretro.so"
ENTRYPOINTS = (
    "genesis_private_cross_roi_consistency.py",
    "genesis_offline_numeric_report_audit.py",
    "genesis_private_bundle_launcher.py",
    "genesis_city_video_vdp_bracket_probe.py",
    "genesis_city_same_step_triplet_probe.py",
    "genesis_city_visual_roi_probe.py",
)
REFERENCE_PATHS = tuple(sorted(set(OBSERVATIONS.values()) | {
    "research/observations/user_rom_vdp_hscroll_2026-10-18.json"
}))
AUTHOR_SOURCE = "src/qs_game.c"
FORBIDDEN = {".gen", ".md", ".bin", ".bk2", ".rom", ".sav", ".srm",
             ".cue", ".iso", ".pkg", ".nro", ".fm2", ".zip", ".7z"}
KNOWN_ROOT = {
    CORE_NAME, MANIFEST, "LICENSE.txt", "BUILD_PROVENANCE.txt",
    "emulator-sha256.txt",
}


def require(condition, message):
    if not condition:
        raise ValueError("Private Genesis bundle: " + message)


def digest_file(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def regular_file(path, parent):
    # Refuse symlink files and directories, including intermediate segments.
    root = parent.resolve(strict=True)
    target = path
    require(path.is_file() and not path.is_symlink(),
            "expected regular public numeric/source file")
    while target != parent:
        require(not target.is_symlink(), "symlink in selected bundle path")
        target = target.parent
        require(parent in target.parents or target == parent,
                "path escapes source/bundle root")
    require(path.resolve(strict=True).is_relative_to(root),
            "file escapes trusted root")
    return path


def resolve_modules(source_root):
    """Use AST to recursively include only directly imported local .py files."""
    tool_root = source_root / "tools"
    visited = set()
    pending = list(ENTRYPOINTS)
    while pending:
        module = pending.pop()
        if module in visited:
            continue
        require(module.endswith(".py") and module == Path(module).name,
                "bad import module path")
        file = regular_file(tool_root / module, source_root)
        parsed = ast.parse(file.read_text(encoding="utf-8"), filename=module)
        visited.add(module)
        for node in ast.walk(parsed):
            names = ()
            if isinstance(node, ast.ImportFrom):
                require(node.level == 0, "relative import not supported")
                if node.module:
                    names = (node.module.split(".")[0],)
            elif isinstance(node, ast.Import):
                names = tuple(n.name.split(".")[0] for n in node.names)
            for name in names:
                candidate = name + ".py"
                if (candidate not in visited and
                        (tool_root / candidate).is_file()):
                    pending.append(candidate)
    return tuple(sorted(visited))


def chosen_paths(source_root):
    modules = resolve_modules(source_root)
    result = {("tools/" + x) for x in modules}
    result.update(REFERENCE_PATHS)
    result.add(AUTHOR_SOURCE)
    for name in sorted(result):
        require(Path(name).suffix in {".py", ".json", ".c"},
                "unexpected type in research source closure")
        regular_file(source_root / name, source_root)
    # Public reference numeric observations and authorship constants only.
    require(validate(*read_inputs(source_root))["rom_sha256"] == ROM_SHA,
            "published numeric references inconsistent")
    # Lookup by exact name; do not rely on sorting for the VDP reference.
    vdp = json.loads((source_root /
        "research/observations/user_rom_vdp_hscroll_2026-10-18.json"
    ).read_text(encoding="utf-8"))
    require(vdp["reference_rom_sha256"] == ROM_SHA and
            vdp["rom_free_instrumented_core_sha256"] ==
            INSTRUMENTED_CORE_SHA and
            vdp["original_rom_in_public_repository"] is False,
            "historic VDP numeric archive SHA differs")
    return tuple(sorted(result))


def scan_bundle_files(dest):
    require(dest.is_dir() and not dest.is_symlink(),
            "bundle root missing or symlinked")
    found = set()
    for p in dest.rglob("*"):
        require(not p.is_symlink(), "symlink found in research bundle")
        if not p.is_file():
            continue
        rel = p.relative_to(dest).as_posix()
        require(p.suffix.lower() not in FORBIDDEN,
                "potential game media or unapproved artifact in bundle")
        if "/" not in rel:
            require(rel in KNOWN_ROOT or
                    (rel.endswith(".py") and rel == Path(rel).name),
                    "unexpected root-level artifact")
        else:
            require(rel.startswith(("tools/", "src/", "research/")),
                    "unexpected nested artifact")
        found.add(rel)
    return found


def build(source_root, dest):
    source_root = source_root.resolve(strict=True)
    dest = dest.resolve(strict=True)
    require(dest != source_root,
            "bundle must not overwrite the repository root")
    core = regular_file(dest / CORE_NAME, dest)
    selected = chosen_paths(source_root)
    for rel in selected:
        target = dest / rel
        require(not target.exists() and not target.is_symlink(),
                "research dependency destination already exists")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_root / rel, target)
    files = {rel: digest_file(dest / rel) for rel in selected}
    files[CORE_NAME] = digest_file(core)
    match = files[CORE_NAME] == INSTRUMENTED_CORE_SHA
    manifest = {
        "schema": SCHEMA,
        "source_rom_sha256_reference": ROM_SHA,
        "historical_instrumented_core_sha256": INSTRUMENTED_CORE_SHA,
        "packaged_core_sha256": files[CORE_NAME],
        "packaged_core_matches_historical_binary": match,
        "status": ("CORE_BINARY_MATCHES_HISTORIC_VDP_WITNESS"
                   if match else "CORE_BINARY_NOT_HISTORIC_VDP_WITNESS"),
        "original_game_media_in_bundle": False,
        "original_rom_executed_by_packaging": False,
        "historical_original_run_reproduced_by_packaging": False,
        "original_collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
        "entrypoints": list(ENTRYPOINTS),
        "files": files,
    }
    output = dest / MANIFEST
    require(not output.exists() and not output.is_symlink(),
            "bundle manifest destination already exists")
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True)+"\n",
                      encoding="utf-8")
    verify(dest)
    return manifest


def verify(dest):
    dest = dest.resolve(strict=True)
    path = regular_file(dest / MANIFEST, dest)
    info = json.loads(path.read_text(encoding="utf-8"))
    require(type(info) is dict and info.get("schema") == SCHEMA and
            type(info.get("files")) is dict and
            info.get("original_game_media_in_bundle") is False and
            info.get("historical_original_run_reproduced_by_packaging") is False and
            info.get("safe_to_modify_switch_physics") is False,
            "research bundle manifest schema/safety flag differs")
    expected = info["files"]
    require(info.get("entrypoints") == list(ENTRYPOINTS) and
            all(type(rel) is str and type(sha) is str and
                len(sha) == 64 and
                (rel == CORE_NAME or rel.startswith(("tools/", "src/", "research/")))
                for rel, sha in expected.items()),
            "unexpected bundle file manifest path/hash")
    existing = scan_bundle_files(dest)
    managed = {x for x in existing if "/" in x or x == CORE_NAME}
    require(managed == set(expected), "missing or extra managed research file")
    for rel, sha in expected.items():
        require(digest_file(regular_file(dest / rel, dest)) == sha,
                "research dependency/core SHA mismatch")
    external_sha_path = dest / "emulator-sha256.txt"
    if external_sha_path.exists():
        require(regular_file(external_sha_path, dest).read_text(
            encoding="utf-8").split()[0] == expected[CORE_NAME],
            "existing core SHA summary differs from bundled core")
    require(info.get("packaged_core_sha256") == expected[CORE_NAME] and
            info.get("historical_instrumented_core_sha256") ==
            INSTRUMENTED_CORE_SHA and
            info.get("source_rom_sha256_reference") == ROM_SHA and
            info.get("packaged_core_matches_historical_binary") is
            (expected[CORE_NAME] == INSTRUMENTED_CORE_SHA),
            "core binary compatibility claim differs")
    # Also verify dependency completeness against the packaged scripts,
    # not only the files from the repository used during build.
    require(set("tools/" + x for x in resolve_modules(dest)) ==
            {x for x in expected if x.startswith("tools/")},
            "packaged Python dependency closure missing or inconsistent")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build", action="store_true")
    mode.add_argument("--verify", action="store_true")
    parser.add_argument("--source-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    try:
        status = (build(args.source_root, args.destination) if args.build
                  else verify(args.destination))
        print("GENESIS_STAGE203_PRIVATE_NUMERIC_BUNDLE_PASS "
              "historical_core_binary_match=" +
              str(status["packaged_core_matches_historical_binary"]).lower())
    except (ValueError, OSError, KeyError, TypeError, SyntaxError,
            json.JSONDecodeError):
        parser.exit(1, "GENESIS_STAGE203_PRIVATE_NUMERIC_BUNDLE_FAIL\n")


if __name__ == "__main__":
    main()
