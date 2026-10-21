#!/usr/bin/env python3
"""Stage189: fail-closed comparison of numeric Genesis Y thresholds and CITY TEST.

This program NEVER runs an original ROM. A file marked as a private-run
witness is externally supplied numerical testimony, not independently
attested original execution. In absence of it, original subpixel timing
is explicitly UNKNOWN, never inferred from authored Switch test tiles.
"""
import argparse
import json
from pathlib import Path

from city_event_gap import original_numbers
from reference_evidence_integrity import read_inputs, validate

SYNTHETIC_SCHEMA = "quackshot_city_drop_height_sensitivity_v1"
WITNESS_SCHEMA = "quackshot_genesis_fractional_fall_event_v1"
WITNESS_FIELDS = {
    "schema", "status", "original_rom_executed_locally", "raw_ram_saved",
    "raw_frames_saved", "rom_or_emulator_payload_in_report",
    "world_y_integer_upper", "world_y_integer_lower",
    "first_y_positive_subpixel_frame", "first_y_above_quarter_pixel_frame",
    "first_y_above_one_pixel_frame", "first_integer_y_increase_frame",
    "first_integer_lower_floor_frame", "first_exact_16_16_lower_floor_frame",
    "fractional_y_units_at_integer_176_frame_2280",
    "fall_duration_by_threshold_frames", "frame_label_caveat", "scope",
    "reference_sha256", "core_binary_sha256", "emulator",
    "independent_original_runs",
}
DURATION_FIELDS = {
    "positive_subpixel_to_integer_floor": "first_y_positive_subpixel_frame",
    "over_quarter_to_integer_floor": "first_y_above_quarter_pixel_frame",
    "over_one_to_integer_floor": "first_y_above_one_pixel_frame",
    "integer_change_to_integer_floor": "first_integer_y_increase_frame",
}


def require(ok, reason):
    if not ok:
        raise ValueError("CITY fractional witness: " + reason)


def exact_integer(value, field, lower, upper):
    require(type(value) is int and lower <= value <= upper,
            "invalid integer field " + field)
    return value


def validate_witness(witness, provenance, original):
    """Validate a small numeric-only Stage188 report; no trust escalation."""
    require(type(witness) is dict and set(witness) == WITNESS_FIELDS,
            "private report contains missing/extra fields or payloads")
    require(witness["schema"] == WITNESS_SCHEMA, "wrong witness schema")
    require(witness["status"] == "PRIVATE_ORIGINAL_EMULATION_NUMERIC_ONLY" and
            witness["original_rom_executed_locally"] is True,
            "unverified synthetic data cannot be claimed as a private run")
    for field in ("raw_ram_saved", "raw_frames_saved",
                  "rom_or_emulator_payload_in_report"):
        require(witness[field] is False, "report declares original payload")
    require(witness["reference_sha256"] == provenance["rom_sha256"],
            "original ROM SHA256 does not match numeric evidence")
    require(witness["core_binary_sha256"] == provenance["genesis_core_sha256"],
            "Genesis Plus GX binary SHA256 does not match numeric evidence")
    exact_integer(witness["independent_original_runs"],
                  "independent_original_runs", 3, 3)
    require(witness["world_y_integer_upper"] == 176 and
            witness["world_y_integer_lower"] == 208,
            "source-original world Y anchors differ")
    require(type(witness["emulator"]) is str and
            witness["emulator"].startswith("Genesis Plus GX "),
            "unexpected emulator identity")
    require(type(witness["frame_label_caveat"]) is str and
            len(witness["frame_label_caveat"]) > 20,
            "missing frame sampling disclaimer")
    require(type(witness["scope"]) is list and
            len(witness["scope"]) == 3 and
            all(type(x) is str and 0 < len(x) <= 300
                for x in witness["scope"]), "unexpected witness scope")
    markers = {}
    for name in DURATION_FIELDS.values():
        markers[name] = exact_integer(witness[name], name, 2260, 2297)
    landing = exact_integer(witness["first_integer_lower_floor_frame"],
                            "first_integer_lower_floor_frame", 2297, 2297)
    require(original["fall"] == 211 and original["land"] == 227,
            "published original event baseline differs")
    require(markers["first_integer_y_increase_frame"] == 2281,
            "original integer fall frame differs from prior evidence")
    require(markers["first_y_positive_subpixel_frame"] <=
            markers["first_y_above_quarter_pixel_frame"] <=
            markers["first_integer_y_increase_frame"] <=
            markers["first_y_above_one_pixel_frame"] < landing,
            "invalid numeric fractional event ordering")
    exact_floor = witness["first_exact_16_16_lower_floor_frame"]
    require(exact_floor is None or
            exact_integer(exact_floor, "exact_floor", 2297, 2510) >= landing,
            "exact floor frame precedes integer landing")
    exact_integer(witness["fractional_y_units_at_integer_176_frame_2280"],
                  "fractional_y_units_at_integer_176_frame_2280", 0, 65535)
    durations = witness["fall_duration_by_threshold_frames"]
    require(type(durations) is dict and
            set(durations) == set(DURATION_FIELDS),
            "missing or injected fractional duration fields")
    for name, source in DURATION_FIELDS.items():
        value = exact_integer(durations[name], name, 1, 50)
        require(value == landing - markers[source],
                "fall duration contradicts absolute numeric event frames")
    return {
        "positive_subpixel": durations["positive_subpixel_to_integer_floor"],
        "quarter_pixel": durations["over_quarter_to_integer_floor"],
        "one_pixel": durations["over_one_to_integer_floor"],
        "integer_y": durations["integer_change_to_integer_floor"],
        "y_fraction_at_frame_2280_is_nonzero": (
            witness["fractional_y_units_at_integer_176_frame_2280"] > 0),
        "exact_16_16_landing_observed": exact_floor is not None,
    }


