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
            if any(k not in frames for k in range(frame-4, frame+1)):
                raise ValueError(f"{name}: missing original consecutive frame near {frame}")
            camera = _number(frames[frame]["software_camera_RAM_candidate"])
            previous = _number(frames[frame-1]["software_camera_RAM_candidate"])
            rendered = _number(frames[frame]["render_buffer_RAM_0x852c"])
            history = tuple(_number(frames[frame-lag]["software_camera_RAM_candidate"])
                            for lag in LAGS)
            if camera != previous:
                comparisons.append((rendered, history))
        if len(comparisons) < 12:
            raise ValueError(f"{name}: too few camera-moving frames")
        scores = []
        for lag in LAGS:
            errors = [abs(observed - history[lag])
                      for observed, history in comparisons]
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
        minimum = min(score["sum_absolute_error_px"] for score in scores)
        winners = [score["lag_frames"] for score in scores
                   if score["sum_absolute_error_px"] == minimum]
        summary["windows"][name] = {
            "scenario": scenario,
            "frame_range_inclusive": [begin, end],
            "moving_frames_compared": len(comparisons),
            "scores": scores,
            "best_lags_tied_included": winners,
            "unique_best_lag": len(winners) == 1,
            "strict_one_tick_lag_matches_all":
                scores[1]["exact_matches"] == len(comparisons),
        }
    return summary
