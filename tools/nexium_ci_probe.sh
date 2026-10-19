#!/usr/bin/env bash
# Verify that a Switch homebrew NRO actually reaches FIRST_FRAME in NeXium.
# IMPORTANT: no firmware, keys, commercial ROMs or user data are used.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: bash tools/nexium_ci_probe.sh app.nro evidence-directory" >&2
    exit 2
fi
nro="$(realpath "$1")"
out="$(realpath -m "$2")"
mkdir -p "$out"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
release="https://github.com/nexium-emu/nexium-nightly/releases/download/nightly/nexium-linux-x86_64.tar.gz"
# Published asset digest from the official GitHub Releases API, checked
# on 2026-10-18. This nightly tag is mutable: any future archive change
# MUST fail closed and be manually verified against the new release digest.
expected="0ec2da5d58b6b362a9f0a4c927af0e55d36a0b50d1ab3f035dd075ea12d76761"

{
    echo "emulator=NeXium experimental nightly"
    echo "emulator_url=$release"
    echo "emulator_sha256_expected=$expected"
    echo "nro_sha256=$(sha256sum "$nro" | cut -d ' ' -f 1)"
    echo "host_arch=$(uname -m)"
} > "$out/metadata.txt"

# Verify a pinned release hash before executing a downloaded binary. Fail closed.
if ! curl -fL --retry 3 --connect-timeout 20 --max-time 240 \
    "$release" -o "$scratch/nexium.tar.gz" > "$out/download.log" 2>&1; then
    echo "BLOCKED: NeXium binary download failed" > "$out/result.txt"
    exit 1
fi
if ! echo "$expected  $scratch/nexium.tar.gz" | sha256sum -c - > "$out/checksum.txt" 2>&1; then
    echo "BLOCKED: NeXium nightly hash differs from pinned SHA-256" > "$out/result.txt"
    exit 1
fi

tar -tzf "$scratch/nexium.tar.gz" > "$out/archive-list.txt"
mkdir -p "$scratch/emulator"
tar -xzf "$scratch/nexium.tar.gz" -C "$scratch/emulator" --no-same-owner
emulator="$(find "$scratch/emulator" -type f \( -name 'nexium-gui' -o -name 'nexium' \) -print -quit)"
if [ -z "$emulator" ]; then
    echo "BLOCKED: no NeXium executable in archive" > "$out/result.txt"
    exit 1
fi
chmod +x "$emulator"
echo "emulator_binary=$(basename "$emulator")" >> "$out/metadata.txt"
ldd "$emulator" > "$out/emulator-libraries.txt" 2>&1 || true

# CPU Vulkan rendering using Mesa Lavapipe, no GPU or display hardware.
export LIBGL_ALWAYS_SOFTWARE=1
# Mesa 26.04 uses a different ICD file name than some older runners.
# Discover the Lavapipe ICD rather than assuming its exact filename.
driver="$(find /usr/share/vulkan/icd.d -maxdepth 1 -type f -name '*lvp*json' -print -quit 2>/dev/null || true)"
if [ -n "$driver" ]; then
    export VK_ICD_FILENAMES="$driver"
    echo "vulkan_icd=$driver" >> "$out/metadata.txt"
else
    unset VK_ICD_FILENAMES
    echo "vulkan_icd=auto (Lavapipe ICD not detected)" >> "$out/metadata.txt"
fi
echo "glibc_version=$(getconf GNU_LIBC_VERSION)" >> "$out/metadata.txt"
export WGPU_BACKEND=vulkan
export GDK_BACKEND=x11
export NEXIUM_VIDEO_FFMPEG=0
export XDG_DATA_HOME="$out/xdg-data"
export XDG_CONFIG_HOME="$out/xdg-config"
export XDG_CACHE_HOME="$out/xdg-cache"
export HOME="$out/emulator-home"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$HOME"
vulkaninfo --summary > "$out/vulkan.txt" 2>&1 || true

