import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_nro import inspect


def make_nro():
    data = bytearray(0x2c0)
    data[0x10:0x14] = b'NRO0'
    struct.pack_into('<III', data, 0x14, 0, 0x180, 0)
    for start, (offset, size) in zip([0x20, 0x28, 0x30], [(0, 0xc0), (0xc0, 0x40), (0x100, 0x40)]):
        struct.pack_into('<II', data, start, offset, size)
    data[0x180:0x184] = b'ASET'
    struct.pack_into('<QQ', data, 0x180 + 0x18, 0x40, 0x100)
    return data


class NroTests(unittest.TestCase):
    def test_valid_nro_with_nacp(self):
        report = inspect(bytes(make_nro()), require_nacp=True)
        self.assertEqual(report['assets_bytes']['nacp'], 0x100)

    def test_missing_magic(self):
        data = make_nro()
        data[0x10:0x14] = b'NOPE'
        with self.assertRaisesRegex(ValueError, 'NRO0'):
            inspect(bytes(data))

    def test_truncated_segment(self):
        data = make_nro()
        struct.pack_into('<II', data, 0x20, 0x100, 0x200)
        with self.assertRaisesRegex(ValueError, 'text segment'):
            inspect(bytes(data))

    def test_out_of_bounds_nacp(self):
        data = make_nro()
        struct.pack_into('<QQ', data, 0x180 + 0x18, 0x40, 0x900)
        with self.assertRaisesRegex(ValueError, 'nacp asset'):
            inspect(bytes(data), require_nacp=True)

    def test_missing_nacp(self):
        data = make_nro()
        struct.pack_into('<QQ', data, 0x180 + 0x18, 0, 0)
        with self.assertRaisesRegex(ValueError, 'NACP metadata'):
            inspect(bytes(data), require_nacp=True)

    def test_short_data(self):
        with self.assertRaisesRegex(ValueError, 'smaller'):
            inspect(b'')


if __name__ == '__main__':
    unittest.main()
