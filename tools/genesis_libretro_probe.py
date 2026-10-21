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

MAX_ROM_BYTES = 8 * 1024 * 1024
MAX_FRAMES = 10000
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


def run_core(core, metadata, rom_data, rom_hash, frames, schedule, private_rom_path=None, video_stride=60, checkpoint_sink=None, ram_window=None):
    if metadata["need_fullpath"] and private_rom_path is None:
        raise ValueError("Core requires a private local ROM path")
    if not 1 <= video_stride <= 600:
        raise ValueError("video_stride must be 1..600")
    if ram_window is not None:
        if (not isinstance(ram_window, tuple) or len(ram_window) != 2 or
                not all(isinstance(v, int) for v in ram_window) or
                not 0 <= ram_window[0] < ram_window[1] <= frames):
            raise ValueError("ram_window requires (start, end_exclusive) within frame count")
    state = {"frame": 0, "video_frames": 0, "pixel_format": 0,
             "frame_checksums": [], "pressed": set(),
             "inside_retro_run": False, "video_callbacks_current_step": 0,
             "video_callbacks_outside_retro_run": 0,
             "frames_without_video_callback": 0,
             "frames_with_multiple_video_callbacks": 0,
             "input_polls": 0, "input_reads": 0, "input_nonzero_reads": 0}
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
    ram_change_counts = Counter()
    checkpoints = []
    try:
        core.retro_init()
        initialized = True
        if not core.retro_load_game(C.byref(game)):
            raise RuntimeError("libretro core rejected the private Mega Drive ROM")
        loaded = True
        core.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD)
        av = AvInfo()
        core.retro_get_system_av_info(C.byref(av))
        ram_len = int(core.retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM))
        if ram_len > 512 * 1024:
            raise RuntimeError("Core reported unexpectedly large system RAM")
        previous_ram = None
        for frame in range(frames):
            state["frame"] = frame
            active = buttons_at(frame, schedule)
            state["pressed"] = {BUTTONS[name] for name in active}
            state["video_callbacks_current_step"] = 0
            state["inside_retro_run"] = True
            try:
                core.retro_run()
            finally:
                state["inside_retro_run"] = False
            if state["video_callbacks_current_step"] == 0:
                state["frames_without_video_callback"] += 1
            elif state["video_callbacks_current_step"] > 1:
                state["frames_with_multiple_video_callbacks"] += 1
            if frame % 60 == 0 or (ram_window is not None and ram_window[0] <= frame < ram_window[1]):
                ptr = core.retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)
                ram = C.string_at(ptr, ram_len) if ptr and ram_len else b""
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
            "ram_checkpoints": checkpoints,
            "most_frequently_changed_ram_offsets": [
                {"offset": f"0x{i:04X}", "checkpoints_changed": count}
                for i, count in ram_change_counts.most_common(32)
            ],
            "limitations": ("No RAM content, video, audio, gameplay positions or private path "
                            "exported. Changed RAM offsets do not identify Donald's position. "
                            "Unattended emulation may remain in menus or title screen."),
        }
    finally:
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
            data, checksum = safe_rom(args.rom, args.expected_sha256)
            result = run_core(core, metadata, data, checksum, args.frames, schedule,
                              private_rom_path=args.rom, video_stride=args.video_stride)
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
