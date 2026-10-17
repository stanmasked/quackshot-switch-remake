import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from rom_report import _read_rom, inspect_data


def fixture():
    image = bytearray(65536)
    image[0x100:0x110] = b'SEGA MEGA DRIVE '
    image[0x120:0x12a] = b'TEST GAME '
    image[0x150:0x15a] = b'TEST GAME '
    image[0x180:0x18e] = b'GM TEST-00'.ljust(14, b' ')
    image[0x1f0:0x1f3] = b'JUE'
    for i in range(0x200, len(image), 2):
        image[i] = (i // 2) % 251
        image[i + 1] = i % 127
    value = sum(int.from_bytes(image[i:i + 2], 'big') for i in range(0x200, len(image), 2)) & 0xffff
    image[0x18e:0x190] = value.to_bytes(2, 'big')
    return bytes(image)


def interleave(image):
    blocks = [b'\x00' * 512]
    for offset in range(0, len(image), 16384):
        block = image[offset:offset + 16384]
        blocks.append(block[1::2] + block[0::2])
    return b''.join(blocks)


class RomTests(unittest.TestCase):
    def test_linear(self):
        result = inspect_data(fixture())
        self.assertEqual(result['container_format'], 'linear')
        self.assertTrue(result['checksum_matches'])
        self.assertEqual(result['rom_size_bytes'], 65536)

    def test_smd_and_linear_fingerprints(self):
        rom = fixture()
        a = inspect_data(rom)
        b = inspect_data(interleave(rom))
        self.assertEqual(b['container_format'], 'smd-interleaved')
        self.assertEqual(a['linear_sha256'], b['linear_sha256'])
        self.assertNotEqual(a['raw_sha256'], b['raw_sha256'])

    def test_modified_body_detected(self):
        corrupted = bytearray(fixture())
        corrupted[0x800] ^= 1
        self.assertFalse(inspect_data(bytes(corrupted))['checksum_matches'])

    def test_bad_header_rejected(self):
        with self.assertRaises(ValueError):
            inspect_data(b'not a Mega Drive ROM')

    def test_zip_single_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as dirname:
            archive = Path(dirname) / 'input.zip'
            with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('original.bin', fixture())
            self.assertEqual(_read_rom(archive), fixture())


if __name__ == '__main__':
    unittest.main()
