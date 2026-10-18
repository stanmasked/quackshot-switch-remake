#!/usr/bin/env python3
"""Private original-ROM inspection of actual Genesis VDP HScroll table words.

Uses a strictly ROM-free, pinned Genesis Plus GX build with a small numerical
read-only research export. Never exports VRAM dumps, source ROM, sprites,
audio, frames, savestates, or raw game memory.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path

from genesis_camera_state_probe import SCENARIOS, START, END_EXCLUSIVE
from genesis_camera_phase_model import evaluate_camera_phase, verify_known_reference_anchors
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY

SCANLINES = (0, 64, 128, 192)
KEYFRAMES = (2069, 2123, 2124, 2180, 2203, 2220, 2281, 2300,
             2340, 2350, 2360, 2374, 2375, 2380, 2390, 2393, 2400, 2430, 2450)


def signed_11bit(word):
    value = int(word) & 0x7ff
    return value-0x800 if value & 0x400 else value


def bind_vdp_probe(core):
    hook = getattr(core, "quackshot_research_vdp_hscroll", None)
    if hook is None:
        raise RuntimeError("ROM-free core lacks required numeric VDP inspection hook")
    hook.argtypes = (C.c_uint, C.POINTER(C.c_ushort), C.c_uint)
    hook.restype = C.c_int
    return hook


def vdp_number_snapshot(hook, line):
    values = (C.c_ushort*6)()
    if hook(line, values, 6) != 1:
        raise RuntimeError("VDP hardware scroll snapshot unavailable")
    return {
        "line": line,
        "vdp_reg11": int(values[0]),
        "vdp_reg13": int(values[1]),
        "hscroll_vram_base_address": int(values[2]),
        "hscroll_scanline_mask": int(values[3]),
        "plane_A_HScroll_11bit_signed": signed_11bit(values[4]),
        "plane_B_HScroll_11bit_signed": signed_11bit(values[5]),
        # Distinct high bits are metadata, not exported VRAM data.
        "plane_A_highbits": int(values[4]) >> 11,
        "plane_B_highbits": int(values[5]) >> 11,
    }



def validate_actual_vdp_parallax(scenarios):
    """Check every sampled *actual VRAM word* against RAM's render-buffer field."""
    sampled = 0
    for name, frames in scenarios.items():
        for frame, observation in frames.items():
            scroll = observation["render_buffer_RAM_0x852c"]
            if not 0 <= scroll < 1024:
                raise ValueError(f"Unsupported HScroll range in {name}, frame {frame}")
            lines = observation["vdp_scanlines"]
            if len(lines) != len(SCANLINES):
                raise ValueError("Incomplete VDP scanline set")
            for expected_line, row in zip(SCANLINES, lines):
                if (
                    row["line"] != expected_line or
                    row["vdp_reg11"] != 0 or
                    row["vdp_reg13"] != 52 or
                    row["hscroll_vram_base_address"] != 0xD000 or
                    row["hscroll_scanline_mask"] != 0 or
                    row["plane_A_HScroll_11bit_signed"] != -scroll or
                    row["plane_B_HScroll_11bit_signed"] != -(scroll//2)
                ):
                    raise ValueError(
                        f"VDP hardware HScroll or parallax mismatch in {name} "
                        f"frame {frame}, scanline {expected_line}"
                    )
            sampled += 1
    return {
        "verified_numeric_RAM_render_buffer": "0x852C",
        "hscroll_vram_base": "0xD000",
        "vdp_reg11": 0, "vdp_reg13": 52,
        "hscroll_mode": "full-screen (same HScroll on each of four sampled lines)",
        "plane_A_formula": "-RAM[0x852C] (signed 11-bit)",
        "plane_B_formula": "-floor(RAM[0x852C] / 2) (signed 11-bit)",
        "frames_verified": sampled,
        "scanlines_verified_per_frame": len(SCANLINES),
        "individual_vdp_plane_word_comparisons": sampled * len(SCANLINES) * 2,
        "all_numeric_comparisons_exact": True,
    }


def run_study(core, metadata, rompath, rawrom, sha):
    hook = bind_vdp_probe(core)
    intro = parse_script(DEFAULT_ENTRY)
    results, callback_counts, visual = {}, {}, {}
    for label, extra in SCENARIOS.items():
        numeric = {}
        def sample(frame, private_ram):
            if not START <= frame < END_EXCLUSIVE:
                return
            cam = int.from_bytes(private_ram[0x8524:0x8526], "little")
            world_x = int.from_bytes(private_ram[0xA2D0:0xA2D2], "little")
            numeric[frame] = {
                "world_x": world_x,
                "software_camera_RAM_candidate": cam,
                "render_buffer_RAM_0x852c": int.from_bytes(private_ram[0x852C:0x852E], "little"),
                "vdp_scanlines": [vdp_number_snapshot(hook, line)
                                  for line in SCANLINES],
            }
        report = run_core(core, metadata, rawrom, sha, 2550,
                          intro+extra, private_rom_path=rompath,
                          ram_window=(START, END_EXCLUSIVE),
                          checkpoint_sink=sample, video_stride=60)
        if report["frames_video_callbacks"] != 2550 or len(numeric) != 382:
            raise RuntimeError("Incomplete frame-aligned original VDP observation")
        results[label] = numeric
        callback_counts[label] = report["input_nonzero_reads"] - 71
        visual[label] = report["video_checkpoints"]
    if results["right"] != results["right_repeat"] or visual["right"] != visual["right_repeat"]:
        raise RuntimeError("Repeated rightward hardware VDP observation was not deterministic")

    checks = {
        "idle":0, "right":450, "right_repeat":450, "reverse":360, "jump":480
    }
    if callback_counts != checks:
        raise RuntimeError("Invalid controller injection counts for VDP comparison")
    parallax_proof = validate_actual_vdp_parallax(results)
    # Reject provenance drift before ranking any temporal hypotheses.
    anchor_check = verify_known_reference_anchors(results)
    # Compare private consecutive RAM/VDP camera samples; export aggregates only.
    phase_scores = evaluate_camera_phase(results)
    keyframes = {
        label: [{"frame":f, **states[f]} for f in KEYFRAMES]
        for label,states in results.items() if label != "right_repeat"
    }
    right = results["right"]
    reverse = results["reverse"]
    return {
        "schema":"quackshot_megadrive_actual_vdp_hscroll_v1",
        "evidence_type":"actual_private_original_rom_vdp_table_numeric_samples",
        "source_ROM_SHA256": sha,
        "emulator":metadata["library_name"]+" "+metadata["library_version"],
        "independent_emulations":len(SCENARIOS),
        "emulated_frames_per_run":2550,
        "sampled_frames_per_run":382,
        "vdp_scanline_sample_set":list(SCANLINES),
        "vdp_table_words_are_read_directly_from_emulator_internal_VRAM":True,
        "controller_delta_reads":callback_counts,
        "identical_right_replays":True,
        "verified_hardware_parallax":parallax_proof,
        "camera_vdp_phase_hypothesis_scores":phase_scores,
        "camera_vdp_published_anchor_check":anchor_check,
        "keyframes":keyframes,
        "limitations":[
            "Decoded HScroll registers/VRAM table words, not an original CPU instruction trace of when writes occurred",
            "Four scanlines sampled at end of each frame; intermediate writes are not captured",
            "Real hardware has further horizontal scroll/palette/interlace behavior not captured by this summary",
            "Original ROM, graphics, full VRAM/RAM, audio and savestates remain local, not released"
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--core",type=Path,required=True)
    p.add_argument("--rom",type=Path,required=True)
    p.add_argument("--expected-sha256",required=True)
    p.add_argument("--output",type=Path)
    args=p.parse_args()
    try:
        core, info=setup_core(args.core)
        rom, sha=safe_rom(args.rom,args.expected_sha256)
        value=run_study(core,info,args.rom,rom,sha)
        value["patched_core_sha256"]=hashlib.sha256(args.core.read_bytes()).hexdigest()
        serialized=json.dumps(value,indent=2,ensure_ascii=False)+"\n"
        if args.output: args.output.write_text(serialized,encoding="utf-8")
        else: print(serialized,end="")
    except (OSError,ValueError,RuntimeError) as exc:
        p.exit(1,f"Private VDP HScroll numeric probe: {exc}\n")


if __name__=="__main__":
    main()
