#!/usr/bin/env python3
"""Run a private Mega Drive ROM locally using a libretro core.

A ROM is NEVER sent to GitHub. Outputs contain numeric observations only,
not RAM dumps, video frames, audio samples, game assets or executable bytes.
"""
import argparse
from collections import Counter
import ctypes as C
import hashlib
import json
from pathlib import Path
import zlib

from genesis_blue_roi_numeric import blue_roi_numeric, validate_roi
from genesis_blue_roi_components import blue_roi_components_numeric

MAX_ROM_BYTES = 8 * 1024 * 1024
MAX_FRAMES = 10000
MAX_CPU_TRACE_WINDOW = 128
MAX_SAT_SNAPSHOT_FRAMES = 128
MAX_SAT_SPRITES = 80
MAX_OBJECT_SNAPSHOT_FRAMES = 128
MAX_COLLISION_PROFILE_SNAPSHOT_FRAMES = 24
COLLISION_PROFILE_TABLE_OFFSET = 0x7E500
RETRO_MEMORY_SYSTEM_RAM = 2
RETRO_DEVICE_JOYPAD = 1
BUTTONS = {
    "B": 0, "Y": 1, "SELECT": 2, "START": 3,
    "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
    "A": 8, "X": 9, "L": 10, "R": 11,
}
ENV_GET_CAN_DUPE = 3
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_GET_SAVE_DIRECTORY = 31
ENV_GET_LANGUAGE = 39

EnvCb = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VideoCb = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AudioCb = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AudioBatchCb = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
InputPollCb = C.CFUNCTYPE(None)
InputStateCb = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]


class SystemInfo(C.Structure):
    _fields_ = [("library_name", C.c_char_p), ("library_version", C.c_char_p),
                ("valid_extensions", C.c_char_p), ("need_fullpath", C.c_bool),
                ("block_extract", C.c_bool)]


class Geometry(C.Structure):
    _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint),
                ("max_width", C.c_uint), ("max_height", C.c_uint),
                ("aspect_ratio", C.c_float)]


class Timing(C.Structure):
    _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]


class AvInfo(C.Structure):
    _fields_ = [("geometry", Geometry), ("timing", Timing)]


def parse_script(spec):
    """Inclusive-exclusive per-frame input ranges; overlapping ranges merge."""
    if not spec:
        return []
    result = []
    for entry in spec.split(","):
        parts = entry.strip().split(":")
        if len(parts) != 3:
            raise ValueError("Expected start:end:BUTTON, comma separated")
        start, stop = (int(parts[0]), int(parts[1]))
        button = parts[2].strip().upper()
        if start < 0 or start >= stop or stop > MAX_FRAMES or button not in BUTTONS:
            raise ValueError("Invalid input range/button: " + entry)
        result.append((start, stop, button))
    return result


def buttons_at(frame, schedule):
    return sorted({name for start, stop, name in schedule if start <= frame < stop})


def safe_rom(rom_path, expected_sha):
    if rom_path.stat().st_size > MAX_ROM_BYTES or rom_path.stat().st_size < 0x200:
        raise ValueError("ROM size outside 512-byte to 8-MiB inspection limit")
    data = rom_path.read_bytes()
    checksum = hashlib.sha256(data).hexdigest()
    if expected_sha and checksum.lower() != expected_sha.lower():
        raise ValueError("Private ROM SHA-256 does not match the pinned reference")
    if data[0x100:0x104] != b"SEGA":
        raise ValueError("Not a linear Sega Mega Drive ROM")
    return data, checksum


def setup_core(core_path):
    core = C.CDLL(str(core_path.resolve()))
    function_signatures = {
        "retro_get_system_info": ([C.POINTER(SystemInfo)], None),
        "retro_get_system_av_info": ([C.POINTER(AvInfo)], None),
        "retro_set_environment": ([EnvCb], None),
        "retro_set_video_refresh": ([VideoCb], None),
        "retro_set_audio_sample": ([AudioCb], None),
        "retro_set_audio_sample_batch": ([AudioBatchCb], None),
        "retro_set_input_poll": ([InputPollCb], None),
        "retro_set_input_state": ([InputStateCb], None),
        "retro_init": ([], None), "retro_deinit": ([], None),
        "retro_load_game": ([C.POINTER(GameInfo)], C.c_bool),
        "retro_unload_game": ([], None),
        "retro_run": ([], None),
        "retro_set_controller_port_device": ([C.c_uint, C.c_uint], None),
        "retro_get_memory_data": ([C.c_uint], C.c_void_p),
        "retro_get_memory_size": ([C.c_uint], C.c_size_t),
    }
    for name, (args, result) in function_signatures.items():
        fn = getattr(core, name)
        fn.argtypes = args
        fn.restype = result
    info = SystemInfo()
    core.retro_get_system_info(C.byref(info))
    return core, {
        "library_name": info.library_name.decode("utf-8", "replace") if info.library_name else "",
        "library_version": info.library_version.decode("utf-8", "replace") if info.library_version else "",
        "extensions": info.valid_extensions.decode("utf-8", "replace") if info.valid_extensions else "",
        "need_fullpath": bool(info.need_fullpath),
    }


def frame_digest_numeric(frame_bytes, width, height, pitch, pixel_format):
    """Hash one transient full framebuffer; return only numeric metadata and SHA-256."""
    if (type(frame_bytes) is not bytes or type(width) is not int or
            type(height) is not int or type(pitch) is not int or
            type(pixel_format) is not int or pixel_format not in (0, 1, 2) or
            not 1 <= width <= 2048 or not 1 <= height <= 2048):
        raise ValueError("invalid full-frame digest input")
    bpp = 4 if pixel_format == 1 else 2
    if pitch < width * bpp or pitch * height > 16 * 1024 * 1024 or len(frame_bytes) != pitch * height:
        raise ValueError("invalid full-frame digest geometry")
    visible_row_bytes = width * bpp
    visible = b"".join(
        frame_bytes[row * pitch:row * pitch + visible_row_bytes]
        for row in range(height)
    )
    return {
        "size": [width, height],
        "pitch": pitch,
        "pixel_format": pixel_format,
        "byte_count": len(frame_bytes),
        "sha256": hashlib.sha256(frame_bytes).hexdigest(),
        "visible_byte_count": len(visible),
        "visible_sha256": hashlib.sha256(visible).hexdigest(),
    }


