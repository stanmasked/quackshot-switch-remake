#!/usr/bin/env bash
# Stage276: real-controller left-facing yellow/red horizontal shot and SDL pixel controls.
# No writes to guest state, no ROM, only sampled guest-produced CSV.
set -euo pipefail
[ "$#" = 4 ] || { echo "Usage: $0 nro output variant profile" >&2; exit 2; }
nro="$(realpath "$1")"; out="$(realpath -m "$2")"; variant="$3"; profile="$4"
case "$variant" in left-yellow|left-red) ;; *) echo "invalid variant" >&2; exit 2;; esac
case "$profile" in 1|2) ;; *) echo "invalid repeat" >&2; exit 2;; esac
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
# Pinned to GitHub release asset digest verified on 2026-10-24; keep fail-closed.
expected=d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180
{
 echo "profile=$profile"
 echo "source=real_virtual_controller_city_horizontal_variant_guest"
 echo "variant=$variant"
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
 timeout --signal=TERM --kill-after=5s 65s "$1" "$2" > "$3" 2>&1 &
 emu_pid=$!
 (
  window=""
  for startup_poll in $(seq 1 80); do
   window="$(xdotool search --onlyvisible --name NeXium 2>/dev/null | tail -n1 || true)"
   [ -n "$window" ] && break
   kill -0 "$emu_pid" 2>/dev/null || { echo "BLOCKED: emulator exited before window"; exit 1; }
   sleep 0.25
  done
  [ -n "$window" ] || { echo "BLOCKED: no emulator window"; exit 1; }
  echo NEXIUM_WINDOW_READY
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
  echo NEXIUM_GUEST_FRAMES_READY
  xdotool windowfocus "$window"; sleep 0.7
  # R + A enters first CITY reference test from the untouched map.
  xdotool keydown --clearmodifiers e
  sleep 0.15
  xdotool key --clearmodifiers --delay 120 c
  sleep 0.12
  xdotool keyup e
  echo CITY_MENU_SENT
  guest=""
  ready=0
  for i in $(seq 1 160); do
   guest="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-city-horizontal-guest.csv -print -quit 2>/dev/null || true)"
   if [ -n "$guest" ] && [ "$(wc -l < "$guest")" -gt 10 ]; then
    IFS=, read -r seq frame attempt mode stage fire up grounded pending active attached red x y vx origin <<< "$(tail -n1 "$guest")"
    if [ "$mode" = 1 ] && [ "$stage" = 0 ] && [ "$grounded" = 1 ] &&
       [ "$active" = 0 ] && [ "$pending" = 0 ] && [ "$red" = 0 ]; then
     echo "CITY_GROUNDED_GUEST frame=$frame attempt=$attempt";ready=1;break
    fi
   fi
   sleep 0.12
  done
  [ "$ready" = 1 ] || { echo "BLOCKED: CITY grounded guest unavailable"; exit 1; }
  variant="$5"
  if [ "$variant" = "left-yellow" ]; then
   # Move right on the real floor to provide enough leftward projectile runway.
   xdotool keydown --clearmodifiers Right
   sleep 1.20
   xdotool keyup Right
   xdotool keydown --clearmodifiers Left
   sleep 0.15
   xdotool keyup Left
   sleep 0.35
   echo CITY_LEFT_FACE_SENT
   xdotool keydown --clearmodifiers z
   sleep 0.24
   xdotool keyup z
   echo CITY_LEFT_YELLOW_FIRE_SENT
  elif [ "$variant" = "left-red" ]; then
   redlog=""
   red_ready=0
   for i in $(seq 1 100); do
    redlog="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-red-acquire.csv -print -quit 2>/dev/null || true)"
    if [ -n "$redlog" ] && [ "$(wc -l < "$redlog")" -gt 10 ]; then
     IFS=, read -r seq frame mode stage x y grounded bits red_before red_after weapon pickup_before pickup_after source <<< "$(tail -n1 "$redlog")"
     if [ "$mode" = 1 ] && [ "$stage" = 0 ] && [ "$grounded" = 1 ] &&
        [ "$red_after" = 0 ] && [ "$weapon" = 0 ] && [ "$pickup_before" = 1 ]; then
      red_ready=1; break
     fi
    fi
    sleep 0.12
   done
   [ "$red_ready" = 1 ] || { echo "BLOCKED: real red pickup witness unavailable"; exit 1; }
   # The pickup is above the floor. Approach on the real pad, then hold
   # physical B+jump while moving right; no position or unlock injection.
   xdotool keydown --clearmodifiers Right
   sleep 0.70
   xdotool keydown --clearmodifiers v
   sleep 0.35
   xdotool keyup v
   sleep 0.05
   xdotool keyup Right
   echo CITY_RED_PICKUP_APPROACH_SENT
   pickup_frame=""
   for i in $(seq 1 100); do
    pickup_frame="$(while IFS=, read -r seq frame mode stage x y grounded bits red_before red_after weapon pickup_before pickup_after source; do
     if [ "$red_before" = "0" ] && [ "$red_after" = "1" ] &&
        [ "$weapon" = "1" ] && [ "$pickup_before" = "1" ] && [ "$pickup_after" = "0" ]; then
      echo "$frame"; break
     fi
    done < "$redlog")"
    [ -n "$pickup_frame" ] && break
    sleep 0.12
   done
   [ -n "$pickup_frame" ] || { echo "BLOCKED: physical jump did not acquire red pickup"; exit 1; }
   echo "CITY_RED_PICKUP_CONFIRMED frame=$pickup_frame"
   # Weapon is set by the pickup itself. Turn left and fire with that live weapon.
   xdotool keydown --clearmodifiers Left
   sleep 0.15
   xdotool keyup Left
   sleep 0.35
   echo CITY_LEFT_FACE_SENT
   xdotool keydown --clearmodifiers z
   sleep 0.24
   xdotool keyup z
   echo CITY_LEFT_RED_FIRE_SENT
  else
   echo "BLOCKED: unknown variant"
   exit 1
  fi
  sleep 1.6
  [ "$(wc -l < "$guest")" -ge 56 ] || { echo "BLOCKED: incomplete guest trace"; exit 1; }
  echo CITY_HORIZONTAL_CAPTURE_COMPLETE
 ) > "$4" 2>&1 &
 input_pid=$!
 wait "$emu_pid"; emu_result=$?
 wait "$input_pid"; input_result=$?
 echo "emulator_exit=$emu_result input_exit=$input_result"
 [ "$input_result" = 0 ] &&
   { [ "$emu_result" = 124 ] || [ "$emu_result" = 137 ] || [ "$emu_result" = 0 ]; }
