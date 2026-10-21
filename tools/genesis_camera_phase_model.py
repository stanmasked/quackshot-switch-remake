"""Aggregate lag-hypothesis scores from private, consecutive Genesis VDP observations.

Only aggregate numeric statistics leave the privately examined source ROM.
End-of-frame correlation is NOT proof of original VBlank/DMA/68000 timing.
"""
LAGS = (0, 1, 2, 3, 4)
WINDOWS = (
    ("right_tracking", "right", 2180, 2340),
    ("reverse_transition", "reverse", 2360, 2390),
    ("left_tracking", "reverse", 2380, 2425),
    ("held_jump_right", "jump", 2310, 2340),
)


def _number(v):
    if type(v) is not int or not 0 <= v <= 65535:
        raise ValueError("invalid bounded original numeric RAM value")
    return v


# Only numerical values already published in the original research PR.
# These are fixed independent checkpoints, NOT reconstructed dense frames.
KNOWN_CAMERA_VDP_ANCHORS = {
    "right": {
        2180: (92, 90), 2220: (157, 155),
        2300: (287, 285), 2400: (358, 358),
    },
    "reverse": {
        2360: (358, 358), 2375: (357, 358),
        2380: (349, 351), 2390: (333, 335),
        2430: (273, 273),
    },
}


def verify_known_reference_anchors(scenarios):
    """Fail closed if a re-run disagrees with the published original anchors."""
    count = 0
    for name, checkpoints in KNOWN_CAMERA_VDP_ANCHORS.items():
        if name not in scenarios:
            raise ValueError(f"anchor: missing scenario {name}")
        for frame, expected in checkpoints.items():
            if frame not in scenarios[name]:
                raise ValueError(f"anchor: missing checkpoint {name} {frame}")
            row = scenarios[name][frame]
            observed = (
                _number(row["software_camera_RAM_candidate"]),
                _number(row["render_buffer_RAM_0x852c"]),
            )
            if observed != expected:
                raise ValueError(f"anchor: camera/VDP mismatch in {name} at {frame}")
            count += 1
    return {
        "published_numeric_camera_vdp_anchors_verified": count,
        "checked_scenarios": sorted(KNOWN_CAMERA_VDP_ANCHORS),
        "original_ram_or_rom_exported": False,
    }


def evaluate_camera_phase(scenarios):
    """Compare 0..4-tick candidates on identical *moving* output frames."""
    summary = {
        "schema": "quackshot_genesis_camera_phase_aggregate_v1",
        "candidate_lags_frames": list(LAGS),
        "windows": {},
        "interpretation": "Numerical lag correlation, not recovered CPU/VDP timing",
        "raw_original_samples_exported": False,
    }
    for name, scenario, begin, end in WINDOWS:
        if scenario not in scenarios:
            raise ValueError("missing original scenario " + scenario)
        frames = scenarios[scenario]
        comparisons = []
        for frame in range(begin, end+1):
            if any(k not in frames for k in range(frame-5, frame+1)):
                raise ValueError(f"{name}: missing original consecutive frame near {frame}")
            camera = _number(frames[frame]["software_camera_RAM_candidate"])
            previous = _number(frames[frame-1]["software_camera_RAM_candidate"])
            rendered = _number(frames[frame]["render_buffer_RAM_0x852c"])
            rendered_previous = _number(frames[frame-1]["render_buffer_RAM_0x852c"])
            history = tuple(_number(frames[frame-lag]["software_camera_RAM_candidate"])
                            for lag in LAGS)
            preceding = tuple(_number(frames[frame-lag-1]["software_camera_RAM_candidate"])
                              for lag in LAGS)
            if camera != previous:
                comparisons.append((rendered, rendered_previous, history, preceding))
        if len(comparisons) < 12:
            raise ValueError(f"{name}: too few camera-moving frames")
        scores, movement_scores, constant_bias_scores = [], [], []
        for lag in LAGS:
            errors = [abs(observed - history[lag])
                      for observed, _, history, _ in comparisons]
            total = sum(errors)
            scores.append({
                "lag_frames": lag,
                "samples": len(comparisons),
                "sum_absolute_error_px": total,
                "mean_absolute_error_px": round(total / len(errors), 4),
                "exact_matches": sum(error == 0 for error in errors),
                "within_one_px": sum(error <= 1 for error in errors),
                "maximum_error_px": max(errors),
            })
            # Compare movement signatures. Subtracting previous-frame
            # positions cancels any constant pixel offset (a major
            # alternative explanation for a one-frame-looking lag).
            step_errors = [
                abs((observed - observed_prev) -
                    (history[lag] - before[lag]))
                for observed, observed_prev, history, before in comparisons
            ]
            movement_scores.append({
                "lag_frames": lag,
                "samples": len(step_errors),
                "sum_absolute_step_error_px": sum(step_errors),
                "exact_step_matches": sum(error == 0 for error in step_errors),
                "max_absolute_step_error_px": max(step_errors),
            })
            # Fitting one constant camera-to-display offset is a competing
            # model; a perfect fixed-offset fit cannot establish frame lag.
            signed_errors = sorted(observed - history[lag]
                                   for observed, _, history, _ in comparisons)
            median_offset = signed_errors[len(signed_errors) // 2]
            constant_bias_scores.append({
                "lag_frames": lag,
                "fitted_constant_pixel_offset": median_offset,
                "sum_absolute_error_after_constant_offset_px":
                    sum(abs(value - median_offset) for value in signed_errors),
            })
        minimum = min(score["sum_absolute_error_px"] for score in scores)
        winners = [score["lag_frames"] for score in scores
                   if score["sum_absolute_error_px"] == minimum]
        min_motion = min(s["sum_absolute_step_error_px"]
                         for s in movement_scores)
        motion_winners = [
            row["lag_frames"] for row in movement_scores
            if row["sum_absolute_step_error_px"] == min_motion
        ]
        # Compare the unmodified best timing hypothesis against the
        # fit of NO lag and one constant unknown screen-space offset.
        bias_only_error = constant_bias_scores[0][
            "sum_absolute_error_after_constant_offset_px"
        ]
        identifiable = (len(winners) == 1 and
                        motion_winners == winners and
                        bias_only_error > minimum)
        summary["windows"][name] = {
            "scenario": scenario,
            "frame_range_inclusive": [begin, end],
            "moving_frames_compared": len(comparisons),
            "scores": scores,
            "movement_signature_scores": movement_scores,
            "constant_pixel_offset_controls": constant_bias_scores,
            "motion_signature_best_lags_including_ties": motion_winners,
            "zero_lag_with_fitted_offset_error_px": bias_only_error,
            "identifiable_by_motion_and_offset_controls": identifiable,
            "lag_inference_status": (
                "correlated_candidate_not_CPU_timing_proof" if identifiable
                else "ambiguous_spatial_offset_or_motion_signature"
            ),
            "best_lags_tied_included": winners,
            "unique_best_lag": len(winners) == 1,
            "strict_one_tick_lag_matches_all":
                scores[1]["exact_matches"] == len(comparisons),
        }
    return summary
