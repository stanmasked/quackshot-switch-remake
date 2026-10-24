#!/usr/bin/env bash
# Drive a genuine NeXium controller from the normal map into ROOFTOP TEST.
# No writes to guest state, no ROM, only sampled guest-produced CSV.
set -euo pipefail
[ "$#" = 3 ] || { echo "Usage: $0 nro output profile" >&2; exit 2; }
nro="$(realpath "$1")"; out="$(realpath -m "$2")"; profile="$3"
case "$profile" in 1|2) ;; *) echo "invalid repeat" >&2; exit 2;; esac
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
# Pinned to GitHub release asset digest verified on 2026-10-24; keep fail-closed.
expected=d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180
{
 echo "profile=$profile"
 echo "source=real_virtual_controller_rooftop_touch_guest"
 echo "nro_sha256=$(sha256sum "$nro" | cut -d ' ' -f1)"
 echo "emulator_sha256_expected=$expected"
} > "$out/metadata.txt"
curl -fL --retry 3 --connect-timeout 20 --max-time 240 \
 https://github.com/nexium-emu/nexium-nightly/releases/download/nightly/nexium-linux-x86_64.tar.gz \
 -o "$tmp/emulator.tgz" > "$out/download.log" 2>&1
printf 'emulator_sha256_observed=%s\n' "$(sha256sum "$tmp/emulator.tgz" | cut -d ' ' -f1)" >> "$out/metadata.txt"
echo "$expected  $tmp/emulator.tgz" | sha256sum -c - > "$out/checksum.txt"
mkdir -p "$tmp/bin"
tar -xzf "$tmp/emulator.tgz" -C "$tmp/bin" --no-same-owner
emu="$(find "$tmp/bin" -type f \( -name nexium-gui -o -name nexium \) -print -quit)"
[ -n "$emu" ] || { echo "BLOCKED: missing executable" > "$out/result.txt"; exit 1; }
chmod +x "$emu"
export HOME="$out/emulator-home" XDG_DATA_HOME="$out/xdg-data"
export XDG_CONFIG_HOME="$out/xdg-config" XDG_CACHE_HOME="$out/xdg-cache"
export LIBGL_ALWAYS_SOFTWARE=1 WGPU_BACKEND=vulkan GDK_BACKEND=x11 NEXIUM_VIDEO_FFMPEG=0
mkdir -p "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
icd="$(find /usr/share/vulkan/icd.d -name '*lvp*json' -print -quit 2>/dev/null || true)"
if [ -n "$icd" ]; then export VK_ICD_FILENAMES="$icd"; else unset VK_ICD_FILENAMES; fi
set +e
xvfb-run -a -s "-screen 0 1280x720x24" bash -c '
 timeout --signal=TERM --kill-after=5s 90s "$1" "$2" > "$3" 2>&1 &
 emu_pid=$!
 (
  # GUI startup is asynchronous; wait for a real visible window and
  # never send input if NeXium exits or fails to expose one.
  window=""
  for startup_poll in $(seq 1 80); do
   window="$(xdotool search --onlyvisible --name NeXium 2>/dev/null | tail -n1 || true)"
   [ -n "$window" ] && break
   kill -0 "$emu_pid" 2>/dev/null || { echo "BLOCKED: emulator exited before visible window"; exit 1; }
   sleep 0.25
  done
  [ -n "$window" ] || { echo "BLOCKED: no emulator window after bounded startup poll"; exit 1; }
  echo "NEXIUM_WINDOW_READY"
  # The window can appear before the NRO has produced frames. Synchronize
  # to the guest-present marker so menu input cannot race application boot.
  game_ready=0
  for boot_poll in $(seq 1 100); do
   bootlog="$(find "$XDG_CONFIG_HOME/NeXium/logs" -maxdepth 1 -type f -name "nexium-*.log" -print -quit 2>/dev/null || true)"
   if [ -n "$bootlog" ] && grep -qF "[guest-present] queued=300" "$bootlog"; then
    game_ready=1; break
   fi
   kill -0 "$emu_pid" 2>/dev/null || { echo "BLOCKED: emulator exited before NRO guest frames"; exit 1; }
   sleep 0.15
  done
  [ "$game_ready" = 1 ] || { echo "BLOCKED: no authenticated NRO guest frames"; exit 1; }
  echo "NEXIUM_GUEST_FRAMES_READY"
  xdotool windowfocus "$window"; sleep 0.7
  # NeXium controller_config.rs: ArrowDown=Nintendo D-pad Down,
  # while S=left-stick Y. The game menu processes D-pad, not stick Y.
  # Use two distinct rising edges, with a neutral release interval.
  for i in 1 2; do
    xdotool keydown --clearmodifiers Down
    sleep 0.20
    xdotool keyup Down
    sleep 0.40
  done
  xdotool key --clearmodifiers --delay 120 c
  echo ROOFTOP_MENU_SENT
  guest=""
  for i in $(seq 1 100); do
   guest="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-guest.csv -print -quit 2>/dev/null || true)"
   [ -n "$guest" ] && [ "$(wc -l < "$guest")" -gt 1 ] && break
   sleep 0.15
  done
  [ -n "$guest" ] && [ "$(wc -l < "$guest")" -gt 1 ] || { echo "BLOCKED: not in rooftop test"; exit 1; }
  # Stage 152: no firing and no guest state writes. Reach the target
  # through 2 controller-driven speeds so the final X stays in AABB range.
  xdotool keydown e
  xdotool keydown d
  reached=0
  for i in $(seq 1 650); do
   sleep 0.06
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 629000 ]; then
    echo "ROOFTOP_APPROACH_GUEST frame=$frame x_milli=$x"; reached=1; break
   fi
  done
  xdotool keyup d
  xdotool keyup e
  [ "$reached" = 1 ] || { echo "BLOCKED: failed contact approach"; exit 1; }
  sleep 0.16
  xdotool keydown d
  touching=0
  for i in $(seq 1 220); do
   sleep 0.025
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 698000 ]; then
    echo "ROOFTOP_TOUCH_GUEST frame=$frame x_milli=$x"; touching=1; break
   fi
  done
  xdotool keyup d
  [ "$touching" = 1 ] || { echo "BLOCKED: did not reach switch body"; exit 1; }
  sleep 0.65
  IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
  if [ "$stage" != 2 ] || ! [[ "$x" =~ ^[0-9]+$ ]] ||
      [ "$x" -lt 693000 ] || [ "$x" -ge 716000 ] ||
      [ "$grounded" != 1 ] || [ "$g0" != 0 ] ||
      [ "$g1" != 0 ] || [ "$tiles" != 0 ] ||
      [ "$sb" != 0 ] || [ "$sa" != 0 ]; then
   echo "BLOCKED: no-fire switch touch not verified by guest"
   exit 1
  fi
  echo "ROOFTOP_NO_BRIDGE_GUEST frame=$frame x_milli=$x"
  # Stage272: seal a COPY of the guest-origin CSV while the no-fire
  # contact is actually verified, before attempting a scene exit.
  # NeXium may continue for hundreds of simulation ticks after the
  # host route ended; a later unrelated death/respawn must not be
  # mistaken for a teleport during this bounded contact experiment.
  # Never fabricate rows, clip the CSV by hand, or alter NRO state.
  snapshot_ok=0
  for attempt in 1 2 3; do
    cp "$guest" "$5" || { echo "BLOCKED: guest snapshot copy failed"; exit 1; }
    if [ "$(wc -l < "$5")" -ge 100 ] &&
       [ "$(tail -c 1 "$5" | wc -l)" = 1 ]; then
      snapshot_ok=1
      break
    fi
    sleep 0.02
  done
  [ "$snapshot_ok" = 1 ] ||
    { echo "BLOCKED: incomplete guest snapshot at contact"; exit 1; }
  echo "ROOFTOP_CONTACT_CSV_SEALED"
  # Leave the guest stage using the ACTUAL controller Start; the
  # already-sealed pre-exit guest CSV is independently authenticated.
  xdotool key --clearmodifiers m
  echo "ROOFTOP_EXIT_SENT=virtual_START"
 ) > "$4" 2>&1 &
 pid=$!
 wait "$emu_pid"; emu_result=$?
 wait "$pid"; input_result=$?
 echo "emulator_exit=$emu_result input_exit=$input_result"
 # A healthy NeXium may ignore timeout SIGTERM and be SIGKILLed
 # after --kill-after=5s. GNU timeout then returns 137, as observed in
 # BOTH true guest traversal archives. This cannot authorize a PASS
 # alone: require finished virtual-controller route here AND validate
 # genuine NRO-produced projectile/bridge CSV separately after return.
 [ "$input_result" = 0 ] &&
   { [ "$emu_result" = 124 ] || [ "$emu_result" = 137 ] || [ "$emu_result" = 0 ]; }
