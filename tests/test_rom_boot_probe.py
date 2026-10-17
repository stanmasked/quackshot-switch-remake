import hashlib
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from rom_boot_probe import MAX_STEPS, longword, trace_prefix, word


def make_synthetic_boot():
    """Artificial microprogram, NOT bytes copied from a commercial ROM."""
    b = bytearray(0x500)
    b[0x100:0x104] = b"SEGA"
    b[4:8] = (0x240).to_bytes(4, "big")
    b[0x240:0x246] = bytes.fromhex("4ab900a10010")  # TST.L mock register
    b[0x246:0x248] = bytes.fromhex("6706")          # BEQ 0x24e
    b[0x248:0x24e] = bytes.fromhex("4a7900c00004")  # TST.W mock VDP
    b[0x24e:0x250] = bytes.fromhex("6010")          # BRA 0x260
    b[0x260:0x264] = bytes.fromhex("4e714e75")      # NOP, unsupported RTS
    return bytes(b)


class BootProbeTests(unittest.TestCase):
    def setUp(self):
        self.rom = make_synthetic_boot()
        self.mock = {0xA10010: 0, 0xC00004: 0}

    def test_mock_zero_branch(self):
        r = trace_prefix(self.rom, self.mock)
        self.assertEqual([e["pc"] for e in r["events"]],
                         ["0x000240", "0x000246", "0x00024E", "0x000260"])
        self.assertEqual(r["executed_instruction_count"], 4)
        self.assertTrue(r["events"][1]["taken"])
        self.assertEqual(r["stop_pc"], "0x000262")
        self.assertEqual(r["stop_reason"], "unsupported_opcode")

    def test_mock_nonzero_branch(self):
        mmio = dict(self.mock)
        mmio[0xA10010] = 0x12345678
        r = trace_prefix(self.rom, mmio)
        self.assertEqual([e["pc"] for e in r["events"]],
                         ["0x000240", "0x000246", "0x000248",
                          "0x00024E", "0x000260"])
        self.assertFalse(r["events"][1]["taken"])
        self.assertEqual(r["events"][2]["io_address"], "0xC00004")
        self.assertEqual(r["events"][0]["sampled_value"], "0x12345678")
        self.assertEqual(r["stop_reason"], "unsupported_opcode")

    def test_second_register_is_sampled_with_real_width(self):
        mmio = dict(self.mock)
        mmio.update({0xA10010: 1, 0xC00004: 0x10001})
        r = trace_prefix(self.rom, mmio)
        self.assertEqual(r["events"][2]["sampled_value"], "0x0001")
        self.assertFalse(r["events"][2]["zero_flag"])

    def test_missing_mock_stops_before_read(self):
        r = trace_prefix(self.rom, {})
        self.assertEqual(r["executed_instruction_count"], 0)
        self.assertEqual(r["stop_reason"],
                         "mock_io_value_missing_at_0xA10010")

    def test_reject_invalid_values(self):
        for v in (-1, 0x100000000):
            with self.assertRaisesRegex(ValueError, "unsigned 32-bit"):
                trace_prefix(self.rom, {0xA10010: v})

    def test_reject_bad_format_and_reset(self):
        with self.assertRaisesRegex(ValueError, "supported linear"):
            trace_prefix(bytes(64), self.mock)
        b = bytearray(self.rom)
        b[4:8] = (0x241).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "reset vector"):
            trace_prefix(bytes(b), self.mock)

    def test_step_limits(self):
        for steps in (0, MAX_STEPS + 1):
            with self.assertRaisesRegex(ValueError, "max_steps"):
                trace_prefix(self.rom, self.mock, steps)
        r = trace_prefix(self.rom, self.mock, 2)
        self.assertEqual(r["stop_reason"], "step_limit")
        self.assertEqual(r["executed_instruction_count"], 2)

    def test_alignment_and_bounds(self):
        with self.assertRaisesRegex(ValueError, "unaligned"):
            word(self.rom, 0x241)
        with self.assertRaisesRegex(ValueError, "out-of-range"):
            longword(self.rom, len(self.rom))

    def test_odd_branch_target_is_not_executed(self):
        b = bytearray(self.rom)
        b[0x246:0x248] = bytes.fromhex("6701")
        r = trace_prefix(bytes(b), self.mock)
        self.assertEqual(r["executed_instruction_count"], 1)
        self.assertEqual(r["stop_reason"], "branch_target_out_of_range")

    def test_no_rom_bytes_are_in_report(self):
        r = trace_prefix(self.rom, self.mock)
        report = json.dumps(r)
        self.assertEqual(r["rom_sha256"], hashlib.sha256(self.rom).hexdigest())
        self.assertEqual(r["evidence_type"],
                         "limited_cpu_execution_with_synthetic_mmio")
        self.assertNotIn("4ab900a10010", report.lower())


if __name__ == "__main__":
    unittest.main()
