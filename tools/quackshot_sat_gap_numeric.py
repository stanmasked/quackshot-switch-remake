#!/usr/bin/env python3
"""Check bounded numeric sprite/video alignment without reading any ROM or media.

Inputs are caller-supplied numerical observations. Passing these checks
establishes internal consistency, NOT authenticity or sprite identity.
"""

EXPECTED_SCENARIOS = ("right", "right_repeat", "right_shift", "left", "fire")
ALLOWED_ROW_KEYS = {"frame", "selector", "cursor", "flag", "candidate_sprites",
                    "blue_pixels", "animation_writes"}


def _window(rows, selector):
    if type(rows) not in (tuple, list) or len(rows) != 3:
        raise ValueError("each observation window must contain three frames")
    previous_frame = None
    for index, row in enumerate(rows):
        if type(row) is not dict or set(row) != ALLOWED_ROW_KEYS:
            raise ValueError("numeric observation has missing or unexpected fields")
        if any(type(value) is not int for value in row.values()):
            raise ValueError("numeric observations require exact integers")
        if not (0 <= row["frame"] <= 100000 and
                row["selector"] == selector and
                row["cursor"] == (0 if index == 0 else 1) and
                row["flag"] == 10 and
                0 <= row["candidate_sprites"] <= 80 and
                0 <= row["blue_pixels"] <= 65536 and
                0 <= row["animation_writes"] <= 8):
            raise ValueError("out-of-range or inconsistent animation observation")
        if previous_frame is not None and row["frame"] != previous_frame + 1:
            raise ValueError("sampled frames must be consecutive")
        previous_frame = row["frame"]
    return rows


def audit_first_frame_gap(windows):
    """Compare matched controls and repeat/shift of a one-frame numeric gap."""
    if type(windows) is not dict or set(windows) != set(EXPECTED_SCENARIOS):
        raise ValueError("all five controls/repeats are required")
    right = _window(windows["right"], 29)
    repeat = _window(windows["right_repeat"], 29)
    shifted = _window(windows["right_shift"], 29)
    left = _window(windows["left"], 28)
    fire = _window(windows["fire"], 19)

    if right != repeat:
        raise ValueError("identically scheduled runs are not numerically repeatable")
    shift = shifted[0]["frame"] - right[0]["frame"]
    if shift != 10 or any(
        {k: v for k, v in row.items() if k != "frame"} !=
        {k: v for k, v in reference.items() if k != "frame"}
        for row, reference in zip(shifted, right)
    ):
        raise ValueError("shifted action does not reproduce the same event")
    if any(seq[0]["frame"] != right[0]["frame"] for seq in (left, fire)):
        raise ValueError("controls must have matched action timing")
    if not (
        right[0]["candidate_sprites"] > 0 and right[0]["blue_pixels"] > 0 and
        right[0]["animation_writes"] == 0 and
        right[1]["candidate_sprites"] == 0 and right[1]["blue_pixels"] == 0 and
        right[1]["animation_writes"] == 2 and
        right[2]["candidate_sprites"] > 0 and right[2]["blue_pixels"] > 0 and
        left[1]["candidate_sprites"] > 0 and left[1]["blue_pixels"] > 0 and
        fire[1]["candidate_sprites"] > 0 and fire[1]["blue_pixels"] > 0
    ):
        raise ValueError("missing gap, recovery, or matched positive control")
    return {
        "schema": "quackshot_numeric_first_frame_gap_check_v1",
        "verified_numeric_scenarios": len(EXPECTED_SCENARIOS),
        "gap_relative_frame": 1,
        "shifted_input_frame_offset": shift,
        "original_rom_or_rendered_pixels_verified_by_this_function": False,
    }
