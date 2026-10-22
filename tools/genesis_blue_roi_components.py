#!/usr/bin/env python3
"""Stage205: ROM-free connected components for a transient libretro blue ROI.

Only integer aggregates leave this function. This intentionally does NOT
identify Donald, infer a hitbox, or prove the original Genesis contact plane.
A 4-connected blue mask is a heuristic and can include background pixels.
"""
from genesis_blue_roi_numeric import validate_roi, MAX_FRAME_BYTES

PIXEL_FORMATS = {0: "0RGB1555", 1: "XRGB8888", 2: "RGB565"}
MAX_ROI_PIXELS = 65536
MAX_COMPONENTS = 4


def require(ok, message):
    if not ok:
        raise ValueError("Genesis blue connected components: " + message)


def _blue_pixel(value, pixel_format):
    if pixel_format == 1:
        red, green, blue = ((value >> 16) & 255) >> 3, ((value >> 8) & 255) >> 3, (value & 255) >> 3
    elif pixel_format == 2:
        red, green, blue = (value >> 11) & 31, ((value >> 5) & 63) >> 1, value & 31
    else:
        red, green, blue = (value >> 10) & 31, (value >> 5) & 31, value & 31
    return blue >= 19 and blue - red >= 8 and blue - green >= 4


def blue_roi_components_numeric(frame, width, height, pitch, pixel_format,
                                roi, *, min_component_pixels=5,
                                max_components=MAX_COMPONENTS):
    """Summarize only the largest 4-connected blue regions in a bounded ROI."""
    x0, y0, w, h = validate_roi(roi)
    require(type(width) is int and type(height) is int and
            type(pitch) is int and type(pixel_format) is int and
            0 < width <= 2048 and 0 < height <= 2048 and
            pixel_format in PIXEL_FORMATS, "unsupported frame geometry/format")
    require(type(min_component_pixels) is int and 2 <= min_component_pixels <= 1024 and
            type(max_components) is int and 1 <= max_components <= MAX_COMPONENTS,
            "unsupported component selection bounds")
    require(w * h <= MAX_ROI_PIXELS and
            x0 + w <= width and y0 + h <= height,
            "ROI must be in-frame and at most 65536 pixels")
    bpp = 4 if pixel_format == 1 else 2
    require(width * bpp <= pitch and
            pitch * height <= MAX_FRAME_BYTES and
            type(frame) in (bytes, bytearray, memoryview) and
            len(frame) == pitch * height, "invalid original-frame buffer length")
    mask = bytearray(w * h)
    blue_total = 0
    for ry in range(h):
        start = (y0 + ry) * pitch + x0 * bpp
        for rx in range(w):
            offset = start + rx * bpp
            word = int.from_bytes(frame[offset:offset + bpp], "little")
            if _blue_pixel(word, pixel_format):
                mask[ry * w + rx] = 1
                blue_total += 1
    visited = bytearray(w * h)
    found = []
    for seed in range(w * h):
        if not mask[seed] or visited[seed]:
            continue
        visited[seed] = 1
        stack = [seed]
        count = sum_x = sum_y = 0
        x_min, y_min = x0 + w, y0 + h
        x_max, y_max = x0 - 1, y0 - 1
        while stack:
            offset = stack.pop()
            rx, ry = offset % w, offset // w
            ax, ay = x0 + rx, y0 + ry
            count += 1
            sum_x += ax
            sum_y += ay
            x_min, x_max = min(x_min, ax), max(x_max, ax)
            y_min, y_max = min(y_min, ay), max(y_max, ay)
            neighbors = []
            if rx > 0:
                neighbors.append(offset - 1)
            if rx + 1 < w:
                neighbors.append(offset + 1)
            if ry > 0:
                neighbors.append(offset - w)
            if ry + 1 < h:
                neighbors.append(offset + w)
            for n in neighbors:
                if mask[n] and not visited[n]:
                    visited[n] = 1
                    stack.append(n)
        found.append((count, x_min, y_min, x_max, y_max, sum_x, sum_y))
    found.sort(key=lambda item: (-item[0], item[2], item[1]))
    eligible = [item for item in found if item[0] >= min_component_pixels]
    selected = []
    for count, xmin, ymin, xmax, ymax, sumx, sumy in eligible[:max_components]:
        selected.append({
            "blue_pixels": count,
            "blue_centroid_x_milli": 1000 * sumx // count,
            "blue_centroid_y_milli": 1000 * sumy // count,
            "blue_bbox_xyxy_exclusive": [xmin, ymin, xmax + 1, ymax + 1],
            "touches_roi_boundary": (
                xmin == x0 or ymin == y0 or
                xmax + 1 == x0 + w or ymax + 1 == y0 + h
            ),
        })
    # Even a unique large connected blob is not identified as a sprite.
    clear_dominance = bool(
        len(eligible) == 1 or
        (len(eligible) > 1 and eligible[0][0] >= 2 * eligible[1][0])
    )
    return {
        "schema": "quackshot_genesis_blue_components_numeric_v1",
        "roi": [x0, y0, w, h],
        "pixel_format": PIXEL_FORMATS[pixel_format],
        "connectivity": 4,
        "min_component_pixels": min_component_pixels,
        "blue_pixel_count": blue_total,
        "all_blue_component_count": len(found),
        "eligible_component_count": len(eligible),
        "reported_component_count": len(selected),
        "largest_component_dominates_by_2x": bool(eligible) and clear_dominance,
        "components": selected,
        "selected_component_is_proven_player": False,
        "original_sprite_hitbox_or_contact_verified": False,
        "safe_to_modify_switch_physics": False,
    }
