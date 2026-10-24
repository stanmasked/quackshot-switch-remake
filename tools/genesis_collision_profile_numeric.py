"""Stage281: numeric-only, bounded private collision rectangle snapshots.

Original ROM and RAM remain transient local inputs. The returned report
contains only decoded numeric fields; no ROM bytes, raw memory or media.
The selected table location and object slots are research hypotheses.
"""
COLLISION_PROFILE_TABLE_OFFSET = 0x7E500
MAX_COLLISION_PROFILE_SNAPSHOT_FRAMES = 24

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


def _signed_byte(value):
    return value - 0x100 if value & 0x80 else value


def _signed_word(value):
    return value - 0x10000 if value & 0x8000 else value


def _collect_collision_profile_snapshot(ram, rom_data, frame):
    """Decode two fixed-slot boxes, with numeric-only offsets and overlap."""
    if type(frame) is not int or frame < 0:
        raise ValueError("collision profile frame must be a nonnegative integer")
    if type(ram) is not bytes or len(ram) != 0x10000:
        raise RuntimeError("System RAM must be exactly 65536 bytes for collision-profile snapshot")
    required_rom_size = COLLISION_PROFILE_TABLE_OFFSET + 256 * 4
    if type(rom_data) is not bytes or len(rom_data) < required_rom_size:
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

    if player_x_min > player_x_max or player_y_min > player_y_max:
        raise ValueError("Invalid player collision rectangle ordering")
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
        if bounds["x_min"] > bounds["x_max"] or bounds["y_min"] > bounds["y_max"]:
            raise ValueError("Invalid object collision rectangle ordering")
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


