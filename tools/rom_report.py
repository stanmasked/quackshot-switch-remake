#!/usr/bin/env python3
"""Read-only Sega Mega Drive ROM metadata inspector. Does not extract game assets."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

MAX_ROM = 8 * 1024 * 1024


def _ascii(data):
    return data.decode('ascii', errors='replace').strip(' \x00')


def _smd_to_linear(data):
    if len(data) <= 512 or (len(data) - 512) % 16384:
        return None
    linear = bytearray()
    for offset in range(512, len(data), 16384):
        block = data[offset:offset + 16384]
        for i in range(8192):
            linear.append(block[8192 + i])
            linear.append(block[i])
    return bytes(linear)


def _read_rom(path):
    if path.suffix.lower() != '.zip':
        if path.stat().st_size > MAX_ROM + 512:
            raise ValueError('File exceeds the 8 MiB inspection limit')
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        entries = [e for e in archive.infolist() if not e.is_dir() and
                   Path(e.filename).suffix.lower() in ('.bin', '.md', '.gen', '.smd')]
        if len(entries) != 1:
            raise ValueError('ZIP must contain exactly one .bin/.md/.gen/.smd ROM')
        if entries[0].file_size > MAX_ROM + 512:
            raise ValueError('ROM in ZIP exceeds the inspection limit')
        with archive.open(entries[0]) as stream:
            data = stream.read(MAX_ROM + 513)
        if len(data) > MAX_ROM + 512:
            raise ValueError('ROM in ZIP exceeds the inspection limit')
        return data


def inspect_data(raw):
    if len(raw) > MAX_ROM + 512:
        raise ValueError('ROM exceeds the inspection limit')
    image = raw
    fmt = 'linear'
    if len(image) < 0x200 or image[0x100:0x104] != b'SEGA':
        candidate = _smd_to_linear(raw)
        if candidate is None or candidate[0x100:0x104] != b'SEGA':
            raise ValueError('No standard SEGA header found (unsupported or invalid ROM)')
        image = candidate
        fmt = 'smd-interleaved'
    declared_checksum = int.from_bytes(image[0x18e:0x190], 'big')
    body = image[0x200:]
    if len(body) % 2:
        body += b'\x00'
    calculated = sum(int.from_bytes(body[i:i + 2], 'big') for i in range(0, len(body), 2)) & 0xffff
    return {
        'container_format': fmt,
        'raw_sha256': hashlib.sha256(raw).hexdigest(),
        'linear_sha256': hashlib.sha256(image).hexdigest(),
        'rom_size_bytes': len(image),
        'console': _ascii(image[0x100:0x110]),
        'domestic_title': _ascii(image[0x120:0x150]),
        'overseas_title': _ascii(image[0x150:0x180]),
        'product_code': _ascii(image[0x180:0x18e]),
        'region': _ascii(image[0x1f0:0x200]),
        'header_checksum': f'{declared_checksum:04X}',
        'calculated_checksum': f'{calculated:04X}',
        'checksum_matches': declared_checksum == calculated,
    }


def compare_profile(result, expected):
    """Return mismatched fields (without copying or exporting ROM bytes)."""
    if not isinstance(expected, dict) or not expected:
        raise ValueError('Expected profile must be a nonempty JSON object')
    return {key: {'expected': value, 'observed': result.get(key)}
            for key, value in expected.items() if key not in result or result[key] != value}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path, help='Local ROM or ZIP file; never uploaded by this script')
    parser.add_argument('--profile', type=Path, help='Optional JSON profile with expected metadata/hash')
    args = parser.parse_args()
    try:
        result = inspect_data(_read_rom(args.rom))
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as e:
        parser.exit(1, f'ROM inspection failed: {e}\n')
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if args.profile:
        try:
            expected = json.loads(args.profile.read_text(encoding='utf-8'))
            mismatches = compare_profile(result, expected)
        except (OSError, ValueError) as e:
            parser.exit(1, f'Profile read failed: {e}\\n')
        if mismatches:
            parser.exit(2, f'Profile mismatch: {json.dumps(mismatches, ensure_ascii=False)}\\n')
        print('PROFILE MATCH: all recorded fields agree')


if __name__ == '__main__':
    main()
