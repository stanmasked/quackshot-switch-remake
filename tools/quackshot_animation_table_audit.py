#!/usr/bin/env python3
"""Audit the QuackShot animation offset directory without exporting ROM bytes.

The CLI accepts only the pinned user-owned QuackShot ROM. Output is numeric
metadata (offsets, bounds, and descriptor shape); no ROM, RAM, VRAM, pixels,
audio, or instruction bytes are written.
"""
import argparse
import hashlib
import json
from pathlib import Path

REFERENCE_ROM_SHA256 = (
    "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34"
)
TABLE_BASE = 0x21372
MAX_DIRECTORY_ENTRIES = 128
EXPECTED_DIRECTORY_BYTES = 0x54
EXPECTED_DIRECTORY_ENTRIES = EXPECTED_DIRECTORY_BYTES // 2


def parse_offset_directory(rom_bytes, table_base=TABLE_BASE):
    """Parse a self-delimiting big-endian offset directory.

    In this format the first 16-bit value is the byte offset of the first
    target and therefore the byte length of the directory itself. The parser
    deliberately preserves entry order: later entries need not be sorted.
    """
    if not isinstance(rom_bytes, (bytes, bytearray, memoryview)):
        raise TypeError("rom_bytes must be a bytes-like object")
    if type(table_base) is not int or table_base < 0:
        raise ValueError("table_base must be a nonnegative integer")
    if table_base + 2 > len(rom_bytes):
        raise ValueError("ROM is too short for the first directory word")

    directory_bytes = int.from_bytes(
        rom_bytes[table_base:table_base + 2], "big", signed=False
    )
    if directory_bytes < 2 or directory_bytes % 2:
        raise ValueError("first directory offset must be a positive even byte length")
    entry_count = directory_bytes // 2
    if entry_count > MAX_DIRECTORY_ENTRIES:
        raise ValueError("offset directory exceeds the 128-entry safety limit")
    if table_base + directory_bytes > len(rom_bytes):
        raise ValueError("ROM is too short for the complete offset directory")

    offsets = []
    for index in range(entry_count):
        start = table_base + 2 * index
        offset = int.from_bytes(rom_bytes[start:start + 2], "big", signed=False)
        if offset < directory_bytes:
            raise ValueError(
                f"directory entry {index} points into the directory itself"
            )
        if offset % 2:
            raise ValueError(f"directory entry {index} is not word-aligned")
        if table_base + offset + 2 > len(rom_bytes):
            raise ValueError(f"directory entry {index} points outside the ROM")
        offsets.append(offset)

    if offsets[0] != directory_bytes:
        raise ValueError(
            "first target must immediately follow the self-delimiting directory"
        )

    return {
        "table_base": table_base,
        "directory_bytes": directory_bytes,
        "entry_count": entry_count,
        "relative_offsets": offsets,
        "all_targets_word_aligned": all(offset % 2 == 0 for offset in offsets),
        "all_targets_in_bounds": all(
            table_base + offset + 2 <= len(rom_bytes) for offset in offsets
        ),
        "unique_target_count": len(set(offsets)),
    }


