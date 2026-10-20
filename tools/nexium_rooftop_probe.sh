#!/usr/bin/env bash
# Drive a genuine NeXium controller from the normal map into ROOFTOP TEST.
# No writes to guest state, no ROM, only sampled guest-produced CSV.
set -euo pipefail
[ "$#" = 3 ] || { echo "Usage: $0 nro output profile" >&2; exit 2; }
nro="$(realpath "$1")"; out="$(realpath -m "$2")"; profile="$3"
case "$profile" in 1|2) ;; *) echo "invalid repeat" >&2; exit 2;; esac
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
expected=c933fbbcf8d1b011de8d4a79c9d8eb55c0678a305947af9388ddb40ce5960a66
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
  sleep 5
  window="$(xdotool search --onlyvisible --name NeXium | tail -n1)"
  [ -n "$window" ] || { echo "BLOCKED: no emulator window"; exit 1; }
  xdotool windowfocus "$window"; sleep 0.7
  # NeXium default WASD=stick, C=Nintendo A, E=R, Z=Nintendo Y.
  for i in 1 2; do xdotool keydown s; sleep 0.2; xdotool keyup s; sleep 0.4; done
  xdotool key --delay 120 c
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
 [ "$input_result" = 0 ] && { [ "$emu_result" = 124 ] || [ "$emu_result" = 0 ]; }
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