# No false positive: timeout, an emulator window, or just loading an NRO
# are not proof that SDL rendered the first guest frame.
# Default NeXium keyboard configuration (verified against its public source):
# C -> Switch A, D -> left-stick right, X -> B, Z -> Y, M -> Plus.
# Input injection is best effort and recorded; only guest events prove success.
set +e
xvfb-run -a -s "-screen 0 1280x720x24" \
  bash -c '
    timeout --signal=TERM --kill-after=5s 50s "$1" "$2" > "$3" 2>&1 &
    emu_pid=$!
    (
      sleep 5
      echo "Visible windows:"
      xdotool search --onlyvisible --name "NeXium" || true
      window="$(xdotool search --onlyvisible --name "NeXium" | tail -n1)"
      if [ -z "$window" ]; then
        echo "INPUT_PROBE_UNAVAILABLE: NeXium window not located"
        exit 0
      fi
      echo "Target window: $window"
      xdotool windowfocus "$window" || true
      sleep 1
      # Hold Switch R (NeXium keyboard E) while pressing A (C)
      # to enter the measured city envelope. R alone does not enter a stage.
      xdotool keydown --clearmodifiers e
      sleep 0.3
      xdotool key --clearmodifiers --delay 120 c
      sleep 0.1
      xdotool keyup e
      sleep 1
      # Jump in a clear section before walking beneath test scaffolding.
      # Long-jump trial: >15 emulated frames held to exercise the measured
      # original Genesis variable-height jump full plateau window.
      xdotool keydown --clearmodifiers x
      sleep 0.65
      xdotool keyup x
      sleep 1
      xdotool keydown --clearmodifiers d
      # Reach beyond the measured 141px right-follow boundary so the
      # guest renderer must update BOTH procedural scroll planes.
      sleep 1.65
      xdotool keyup d
      sleep 0.2
      # NeXium default A = left-stick left, per controller_config.rs.
      # Demand falling A/B offsets in the guest-generated trace.
      xdotool keydown --clearmodifiers a
      sleep 1.15
      xdotool keyup a
      sleep 0.3
      xdotool keydown --clearmodifiers z
      sleep 0.25
      xdotool keyup z
      sleep 2
      xdotool key --clearmodifiers --delay 120 m
      echo "INPUT_PROBE_SENT: R+A reference-city, B, stick right then left, Y, Plus"
      # Stage 91: start a second live-controller CITY TEST attempt.
      # The host reads X/grounded from the NRO-written SD-side CSV;
      # no scripted QsInput, state edits, or character teleportation.
      sleep 0.55
      xdotool keydown --clearmodifiers e
      sleep 0.30
      xdotool key --clearmodifiers --delay 120 c
      sleep 0.12
      xdotool keyup e
      sleep 0.30
      xdotool keydown --clearmodifiers d
      obstacle_file=""
      postcheck_file=""
      step=0
      wall_recovery=0
      for attempt in $(seq 1 180); do
        sleep 0.085
        if [ -z "$obstacle_file" ]; then
          obstacle_file="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
            -type f -name quackshot-obstacle.csv -print -quit 2>/dev/null || true)"
        fi
        if [ -z "$obstacle_file" ]; then continue; fi
        line="$(tail -n 1 "$obstacle_file" || true)"
        IFS=, read -r seq frame stage_tick x_milli y_milli grounded mask checkpoint mode source <<< "$line"
        if ! [[ "$x_milli" =~ ^[0-9]+$ && "$grounded" =~ ^[01]$ && \
               "$checkpoint" =~ ^[01]$ && "$mode" =~ ^[01]$ ]]; then
          continue
        fi
        # Guest-side continuation data: read only; the CI host never
        # writes or injects any game-state values.
        if [ -z "$postcheck_file" ]; then
          postcheck_file="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
            -type f -name quackshot-postcheck.csv -print -quit 2>/dev/null || true)"
        fi
        pc_crate=""
        pc_flash=""
        if [ -n "$postcheck_file" ]; then
          pc_line="$(tail -n 1 "$postcheck_file" || true)"
          IFS=, read -r pc_seq pc_frame pc_attempt pc_x pc_y pc_vy pc_ground pc_mask pc_flag pc_crate pc_flash pc_heart pc_ledge pc_clear pc_mode pc_source <<< "$pc_line"
        fi
        if [ "$step" -eq 0 ] && [ "$mode" -eq 1 ] && \
           [ "$x_milli" -ge 276000 ] && [ "$x_milli" -le 305000 ] && \
           [ "$grounded" -eq 1 ]; then
          echo "PIT_LAUNCH_GUEST frame=$frame x_milli=$x_milli"
          xdotool keydown --clearmodifiers x
          sleep 0.36
          xdotool keyup x
          step=1
        elif [ "$step" -eq 1 ] && [ "$mode" -eq 1 ] && \
             [ "$x_milli" -ge 382000 ] && [ "$x_milli" -le 410000 ] && \
             [ "$grounded" -eq 1 ]; then
          echo "ROOF_LAUNCH_GUEST frame=$frame x_milli=$x_milli"
          xdotool keydown --clearmodifiers x
          sleep 0.36
          xdotool keyup x
          step=2
        elif [ "$step" -eq 2 ] && [ "$mode" -eq 1 ] && \
             [ "$wall_recovery" -eq 0 ] && [ "$checkpoint" -eq 0 ] && \
             [ "$x_milli" -ge 547000 ] && [ "$x_milli" -le 551000 ] && \
             [ "$grounded" -eq 1 ]; then
          echo "WALL_RECOVERY_GUEST frame=$frame x_milli=$x_milli"
          wall_recovery=1
          xdotool keydown --clearmodifiers x
          sleep 0.36
          xdotool keyup x
        elif [ "$step" -eq 2 ] && [ "$mode" -eq 1 ] && \
             [ "$checkpoint" -eq 1 ]; then
          echo "CHECKPOINT_REACHED_GUEST frame=$frame x_milli=$x_milli"
          # Keep the right stick held. The next goal is the crate.
          step=3
        elif [ "$step" -eq 3 ] && [ "$mode" -eq 1 ] && \
             [ "$x_milli" -ge 902000 ] && [ "$x_milli" -le 946000 ] && \
             [ "$pc_crate" = "1" ]; then
          echo "CRATE_FIRE_GUEST frame=$frame x_milli=$x_milli"
          xdotool keydown --clearmodifiers z
          sleep 0.18
          xdotool keyup z
          step=4
        elif [ "$step" -eq 4 ] && [ "$pc_crate" = "0" ] && \
             [ "$mode" -eq 1 ]; then
          echo "CRATE_DESTROYED_GUEST frame=$frame x_milli=$x_milli"
          step=5
        elif [ "$step" -eq 5 ] && [[ "$pc_flash" =~ ^[0-9]+$ ]] && \
             [ "$pc_flash" -gt 0 ]; then
          echo "SPRING_REACHED_GUEST frame=$frame x_milli=$x_milli"
          xdotool keyup d
          step=6
          break
        fi
      done
      xdotool keyup d 2>/dev/null || true
      xdotool keyup x 2>/dev/null || true
      echo "OBSTACLE_INPUT_STEP=$step"
    ) > "$4" 2>&1 &
    input_pid=$!
    wait "$emu_pid"
    rc=$?
    wait "$input_pid" 2>/dev/null || true
    exit "$rc"
  ' bash "$emulator" "$nro" "$out/emulator-stdout.txt" "$out/input-injection.txt"
