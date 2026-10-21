"""Independent verifier of guest-generated, bounded camera timing windows.

Event 14 contains a packed pair of integer-pixel camera positions:
current simulation camera (high16) and presented camera (low16).
Do not manufacture missing guest rows. Only a genuine NRO trace is accepted.
"""
from __future__ import annotations

WINDOW = 24
EVENT_BURST = 14
EVENT_PHASE = 13
EVENT_PARALLAX = 12


def _num(row, key):
    try:
        return int(row[key])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"invalid {key!r} field") from exc


def _decode(row):
    detail = _num(row, "detail")
    if not 0 <= detail <= 0xFFFFFFFF:
        raise ValueError("packed camera field is outside unsigned 32-bit range")
    return detail >> 16, detail & 0xFFFF


def verify_camera_bursts(records):
    """Return measured directional evidence, or raise ValueError (fail closed).

    The two windows must cover 24 consecutive simulation frames each,
    with a strict single-tick presentation lag. Sparse samples cannot
    substitute for these windows.
    """
    rows = [r for r in records if _num(r, "event") == EVENT_BURST]
    if len(rows) != WINDOW * 2:
        raise ValueError(f"expected two {WINDOW}-tick camera bursts, got {len(rows)}")
    if any(_num(r, "mode") != 1 or _num(r, "stage") != 0 or
           _num(r, "controllers") != 1 for r in rows):
        raise ValueError("camera burst outside active, controller-driven CITY TEST")
    if any(_num(b, "seq") <= _num(a, "seq") or
           _num(b, "frames") <= _num(a, "frames")
           for a, b in zip(rows, rows[1:])):
        raise ValueError("camera burst sequence/frame ordering invalid")

    groups = [rows[:WINDOW], rows[WINDOW:]]
    if _num(groups[1][0], "frames") <= _num(groups[0][-1], "frames") + 1:
        raise ValueError("camera bursts overlap or were not separated by a reversal")

    evidence = {}
    for group, direction, name in (
        (groups[0], 1, "forward"),
        (groups[1], -1, "reverse"),
    ):
        frames = [_num(row, "frames") for row in group]
        if any(b != a + 1 for a, b in zip(frames, frames[1:])):
            raise ValueError(f"{name}: dropped or duplicated simulation tick")
        pairs = [_decode(row) for row in group]
        moves = []
        for (prev_current, _), (current, presented) in zip(pairs, pairs[1:]):
            if presented != prev_current:
                raise ValueError(f"{name}: one-tick presentation history mismatch")
            delta = current - prev_current
            if not 0 <= delta * direction <= 3:
                raise ValueError(f"{name}: incorrect direction or camera teleport")
            moves.append(delta)
        moving = sum(delta * direction > 0 for delta in moves)
        if moving < 12 or abs(pairs[-1][0] - pairs[0][0]) < 16:
            raise ValueError(f"{name}: camera did not move convincingly")
        if max(abs(a-b) for a, b in pairs) > 3:
            raise ValueError(f"{name}: implausible current/render gap")
        evidence[f"camera_{name}_steps"] = WINDOW
        evidence[f"camera_{name}_moving_ticks"] = moving
        evidence[f"camera_{name}_delta_px"] = pairs[-1][0] - pairs[0][0]

    # Independently join event 14 against the existing renderer phase
    # event 13 and two-plane event 12 at any overlapping 15-frame sample.
    windows = { _num(r, "frames"): _decode(r) for r in rows }
    phase = { _num(r, "frames"): _decode(r)
              for r in records if _num(r, "event") == EVENT_PHASE }
    planes = { _num(r, "frames"): _decode(r)
               for r in records if _num(r, "event") == EVENT_PARALLAX }
    matching = 0
    for frame, (current, presented) in windows.items():
        if frame not in phase:
            continue
        if frame not in planes or phase[frame] != (current, presented):
            raise ValueError("guest sparse camera phase disagrees with dense camera witness")
        foreground, distant = planes[frame]
        if foreground != presented or distant != foreground // 2:
            raise ValueError("renderer plane scroll differs from dense camera witness")
        matching += 1
    if matching < 2:
        raise ValueError("insufficient overlapping renderer and camera witnesses")
    evidence["camera_dense_sparse_matches"] = matching
    evidence["camera_burst_samples"] = len(rows)
    return evidence
