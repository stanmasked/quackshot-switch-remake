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


## Verified reference — 2026-10-17

A user-provided `.gen` image was inspected locally. The corresponding
**metadata-only** fingerprint is committed as
[`research/reference_profiles/user_rom_2026-10-17.json`](../research/reference_profiles/user_rom_2026-10-17.json).
The image itself is deliberately excluded from GitHub.

The ROM's overseas title contains `RUSSIAN` and its country code is `C`.
These are literal header values, **not** proof of a particular publisher,
specific Russian-language patch, or a standard USA/Europe/Japan region.
Its reported checksum matches the checksum calculated over the image.
That still does not independently validate the game's authenticity.

Re-check the exact private local file using:

```sh
python3 tools/rom_report.py "/private/path/QuackShot Starring Donald Duck.gen" \
  --profile research/reference_profiles/user_rom_2026-10-17.json
```

Expected result: `PROFILE MATCH: all recorded fields agree`.
The inspector will exit with status 2 if any recorded field differs.
CI tests use exclusively synthetic ROMs, never the original file.

## Future game-mechanics research

Measurements of movement speed, jump height, map transitions, animation
timing and enemy AI should come from **observed emulation/hardware traces**
of the exact pinned reference image. ROM header metadata alone does not
establish any of these behaviors.
