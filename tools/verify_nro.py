#!/usr/bin/env python3
"""Validate structural invariants of a Nintendo Switch homebrew NRO.

This deliberately does not execute the binary and is not an emulator test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(data: bytes, require_nacp: bool = False) -> dict:
    if len(data) < 0x80:
        raise ValueError('NRO is smaller than its header')
    if data[0x10:0x14] != b'NRO0':
        raise ValueError('Missing NRO0 signature at offset 0x10')
    version, image_size, flags = struct.unpack_from('<III', data, 0x14)
    if image_size < 0x80 or image_size > len(data):
        raise ValueError('Declared NRO image size is out of bounds')
    segments = {}
    for name, offset in [('text', 0x20), ('rodata', 0x28), ('data', 0x30)]:
        start, size = struct.unpack_from('<II', data, offset)
        if start < 0x80 or size == 0 or start > image_size or size > image_size - start:
            raise ValueError(f'{name} segment outside declared image')
        segments[name] = {'offset': start, 'size': size}
    if len(data) == image_size:
        if require_nacp:
            raise ValueError('NACP metadata missing (no ASET section)')
        metadata = None
    else:
        if len(data) - image_size < 0x38:
            raise ValueError('Trailing data too small for ASET section header')
        if data[image_size:image_size + 4] != b'ASET':
            raise ValueError('Expected ASET header after NRO image')
        aset_version = struct.unpack_from('<I', data, image_size + 4)[0]
        if aset_version != 0:
            raise ValueError('Unsupported ASET version')
        asset_sizes = {}
        for name, off in [('icon', 0x08), ('nacp', 0x18), ('romfs', 0x28)]:
            rel, size = struct.unpack_from('<QQ', data, image_size + off)
            if size and (rel < 0x38 or rel > len(data) - image_size or
                         size > len(data) - image_size - rel):
                raise ValueError(f'{name} asset outside file')
            asset_sizes[name] = size
        if require_nacp and not asset_sizes['nacp']:
            raise ValueError('NACP metadata missing')
        metadata = asset_sizes
    return {'image_size': image_size, 'file_size': len(data), 'version': version,
            'flags': flags, 'segments': segments, 'assets_bytes': metadata,
            'sha256': hashlib.sha256(data).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nro', type=Path)
    parser.add_argument('--require-nacp', action='store_true')
    args = parser.parse_args()
    try:
        report = inspect(args.nro.read_bytes(), require_nacp=args.require_nacp)
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Invalid NRO: {exc}\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
