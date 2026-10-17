#!/usr/bin/env python3
"""Bounded M68000 reset-prefix interpreter with MOCK hardware registers.

This is NOT an emulator of the Sega Mega Drive or a gameplay trace.
No ROM bytes are saved, displayed, or embedded in the report.
"""
import argparse
import hashlib
import json
from pathlib import Path

MAX_STEPS = 64


def word(image, offset):
    if offset < 0 or (offset & 1) or offset + 2 > len(image):
        raise ValueError("unaligned or out-of-range word fetch")
    return int.from_bytes(image[offset:offset + 2], "big")


def longword(image, offset):
    if offset < 0 or (offset & 1) or offset + 4 > len(image):
        raise ValueError("unaligned or out-of-range long fetch")
    return int.from_bytes(image[offset:offset + 4], "big")


def signed8(value):
    return value - 256 if value >= 128 else value


def signed16(value):
    return value - 65536 if value >= 32768 else value


def trace_prefix(image, mmio, max_steps=MAX_STEPS):
    """Execute an explicit opcode allowlist with provided synthetic MMIO values."""
    if max_steps < 1 or max_steps > MAX_STEPS:
        raise ValueError("max_steps must be 1..64")
    if len(image) < 0x208 or image[0x100:0x104] != b"SEGA":
        raise ValueError("not a supported linear Sega Mega Drive ROM")
    pc = longword(image, 4)
    if pc & 1 or not 0x200 <= pc < len(image):
        raise ValueError("invalid reset vector")
    z = None
    events = []
    stop = "step_limit"
    for _ in range(max_steps):
        op = word(image, pc)
        next_pc = pc + 2
        event = {"pc": f"0x{pc:06X}"}
        if op in (0x4AB9, 0x4A79, 0x4A39):
            width = {0x4AB9: 32, 0x4A79: 16, 0x4A39: 8}[op]
            addr = longword(image, pc + 2)
            if addr not in mmio:
                stop = f"mock_io_value_missing_at_0x{addr:06X}"
                break
            raw = mmio[addr]
            if not isinstance(raw, int) or not (0 <= raw <= 0xFFFFFFFF):
                raise ValueError("MMIO values must be unsigned 32-bit integers")
            sampled = raw & ((1 << width) - 1)
            z = sampled == 0
            event.update({
                "instruction": f"TST.{ {8: 'B', 16: 'W', 32: 'L'}[width]} (abs.l)",
                "io_address": f"0x{addr:06X}",
                "sampled_value": f"0x{sampled:0{width//4}X}",
                "zero_flag": z,
                "mocked": True,
            })
            next_pc = pc + 6
        elif op & 0xFF00 in (0x6600, 0x6000, 0x6700):
            cond = op & 0xFF00
            if cond != 0x6000 and z is None:
                stop = "condition_flags_unknown"
                break
            displacement = op & 0xFF
            if displacement == 0:
                displacement = signed16(word(image, pc + 2))
                next_pc = pc + 4
            elif displacement == 0xFF:
                stop = "unsupported_long_branch"
                break
            else:
                displacement = signed8(displacement)
            taken = (cond == 0x6000 or
                     (cond == 0x6600 and not z) or
                     (cond == 0x6700 and z))
            next_pc = pc + 2 + displacement if taken else next_pc
            event.update({"instruction": {0x6000: "BRA", 0x6600: "BNE",
                                          0x6700: "BEQ"}[cond],
                          "taken": bool(taken),
                          "next_pc": f"0x{next_pc:06X}"})
        elif op == 0x4E71:
            event["instruction"] = "NOP"
        else:
            stop = "unsupported_opcode"
            break
        if next_pc < 0 or next_pc >= len(image) or next_pc & 1:
            stop = "branch_target_out_of_range"
            break
        events.append(event)
        pc = next_pc
    return {
        "schema": "mega_drive_bounded_boot_probe_v1",
        "evidence_type": "limited_cpu_execution_with_synthetic_mmio",
        "rom_sha256": hashlib.sha256(image).hexdigest(),
        "reset_pc": f"0x{longword(image, 4):06X}",
        "mock_mmio": {f"0x{k:06X}": f"0x{v:08X}" for k, v in sorted(mmio.items())},
        "executed_instruction_count": len(events),
        "events": events,
        "stop_pc": f"0x{pc:06X}",
        "stop_reason": stop,
        "limitations": ("Synthetic MMIO inputs, tiny opcode subset, no VDP timing "
                        "or joystick model. Not original gameplay emulation."),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--mmio-io1", type=lambda s: int(s, 0), default=0,
                        help="MOCK value for 0xA10008")
    parser.add_argument("--mmio-io2", type=lambda s: int(s, 0), default=0,
                        help="MOCK value for 0xA1000C")
    parser.add_argument("--mmio-vdp", type=lambda s: int(s, 0), default=0,
                        help="MOCK value for 0xC00004")
    parser.add_argument("--output", type=Path, help="Metadata-only JSON path")
    parser.add_argument("--expected-sha256", type=str, help="Required private ROM hash")
    args = parser.parse_args()
    try:
        if args.rom.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("ROM exceeds 8 MiB limit")
        image = args.rom.read_bytes()
        actual = hashlib.sha256(image).hexdigest()
        if args.expected_sha256 and actual != args.expected_sha256:
            raise ValueError("ROM SHA-256 does not match reference")
        report = trace_prefix(image, {0xA10008: args.mmio_io1,
                                      0xA1000C: args.mmio_io2,
                                      0xC00004: args.mmio_vdp})
        output = json.dumps(report, indent=2) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Boot probe: {error}\n")


if __name__ == "__main__":
    main()
