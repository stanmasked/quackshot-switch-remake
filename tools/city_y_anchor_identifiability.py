#!/usr/bin/env python3
"""Stage196 ROM-free proof: two observed Y values cannot identify a Y anchor.

Uses already published numeric observations. Offset examples are translations
in the independently authored CITY coordinate system, NOT measured properties
of the Genesis sprite/collision box. No original ROM or core is loaded.
"""
import argparse
import json
import re
from pathlib import Path

from reference_evidence_integrity import OBSERVATIONS, read_inputs, validate

AUTHORED_BODY_HEIGHT = 20  # independently authored qs_game.c collision box
AUTHORED_SURFACES = (176, 208)  # selected Stage185/186 32px synthetic fixture
TRANSLATIONS = (
    ("zero_offset", 0),
    ("half_authored_body_offset", -AUTHORED_BODY_HEIGHT // 2),
    ("full_authored_body_offset", -AUTHORED_BODY_HEIGHT),
)


def require(ok, message):
    if not ok:
        raise ValueError("CITY Y anchor identifiability: " + message)


def analyze(observations, game_source):
    # Same provenance/active-physics checks as existing reference audit.
    provenance = validate(observations, game_source)
    ledge = observations["obstacles"]["ledge"]
    upper = ledge["world_y_upper_floor"]
    lower = ledge["lower_floor_world_xy"][1]
    require(type(upper) is int and type(lower) is int and
            lower - upper == ledge["drop_pixels"] == 32,
            "published original numeric Y difference changed")
    # Guard the C99 authored body assumption; a source change requires
    # review of the examples, not inference of original collision size.
    require(bool(re.search(
        r"hits_tiles\s*\(\s*g\s*,\s*trial\s*,\s*h->y\s*,\s*12\s*,\s*20\s*\)",
        game_source)),
        "authored 12x20 collision box declaration differs")
    require(bool(re.search(
        r"h->y\s*=\s*floorf\s*\(\s*\(\s*trial\s*\+\s*20\s*\)\s*/"
        r"\s*QS_TILE\s*\)\s*\*\s*QS_TILE\s*-\s*20\s*;",
        game_source)),
        "authored 20px foot-to-top landing rule differs")
    rows = []
    for label, offset in TRANSLATIONS:
        upper_top = upper + offset
        lower_top = lower + offset
        require(lower_top - upper_top == 32,
                "translation unexpectedly changed measured delta")
        rows.append({
            "hypothetical_translation": label,
            "translation_px": offset,
            "shifted_upper_y": upper_top,
            "shifted_lower_y": lower_top,
            "shifted_delta_y": lower_top - upper_top,
            "coincides_with_authored_top_pair": (
                (upper_top, lower_top) ==
                tuple(y - AUTHORED_BODY_HEIGHT for y in AUTHORED_SURFACES)),
            "original_collision_anchor_verified": False,
        })
    require(len({r["shifted_upper_y"] for r in rows}) == len(rows),
            "illustrative translations are not distinct")
    require(len({r["shifted_delta_y"] for r in rows}) == 1,
            "drop delta is not invariant")
    return {
        "schema": "quackshot_city_original_y_anchor_identifiability_v1",
        "status": "UNDERDETERMINED_BY_PUBLISHED_NUMERIC_Y_ONLY",
        "original_rom_was_run": False,
        "original_game_media_in_report": False,
        "physics_changed": False,
        "original_reference_rom_sha256": provenance["rom_sha256"],
        "published_original_numeric_y": {
            "upper": upper, "lower": lower, "difference": lower - upper,
            "physical_anchor": "UNKNOWN",
        },
        "authored_only_geometry": {
            "body_width_px": 12,
            "body_height_px": AUTHORED_BODY_HEIGHT,
            "upper_surface_y": AUTHORED_SURFACES[0],
            "lower_surface_y": AUTHORED_SURFACES[1],
            "upper_body_top_y": AUTHORED_SURFACES[0] - AUTHORED_BODY_HEIGHT,
            "lower_body_top_y": AUTHORED_SURFACES[1] - AUTHORED_BODY_HEIGHT,
        },
        "hypothetical_uniform_translations_not_original_measurements": rows,
        "original_anchor_selected": None,
        "original_collision_box_measured": False,
        "safe_to_modify_switch_physics": False,
        "what_is_proven": (
            "A uniform unknown Y offset cancels when subtracting the "
            "two observed Y values; their 32px difference cannot select "
            "one of several distinct possible coordinate origins."
        ),
        "caveats": [
            "Translations are illustrative and do not posit actual Genesis body height",
            "Numerical overlap with the authored floor is not geometric evidence",
            "No independent original sprite/collision-box or scanline timing recovered",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        obs, game_source = read_inputs(root)
        report = analyze(obs, game_source)
        data = json.dumps(report, ensure_ascii=False, indent=2,
                          sort_keys=True) + "\n"
        if args.out:
            protected = {root / "src/qs_game.c"}
            protected.update((root / p).resolve() for p in OBSERVATIONS.values())
            require(args.out.resolve() not in protected,
                    "refusing to overwrite numeric evidence")
            args.out.write_text(data, encoding="utf-8")
        else:
            print(data, end="")
    except (ValueError, KeyError, TypeError, OSError) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