rc=$?
set -e
echo "emulator_exit_code=$rc" >> "$out/metadata.txt"

# Any guest trace should have been created by the NRO through the emulator
# SD filesystem emulation; we do not fabricate it from host events.
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-runtime.csv' -print > "$out/guest-trace-paths.txt"
if [ -s "$out/guest-trace-paths.txt" ]; then
    IFS= read -r first < "$out/guest-trace-paths.txt"
    cp "$first" "$out/guest-runtime.csv"
fi
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-motion.csv' -print > "$out/motion-trace-paths.txt"
if [ -s "$out/motion-trace-paths.txt" ]; then
    IFS= read -r first < "$out/motion-trace-paths.txt"
    cp "$first" "$out/guest-motion.csv"
fi
# A separate CSV is written INSIDE the NRO by a deterministic QsInput
# self-test. The host must not fabricate or prepopulate this evidence.
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-route.csv' -print > "$out/route-trace-paths.txt"
if [ -s "$out/route-trace-paths.txt" ]; then
    IFS= read -r first < "$out/route-trace-paths.txt"
    cp "$first" "$out/guest-route.csv"
fi
python3 tools/verify_guest_route.py "$out/guest-route.csv" "$out/route-summary.txt"
# Independent live SDL controller evidence. Unlike the route self-test,
# these records are only written after actual qs_step(read_input(pad)).
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-live-control.csv' -print > "$out/live-trace-paths.txt"
if [ -s "$out/live-trace-paths.txt" ]; then
    IFS= read -r first < "$out/live-trace-paths.txt"
    cp "$first" "$out/guest-live-control.csv"
fi
python3 tools/verify_live_control.py "$out/guest-live-control.csv" \
    "$out/guest-runtime.csv" "$out/guest-motion.csv" \
    "$out/live-control-summary.txt"
python3 tools/verify_live_timeline.py "$out/guest-motion.csv" \
    "$out/guest-live-control.csv" "$out/guest-runtime.csv" \
    "$out/live-timeline-summary.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-obstacle.csv' -print > "$out/obstacle-trace-paths.txt"
if [ -s "$out/obstacle-trace-paths.txt" ]; then
    IFS= read -r first < "$out/obstacle-trace-paths.txt"
    cp "$first" "$out/guest-obstacle.csv"
fi
python3 tools/verify_obstacle_route.py "$out/guest-obstacle.csv" \
    "$out/obstacle-summary.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" \
    -type f -name 'quackshot-postcheck.csv' -print > "$out/postcheck-trace-paths.txt"
