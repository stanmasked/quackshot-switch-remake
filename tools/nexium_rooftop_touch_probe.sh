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
 echo "source=real_virtual_controller_rooftop_touch_guest"
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
  # Leave the guest stage with a genuine held START input. A
  # millisecond-scale xdotool tap can fall BETWEEN 60 Hz guest polls,
  # silently keeping ROOFTOP active until the game later respawns.
  # Verify that native guest CSV production ACTUALLY stops; never
  # bless an unacknowledged host keypress as proof of game exit.
  # BEGIN ROOFTOP START EXIT GATE
  # Confirm the NRO witness is advancing BEFORE attributing its later
  # silence to a delivered START button. Host/CI stalls cannot pass.
  before="$(wc -l < "$guest")"
  live=0
  for tick in $(seq 1 20); do
   sleep 0.10
   now="$(wc -l < "$guest")"
   if [ "$now" -gt "$before" ]; then
    live=1
    echo "ROOFTOP_GUEST_LIVE_BEFORE_EXIT rows=$now"
    break
   fi
  done
  [ "$live" = 1 ] || {
   echo "BLOCKED: no live guest frames before START"; exit 1;
  }
  exited=0
  for attempt in 1 2 3; do
   xdotool windowfocus "$window" || {
    echo "BLOCKED: NeXium window focus lost before START"; exit 1;
   }
   xdotool keydown --clearmodifiers m
   sleep 0.18
   xdotool keyup m
   echo "ROOFTOP_EXIT_ATTEMPT=$attempt"
   prev="$(wc -l < "$guest")"
   stable=0
   for poll in $(seq 1 16); do
    sleep 0.15
    count="$(wc -l < "$guest")"
    if [ "$count" = "$prev" ]; then
     stable=$((stable+1))
    else
     stable=0
    fi
    prev="$count"
    if [ "$stable" -ge 4 ] &&
       [ "$count" -gt 2 ] && [ "$count" -lt 1000 ]; then
     exited=1
     echo "ROOFTOP_EXIT_CONFIRMED rows=$count"
     break
    fi
   done
   [ "$exited" = 1 ] && break
  done
  [ "$exited" = 1 ] || {
   echo "BLOCKED: native ROOFTOP guest did not acknowledge START exit";
   exit 1;
  }
  echo "ROOFTOP_EXIT_SENT=virtual_START"
  # END ROOFTOP START EXIT GATE
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
for event in ROOFTOP_MENU_SENT ROOFTOP_APPROACH_GUEST ROOFTOP_TOUCH_GUEST ROOFTOP_NO_BRIDGE_GUEST ROOFTOP_GUEST_LIVE_BEFORE_EXIT ROOFTOP_EXIT_CONFIRMED ROOFTOP_EXIT_SENT; do
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