def parse_frame_indices(spec, frame_count, limit=128):
    """Parse a comma-separated set of frame indices without accepting ambiguity."""
    if type(spec) is not str or type(frame_count) is not int or frame_count < 1:
        raise ValueError("invalid video digest frame specification")
    if spec == "":
        return []
    tokens = spec.split(",")
    if len(tokens) > limit or any(not token.isascii() or not token.isdecimal() for token in tokens):
        raise ValueError("video digest frames must be comma-separated decimal indices")
    frames = [int(token) for token in tokens]
    if len(set(frames)) != len(frames) or any(f < 0 or f >= frame_count for f in frames):
        raise ValueError("video digest frames must be unique and within the run")
    return sorted(frames)



def parse_cpu_trace_window(spec, frame_count, limit=MAX_CPU_TRACE_WINDOW):
    """Parse an inclusive bounded frame window for opt-in C-side CPU tracing."""
    if type(spec) is not str or type(frame_count) is not int or frame_count < 1:
        raise ValueError("invalid CPU trace frame window")
    if spec == "":
        return None
    parts = spec.split(":")
    if (len(parts) != 2 or any(not p.isascii() or not p.isdecimal()
                               for p in parts)):
        raise ValueError("CPU trace window must be FIRST:LAST decimal frame indices")
    first, last = (int(p) for p in parts)
    if (first < 0 or first > last or last >= frame_count or
            last - first + 1 > limit):
        raise ValueError("CPU trace window must be in range and at most 128 frames")
    return first, last


CPU_TRACE_WATCH_RANGES = (
    (0x8F28, 0x8F32),
    (0x8F52, 0x8F5A),
    (0x9025, 0x9029),
    (0x904E, 0x9052),
    (0x8F96, 0x8F9A),
    (0xA2C0, 0xA2F0),
    (0xEE03, 0xEE04),
    (0xEE08, 0xEE0B),
    (0xEE41, 0xEE46),
    (0xEE58, 0xEE59),
    (0xEE66, 0xEE67),
    (0xF374, 0xF378),
)

CPU_PC_TRACE_RANGES = (
    (0x0206, 0x0208),  # synthetic CI instruction only
    (0x5D20, 0x5DE0),  # coordinate/state writers
    (0x6100, 0x6160),  # object-state dispatch and movement gate
    (0x5E90, 0x5F80),  # animation/state helper
    (0x6A40, 0x6B80),  # candidate countdown and its branches
    (0x94C0, 0x94E4),  # fixed-size state copy helper
    (0xB866, 0xB868),  # one instruction that raises the collision/event flag
)


def _prepare_cpu_trace_api(core, window):
    """Bind only the numeric, bounded CPU write-trace exports."""
    names = (
        "quackshot_research_cpu_trace_reset",
        "quackshot_research_cpu_trace_set_frame",
        "quackshot_research_cpu_trace_stop",
        "quackshot_research_cpu_trace_count",
        "quackshot_research_cpu_trace_overflow",
        "quackshot_research_cpu_trace_read",
        "quackshot_research_cpu_pc_trace_count",
        "quackshot_research_cpu_pc_trace_overflow",
        "quackshot_research_cpu_pc_trace_read",
    )
    try:
        api = {name: getattr(core, name) for name in names}
    except AttributeError as error:
        raise RuntimeError("CPU write trace requested but core lacks Stage219 trace exports") from error
    api["quackshot_research_cpu_trace_reset"].argtypes = [C.c_uint32, C.c_uint32]
    api["quackshot_research_cpu_trace_reset"].restype = C.c_int
    api["quackshot_research_cpu_trace_set_frame"].argtypes = [C.c_uint32]
    api["quackshot_research_cpu_trace_set_frame"].restype = None
    api["quackshot_research_cpu_trace_stop"].argtypes = []
    api["quackshot_research_cpu_trace_stop"].restype = None
    api["quackshot_research_cpu_trace_count"].argtypes = []
    api["quackshot_research_cpu_trace_count"].restype = C.c_uint32
    api["quackshot_research_cpu_trace_overflow"].argtypes = []
    api["quackshot_research_cpu_trace_overflow"].restype = C.c_uint32
    api["quackshot_research_cpu_trace_read"].argtypes = (
        [C.c_uint32] + [C.POINTER(C.c_uint32)] * 5
    )
    api["quackshot_research_cpu_trace_read"].restype = C.c_int
    api["quackshot_research_cpu_pc_trace_count"].argtypes = []
    api["quackshot_research_cpu_pc_trace_count"].restype = C.c_uint32
    api["quackshot_research_cpu_pc_trace_overflow"].argtypes = []
    api["quackshot_research_cpu_pc_trace_overflow"].restype = C.c_uint32
    api["quackshot_research_cpu_pc_trace_read"].argtypes = (
        [C.c_uint32] + [C.POINTER(C.c_uint32)] * 6
    )
    api["quackshot_research_cpu_pc_trace_read"].restype = C.c_int
    if not api["quackshot_research_cpu_trace_reset"](*window):
        raise RuntimeError("CPU trace core rejected the bounded frame window")
    return api