' bash "$emu" "$nro" "$out/emulator-stdout.log" "$out/input-injection.txt" "$variant" > "$out/runner-status.log" 2>&1
rc=$?
set -e
echo "runner_exit=$rc" >> "$out/metadata.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-city-horizontal-guest.csv -print > "$out/guest-paths.txt"
[ "$(wc -l < "$out/guest-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous guest file" > "$out/result.txt"; exit 1; }
IFS= read -r guest < "$out/guest-paths.txt"
cp "$guest" "$out/guest-city-horizontal.csv"
for event in NEXIUM_WINDOW_READY NEXIUM_GUEST_FRAMES_READY CITY_MENU_SENT CITY_GROUNDED_GUEST CITY_LEFT_FACE_SENT CITY_HORIZONTAL_CAPTURE_COMPLETE; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
if [ "$variant" = "left-yellow" ]; then
 grep -q "^CITY_LEFT_YELLOW_FIRE_SENT" "$out/input-injection.txt" || { echo "BLOCKED: yellow fire event missing" > "$out/result.txt"; exit 1; }
else
 for event in CITY_RED_PICKUP_APPROACH_SENT CITY_RED_PICKUP_CONFIRMED CITY_LEFT_RED_FIRE_SENT; do
  grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: red variant event missing" > "$out/result.txt"; exit 1; }
 done
fi
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-city-horizontal-raster.csv -print > "$out/raster-paths.txt"
[ "$(wc -l < "$out/raster-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous SDL pixel witness" > "$out/result.txt"; exit 1; }
IFS= read -r raster < "$out/raster-paths.txt"
cp "$raster" "$out/guest-city-horizontal-raster.csv"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-red-acquire.csv -print > "$out/red-acquire-paths.txt"
[ "$(wc -l < "$out/red-acquire-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous red-pickup trace" > "$out/result.txt"; exit 1; }
IFS= read -r redlog < "$out/red-acquire-paths.txt"
cp "$redlog" "$out/city-red-acquire.csv"
python3 tools/verify_city_horizontal_variant.py "$out/guest-city-horizontal.csv" "$out/guest-city-horizontal-raster.csv" "$out/city-red-acquire.csv" "$variant" "$out/city-horizontal-variant-summary.json"
echo "PASS: real virtual-controller horizontal variant $variant and SDL counterfactual readback match the NRO guest" > "$out/result.txt"
