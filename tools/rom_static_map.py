#!/usr/bin/env python3
"""Read-only static M68000 reset, exception and MMIO footprint from a local ROM.

This is NOT a Sega emulator or gameplay trace. No ROM bytes or assets are output.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import zipfile

from rom_report import _read_rom, _smd_to_linear, inspect_data, compare_profile

VECTOR_NAMES = {
    2: "bus_error", 3: "address_error", 4: "illegal_instruction",
    5: "divide_by_zero", 6: "chk_instruction", 7: "trapv_instruction",
    8: "privilege_violation", 9: "trace", 10: "line_a", 11: "line_f",
    24: "spurious_interrupt", 25: "irq_level_1", 26: "irq_level_2",
    27: "irq_level_3", 28: "irq_level_4", 29: "irq_level_5",
    30: "irq_level_6", 31: "irq_level_7",
}

# Deliberately tiny exact-opcode subset. Only word-aligned static *candidates*.
# Bytes matching an instruction in an embedded table are false positives.
DIRECT_ABS_LONG = {
    0x4A39: "TST.B", 0x4A79: "TST.W", 0x4AB9: "TST.L",
    0x4EB9: "JSR", 0x4EF9: "JMP",
}


def normalize_image(raw):
    profile = inspect_data(raw)
    if profile["container_format"] == "linear":
        return raw, profile
    image = _smd_to_linear(raw)
    if image is None:
        raise ValueError("Unable to normalize interleaved image")
    return image, profile


def hexaddr(address):
    return f"0x{address:06X}"


def io_region(address):
    if 0xA10000 <= address <= 0xA1001F:
        return "genesis_io_register_window"
    if 0xA14000 <= address <= 0xA14003:
        return "tmss_security_register_window"
    if 0xC00000 <= address <= 0xC0001F:
        return "genesis_vdp_register_window"
    return None


def static_map(raw):
    image, meta = normalize_image(raw)
    if len(image) < 0x400:
        raise ValueError("ROM too small to assess boot and vector table")
    vectors = [int.from_bytes(image[4*i:4*i + 4], "big") for i in range(64)]
    ssp, reset = vectors[:2]
    if reset & 1 or not 0x200 <= reset < len(image):
        raise ValueError("Reset PC is not an even address within ROM program area")
    if (ssp & 1) or (ssp & 0xFF000000) or (ssp & 0x00FF0000) != 0x00FF0000:
        raise ValueError("Initial stack pointer does not fall inside Genesis 68K work RAM")
    valid = {v for v in vectors[2:] if v and v % 2 == 0 and 0x200 <= v < len(image)}
    if not valid:
        raise ValueError("No in-range exception handler vectors found")
    valid_vectors = [v for v in vectors[2:] if v in valid]
    default_handler, default_count = Counter(valid_vectors).most_common(1)[0]
    nondefault = [
        {"number": i, "vector_offset": hexaddr(i*4),
         "name": VECTOR_NAMES.get(i, f"trap_{i - 32}" if i >= 32 else "reserved"),
         "handler": hexaddr(target)}
        for i, target in enumerate(vectors)
        if i >= 2 and target in valid and target != default_handler
    ]
    invalid_vectors = [
        {"number": i, "target": hexaddr(v)}
        for i, v in enumerate(vectors)
        if i >= 2 and v not in valid and v != 0
    ]
    access = []
    for off in range(0x200, len(image)-6+1, 2):
        op = int.from_bytes(image[off:off+2], "big")
        mnemonic = DIRECT_ABS_LONG.get(op)
        if mnemonic is None:
            continue
        address = int.from_bytes(image[off+2:off+6], "big")
        region = io_region(address)
        if region:
            access.append({
                "rom_offset": hexaddr(off),
                "opcode_family": mnemonic,
                "absolute_address": hexaddr(address),
                "region": region,
            })
    return {
        "schema": "mega_drive_static_vectors_mmio_v1",
        "evidence_type": "static_byte_patterns_not_execution",
        "rom_sha256": meta["linear_sha256"],
        "rom_size_bytes": len(image),
        "initial_supervisor_stack_pointer": hexaddr(ssp),
        "reset_program_counter": hexaddr(reset),
        "exception_vectors": {
            "default_handler": hexaddr(default_handler),
            "default_handler_vector_count": default_count,
            "irq_level_6": hexaddr(vectors[30]) if vectors[30] in valid else None,
            "nondefault_handlers": nondefault,
            "nonzero_invalid_vectors": invalid_vectors,
        },
        "static_io_instruction_candidates": access,
        "caveat": ("Word-aligned opcode matches may be embedded data. "
                   "IRQ level 6 is commonly used for VBlank but vector content "
                   "does not prove that an interrupt fired. No gameplay sampled."),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="Original ROM kept privately on local disk")
    parser.add_argument("--profile", type=Path,
                        help="Require exact metadata and SHA256 profile before reporting")
    parser.add_argument("--output", type=Path,
                        help="Optional metadata-only JSON file (contains no ROM bytes)")
    args = parser.parse_args()
    try:
        raw = _read_rom(args.rom)
        if args.profile:
            expected = json.loads(args.profile.read_text(encoding="utf-8"))
            mismatches = compare_profile(inspect_data(raw), expected)
            if mismatches:
                parser.exit(2, "Reference profile mismatch: " +
                            json.dumps(mismatches, ensure_ascii=False) + "\n")
        report = static_map(raw)
        data = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            args.output.write_text(data, encoding="utf-8")
        else:
            print(data, end="")
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"Static map failed: {exc}\n")


if __name__ == "__main__":
    main()