' bash "$emu" "$nro" "$out/emulator-stdout.log" "$out/input-injection.txt" "$out/guest-rooftop.csv" > "$out/runner-status.log" 2>&1
rc=$?
set -e
echo "runner_exit=$rc" >> "$out/metadata.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-rooftop-guest.csv -print > "$out/guest-paths.txt"
[ "$(wc -l < "$out/guest-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous guest file" > "$out/result.txt"; exit 1; }
IFS= read -r guest < "$out/guest-paths.txt"
# Require the snapshot from the authenticated live controller process,
# NOT a late file copied after the emulator's timeout/possible respawn.
[ -s "$out/guest-rooftop.csv" ] ||
  { echo "BLOCKED: no sealed guest contact snapshot" > "$out/result.txt"; exit 1; }
for event in ROOFTOP_MENU_SENT ROOFTOP_APPROACH_GUEST ROOFTOP_NO_BRIDGE_GUEST ROOFTOP_CONTACT_CSV_SEALED ROOFTOP_EXIT_SENT ROOFTOP_TOUCH_GUEST; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
python3 - "$out/guest-rooftop.csv" "$out/rooftop-touch-summary.json" <<'PY'
import json,pathlib,sys
from tools.verify_rooftop_guest_trace import read_trace
from tools.verify_rooftop_touch_trace import verify_touch
r=verify_touch(read_trace(sys.argv[1]))
pathlib.Path(sys.argv[2]).write_text(json.dumps(r,sort_keys=True)+"\n")
print("ROOFTOP_GUEST_NO_FIRE_TOUCH_PASS",json.dumps(r,sort_keys=True))
PY
echo "PASS: real virtual-controller touch without fire keeps bridge closed" > "$out/result.txt"
