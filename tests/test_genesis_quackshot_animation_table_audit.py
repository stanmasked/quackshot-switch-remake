import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "quackshot_animation_table_audit.py"
)
SPEC = importlib.util.spec_from_file_location(
    "quackshot_animation_table_audit_under_test", MODULE_PATH
)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class AnimationOffsetDirectoryTests(unittest.TestCase):
    def make_directory(self, table_base, offsets, size=0x500):
        data = bytearray(size)
        for index, offset in enumerate(offsets):
            start = table_base + index * 2
            data[start:start + 2] = int(offset).to_bytes(2, "big")
        directory_bytes = offsets[0] if offsets else 0
        for offset in offsets:
            target = table_base + offset
            if offset >= directory_bytes and target + 2 <= len(data):
                data[target:target + 2] = b"\x12\x34"
        return data

    def test_first_offset_defines_directory_size_and_order_is_preserved(self):
        table_base = 0x100
        data = self.make_directory(table_base, [8, 0x20, 0x12, 0x10], size=0x200)
        parsed = AUDIT.parse_offset_directory(data, table_base)
        self.assertEqual(parsed["directory_bytes"], 8)
        self.assertEqual(parsed["entry_count"], 4)
        self.assertEqual(parsed["relative_offsets"], [8, 0x20, 0x12, 0x10])
        self.assertTrue(parsed["all_targets_word_aligned"])
        self.assertTrue(parsed["all_targets_in_bounds"])

    def test_rejects_odd_directory_byte_length(self):
        data = self.make_directory(0x100, [9], size=0x200)
        with self.assertRaisesRegex(ValueError, "positive even"):
            AUDIT.parse_offset_directory(data, 0x100)

    def test_rejects_target_inside_directory(self):
        data = self.make_directory(0x100, [8, 6, 0x20, 0x30], size=0x200)
        with self.assertRaisesRegex(ValueError, "points into the directory"):
            AUDIT.parse_offset_directory(data, 0x100)

    def test_rejects_unaligned_target(self):
        data = self.make_directory(0x100, [8, 0x11, 0x20, 0x30], size=0x200)
        with self.assertRaisesRegex(ValueError, "word-aligned"):
            AUDIT.parse_offset_directory(data, 0x100)

    def test_rejects_out_of_bounds_target(self):
        data = self.make_directory(0x100, [8, 0x1FE, 0x20, 0x30], size=0x200)
        with self.assertRaisesRegex(ValueError, "outside the ROM"):
            AUDIT.parse_offset_directory(data, 0x100)

    def test_detects_42_entry_directory_and_interior_references(self):
        table_base = 0x100
        primary = [
            0x54, 0x82, 0xB0, 0xBE, 0xCC, 0xE0, 0xF4, 0xF8,
            0xFC, 0x102, 0x108, 0x116, 0x124, 0x132, 0x140, 0x148,
        ]
        rest = [
            0x156, 0x150, 0x15C, 0x168, 0x174, 0x17E, 0x188, 0x190,
            0x198, 0x1A2, 0x58, 0x86, 0x1C0, 0x1CA, 0x1D4, 0x1DE,
            0x1F4, 0x1FA, 0x1E8, 0x1EE, 0x1AC, 0x1B6, 0x200, 0x216,
            0x224, 0x228,
        ]
        offsets = primary + rest
        data = self.make_directory(table_base, offsets, size=0x500)
        # Restore the self-delimiting first word: 42 entries occupy 0x54 bytes.
        data[table_base:table_base + 2] = (0x54).to_bytes(2, "big")
        primary_headers = [
            0x00FF, 0x00FF, 0x05FF, 0x05FF, 0x03FF, 0x03FF, 0x0004, 0x0004,
            0x0104, 0x0104, 0x0504, 0x0504, 0x0500, 0x0500, 0x0204, 0x0204,
        ]
        for offset, header in zip(primary, primary_headers):
            start = table_base + offset
            data[start:start + 2] = header.to_bytes(2, "big")
        summary = AUDIT.summarize_directory(data, table_base)
        self.assertEqual(summary["entry_count"], 42)
        self.assertEqual(summary["directory_bytes"], 0x54)
        self.assertEqual(summary["unique_target_count"], 42)
        self.assertTrue(summary["first_16_offsets_strictly_increasing"])
        self.assertEqual(
            [record["payload_word_count"] for record in summary["first_16_descriptor_candidates"]],
            [22, 22, 6, 6, 9, 9, 1, 1, 2, 2, 6, 6, 6, 6, 3, 3],
        )
        self.assertEqual(
            summary["first_16_descriptor_candidates"][9]["header_word"], "0x0104"
        )
        directory_offsets = {
            item["index"]: item["relative_offset"]
            for item in summary["directory_entries"]
        }
        self.assertEqual(
            {index: directory_offsets[index] for index in (1, 3, 5, 27)},
            {1: "0x0082", 3: "0x00BE", 5: "0x00E0", 27: "0x0086"},
        )
        self.assertEqual(
            summary["interior_reference_candidates"],
            [
                {
                    "directory_index": 26,
                    "relative_offset": "0x0058",
                    "containing_primary_index": 0,
                },
                {
                    "directory_index": 27,
                    "relative_offset": "0x0086",
                    "containing_primary_index": 1,
                },
            ],
        )
        self.assertFalse(summary["raw_rom_exported"])
        self.assertNotIn("rom_bytes", summary)
        self.assertNotIn("raw_bytes", summary)


    def make_nested_record(self, header_first=2, header_second=4,
                           sequence_word=0x10, control=0x03,
                           animation_byte_24=0x09, animation_byte_25=0x09):
        table_base = 0x100
        offsets = [8, 0x20, 0x30, 0x40]
        data = self.make_directory(table_base, offsets, size=0x200)
        descriptor = table_base + offsets[0]
        data[descriptor:descriptor + 2] = bytes([header_first, header_second])
        data[descriptor + 2:descriptor + 4] = sequence_word.to_bytes(2, "big")
        adjusted = (sequence_word - 2) & 0xFFFF
        if adjusted & 0x8000:
            target = descriptor + (sequence_word & 0x7FFF)
            data[target:target + 5] = bytes(
                [control, 0xAA, 0xBB, animation_byte_24, animation_byte_25]
            )
        else:
            target = descriptor + sequence_word
            data[target:target + 3] = bytes(
                [control, animation_byte_24, animation_byte_25]
            )
        return data

    def test_resolves_nested_record_relative_to_descriptor_start(self):
        data = self.make_nested_record()
        resolved = AUDIT.resolve_nested_record(data, selector=0, cursor=0, table_base=0x100)
        self.assertEqual(resolved["status"], "resolved")
        self.assertEqual(resolved["descriptor_relative_offset"], "0x0008")
        self.assertEqual(resolved["sequence_word"], "0x0010")
        self.assertEqual(resolved["target_relative_offset"], "0x0018")
        self.assertEqual(resolved["control_byte"], 3)
        self.assertEqual(resolved["animation_byte_24"], 9)
        self.assertEqual(resolved["animation_byte_25"], 9)
        self.assertEqual(resolved["threshold_action"], "set_threshold_from_header")

    def test_ff_header_inherits_existing_threshold(self):
        data = self.make_nested_record(header_second=0xFF)
        resolved = AUDIT.resolve_nested_record(data, selector=0, cursor=0, table_base=0x100)
        self.assertEqual(resolved["threshold_action"], "inherit_existing_threshold")

    def test_high_bit_sequence_offset_uses_alternate_layout(self):
        data = self.make_nested_record(sequence_word=0x8010)
        resolved = AUDIT.resolve_nested_record(data, selector=0, cursor=0, table_base=0x100)
        self.assertEqual(resolved["status"], "resolved_alternate_layout")
        self.assertTrue(resolved["alternate_layout"])
        self.assertEqual(resolved["target_relative_offset"], "0x0018")
        self.assertEqual(resolved["animation_byte_24"], 9)
        self.assertEqual(resolved["animation_byte_25"], 9)

    def test_high_bit_control_byte_uses_special_path(self):
        data = self.make_nested_record(control=0x80)
        resolved = AUDIT.resolve_nested_record(data, selector=0, cursor=0, table_base=0x100)
        self.assertEqual(resolved["status"], "special_control_path")
        self.assertTrue(resolved["special_control_path"])
        self.assertIsNone(resolved["animation_byte_24"])

    def test_alternate_layout_with_special_control_does_not_expose_animation_bytes(self):
        data = self.make_nested_record(
            sequence_word=0x8010, control=0x80,
            animation_byte_24=0x45, animation_byte_25=0x67,
        )
        resolved = AUDIT.resolve_nested_record(
            data, selector=0, cursor=0, table_base=0x100
        )
        self.assertEqual(resolved["status"], "special_control_path")
        self.assertTrue(resolved["alternate_layout"])
        self.assertTrue(resolved["special_control_path"])
        self.assertIsNone(resolved["animation_byte_24"])
        self.assertIsNone(resolved["animation_byte_25"])
        self.assertEqual(resolved["target_relative_offset"], "0x0018")

    def test_alternate_layout_out_of_bounds_fails_closed(self):
        data = self.make_nested_record(sequence_word=0x8010)
        # Leave the target control byte present, but truncate the alternate
        # layout payload. The resolver must reject rather than read padding.
        truncated = data[:0x11C]
        with self.assertRaisesRegex(ValueError, "outside the ROM"):
            AUDIT.resolve_nested_record(
                truncated, selector=0, cursor=0, table_base=0x100
            )

    def test_rejects_cursor_outside_rom(self):
        data = self.make_nested_record()
        with self.assertRaisesRegex(ValueError, "cursor is outside the ROM"):
            AUDIT.resolve_nested_record(data, selector=0, cursor=256, table_base=0x100)

    def test_rejects_selector_outside_directory(self):
        data = self.make_nested_record()
        with self.assertRaisesRegex(ValueError, "selector is outside"):
            AUDIT.resolve_nested_record(data, selector=4, cursor=0, table_base=0x100)


    def test_pinned_rom_audit_rejects_non_reference_hash(self):
        data = bytearray(0x300)
        data[0x100:0x104] = b"SEGA"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.gen"
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                AUDIT.audit_pinned_rom(path)

    def test_pinned_audit_emits_numeric_only_summary(self):
        table_base = AUDIT.TABLE_BASE
        primary = [
            0x54, 0x82, 0xB0, 0xBE, 0xCC, 0xE0, 0xF4, 0xF8,
            0xFC, 0x102, 0x108, 0x116, 0x124, 0x132, 0x140, 0x148,
        ]
        rest = [
            0x156, 0x150, 0x15C, 0x168, 0x174, 0x17E, 0x188, 0x190,
            0x198, 0x1A2, 0x58, 0x86, 0x1C0, 0x1CA, 0x1D4, 0x1DE,
            0x1F4, 0x1FA, 0x1E8, 0x1EE, 0x1AC, 0x1B6, 0x200, 0x216,
            0x224, 0x228,
        ]
        offsets = primary + rest
        data = self.make_directory(table_base, offsets, size=0x21600)
        data[table_base:table_base + 2] = (0x54).to_bytes(2, "big")
        data[0x100:0x104] = b"SEGA"
        headers = [
            0x00FF, 0x00FF, 0x05FF, 0x05FF, 0x03FF, 0x03FF, 0x0004, 0x0004,
            0x0104, 0x0104, 0x0504, 0x0504, 0x0500, 0x0500, 0x0204, 0x0204,
        ]
        for offset, header in zip(primary, headers):
            start = table_base + offset
            data[start:start + 2] = header.to_bytes(2, "big")
        digest = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.gen"
            path.write_bytes(data)
            original_hash = AUDIT.REFERENCE_ROM_SHA256
            try:
                AUDIT.REFERENCE_ROM_SHA256 = digest
                summary = AUDIT.audit_pinned_rom(path)
            finally:
                AUDIT.REFERENCE_ROM_SHA256 = original_hash
        self.assertEqual(summary["status"], "PASS_NUMERIC_OFFSET_DIRECTORY_AUDIT")
        self.assertEqual(summary["entry_count"], 42)
        self.assertFalse(summary["raw_rom_exported"])
        self.assertFalse(summary["raw_ram_exported"])
        self.assertNotIn("rom_bytes", summary)
        self.assertNotIn("raw_bytes", summary)


if __name__ == "__main__":
    unittest.main()