def summarize_directory(rom_bytes, table_base=TABLE_BASE):
    """Return numeric-only offset and first-record summaries."""
    directory = parse_offset_directory(rom_bytes, table_base)
    offsets = directory["relative_offsets"]
    entries = []
    for index, offset in enumerate(offsets):
        target = table_base + offset
        target_word = int.from_bytes(
            rom_bytes[target:target + 2], "big", signed=False
        )
        entries.append({
            "index": index,
            "relative_offset": f"0x{offset:04X}",
            "rom_offset": f"0x{target:06X}",
            "target_first_word": f"0x{target_word:04X}",
        })

    # The first 16 entries form the initial monotonic descriptor run. The
    # following entries include two references into that run's first records.
    primary_offsets = offsets[:16]
    primary_records = []
    for index, start in enumerate(primary_offsets):
        if index + 1 < len(primary_offsets):
            end = primary_offsets[index + 1]
        else:
            later = [offset for offset in offsets if offset > start]
            if not later:
                raise ValueError("cannot determine end of final primary descriptor")
            end = min(later)
        span = end - start
        if span < 2 or span % 2:
            raise ValueError(f"invalid primary descriptor span at index {index}")
        header = int.from_bytes(
            rom_bytes[table_base + start:table_base + start + 2],
            "big",
            signed=False,
        )
        primary_records.append({
            "index": index,
            "relative_offset": f"0x{start:04X}",
            "header_word": f"0x{header:04X}",
            "span_bytes": span,
            "payload_word_count": span // 2 - 1,
        })

    interior_references = []
    for index, offset in enumerate(offsets[16:], start=16):
        for primary_index, start in enumerate(primary_offsets):
            if primary_index + 1 < len(primary_offsets):
                end = primary_offsets[primary_index + 1]
            else:
                end = min(value for value in offsets if value > start)
            if start < offset < end:
                interior_references.append({
                    "directory_index": index,
                    "relative_offset": f"0x{offset:04X}",
                    "containing_primary_index": primary_index,
                })
                break

    primary_offsets_strictly_increasing = all(
        left < right for left, right in zip(primary_offsets, primary_offsets[1:])
    )
    # Classify directory entries conservatively. Only the initial monotonic
    # run is treated as primary descriptor candidates; later entries are
    # candidates until interval membership is established.
    entry_roles = []
    for index, offset in enumerate(offsets):
        role = "primary_descriptor_candidate" if index < 16 else "secondary_target_candidate"
        containing = None
        for primary_index, start in enumerate(primary_offsets):
            if primary_index + 1 < len(primary_offsets):
                end = primary_offsets[primary_index + 1]
            else:
                later = [value for value in offsets if value > start]
                end = min(later) if later else start
            if start < offset < end:
                role = "interior_reference_candidate"
                containing = primary_index
                break
        entry_roles.append({
            "directory_index": index,
            "relative_offset": f"0x{offset:04X}",
            "role": role,
            "containing_primary_index": containing,
        })

    return {
        "schema": "quackshot_stage226_animation_offset_directory_v1",
        **{key: value for key, value in directory.items()
           if key != "relative_offsets"},
        "directory_entries": entries,
        "directory_entry_roles": entry_roles,
        "first_16_offsets_strictly_increasing": primary_offsets_strictly_increasing,
        "first_16_descriptor_candidates": primary_records,
        "interior_reference_candidates": interior_references,
        "raw_rom_exported": False,
        "raw_ram_exported": False,
        "raw_vram_exported": False,
        "raw_sat_bytes_exported": False,
        "frame_pixels_exported": False,
        "audio_exported": False,
        "instruction_bytes_exported": False,
    }



