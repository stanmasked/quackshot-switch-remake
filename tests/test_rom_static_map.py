import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import rom_static_map as m
from rom_report import inspect_data


def fixture():
    data = bytearray(0x800)
    struct.pack_into(">II", data, 0, 0x00FFFF00, 0x206)
    for i in range(2, 64):
        struct.pack_into(">I", data, i*4, 0x200)
    struct.pack_into(">I", data, 6*4, 0x522)
    struct.pack_into(">I", data, 30*4, 0x52A)
    data[0x100:0x104] = b"SEGA"
    data[0x206:0x20c] = bytes.fromhex("4ab900a10008")
    data[0x20e:0x214] = bytes.fromhex("4a7900a1000c")
    data[0x300:0x306] = bytes.fromhex("4a7900c00004")
    return bytes(data)


class StaticMapTests(unittest.TestCase):
    def test_vectors_and_known_static_io(self):
        result = m.static_map(fixture())
        self.assertEqual(result["schema"], "mega_drive_static_vectors_mmio_v1")
        self.assertEqual(result["initial_supervisor_stack_pointer"], "0xFFFF00")
        self.assertEqual(result["reset_program_counter"], "0x000206")
        vec = result["exception_vectors"]
        self.assertEqual(vec["default_handler"], "0x000200")
        self.assertEqual(vec["default_handler_vector_count"], 60)
        self.assertEqual(vec["irq_level_6"], "0x00052A")
        self.assertEqual([x["number"] for x in vec["nondefault_handlers"]], [6, 30])
        self.assertEqual([x["absolute_address"] for x in
                          result["static_io_instruction_candidates"]],
                         ["0xA10008", "0xA1000C", "0xC00004"])

    def test_odd_reset_is_refused(self):
        b = bytearray(fixture())
        struct.pack_into(">I", b, 4, 0x207)
        with self.assertRaisesRegex(ValueError, "Reset PC"):
            m.static_map(bytes(b))

    def test_reset_outside_rom_is_refused(self):
        b = bytearray(fixture())
        struct.pack_into(">I", b, 4, 0x800)
        with self.assertRaisesRegex(ValueError, "Reset PC"):
            m.static_map(bytes(b))

    def test_invalid_supervisor_stack_is_refused(self):
        b = bytearray(fixture())
        struct.pack_into(">I", b, 0, 0x00123400)
        with self.assertRaisesRegex(ValueError, "stack pointer"):
            m.static_map(bytes(b))

    def test_io_pattern_only_word_aligned(self):
        b = bytearray(fixture())
        b[0x300:0x306] = bytes(6)
        b[0x401:0x407] = bytes.fromhex("4a7900c00004")
        offsets = [a["rom_offset"] for a in m.static_map(bytes(b))["static_io_instruction_candidates"]]
        self.assertNotIn("0x000401", offsets)
        self.assertNotIn("0x000300", offsets)

    def test_invalid_handler_is_reported_not_executed(self):
        b = bytearray(fixture())
        struct.pack_into(">I", b, 29*4, 0xF1234567)
        result = m.static_map(bytes(b))
        self.assertIn({"number": 29, "target": "0xF1234567"},
                      result["exception_vectors"]["nonzero_invalid_vectors"])

    def test_other_move_forms_target_io(self):
        b = bytearray(fixture())
        b[0x310:0x316] = bytes.fromhex("103900a10003")  # MOVE.B abs.l to D0
        b[0x320:0x328] = bytes.fromhex("13fc00c000a10003")  # MOVE.B #imm to abs.l
        b[0x330:0x33a] = bytes.fromhex("23fc0000000300c00004")  # MOVE.L #imm to abs.l
        b[0x340:0x346] = bytes.fromhex("13c000c00011")  # MOVE.B D0 to abs.l
        found = {(e["rom_offset"], e["opcode_family"], e["absolute_address"])
                 for e in m.static_map(bytes(b))["static_io_instruction_candidates"]}
        self.assertIn(("0x000310", "MOVE.B_abs_to_Dn", "0xA10003"), found)
        self.assertIn(("0x000320", "MOVE.B_imm_to_abs", "0xA10003"), found)
        self.assertIn(("0x000330", "MOVE.L_imm_to_abs", "0xC00004"), found)
        self.assertIn(("0x000340", "MOVE.B_Dn_to_abs", "0xC00011"), found)

    def test_truncated_immediate_op_not_decoded(self):
        self.assertIsNone(m.absolute_long_operand(bytes.fromhex("23fc00000003"), 0))
        self.assertIsNone(m.absolute_long_operand(b"\x00\x00", 0))

    def test_profile_hash_mismatch_detected(self):
        result = inspect_data(fixture())
        self.assertNotEqual(result["raw_sha256"],
                            "30581220f6c914709853828811fe143e283896189a64eedf1444dc908b5cac34")

    def test_no_rom_bytes_are_in_json(self):
        import json
        report = json.dumps(m.static_map(fixture()))
        self.assertNotIn("SEGA", report)
        self.assertNotIn("4ab900a10008", report.lower())
        self.assertIn("static_byte_patterns_not_execution", report)

    def test_public_witness_bound_to_reference_profile(self):
        import json
        root = Path(__file__).resolve().parents[1]
        witness = json.loads((root / "research/observations/user_rom_68000_static_2026-10-17.json").read_text())
        profile = json.loads((root / "research/reference_profiles/user_rom_2026-10-17.json").read_text())
        self.assertEqual(witness["rom_sha256"], profile["linear_sha256"])
        self.assertEqual(witness["rom_size_bytes"], profile["rom_size_bytes"])
        self.assertEqual(witness["reset_program_counter"], "0x000206")
        self.assertEqual(witness["exception_vectors"]["irq_level_6"], "0x00052A")
        self.assertEqual([x["number"] for x in
                          witness["exception_vectors"]["nondefault_handlers"]],
                         [6, 30, 32, 33, 34, 35, 36, 37])
        hardware = witness["static_io_instruction_candidates"]
        self.assertEqual(len(hardware), 21)
        self.assertEqual([x["absolute_address"] for x in hardware[:3]],
                         ["0xA10008", "0xA1000C", "0xC00004"])
        self.assertEqual([x["rom_offset"] for x in hardware
                          if x["opcode_family"] == "MOVE.B_abs_to_Dn" and
                          x["absolute_address"] == "0xA10003"],
                         ["0x002120", "0x002138"])
        self.assertEqual(hardware[4]["region"], "tmss_security_register_window")
        self.assertEqual(witness["evidence_type"], "static_byte_patterns_not_execution")

    def test_smd_conversion_produces_same_map(self):
        raw = fixture() + bytes(16384 - len(fixture()))
        smd_block = bytearray(16384)
        for i in range(8192):
            smd_block[8192+i] = raw[2*i]
            smd_block[i] = raw[2*i+1]
        smd = bytes(512) + smd_block
        standard = m.static_map(raw)
        interleaved = m.static_map(smd)
        self.assertEqual(standard["reset_program_counter"],
                         interleaved["reset_program_counter"])
        self.assertEqual(standard["static_io_instruction_candidates"],
                         interleaved["static_io_instruction_candidates"])


if __name__ == "__main__":
    unittest.main()
