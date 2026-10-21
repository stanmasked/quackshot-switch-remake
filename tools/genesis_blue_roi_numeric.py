#!/usr/bin/env python3
"""Stage198: ROM-free 5-bit blue-pixel ROI statistics for private libretro video.

The output contains counts, integer centroid coordinates and bounding boxes,
never pixel values, framebuffer bytes, sprite art or raw frames. This is an
appearance heuristic, NOT an independently measured contact/collision plane.
"""
from collections.abc import Sequence

MAX_FRAME_BYTES = 16 * 1024 * 1024


def _check(condition, message):
    if not condition:
        raise ValueError("Visual ROI numeric: " + message)


def validate_roi(roi):
    _check(type(roi) in (tuple, list) and len(roi) == 4 and
           all(type(n) is int for n in roi), "ROI requires four exact integers")
    x, y, w, h = roi
    _check(x >= 0 and y >= 0 and 1 <= w <= 1024 and 1 <= h <= 1024,
           "ROI location/size outside safe bounds")
    return x, y, w, h


def blue_roi_numeric(frame_bytes, width, height, pitch, pixel_format, roi):
    """Aggregate one transient little-endian libretro frame into numeric data.

    libretro format 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565.
    Blue classification is an author-chosen illustrative RGB heuristic.
    """
    x0, y0, w, h = validate_roi(roi)
    _check(all(type(v) is int for v in (width, height, pitch, pixel_format)),
           "non-integer video dimensions or pixel format")
    _check(0 < width <= 2048 and 0 < height <= 2048 and
           pixel_format in (0, 1, 2), "unsupported video geometry or format")
    bpp = 4 if pixel_format == 1 else 2
    _check(pitch >= width * bpp and pitch * height <= MAX_FRAME_BYTES,
           "invalid stride/frame size")
    _check(x0 + w <= width and y0 + h <= height,
           "ROI extends beyond this frame")
    _check(isinstance(frame_bytes, (bytes, bytearray, memoryview)) and
           len(frame_bytes) == pitch * height,
           "private frame buffer length invalid")
    count = sum_x = sum_y = 0
    min_x = x0 + w
    min_y = y0 + h
    max_x = x0 - 1
    max_y = y0 - 1
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            off = y * pitch + x * bpp
            value = int.from_bytes(frame_bytes[off:off + bpp], "little")
            if pixel_format == 1:
                r = ((value >> 16) & 255) >> 3
                g = ((value >> 8) & 255) >> 3
                b = (value & 255) >> 3
            elif pixel_format == 2:
                r, g, b = (value >> 11) & 31, ((value >> 5) & 63) >> 1, value & 31
            else:
                r, g, b = (value >> 10) & 31, (value >> 5) & 31, value & 31
            if b >= 19 and b - r >= 8 and b - g >= 4:
                count += 1
                sum_x += x
                sum_y += y
                min_x, max_x = min(min_x, x), max(max_x, x)
                min_y, max_y = min(min_y, y), max(max_y, y)
    return {
        "roi": [x0, y0, w, h],
        "pixel_format": {0: "0RGB1555", 1: "XRGB8888", 2: "RGB565"}[pixel_format],
        "blue_pixel_count": count,
        "blue_centroid_x_milli": (1000 * sum_x // count) if count else None,
        "blue_centroid_y_milli": (1000 * sum_y // count) if count else None,
        "blue_bbox_xyxy_exclusive": (
            [min_x, min_y, max_x + 1, max_y + 1] if count else None
        ),
        "source_collision_plane_measured": False,
    }
