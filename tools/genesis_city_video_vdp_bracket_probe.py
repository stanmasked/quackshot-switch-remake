#!/usr/bin/env python3
"""Stage201: bracket HScroll at video callback and after the SAME retro_run.

The research-only callback queries numeric HScroll via the instrumented
Genesis Plus GX hook. Same values do not attest VBlank/scanout timing or
the original character hitbox. No VRAM, pixels or source RAM are written.
"""
import argparse
import json
from pathlib import Path

from genesis_blue_roi_numeric import validate_roi
from genesis_city_obstacle_probe import RUNS, TOTAL
from genesis_city_same_step_triplet_probe import (
    ROM_SHA, INSTRUMENTED_CORE_SHA, MARKERS, SCANLINES,
    DEFAULT_ROI, validate_triplet, validate_instrumented_vdp_archive,
    VDP_ARCHIVE_PATH, private_write,
)
from genesis_libretro_probe import parse_script, run_core, safe_rom, setup_core
from genesis_playable_physics import DEFAULT_ENTRY
from genesis_vdp_scroll_probe import bind_vdp_probe, vdp_number_snapshot
from private_genesis_input_guard import PinnedOriginalInputs
from reference_evidence_integrity import read_inputs, validate

NUMBERS_PER_SAMPLE = 8


def require(ok, why):
    if not ok:
        raise ValueError("Genesis video VDP bracket: " + why)


def phase_bracket(callback_rows, after_rows):
    """Summarize strictly bounded sideband numbers; never return full VDP rows."""
    require(type(callback_rows) is dict and type(after_rows) is dict and
            set(callback_rows) == set(after_rows) == set(MARKERS),
            "video and end-of-step marker sets differ")
    result = []
    for frame in MARKERS:
        first, last = callback_rows[frame], after_rows[frame]
        require(all(type(v) in (list, tuple) and len(v) == NUMBERS_PER_SAMPLE
                    and all(type(n) is int and -1024 <= n <= 1023
                            for n in v) for v in (first,last)),
                "unsafe numeric VDP sideband")
        differences = [last[i] - first[i] for i in range(NUMBERS_PER_SAMPLE)]
        result.append({
            "libretro_frame": frame,
            "all_plane_scroll_values_equal_at_two_observation_points": (
                all(v == 0 for v in differences)),
            "plane_a_deltas_by_scanline": differences[:4],
            "plane_b_deltas_by_scanline": differences[4:],
        })
    return {
        "schema": "quackshot_private_genesis_video_vdp_bracket_v1",
        "status": "SAME_LIBRETRO_STEP_TWO_VDP_OBSERVATIONS_NOT_CPU_SCANOUT_ATTESTATION",
        "vdp_video_callback_phase": "inside_retro_run_video_callback",
        "vdp_ram_checkpoint_phase": "after_retro_run_before_next_input",
        "checks": result,
        "changed_hscroll_at_any_observation_boundary": any(
            not v["all_plane_scroll_values_equal_at_two_observation_points"]
            for v in result),
        "equal_values_prove_same_cpu_vdp_scanout_phase": False,
        "original_collision_or_sprite_identity_verified": False,
        "safe_to_modify_switch_physics": False,
    }