def resolve_nested_record(rom_bytes, selector, cursor, table_base=TABLE_BASE):
    """Resolve one animation cursor to a numeric subrecord summary.

    The high bit of the adjusted offset selects an alternate subrecord layout:
    it is cleared for address calculation, and the two animation bytes are
    read after two additional layout bytes.
    """
    directory = parse_offset_directory(rom_bytes, table_base)
    if type(selector) is not int or not 0 <= selector < directory["entry_count"]:
        raise ValueError("selector is outside the offset directory")
    if type(cursor) is not int or cursor < 0:
        raise ValueError("cursor must be a nonnegative integer")

    descriptor_relative = directory["relative_offsets"][selector]
    descriptor = table_base + descriptor_relative
    if descriptor + 2 > len(rom_bytes):
        raise ValueError("selected descriptor header is outside the ROM")

    header_first = rom_bytes[descriptor]
    header_second = rom_bytes[descriptor + 1]
    # The first descriptor-header byte is the maximum valid sequence cursor
    # (observed sequences include cursor 0 through this value).
    if cursor > header_first:
        raise ValueError("cursor exceeds selected descriptor sequence limit")
    cursor_word = descriptor + 2 + 2 * cursor
    if cursor_word + 2 > len(rom_bytes):
        raise ValueError("selected sequence cursor is outside the ROM")
    sequence_word = int.from_bytes(
        rom_bytes[cursor_word:cursor_word + 2], "big", signed=False
    )
    if sequence_word < 2:
        raise ValueError("selected sequence word is smaller than its two-byte header")
    adjusted_word = sequence_word - 2
    alternate_layout = bool(adjusted_word & 0x8000)
    offset_word = adjusted_word & 0x7FFF if alternate_layout else adjusted_word
    target = descriptor + 2 + offset_word
    common = {
        "selector": selector,
        "cursor": cursor,
        "descriptor_relative_offset": f"0x{descriptor_relative:04X}",
        "descriptor_rom_offset": f"0x{descriptor:06X}",
        "header_first_byte": header_first,
        "header_second_byte": header_second,
        "sequence_word": f"0x{sequence_word:04X}",
        "adjusted_sequence_word": f"0x{adjusted_word:04X}",
        "offset_word_after_flag": f"0x{offset_word:04X}",
        "alternate_layout": alternate_layout,
        "threshold_action": (
            "inherit_existing_threshold" if header_second == 0xFF
            else "set_threshold_from_header"
        ),
    }

    if target < 0 or target + (5 if alternate_layout else 3) > len(rom_bytes):
        raise ValueError("resolved animation subrecord is outside the ROM")
    control = rom_bytes[target]
    if control & 0x80:
        return {
            **common,
            "status": "special_control_path",
            "target_relative_offset": f"0x{target - table_base:04X}",
            "target_rom_offset": f"0x{target:06X}",
            "control_byte": control,
            "special_control_path": True,
            "animation_byte_24": None,
            "animation_byte_25": None,
        }

    byte_offset = target + (3 if alternate_layout else 1)
    return {
        **common,
        "status": "resolved_alternate_layout" if alternate_layout else "resolved",
        "target_relative_offset": f"0x{target - table_base:04X}",
        "target_rom_offset": f"0x{target:06X}",
        "control_byte": control,
        "special_control_path": False,
        "animation_byte_24": rom_bytes[byte_offset],
        "animation_byte_25": rom_bytes[byte_offset + 1],
    }

def audit_pinned_rom(rom_path):
    """Audit the pinned local ROM; never include its bytes in the result."""
    rom_path = Path(rom_path)
    rom_bytes = rom_path.read_bytes()
    actual_sha256 = hashlib.sha256(rom_bytes).hexdigest()
    if actual_sha256 != REFERENCE_ROM_SHA256:
        raise ValueError("private ROM SHA-256 does not match the pinned QuackShot reference")
    if len(rom_bytes) < 0x104 or rom_bytes[0x100:0x104] != b"SEGA":
        raise ValueError("pinned input is not a linear Sega Mega Drive ROM")

    report = summarize_directory(rom_bytes, TABLE_BASE)
    if (report["directory_bytes"] != EXPECTED_DIRECTORY_BYTES or
            report["entry_count"] != EXPECTED_DIRECTORY_ENTRIES or
            report["unique_target_count"] != EXPECTED_DIRECTORY_ENTRIES or
            not report["first_16_offsets_strictly_increasing"]):
        raise ValueError("pinned animation-directory shape differs from the audited reference")

    report["reference_rom_sha256"] = actual_sha256
    report["status"] = "PASS_NUMERIC_OFFSET_DIRECTORY_AUDIT"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True,
                        help="Local pinned QuackShot ROM; never uploaded")
    parser.add_argument("--output", type=Path,
                        help="Optional path for numeric-only JSON output")
    args = parser.parse_args()
    try:
        report = audit_pinned_rom(args.rom)
        rendered = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
    except (OSError, TypeError, ValueError) as error:
        parser.exit(1, f"QuackShot animation-table audit: {error}\n")


if __name__ == "__main__":
    main()
