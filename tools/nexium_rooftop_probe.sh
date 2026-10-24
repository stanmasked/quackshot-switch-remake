#!/usr/bin/env bash
# Drive a genuine NeXium controller from the normal map into ROOFTOP TEST.
# No writes to guest state, no ROM, only sampled guest-produced CSV.
set -euo pipefail
[ "$#" = 3 ] || { echo "Usage: $0 nro output profile" >&2; exit 2; }
nro="$(realpath "$1")"; out="$(realpath -m "$2")"; profile="$3"
case "$profile" in 1|2) ;; *) echo "invalid repeat" >&2; exit 2;; esac
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
expected=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642
{
 echo "profile=$profile"
 echo "source=real_virtual_controller_rooftop_guest"
 echo "nro_sha256=$(sha256sum "$nro" | cut -d ' ' -f1)"
 echo "emulator_sha256_expected=$expected"
} > "$out/metadata.txt"
curl -fL --retry 3 --connect-timeout 20 --max-time 240 \
 https://github.com/nexium-emu/nexium-nightly/releases/download/nightly/nexium-linux-x86_64.tar.gz \
 -o "$tmp/emulator.tgz" > "$out/download.log" 2>&1
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
  # Wait for a real visible window; a fixed startup sleep can race Vulkan.
  window=""
  for startup_poll in $(seq 1 80); do
   window="$(xdotool search --onlyvisible --name NeXium 2>/dev/null | tail -n1 || true)"
   [ -n "$window" ] && break
   kill -0 "$emu_pid" 2>/dev/null || { echo "BLOCKED: emulator exited before visible window"; exit 1; }
   sleep 0.25
  done
  [ -n "$window" ] || { echo "BLOCKED: no emulator window after bounded startup poll"; exit 1; }
  echo "NEXIUM_WINDOW_READY"
  # A visible window does not prove the NRO has rendered; wait for the
  # authenticated guest-present marker from NeXium before sending input.
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
  xdotool keydown e; xdotool keydown d
  arrived=0
  for i in $(seq 1 650); do
   sleep 0.06
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 619000 ]; then
    echo "ROOFTOP_APPROACH_GUEST frame=$frame x_milli=$x"; arrived=1; break
   fi
  done
  xdotool keyup d; xdotool keyup e
  [ "$arrived" = 1 ] || { echo "BLOCKED: failed approach"; exit 1; }
  sleep 0.12; xdotool keydown z; sleep 0.18; xdotool keyup z
  echo ROOFTOP_FIRE_SENT
  opened=0
  for i in $(seq 1 150); do
   sleep 0.07
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$g1" = 1 ] && [ "$tiles" = 6 ]; then
    echo "ROOFTOP_BRIDGE_OPEN_GUEST frame=$frame"; opened=1; break
   fi
  done
  [ "$opened" = 1 ] || { echo "BLOCKED: no true bridge opening"; exit 1; }
  xdotool keydown d
  crossed=0
  for i in $(seq 1 520); do
   sleep 0.06
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 904000 ] && [ "$grounded" = 1 ]; then
    echo "ROOFTOP_BRIDGE_CROSSED_GUEST frame=$frame"; crossed=1; break
   fi
  done
  xdotool keyup d
  [ "$crossed" = 1 ]
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
' bash "$emu" "$nro" "$out/emulator-stdout.log" "$out/input-injection.txt" > "$out/runner-status.log" 2>&1
rc=$?
set -e
echo "runner_exit=$rc" >> "$out/metadata.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-rooftop-guest.csv -print > "$out/guest-paths.txt"
[ "$(wc -l < "$out/guest-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous guest file" > "$out/result.txt"; exit 1; }
IFS= read -r guest < "$out/guest-paths.txt"
cp "$guest" "$out/guest-rooftop.csv"
for event in ROOFTOP_MENU_SENT ROOFTOP_APPROACH_GUEST ROOFTOP_FIRE_SENT ROOFTOP_BRIDGE_OPEN_GUEST ROOFTOP_BRIDGE_CROSSED_GUEST; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
python3 - "$out/guest-rooftop.csv" "$out/rooftop-summary.json" <<'PY'
import json,pathlib,sys
from tools.verify_rooftop_guest_trace import read_trace,verify
r=verify(read_trace(sys.argv[1]))
pathlib.Path(sys.argv[2]).write_text(json.dumps(r,sort_keys=True)+"\n")
print("ROOFTOP_GUEST_TRACE_PASS",json.dumps(r,sort_keys=True))
PY
echo "PASS: real virtual-controller rooftop bridge crossing" > "$out/result.txt"