def _collect_cpu_trace(api, window):
    """Read numeric write events only; no opcode, ROM, RAM or pixel bytes."""
    api["quackshot_research_cpu_trace_stop"]()
    count = int(api["quackshot_research_cpu_trace_count"]())
    overflow = int(api["quackshot_research_cpu_trace_overflow"]())
    events = []
    for index in range(count):
        slots = [C.c_uint32() for _ in range(5)]
        ok = api["quackshot_research_cpu_trace_read"](
            index, *(C.byref(slot) for slot in slots)
        )
        if not ok:
            raise RuntimeError("CPU trace event disappeared while reading")
        frame, pc, address, width, value = (int(slot.value) for slot in slots)
        events.append({
            "frame": frame,
            "instruction_pc": pc,
            "cpu_address": address,
            "ram_offset": address & 0xFFFF,
            "width_bytes": width,
            "write_value": value,
        })
    return {
        "schema": "quackshot_private_m68k_cpu_write_trace_v1",
        "status": "COMPLETE" if overflow == 0 else "OVERFLOW",
        "frame_window_inclusive": list(window),
        "watch_ranges": [
            {"start_inclusive": f"0x{start:04X}",
             "end_exclusive": f"0x{end:04X}"}
            for start, end in CPU_TRACE_WATCH_RANGES
        ],
        "event_count": count,
        "overflow_count": overflow,
        "events": events,
        "raw_rom_exported": False,
        "raw_ram_exported": False,
        "instruction_bytes_exported": False,
        "player_identity_verified": False,
        "collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def _collect_cpu_pc_trace(api, window):
    """Read only selected instruction PCs, A0 and CCR; no instruction bytes."""
    api["quackshot_research_cpu_trace_stop"]()
    count = int(api["quackshot_research_cpu_pc_trace_count"]())
    overflow = int(api["quackshot_research_cpu_pc_trace_overflow"]())
    events = []
    for index in range(count):
        slots = [C.c_uint32() for _ in range(6)]
        ok = api["quackshot_research_cpu_pc_trace_read"](
            index, *(C.byref(slot) for slot in slots)
        )
        if not ok:
            raise RuntimeError("CPU PC trace event disappeared while reading")
        frame, pc, a0, ccr, sp, stack_return_pc = (int(slot.value) for slot in slots)
        events.append({
            "frame": frame,
            "instruction_pc": pc,
            "a0_address": a0,
            "ccr_low5": ccr,
            "stack_pointer_address": sp,
            "stack_return_pc": stack_return_pc,
        })
    return {
        "schema": "quackshot_private_m68k_control_flow_trace_v3",
        "status": "COMPLETE" if overflow == 0 else "OVERFLOW",
        "frame_window_inclusive": list(window),
        "pc_ranges": [
            {"start_inclusive": f"0x{start:06X}",
             "end_exclusive": f"0x{end:06X}"}
            for start, end in CPU_PC_TRACE_RANGES
        ],
        "object_pointer_filter": (
            "A0 equals 0xFFA2C0 for player-state ranges; for the single "
            "collision-event writer PC 0x00B866, any A0 value is captured as "
            "numeric context"
        ),
        "collision_event_writer_pc": "0x00B866",
        "stack_return_capture": (
            "At PC 0x00B866 only, SP and the numeric longword at SP are decoded "
            "from word-swapped Work RAM; raw stack bytes are never exported"
        ),
        "event_count": count,
        "overflow_count": overflow,
        "events": events,
        "raw_rom_exported": False,
        "raw_ram_exported": False,
        "instruction_bytes_exported": False,
        "frame_pixels_exported": False,
        "player_identity_verified": False,
        "collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def _prepare_sat_api(core):
    """Bind parsed numeric VDP SAT exports; no raw VRAM interface is used."""
    names = ("quackshot_research_sat_count", "quackshot_research_sat_read")
    try:
        api = {name: getattr(core, name) for name in names}
    except AttributeError as error:
        raise RuntimeError("SAT snapshot requested but core lacks Stage223 numeric SAT exports") from error
    api["quackshot_research_sat_count"].argtypes = []
    api["quackshot_research_sat_count"].restype = C.c_uint32
    api["quackshot_research_sat_read"].argtypes = (
        [C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_int)] +
        [C.POINTER(C.c_uint32)] * 8
    )
    api["quackshot_research_sat_read"].restype = C.c_int
    return api


def _collect_sat_snapshot(api, frame):
    """Return decoded sprite metadata only; never raw SAT/VRAM words."""
    count = int(api["quackshot_research_sat_count"]())
    if not 0 <= count <= MAX_SAT_SPRITES:
        raise RuntimeError("VDP SAT core returned an invalid sprite count")
    sprites = []
    for entry_index in range(count):
        sprite_index = C.c_uint32()
        x_screen = C.c_int()
        y_screen = C.c_int()
        slots = [C.c_uint32() for _ in range(8)]
        ok = api["quackshot_research_sat_read"](
            entry_index, C.byref(sprite_index), C.byref(x_screen),
            C.byref(y_screen), *(C.byref(slot) for slot in slots)
        )
        if not ok:
            raise RuntimeError("VDP SAT entry disappeared while reading")
        width, height, pattern, palette, priority, hflip, vflip, next_link = (
            int(slot.value) for slot in slots
        )
        sprite_index = int(sprite_index.value)
        x_screen, y_screen = int(x_screen.value), int(y_screen.value)
        if (not 0 <= sprite_index < MAX_SAT_SPRITES or
                not -128 <= x_screen <= 383 or
                not -128 <= y_screen <= 383 or
                width not in (8, 16, 24, 32) or
                height not in (8, 16, 24, 32) or
                not 0 <= pattern <= 0x7FF or
                not 0 <= palette <= 3 or
                priority not in (0, 1) or hflip not in (0, 1) or
                vflip not in (0, 1) or not 0 <= next_link < MAX_SAT_SPRITES):
            raise RuntimeError("VDP SAT core returned invalid decoded sprite metadata")
        sprites.append({
            "entry_index": entry_index,
            "sprite_index": sprite_index,
            "x_screen": x_screen,
            "y_screen": y_screen,
            "width": width,
            "height": height,
            "pattern_index": pattern,
            "palette": palette,
            "priority": priority,
            "hflip": hflip,
            "vflip": vflip,
            "next_link": next_link,
        })
    return {"frame": frame, "entry_count": count, "sprites": sprites}


def _ram_byte_swapped(ram, offset):
    """Read one logical 68000 byte from Genesis Plus GX's word-swapped RAM."""
    if offset < 0 or offset >= len(ram):
        raise ValueError("RAM byte offset out of range")
    return ram[offset ^ 1]


def _ram_word_swapped(ram, offset):
    """Read one logical big-endian 68000 word from the core's host buffer."""
    if offset < 0 or offset + 2 > len(ram):
        raise ValueError("RAM word offset out of range")
    return int.from_bytes(ram[offset:offset + 2], "little")


def _ram_long_swapped(ram, offset):
    """Read one logical 68000 longword while preserving its word order."""
    return (_ram_word_swapped(ram, offset) << 16) | _ram_word_swapped(ram, offset + 2)


def _collect_object_record_snapshot(ram, frame):
    """Export selected decoded fields from one candidate object, never raw RAM."""
    if len(ram) < 0x10000:
        raise RuntimeError("System RAM buffer is too small for the candidate object record")
    base = 0xA2C0
    state_word = _ram_word_swapped(ram, base + 0x02)
    world_x_raw = _ram_long_swapped(ram, base + 0x10)
    world_y_raw = _ram_long_swapped(ram, base + 0x14)
    animation_pointer = _ram_long_swapped(ram, base + 0x20)
    global_gate = _ram_byte_swapped(ram, 0xEE03)
    return {
        "frame": frame,
        "object_cpu_address": "0xFFA2C0",
        "object_ram_offset": "0xA2C0",
        "state_word": f"0x{state_word:04X}",
        "state_code": state_word & 0x7FFF,
        "new_state_flag": (state_word >> 15) & 1,
        "animation_sequence": _ram_word_swapped(ram, base + 0x04),
        "animation_frame": _ram_word_swapped(ram, base + 0x06),
        "animation_tick": _ram_byte_swapped(ram, base + 0x08),
        "animation_duration": _ram_byte_swapped(ram, base + 0x09),
        "object_flags_0d": _ram_byte_swapped(ram, base + 0x0D),
        "object_flags_0e": _ram_byte_swapped(ram, base + 0x0E),
        "object_flags_0f": _ram_byte_swapped(ram, base + 0x0F),
        "world_x_fixed16_16": world_x_raw,
        "world_x": world_x_raw / 65536.0,
        "world_y_fixed16_16": world_y_raw,
        "world_y": world_y_raw / 65536.0,
        "animation_pointer": f"0x{animation_pointer:08X}",
        "timer_26": _ram_word_swapped(ram, base + 0x26),
        "global_gate_ee03": f"0x{global_gate:02X}",
        "global_gate_bit7": (global_gate >> 7) & 1,
        "global_event_ee08": f"0x{_ram_byte_swapped(ram, 0xEE08):02X}",
        "global_event_ee09": f"0x{_ram_byte_swapped(ram, 0xEE09):02X}",
        "global_collision_gate_ee62": f"0x{_ram_byte_swapped(ram, 0xEE62):02X}",
    }


def _signed_byte(value):
    return value - 0x100 if value & 0x80 else value


def _signed_word(value):
    return value - 0x10000 if value & 0x8000 else value


def _collect_collision_profile_snapshot(ram, rom_data, frame):
    """Decode two selected object boxes and their overlap with the game's player rectangle."""
    if len(ram) < 0x10000:
        raise RuntimeError("System RAM buffer is too small for collision-profile snapshot")
    required_rom_size = COLLISION_PROFILE_TABLE_OFFSET + 256 * 4
    if len(rom_data) < required_rom_size:
        raise RuntimeError("Private ROM is too small for the complete collision-profile table")

    player_y_min = _signed_word(_ram_word_swapped(ram, 0x8F54))
    player_y_max = _signed_word(_ram_word_swapped(ram, 0x8F56))
    player_x_min = _signed_word(_ram_word_swapped(ram, 0x8F58))
    player_x_max = _signed_word(_ram_word_swapped(ram, 0x8F5A))
    player_rect = {
        "x_min": player_x_min,
        "x_max": player_x_max,
        "y_min": player_y_min,
        "y_max": player_y_max,
    }

    objects = []
    for base in (0x9000, 0x9040):
        object_type = _ram_word_swapped(ram, base)
        substate = _ram_word_swapped(ram, base + 0x02)
        x_raw = _ram_long_swapped(ram, base + 0x10)
        y_raw = _ram_long_swapped(ram, base + 0x14)
        x_integer = _signed_word(_ram_word_swapped(ram, base + 0x10))
        y_integer = _signed_word(_ram_word_swapped(ram, base + 0x14))
        profile_index = _ram_byte_swapped(ram, base + 0x24)
        profile_offset = COLLISION_PROFILE_TABLE_OFFSET + profile_index * 4
        profile = [_signed_byte(value) for value in rom_data[profile_offset:profile_offset + 4]]
        if len(profile) != 4:
            raise RuntimeError("Collision profile table entry is incomplete")
        y_min_offset, y_max_offset, x_min_offset, x_max_offset = profile
        bounds = {
            "x_min": x_integer + x_min_offset,
            "x_max": x_integer + x_max_offset,
            "y_min": y_integer + y_min_offset,
            "y_max": y_integer + y_max_offset,
        }
        ix_min = max(player_x_min, bounds["x_min"])
        ix_max = min(player_x_max, bounds["x_max"])
        iy_min = max(player_y_min, bounds["y_min"])
        iy_max = min(player_y_max, bounds["y_max"])
        overlap = ix_min <= ix_max and iy_min <= iy_max
        objects.append({
            "cpu_address": f"0xFF{base:04X}",
            "ram_offset": f"0x{base:04X}",
            "object_type": object_type,
            "substate": substate,
            "object_flags_0c": _ram_byte_swapped(ram, base + 0x0C),
            "object_flags_0d": _ram_byte_swapped(ram, base + 0x0D),
            "object_flag_15": _ram_byte_swapped(ram, base + 0x15),
            "world_x_fixed16_16": x_raw,
            "world_x": x_raw / 65536.0,
            "world_x_integer_used_by_collision": x_integer,
            "world_y_fixed16_16": y_raw,
            "world_y": y_raw / 65536.0,
            "world_y_integer_used_by_collision": y_integer,
            "animation_pointer": f"0x{_ram_long_swapped(ram, base + 0x20):08X}",
            "collision_profile_index": profile_index,
            "collision_profile_table_offset": f"0x{profile_offset:06X}",
            "collision_profile_offsets_ymin_ymax_xmin_xmax": profile,
            "collision_bounds": bounds,
            "overlaps_player_collision_rectangle": overlap,
            "intersection_with_player": ({
                "x_min": ix_min, "x_max": ix_max,
                "y_min": iy_min, "y_max": iy_max,
            } if overlap else None),
        })

    return {
        "schema": "quackshot_private_collision_profile_numeric_v1",
        "frame": frame,
        "player_collision_rectangle": player_rect,
        "objects": objects,
        "global_event_ee08": f"0x{_ram_byte_swapped(ram, 0xEE08):02X}",
        "global_event_ee09": f"0x{_ram_byte_swapped(ram, 0xEE09):02X}",
        "global_collision_gate_ee62": f"0x{_ram_byte_swapped(ram, 0xEE62):02X}",
        "raw_rom_exported": False,
        "raw_ram_exported": False,
        "collision_profile_bytes_exported": False,
        "instruction_bytes_exported": False,
        "frame_pixels_exported": False,
        "audio_exported": False,
        "collision_geometry_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def run_core(core, metadata, rom_data, rom_hash, frames, schedule, private_rom_path=None, video_stride=60, checkpoint_sink=None, ram_window=None, video_roi=None, video_roi_frames=(), video_roi_vdp_hook=None, video_roi_components=False, video_digest_frames=(), cpu_write_trace_window=None, cpu_control_flow_trace_window=None, sat_snapshot_frames=(), object_record_snapshot_frames=(), collision_profile_snapshot_frames=()):
    if metadata["need_fullpath"] and private_rom_path is None:
        raise ValueError("Core requires a private local ROM path")
    if not 1 <= video_stride <= 600:
        raise ValueError("video_stride must be 1..600")
    if ram_window is not None:
        if (not isinstance(ram_window, tuple) or len(ram_window) != 2 or
                not all(isinstance(v, int) for v in ram_window) or
                not 0 <= ram_window[0] < ram_window[1] <= frames):
            raise ValueError("ram_window requires (start, end_exclusive) within frame count")
    if video_roi is None:
        if video_roi_frames:
            raise ValueError("video ROI frames require a video_roi")
        roi_frame_set = set()
    else:
        validate_roi(video_roi)
        if (type(video_roi_frames) not in (tuple, list) or
                not 1 <= len(video_roi_frames) <= 24 or
                any(type(f) is not int or not 0 <= f < frames
                    for f in video_roi_frames) or
                len(set(video_roi_frames)) != len(video_roi_frames)):
            raise ValueError("video ROI frames require 1..24 unique valid frame indices")
        roi_frame_set = set(video_roi_frames)
    if video_roi_vdp_hook is not None and (video_roi is None or
            not callable(video_roi_vdp_hook)):
        raise ValueError("video VDP hook requires a callable and selected ROI")
    if type(video_roi_components) is not bool or (
            video_roi_components and video_roi is None):
        raise ValueError("visual components require a selected ROI and boolean flag")
    if video_roi_components and video_roi[2] * video_roi[3] > 65536:
        raise ValueError("visual components require at most 65536 ROI pixels")
    if (type(video_digest_frames) not in (tuple, list) or
            len(video_digest_frames) > 128 or
            any(type(f) is not int or not 0 <= f < frames
                for f in video_digest_frames) or
            len(set(video_digest_frames)) != len(video_digest_frames)):
        raise ValueError("video_digest_frames requires up to 128 unique valid frame indices")
    digest_frame_set = set(video_digest_frames)
    if (type(sat_snapshot_frames) not in (tuple, list) or
            len(sat_snapshot_frames) > MAX_SAT_SNAPSHOT_FRAMES or
            any(type(f) is not int or not 0 <= f < frames
                for f in sat_snapshot_frames) or
            len(set(sat_snapshot_frames)) != len(sat_snapshot_frames)):
        raise ValueError("sat_snapshot_frames requires up to 128 unique valid frame indices")
    sat_frame_set = set(sat_snapshot_frames)
    if (type(object_record_snapshot_frames) not in (tuple, list) or
            len(object_record_snapshot_frames) > MAX_OBJECT_SNAPSHOT_FRAMES or
            any(type(f) is not int or not 0 <= f < frames
                for f in object_record_snapshot_frames) or
            len(set(object_record_snapshot_frames)) != len(object_record_snapshot_frames)):
        raise ValueError("object_record_snapshot_frames requires up to 128 unique valid frame indices")
    object_frame_set = set(object_record_snapshot_frames)
    if (type(collision_profile_snapshot_frames) not in (tuple, list) or
            len(collision_profile_snapshot_frames) > MAX_COLLISION_PROFILE_SNAPSHOT_FRAMES or
            any(type(f) is not int or f < 0 or f >= frames
                for f in collision_profile_snapshot_frames) or
            len(set(collision_profile_snapshot_frames)) != len(collision_profile_snapshot_frames)):
        raise ValueError("collision_profile_snapshot_frames requires up to 24 unique valid frame indices")
    collision_profile_frame_set = set(collision_profile_snapshot_frames)
    trace_windows = []
    for label, candidate in (
        ("cpu_write_trace_window", cpu_write_trace_window),
        ("cpu_control_flow_trace_window", cpu_control_flow_trace_window),
    ):
        if candidate is None:
            continue
        if (type(candidate) not in (tuple, list) or len(candidate) != 2 or
                any(type(f) is not int for f in candidate)):
            raise ValueError(label + " must be a pair of integer frame indices")
        candidate = (candidate[0], candidate[1])
        if (candidate[0] < 0 or candidate[0] > candidate[1] or
                candidate[1] >= frames or
                candidate[1] - candidate[0] + 1 > MAX_CPU_TRACE_WINDOW):
            raise ValueError(label + " must be in range and at most 128 frames")
        trace_windows.append(candidate)
    if len(set(trace_windows)) > 1:
        raise ValueError("CPU write and control-flow trace windows must match")
    trace_window = trace_windows[0] if trace_windows else None
    state = {"frame": 0, "video_frames": 0, "pixel_format": 0,
             "frame_checksums": [], "pressed": set(),
             "inside_retro_run": False, "video_callbacks_current_step": 0,
             "video_callbacks_outside_retro_run": 0,
             "frames_without_video_callback": 0,
             "frames_with_multiple_video_callbacks": 0,
             "input_polls": 0, "input_reads": 0, "input_nonzero_reads": 0,
             "video_roi_samples": [], "video_roi_error": None,
             "video_roi_vdp_samples": [], "video_roi_vdp_error": None,
             "video_roi_components_samples": [], "video_frame_digests": [],
             "video_digest_error": None}
    backing = C.create_string_buffer(rom_data)
    # Fullpath-only cores read the ROM privately from the caller's filesystem.
    # This path is never printed, exported, or included in numerical evidence.
    private_path_bytes = (str(private_rom_path.resolve()).encode("utf-8")
                          if metadata["need_fullpath"] else None)
    game = GameInfo(private_path_bytes, C.cast(backing, C.c_void_p), len(rom_data), None)
    tmp_dir = C.create_string_buffer(b"/tmp")
    keepalive = []

    @EnvCb
    def environment(command, ptr):
        if command == ENV_GET_CAN_DUPE and ptr:
            C.cast(ptr, C.POINTER(C.c_bool))[0] = True
            return True
        if command == ENV_SET_PIXEL_FORMAT and ptr:
            value = C.cast(ptr, C.POINTER(C.c_int))[0]
            if value in (0, 1, 2):
                state["pixel_format"] = int(value)
                return True
        if command in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY) and ptr:
            C.cast(ptr, C.POINTER(C.c_char_p))[0] = C.cast(tmp_dir, C.c_char_p)
            return True
        if command == ENV_GET_LANGUAGE and ptr:
            C.cast(ptr, C.POINTER(C.c_uint))[0] = 0
            return True
        return False

    @VideoCb
    def video(data, width, height, pitch):
        state["video_frames"] += 1
        if state["inside_retro_run"]:
            state["video_callbacks_current_step"] += 1
        else:
            state["video_callbacks_outside_retro_run"] += 1
        # Optional Stage198 numeric screen-position observation. The
        # underlying C framebuffer bytes stay transient inside this callback.
        # Never export them or confuse blue-pixel shape with collision size.
        if video_roi is not None and state["frame"] in roi_frame_set:
            try:
                if not data or not width or not height:
                    raise ValueError("missing ROI framebuffer callback")
                bpp = 4 if state["pixel_format"] == 1 else 2
                count = int(pitch) * int(height)
                if (width > 2048 or height > 2048 or
                        pitch < width * bpp or count > 16 * 1024 * 1024):
                    raise ValueError("unsupported ROI frame geometry")
                metrics = blue_roi_numeric(
                    C.string_at(data, count), int(width), int(height),
                    int(pitch), int(state["pixel_format"]), video_roi)
                state["video_roi_samples"].append({
                    "frame": state["frame"], **metrics
                })
                if video_roi_components:
                    numeric_components = blue_roi_components_numeric(
                        C.string_at(data, count), int(width), int(height),
                        int(pitch), int(state["pixel_format"]), video_roi)
                    state["video_roi_components_samples"].append({
                        "frame": int(state["frame"]), **numeric_components
                    })
                if video_roi_vdp_hook is not None:
                    # Research-only read of *numeric* VDP lines while the
                    # video callback executes. Never export VRAM contents.
                    values = video_roi_vdp_hook(int(state["frame"]))
                    if (type(values) not in (tuple, list) or
                            len(values) != 8 or
                            any(type(v) is not int or not -1024 <= v <= 1023
                                for v in values)):
                        raise ValueError("VDP callback returned unsafe numbers")
                    state["video_roi_vdp_samples"].append({
                        "frame": int(state["frame"]), "plane_scroll": list(values)
                    })
            except (ValueError, OverflowError, RuntimeError, TypeError):
                state["video_roi_error"] = "invalid private visual ROI sample"
        if state["frame"] in digest_frame_set:
            try:
                if not data or not width or not height:
                    raise ValueError("missing digest framebuffer callback")
                bpp = 4 if state["pixel_format"] == 1 else 2
                count = int(pitch) * int(height)
                if (width > 2048 or height > 2048 or pitch < width * bpp or
                        count > 16 * 1024 * 1024):
                    raise ValueError("unsupported digest framebuffer geometry")
                frame_bytes = C.string_at(data, count)
                numeric = frame_digest_numeric(
                    frame_bytes, int(width), int(height), int(pitch),
                    int(state["pixel_format"]))
                state["video_frame_digests"].append({
                    "frame": int(state["frame"]), **numeric
                })
            except (ValueError, OverflowError, RuntimeError, TypeError):
                state["video_digest_error"] = "invalid full-frame digest sample"
        if not data or state["frame"] % video_stride or not width or not height:
            return
        bytes_per_pixel = 4 if state["pixel_format"] == 1 else 2
        count = int(pitch) * int(height)
        if (width > 2048 or height > 2048 or
                pitch < width * bytes_per_pixel or count > 16*1024*1024):
            return
        checksum = zlib.crc32(C.string_at(data, count)) & 0xffffffff
        state["frame_checksums"].append({"frame": state["frame"],
                                         "size": [int(width), int(height)],
                                         "crc32": f"{checksum:08X}"})

    @AudioCb
    def audio(left, right):
        pass

    @AudioBatchCb
    def audio_batch(samples, length):
        return length

    @InputPollCb
    def input_poll():
        state["input_polls"] += 1

    @InputStateCb
    def input_state(port, device, index, button):
        state["input_reads"] += 1
        active = int(port == 0 and device == RETRO_DEVICE_JOYPAD and
                     index == 0 and button in state["pressed"])
        state["input_nonzero_reads"] += active
        return active

    keepalive.extend((environment, video, audio, audio_batch, input_poll, input_state))
    core.retro_set_environment(environment)
    core.retro_set_video_refresh(video)
    core.retro_set_audio_sample(audio)
    core.retro_set_audio_sample_batch(audio_batch)
    core.retro_set_input_poll(input_poll)
    core.retro_set_input_state(input_state)
    initialized, loaded = False, False
    cpu_trace_api, cpu_trace_active, cpu_trace_result, cpu_pc_trace_result = None, False, None, None
    sat_api, sat_snapshots = None, []
    object_snapshots = []
    collision_profile_snapshots = []
    ram_change_counts = Counter()
    checkpoints = []
    try:
        core.retro_init()
        initialized = True
        if not core.retro_load_game(C.byref(game)):
            raise RuntimeError("libretro core rejected the private Mega Drive ROM")
        loaded = True
        if trace_window is not None:
            cpu_trace_api = _prepare_cpu_trace_api(core, trace_window)
            cpu_trace_active = True
        if sat_frame_set:
            sat_api = _prepare_sat_api(core)
        core.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD)
        av = AvInfo()
        core.retro_get_system_av_info(C.byref(av))
        ram_len = int(core.retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM))
        if ram_len > 512 * 1024:
            raise RuntimeError("Core reported unexpectedly large system RAM")
        previous_ram = None
        for frame in range(frames):
            if cpu_trace_active:
                cpu_trace_api["quackshot_research_cpu_trace_set_frame"](frame)
            state["frame"] = frame
            active = buttons_at(frame, schedule)
            state["pressed"] = {BUTTONS[name] for name in active}
            state["video_callbacks_current_step"] = 0
            state["inside_retro_run"] = True
            try:
                core.retro_run()
            finally:
                state["inside_retro_run"] = False
            if frame in sat_frame_set:
                sat_snapshots.append(_collect_sat_snapshot(sat_api, frame))
            if state["video_callbacks_current_step"] == 0:
                state["frames_without_video_callback"] += 1
            elif state["video_callbacks_current_step"] > 1:
                state["frames_with_multiple_video_callbacks"] += 1
            checkpoint_frame = (frame % 60 == 0 or
                                (ram_window is not None and
                                 ram_window[0] <= frame < ram_window[1]))
            if checkpoint_frame or frame in object_frame_set or frame in collision_profile_frame_set:
                ptr = core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)
                ram = C.string_at(ptr, ram_len) if ptr and ram_len else b""
                if frame in object_frame_set:
                    object_snapshots.append(_collect_object_record_snapshot(ram, frame))
                if frame in collision_profile_frame_set:
                    collision_profile_snapshots.append(
                        _collect_collision_profile_snapshot(ram, rom_data, frame)
                    )
                if checkpoint_frame:
                    changed = []
                    if previous_ram is not None and len(previous_ram) == len(ram):
                        changed = [i for i, (a, b) in enumerate(zip(previous_ram, ram)) if a != b]
                        ram_change_counts.update(changed)
                    previous_ram = ram
                    if checkpoint_sink is not None:
                        # Only callers trusted with the local private ROM can see
                        # transient RAM bytes. The public JSON never embeds them.
                        checkpoint_sink(frame, ram)
                    checkpoints.append({
                        "frame": frame,
                        "input": active,
                        "ram_changed_address_count": len(changed),
                        # Neither RAM values nor ROM bytes are exported.
                    })
        if cpu_trace_active:
            if cpu_write_trace_window is not None:
                cpu_trace_result = _collect_cpu_trace(cpu_trace_api, trace_window)
            if cpu_control_flow_trace_window is not None:
                cpu_pc_trace_result = _collect_cpu_pc_trace(cpu_trace_api, trace_window)
            cpu_trace_active = False
        if sat_frame_set and sorted(row["frame"] for row in sat_snapshots) != sorted(sat_frame_set):
            raise RuntimeError("Invalid VDP SAT snapshot frame cadence")
        if (object_frame_set and
                sorted(row["frame"] for row in object_snapshots) != sorted(object_frame_set)):
            raise RuntimeError("Invalid object-record snapshot frame cadence")
        if digest_frame_set:
            if (state["video_digest_error"] is not None or
                    sorted(row["frame"] for row in state["video_frame_digests"]) !=
                    sorted(digest_frame_set)):
                raise RuntimeError("Invalid full-frame digest callback cadence or geometry")
        if video_roi is not None:
            if (state["video_roi_error"] is not None or
                    sorted(row["frame"] for row in state["video_roi_samples"]) !=
                    sorted(roi_frame_set) or
                    (video_roi_vdp_hook is not None and
                     sorted(row["frame"] for row in state["video_roi_vdp_samples"]) !=
                     sorted(roi_frame_set)) or
                    (video_roi_components and
                     sorted(row["frame"] for row in state["video_roi_components_samples"]) !=
                     sorted(roi_frame_set))):
                raise RuntimeError("Invalid visual ROI callback cadence or geometry")
        return {
            "schema": "private_genesis_libretro_numeric_probe_v1",
            "evidence_type": "actual_emulator_frames_numeric_metadata_only",
            # Input is set before retro_run(frame); checkpoint_sink reads
            # RAM after that call returns, before next frame's input.
            # This is a harness sampling fact, NOT original 68000
            # CPU instruction order or exact scanout timing.
            "ram_checkpoint_phase": "post_retro_run_before_next_input",
            "video_callback_phase": "during_retro_run",
            "core": metadata,
            "reference_rom_sha256": rom_hash,
            "frames_requested": frames,
            "frames_video_callbacks": state["video_frames"],
            # A correct total alone may hide a zero-callback frame paired
            # with a double-callback frame. Keep only aggregate counts.
            "frames_without_video_callback": state["frames_without_video_callback"],
            "frames_with_multiple_video_callbacks": state["frames_with_multiple_video_callbacks"],
            "video_callbacks_outside_retro_run": state["video_callbacks_outside_retro_run"],
            "video_stride": video_stride,
            "input_polls": state["input_polls"],
            "input_reads": state["input_reads"],
            "input_nonzero_reads": state["input_nonzero_reads"],
            "system_fps_reported": round(float(av.timing.fps), 6),
            "system_ram_size": ram_len,
            "input_ranges": [{"first_frame": a, "end_exclusive": b, "button": n}
                             for a, b, n in schedule],
            "video_checkpoints": state["frame_checksums"],
            "video_digest_frames_requested": sorted(digest_frame_set),
            "video_frame_digests": (state["video_frame_digests"]
                                    if digest_frame_set else None),
            "cpu_write_trace": cpu_trace_result,
            "cpu_control_flow_trace": cpu_pc_trace_result,
            "sat_sprite_trace": ({
                "schema": "quackshot_private_genesis_vdp_sat_numeric_v1",
                "status": "COMPLETE",
                "frame_indices_requested": sorted(sat_frame_set),
                "max_frames": MAX_SAT_SNAPSHOT_FRAMES,
                "max_sprites_per_frame": MAX_SAT_SPRITES,
                "frames": sat_snapshots,
                "raw_vram_exported": False,
                "raw_sat_bytes_exported": False,
                "frame_pixels_exported": False,
                "audio_exported": False,
                "sprite_identity_verified": False,
            } if sat_frame_set else None),
            "object_record_trace": ({
                "schema": "quackshot_private_object_record_numeric_v2",
                "status": "COMPLETE",
                "frame_indices_requested": sorted(object_frame_set),
                "max_frames": MAX_OBJECT_SNAPSHOT_FRAMES,
                "frames": object_snapshots,
                "raw_ram_exported": False,
                "raw_rom_exported": False,
                "instruction_bytes_exported": False,
                "frame_pixels_exported": False,
                "audio_exported": False,
                "player_identity_verified": False,
                "collision_geometry_verified": False,
            } if object_frame_set else None),
            "collision_profile_trace": ({
                "schema": "quackshot_private_collision_profile_trace_v1",
                "status": "COMPLETE",
                "frame_indices_requested": sorted(collision_profile_frame_set),
                "max_frames": MAX_COLLISION_PROFILE_SNAPSHOT_FRAMES,
                "frames": collision_profile_snapshots,
                "raw_rom_exported": False,
                "raw_ram_exported": False,
                "collision_profile_bytes_exported": False,
                "instruction_bytes_exported": False,
                "frame_pixels_exported": False,
                "audio_exported": False,
                "collision_geometry_verified": False,
                "safe_to_modify_switch_physics": False,
            } if collision_profile_frame_set else None),
            "video_roi_blue_numeric": (state["video_roi_samples"]
                                        if video_roi is not None else None),
            "video_roi_vdp_numeric": (state["video_roi_vdp_samples"]
                                       if video_roi_vdp_hook is not None else None),
            "video_roi_components_numeric": (
                state["video_roi_components_samples"]
                if video_roi_components else None),
            "ram_checkpoints": checkpoints,
            "most_frequently_changed_ram_offsets": [
                {"offset": f"0x{i:04X}", "checkpoints_changed": count}
                for i, count in ram_change_counts.most_common(32)
            ],
            "limitations": ("Raw RAM/ROM bytes, frame pixels, audio and private path are never "
                            "exported. Selected numeric object coordinates/animation fields are "
                            "included only when object snapshots are explicitly requested; these "
                            "do not prove collision geometry or actor identity. Changed RAM offsets "
                            "do not identify Donald's position. Unattended emulation may remain in "
                            "menus or title screen."),
        }
    finally:
        if cpu_trace_active and cpu_trace_api is not None:
            cpu_trace_api["quackshot_research_cpu_trace_stop"]()
        if loaded:
            core.retro_unload_game()
        if initialized:
            core.retro_deinit()



