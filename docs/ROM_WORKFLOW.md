# Original reference ROM — local-only handling

This repository must **never** contain a QuackShot game ROM, extracted
proprietary assets, or captures containing copyrighted game content without
appropriate rights.

For a legally obtained, user-provided Mega Drive/Genesis dump, run locally:

    python3 tools/rom_report.py /path/to/private/quackshot.bin

A ZIP containing exactly one .bin/.gen/.md/.smd game dump is also accepted.
The script reads the source, optionally de-interleaves legacy SMD dumps in
memory, and reports header metadata, SHA-256 values, and the Mega Drive
header checksum result. It does not publish, export, patch, or extract assets.

**Important:** header checksum agreement is only a sanity check; it is not
proof that a ROM is authentic, complete, or licensed.

Synthetic test fixtures are generated on the fly:

    python3 -m unittest discover -s tests -p test_rom_report.py -v

A reference image is not needed to build or test the independent prototype.