def validate_synthetic(report):
    """Extract the authored 32px trace; no claim about original tiles."""
    require(type(report) is dict and
            report.get("schema") == SYNTHETIC_SCHEMA and
            report.get("status") == "SYNTHETIC_DROP_EXPERIMENT_NOT_ROM_PARITY",
            "missing or incorrect Stage186 synthetic report")
    require(report.get("original_rom_was_run") is False and
            report.get("original_assets_in_report") is False and
            report.get("physics_changed") is False,
            "synthetic report has unexpected provenance or physics")
    rows = report.get("rows")
    require(type(rows) is list and len(rows) == 20,
            "expected twenty synthetic tile combinations")
    matching = [r for r in rows if type(r) is dict and
                r.get("drop_height_px") == 32 and r.get("ledge_column") == 25]
    require(len(matching) == 1, "missing/duplicate 32px baseline")
    row = matching[0]
    for key in ("fall_0p01", "fall_0p25", "fall_1px", "landing"):
        exact_integer(row[key], key, 0, 350)
    require(row["fall_0p01"] < row["fall_0p25"] <
            row["fall_1px"] < row["landing"], "invalid synthetic chronology")
    result = {
        "above_0p01": row["landing"] - row["fall_0p01"],
        "above_0p25": row["landing"] - row["fall_0p25"],
        "above_1p0": row["landing"] - row["fall_1px"],
    }
    require(result == {"above_0p01": 17, "above_0p25": 16,
                       "above_1p0": 15},
            "Stage186 synthetic baseline has changed")
    return result


def compare(observations, source, synthetic_report=None, numeric_witness=None):
    provenance = validate(observations, source)
    original = original_numbers(observations)
    require(original["land"] - original["fall"] == 16,
            "original integer floor event baseline changed")
    synthetic = (validate_synthetic(synthetic_report)
                 if synthetic_report is not None else None)
    result = {
        "schema": "quackshot_city_fractional_witness_comparison_v1",
        "status": ("USER_NUMERIC_WITNESS_NOT_INDEPENDENTLY_ATTESTED"
                   if numeric_witness is not None
                   else "NO_NEW_ORIGINAL_FRACTIONAL_MEASUREMENTS"),
        "original_rom_executed_by_this_comparison": False,
        "contains_original_media_or_raw_ram": False,
        "original_reference_rom_sha256": provenance["rom_sha256"],
        "public_integer_only_observations": {
            "first_integer_fall_frame": 2281,
            "first_integer_landing_frame": 2297,
            "integer_event_interval_frames": 16,
        },
        "original_fractional_threshold_intervals": None,
        "authored_switch_32px_threshold_intervals": synthetic,
        "strictly_comparable_threshold_deltas": None,
        "measurement_caveats": [
            "An external numeric JSON report is not cryptographic proof of original emulation",
            "Stage186 >0.01px and Stage188 >0px thresholds are NOT interchangeable",
            "Original integer Y and authored hero top Y have different origins and hitboxes",
            "A matching time interval cannot prove identical geometry or collision physics",
            "Genesis libretro memory callback phase vs displayed frames is not independently established",
        ],
    }
    if numeric_witness is not None:
        witness_values = validate_witness(numeric_witness, provenance, original)
        result["original_fractional_threshold_intervals"] = witness_values
        if synthetic is not None:
            result["strictly_comparable_threshold_deltas"] = {
                "quarter_pixel_synthetic_minus_private_numeric": (
                    synthetic["above_0p25"] - witness_values["quarter_pixel"]),
                "one_pixel_synthetic_minus_private_numeric": (
                    synthetic["above_1p0"] - witness_values["one_pixel"]),
            }
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path,
                   default=Path(__file__).resolve().parents[1])
    p.add_argument("--synthetic-report", type=Path)
    p.add_argument("--fractional-witness", type=Path)
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    try:
        root = args.root.resolve()
        obs, source = read_inputs(root)
        synthetic = (json.loads(args.synthetic_report.read_text(encoding="utf-8"))
                     if args.synthetic_report else None)
        witness = (json.loads(args.fractional_witness.read_text(encoding="utf-8"))
                   if args.fractional_witness else None)
        if args.out:
            protected = {(root / "src/qs_game.c").resolve()}
            protected |= {
                (root / path).resolve()
                for path in (
                    "research/observations/user_rom_fixedpoint_motion_2026-10-18.json",
                    "research/observations/user_rom_city_obstacles_2026-10-18.json",
                    "research/observations/user_rom_camera_state_2026-10-18.json"
                )
            }
            for arg in (args.synthetic_report, args.fractional_witness):
                if arg:
                    protected.add(arg.resolve())
            require(args.out.resolve() not in protected,
                    "refusing to overwrite numeric inputs")
        report = compare(obs, source, synthetic, witness)
        data = json.dumps(report, ensure_ascii=False, indent=2,
                          sort_keys=True) + "\n"
        if args.out:
            args.out.write_text(data, encoding="utf-8")
        else:
            print(data, end="")
        print("STAGE189_FRACTIONAL_COMPARISON_PASS: numeric-only, "
              "private ROM not executed")
    except (ValueError, TypeError, KeyError, OSError,
            json.JSONDecodeError) as exc:
        p.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