def compare_idle_to_input(idle, active):
    """Compare numeric metadata only, not copyrighted framebuffer/RAM bytes."""
    if (idle["reference_rom_sha256"] != active["reference_rom_sha256"] or
            idle["frames_requested"] != active["frames_requested"]):
        raise ValueError("Comparison requires same ROM and same frame count")
    a = {x["frame"]: x["crc32"] for x in idle["video_checkpoints"]}
    b = {x["frame"]: x["crc32"] for x in active["video_checkpoints"]}
    if a.keys() != b.keys():
        raise ValueError("Missing checkpoint in compared runs")
    difference = [frame for frame in sorted(a) if a[frame] != b[frame]]
    return {
        "type": "idle_vs_scripted_inputs",
        "checkpoints_compared": len(a),
        "first_different_video_checkpoint": difference[0] if difference else None,
        "different_video_checkpoint_count": len(difference),
        "core_input_nonzero_reads_idle": idle["input_nonzero_reads"],
        "core_input_nonzero_reads_scripted": active["input_nonzero_reads"],
        "observation_limit": (
            "CRC divergence proves output differs, not that gameplay input or "
            "Donald coordinates changed. Checkpoints occur every 60 frames."),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True, help="Locally compiled libretro .so")
    parser.add_argument("--compare-to-idle", action="store_true",
                        help="Run an independent no-input baseline to test causal output changes")
    parser.add_argument("--core-info", action="store_true",
                        help="Inspect binary metadata without any game ROM")
    parser.add_argument("--rom", type=Path, help="Local original ROM (never uploaded)")
    parser.add_argument("--expected-sha256", help="Pinned source SHA-256")
    parser.add_argument("--frames", type=int, default=1200)
    parser.add_argument("--video-stride", type=int, default=60,
                        help="Save frame checksums every N frames; use 1 for precise input-response timing")
    parser.add_argument("--video-digest-frames", default="",
                        help="Comma-separated frames to SHA-256 hash transiently; pixels are not saved")
    parser.add_argument("--cpu-write-trace-window", default="",
                        help="Optional inclusive FIRST:LAST frame window for numeric 68000 RAM-write PC tracing (max 128 frames)")
    parser.add_argument("--cpu-control-flow-trace-window", default="",
                        help="Optional inclusive FIRST:LAST frame window for selected 68000 instruction PCs, A0 and CCR (max 128 frames)")
    parser.add_argument("--sat-snapshot-frames", default="",
                        help="Comma-separated frames for decoded numeric VDP sprite attributes only (max 128 frames; no raw VRAM)")
    parser.add_argument("--object-record-snapshot-frames", default="",
                        help="Comma-separated frames for decoded numeric fields of candidate object 0xFFA2C0 only (max 128 frames; no raw RAM)")
    parser.add_argument("--collision-profile-snapshot-frames", default="",
                        help="Comma-separated frames for numeric collision boxes of objects 0xFF9000 and 0xFF9040 against the game's player rectangle (max 24 frames; no raw ROM/RAM)")
    parser.add_argument("--input", default="120:132:START,450:462:START,900:960:RIGHT,970:1000:B,1040:1060:A")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if not args.core.is_file():
            raise ValueError("Libretro shared library not found")
        core, metadata = setup_core(args.core)
        if args.core_info:
            result = {"core_info_only": True, "metadata": metadata}
        else:
            if args.rom is None:
                raise ValueError("--rom is required unless --core-info is set")
            if not 1 <= args.frames <= MAX_FRAMES:
                raise ValueError(f"--frames must be 1..{MAX_FRAMES}")
            schedule = parse_script(args.input)
            digest_frames = parse_frame_indices(args.video_digest_frames, args.frames)
            cpu_trace_window = parse_cpu_trace_window(args.cpu_write_trace_window, args.frames)
            cpu_control_flow_trace_window = parse_cpu_trace_window(
                args.cpu_control_flow_trace_window, args.frames)
            sat_frames = parse_frame_indices(args.sat_snapshot_frames, args.frames)
            object_frames = parse_frame_indices(args.object_record_snapshot_frames, args.frames)
            collision_profile_frames = parse_frame_indices(args.collision_profile_snapshot_frames, args.frames)
            if len(collision_profile_frames) > MAX_COLLISION_PROFILE_SNAPSHOT_FRAMES:
                raise ValueError("--collision-profile-snapshot-frames accepts at most 24 frames")
            data, checksum = safe_rom(args.rom, args.expected_sha256)
            result = run_core(core, metadata, data, checksum, args.frames, schedule,
                              private_rom_path=args.rom, video_stride=args.video_stride,
                              video_digest_frames=digest_frames,
                              cpu_write_trace_window=cpu_trace_window,
                              cpu_control_flow_trace_window=cpu_control_flow_trace_window,
                              sat_snapshot_frames=sat_frames,
                              object_record_snapshot_frames=object_frames,
                              collision_profile_snapshot_frames=collision_profile_frames)
            if args.compare_to_idle:
                idle = run_core(core, metadata, data, checksum, args.frames, [],
                                private_rom_path=args.rom, video_stride=args.video_stride)
                result["comparison_to_idle"] = compare_idle_to_input(idle, result)
        output = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Genesis libretro probe: {error}\n")


if __name__ == "__main__":
    main()