if [ -s "$out/postcheck-trace-paths.txt" ]; then
    IFS= read -r first < "$out/postcheck-trace-paths.txt"
    cp "$first" "$out/guest-postcheck.csv"
fi
python3 tools/verify_postcheck.py "$out/guest-postcheck.csv" \
    "$out/guest-obstacle.csv" "$out/postcheck-summary.txt"

python3 - "$out" <<'PY'
import csv
import pathlib
import sys
from tools.verify_camera_burst import verify_camera_bursts

out = pathlib.Path(sys.argv[1])
result = out / "result.txt"
trace = out / "guest-runtime.csv"
if not trace.is_file():
    result.write_text("INCONCLUSIVE: no guest-created diagnostic CSV found. See emulator log.\n")
    sys.exit(1)
try:
    with trace.open(newline="") as stream:
        records = list(csv.DictReader(stream))
    events = [int(r["event"]) for r in records]
    sequential = [int(r["seq"]) for r in records] == list(range(len(records)))
    if not sequential or events[:4] != [1, 2, 3, 4] or 5 not in events:
        result.write_text(f"INCONCLUSIVE: guest trace lacks verified boot/first frame; events={events}\n")
        sys.exit(1)
    transitions = [r for r in records if int(r["event"]) == 7]
    if (len(transitions) < 2 or
            (int(transitions[0]["mode"]), int(transitions[1]["mode"])) != (1, 0) or
            not all(int(r["controllers"]) == 1 for r in transitions[:2])):
        result.write_text(f"INCONCLUSIVE: controller stage-enter/return not proven; events={events}\n")
        sys.exit(1)
    if int(transitions[0]["detail"]) != 1:
        result.write_text("INCONCLUSIVE: R+A did not activate optional reference-city profile\n")
        sys.exit(1)
    parallax_rows = [r for r in records if int(r["event"]) == 12]
    if len(parallax_rows) < 5 or any(
            int(r["mode"]) != 1 or int(r["stage"]) != 0
            for r in parallax_rows):
        result.write_text("INCONCLUSIVE: guest did not sample the real reference-city parallax renderer\n")
        sys.exit(1)
    front_scroll = [int(r["detail"]) >> 16 for r in parallax_rows]
    distant_scroll = [int(r["detail"]) & 0xffff for r in parallax_rows]
    if not all(front // 2 == distant for front, distant in zip(front_scroll, distant_scroll)):
        result.write_text("INCONCLUSIVE: guest A/B parallax scroll ratio differs from 2:1\n")
        sys.exit(1)
    phase_rows = [r for r in records if int(r["event"]) == 13]
    if len(phase_rows) != len(parallax_rows):
        result.write_text("INCONCLUSIVE: guest camera phase samples missing or incomplete\n")
        sys.exit(1)
    camera_current = [int(r["detail"]) >> 16 for r in phase_rows]
    camera_presented = [int(r["detail"]) & 0xffff for r in phase_rows]
    if any(
        int(c["frames"]) != int(p["frames"]) or
        int(c["seq"]) + 1 != int(p["seq"]) or
        c["mode"] != p["mode"] or c["stage"] != p["stage"] or
        presentation != foreground
        for c, p, presentation, foreground in zip(
            phase_rows, parallax_rows, camera_presented, front_scroll
        )
    ):
        result.write_text("INCONCLUSIVE: camera phase not bound to the rendered parallax samples\n")
        sys.exit(1)
    burst_proof = verify_camera_bursts(records)
    (out / "camera-burst-summary.txt").write_text(
        "CAMERA_BURST_GUEST_PASS: " +
        " ".join(f"{key}={value}" for key, value in sorted(burst_proof.items())) + "\n"
    )
    phase_gaps = [current - presented for current, presented
                  in zip(camera_current, camera_presented)]
    if not any(gap > 0 for gap in phase_gaps) or not any(gap < 0 for gap in phase_gaps):
        result.write_text(f"INCONCLUSIVE: guest camera has no forward/reverse frame-lag proof: {phase_gaps}\n")
        sys.exit(1)
    if max(map(abs, phase_gaps)) > 3:
        result.write_text(f"INCONCLUSIVE: unexpected camera presentation lag: {phase_gaps}\n")
        sys.exit(1)
    if max(front_scroll) < 10 or max(distant_scroll) < 5:
        result.write_text("INCONCLUSIVE: actual camera did not scroll enough for independent two-plane proof\n")
        sys.exit(1)
    rises = sum(b > a for a, b in zip(front_scroll, front_scroll[1:]))
    falls = sum(b < a for a, b in zip(front_scroll, front_scroll[1:]))
    drop = max(front_scroll) - front_scroll[-1]
    if rises < 2 or falls < 2 or drop < 8:
        result.write_text(
            f"INCONCLUSIVE: guest parallax did not reverse convincingly; "
            f"rises={rises} falls={falls} drop={drop} front={front_scroll}\n"
        )
        sys.exit(1)
    (out / "parallax-summary.txt").write_text(
        "PARALLAX_GUEST_PASS: samples={} max_foreground={} max_background={} "
        "ratio=2:1 rises={} falls={} reverse_drop_px={}\n".format(
            len(front_scroll), max(front_scroll), max(distant_scroll),
            rises, falls, drop
        )
    )
    if 8 not in events or max(int(r["frames"]) for r in records) < 600:
        result.write_text(f"INCONCLUSIVE: guest did not complete 600 simulation steps; events={events}\n")
        sys.exit(1)
    motion_path = out / "guest-motion.csv"
    if not motion_path.is_file():
        result.write_text("INCONCLUSIVE: guest motion CSV absent; no motion/weapon proof\n")
        sys.exit(1)
    with motion_path.open(newline="") as stream:
        samples = list(csv.DictReader(stream))
    expected_columns = ["seq", "frame", "ticks_ms", "stage", "input_bits",
                        "x_milli", "y_milli", "vy_milli", "grounded",
                        "active_shots", "attached_shots", "stunned_enemies"]
    if not samples or list(samples[0].keys()) != expected_columns or len(samples) > 256:
        result.write_text("INCONCLUSIVE: guest motion sample shape or bounds invalid\n")
        sys.exit(1)
    if [int(r["seq"]) for r in samples] != list(range(len(samples))):
        result.write_text("INCONCLUSIVE: guest motion sequence not continuous\n")
        sys.exit(1)
    if any(int(r["stage"]) != 0 for r in samples):
        result.write_text("INCONCLUSIVE: input probe unexpectedly changed stage\n")
        sys.exit(1)
    inputs = [int(r["input_bits"]) for r in samples]
    xs = [int(r["x_milli"]) for r in samples]
    ys = [int(r["y_milli"]) for r in samples]
    shots = [int(r["active_shots"]) for r in samples]
    checks = {
        "right_input_seen": any(mask & 2 for mask in inputs),
        "left_input_seen": any(mask & 1 for mask in inputs),
        "reverse_motion_px": (max(xs) - xs[-1]) / 1000.0,
        "jump_input_seen": any(mask & 4 for mask in inputs),
        "fire_input_seen": any(mask & 8 for mask in inputs),
        "horizontal_motion_px": (max(xs) - min(xs)) / 1000.0,
        "jump_rise_px": (ys[0] - min(ys)) / 1000.0,
        "maximum_active_shots": max(shots),
        "sample_count": len(samples),
        "first_motion_frame": int(samples[0]["frame"]),
        "last_motion_frame": int(samples[-1]["frame"]),
        "parallax_samples": len(parallax_rows),
        "camera_phase_samples": len(phase_rows),
        **burst_proof,
        "camera_phase_positive_count": sum(g > 0 for g in phase_gaps),
        "camera_phase_negative_count": sum(g < 0 for g in phase_gaps),
        "camera_phase_max_gap_px": max(map(abs, phase_gaps)),
        "parallax_rising_steps": rises,
        "parallax_falling_steps": falls,
        "parallax_reverse_drop_px": drop,
        "max_foreground_scroll_px": max(front_scroll),
        "max_distant_scroll_px": max(distant_scroll),
    }
    import json
    (out / "motion-summary.txt").write_text(json.dumps(checks, indent=2) + "\n")
    if not (checks["right_input_seen"] and checks["left_input_seen"] and
            checks["reverse_motion_px"] > 8 and
            checks["jump_input_seen"] and
            checks["fire_input_seen"] and checks["horizontal_motion_px"] > 15 and
            55 < checks["jump_rise_px"] < 70 and checks["maximum_active_shots"] >= 1):
        result.write_text("INCONCLUSIVE: a measured control-to-motion/shot effect is absent; "
                          + json.dumps(checks) + "\n")
        sys.exit(1)
    result.write_text(
        f"PASS: guest NRO stage-enter/return, input/motion/jump/fire validated; "
        f"enter_frame={transitions[0]['frames']} return_frame={transitions[1]['frames']}; "
        + json.dumps(checks) + "\n"
    )
except (ValueError, KeyError, OSError) as exc:
    result.write_text(f"INCONCLUSIVE: invalid guest trace: {exc}\n")
    sys.exit(1)
PY