def collect(core, metadata, rompath, rawrom, roi, verify_inputs):
    """Three pinned original-game runs, each bracketed within libretro frame."""
    validate_roi(roi)
    require(callable(verify_inputs), "source-file guard required")
    hook = bind_vdp_probe(core)
    entry = parse_script(DEFAULT_ENTRY)
    traces, reads, digests = {}, {}, {}
    sidebands = {}
    after_values = {}
    for label in ("idle", "right", "right_repeat"):
        steps = {}
        after = {}
        def on_private_ram(frame, ram):
            if frame not in MARKERS:
                return
            require(len(ram) == 65536, "unexpected local Genesis RAM size")
            get = lambda offset: int.from_bytes(ram[offset:offset+2],"little")
            vdp_rows = [vdp_number_snapshot(hook, line) for line in SCANLINES]
            steps[frame] = {
                "player_world_x": get(0xA2D0),
                "player_world_y": get(0xA2D4),
                "camera_ram": get(0x8524),
                "render_buffer_ram": get(0x852C),
                "vdp_scanlines": vdp_rows,
            }
            after[frame] = [
                *(row["plane_A_HScroll_11bit_signed"] for row in vdp_rows),
                *(row["plane_B_HScroll_11bit_signed"] for row in vdp_rows),
            ]
        def on_video_vdp(_frame):
            lines = [vdp_number_snapshot(hook, line) for line in SCANLINES]
            return tuple([
                *(row["plane_A_HScroll_11bit_signed"] for row in lines),
                *(row["plane_B_HScroll_11bit_signed"] for row in lines),
            ])
        verify_inputs()
        try:
            out = run_core(
                core, metadata, rawrom, ROM_SHA, TOTAL, entry + RUNS[label],
                private_rom_path=rompath, video_stride=60,
                ram_window=(MARKERS[0], MARKERS[-1]+1),
                checkpoint_sink=on_private_ram,
                video_roi=roi, video_roi_frames=MARKERS,
                video_roi_vdp_hook=on_video_vdp)
        finally:
            verify_inputs()
        require(out.get("frames_video_callbacks") == TOTAL and
                all(out.get(key) == 0 for key in (
                    "frames_without_video_callback",
                    "frames_with_multiple_video_callbacks",
                    "video_callbacks_outside_retro_run")) and
                out.get("ram_checkpoint_phase") ==
                "post_retro_run_before_next_input" and
                out.get("video_callback_phase") == "during_retro_run",
                "bad phase or libretro callback cadence")
        visual_rows=out.get("video_roi_blue_numeric")
        vdp_rows=out.get("video_roi_vdp_numeric")
        require(type(visual_rows) is list and type(vdp_rows) is list and
                len(visual_rows)==len(vdp_rows)==len(MARKERS),
                "missing video or VDP callback samples")
        callback={}
        for row in vdp_rows:
            require(type(row) is dict and set(row)=={"frame","plane_scroll"} and
                    type(row["frame"]) is int and
                    row["frame"] not in callback,
                    "unexpected or duplicate callback VDP frame")
            callback[row["frame"]]=row["plane_scroll"]
        for row in visual_rows:
            frame=row.get("frame")
            require(type(frame) is int and frame in steps and
                    "blue_visual" not in steps[frame],
                    "duplicate or unpaired video frame")
            steps[frame]["blue_visual"]={
                key:value for key,value in row.items() if key!="frame"}
        require(set(steps)==set(after)==set(callback)==set(MARKERS),
                "missing or extra VDP/visual/RAM frame")
        traces[label]=steps
        reads[label]=out["input_nonzero_reads"]
        digests[label]=out["video_checkpoints"]
        sidebands[label]=callback
        after_values[label]=after
    triplet=validate_triplet(
        traces,reads,digests,roi,rom_sha=ROM_SHA,
        core_sha=INSTRUMENTED_CORE_SHA)
    require(sidebands["right"]==sidebands["right_repeat"] and
            after_values["right"]==after_values["right_repeat"],
            "repeated HScroll phase bracket differs")
    bracket=phase_bracket(sidebands["right"],after_values["right"])
    # No raw scanlines or transient pixmaps/ROM/RAM in this numeric report.
    return {
        "schema":"quackshot_genesis_video_hscroll_same_step_bracket_v1",
        "original_rom_executed_locally":True,
        "reference_rom_sha256":ROM_SHA,
        "instrumented_core_sha256":INSTRUMENTED_CORE_SHA,
        "three_runs_repeated_exact":True,
        "triplet_sample_frames":triplet["frames"],
        "vdp_callback_vs_post_run":bracket,
        "original_collision_plane_verified":False,
        "original_cpu_vdp_scanout_phase_verified":False,
        "original_media_or_raw_memory_exported":False,
        "safe_to_modify_switch_physics":False,
    }


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom",type=Path,required=True)
    parser.add_argument("--core",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--roi",nargs=4,type=int,default=DEFAULT_ROI)
    args=parser.parse_args()
    try:
        root=Path(__file__).resolve().parents[1]
        roi=tuple(args.roi)
        validate_roi(roi)
        prior=validate(*read_inputs(root))
        require(prior["rom_sha256"]==ROM_SHA,"original SHA changed")
        archive=json.loads((root/VDP_ARCHIVE_PATH).read_text(encoding="utf-8"))
        validate_instrumented_vdp_archive(archive)
        guard=PinnedOriginalInputs(args.rom,args.core,ROM_SHA,INSTRUMENTED_CORE_SHA)
        guard()
        core,meta=setup_core(args.core)
        rom_data,sha=safe_rom(args.rom,ROM_SHA)
        require(sha==ROM_SHA,"original ROM changed")
        guard()
        report=collect(core,meta,args.rom,rom_data,roi,guard)
        guard()
        private_write(report,args.output,root)
    except OSError:
        parser.exit(1,"Genesis video VDP bracket: local I/O failed (private paths redacted)\n")
    except (ValueError,RuntimeError,TypeError,KeyError) as exc:
        parser.exit(1,str(exc)+"\n")


if __name__=="__main__":
    main()
