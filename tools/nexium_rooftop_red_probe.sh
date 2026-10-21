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
 echo "source=real_virtual_controller_rooftop_red_guest"
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
  # Map starts on CITY TEST. Nintendo A enters the native prototype.
  # The red weapon can only be unlocked here by a genuine jumping pickup.
  xdotool key --clearmodifiers --delay 120 c
  echo CITY_MENU_SENT
  city=""
  for i in $(seq 1 160); do
   city="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-red-acquire.csv -print -quit 2>/dev/null || true)"
   [ -n "$city" ] && [ "$(wc -l < "$city")" -gt 2 ] && break
   sleep 0.10
  done
  [ -n "$city" ] && [ "$(wc -l < "$city")" -gt 2 ] || { echo "BLOCKED: no native CITY guest sample"; exit 1; }
  xdotool keydown d
  approach=0
  for i in $(seq 1 250); do
   sleep 0.025
   IFS=, read -r seq frame mode stage x y grounded bits rb ra weapon pb pa origin <<< "$(tail -n1 "$city")"
   if [ "$stage" = 0 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 101000 ]; then
    echo "CITY_RED_JUMP_APPROACH frame=$frame x_milli=$x"; approach=1; break
   fi
  done
  [ "$approach" = 1 ] || { echo "BLOCKED: CITY did not reach real jump approach"; exit 1; }
  # Nintendo B is jump on Switch SDL gamepad. The REAL pick-up sits
  # at authored x=179,y=140, atop the platform in CITY TEST.
  xdotool keydown x; sleep 0.20; xdotool keyup x
  echo CITY_RED_JUMP_SENT
  acquired=0
  for i in $(seq 1 230); do
   sleep 0.035
   IFS=, read -r seq frame mode stage x y grounded bits rb ra weapon pb pa origin <<< "$(tail -n1 "$city")"
   if [ "$stage" = 0 ] && [ "$ra" = 1 ] && [ "$weapon" = 1 ] && [ "$pa" = 0 ]; then
    echo "CITY_RED_PICKUP_GUEST frame=$frame x_milli=$x y_milli=$y"; acquired=1; break
   fi
  done
  xdotool keyup d
  [ "$acquired" = 1 ] || { echo "BLOCKED: CITY real red pickup not collected"; exit 1; }
  # Wait for a guest-authenticated CITY->MAP transition. Previously
  # we slept a fixed 550ms and sometimes pressed menu controls before
  # NeXium consumed START; a valid red pickup then sat in CITY forever.
  transition="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-transitions.csv -print -quit 2>/dev/null || true)"
  [ -n "$transition" ] || { echo "BLOCKED: no NRO transition witness"; exit 1; }
  exited=0
  for attempt in 1 2 3; do
   xdotool keydown --clearmodifiers m; sleep 0.18; xdotool keyup m
   for poll in $(seq 1 22); do
    sleep 0.07
    IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
    if [ "$seq" = 0 ] && [ "$om" = 1 ] && [ "$os" = 0 ] && [ "$nm" = 0 ] &&
       [ "$red" = 1 ] && [ "$weapon" = 1 ]; then
     echo "CITY_EXIT_GUEST frame=$frame attempt=$attempt"; exited=1; break
    fi
   done
   [ "$exited" = 1 ] && break
  done
  [ "$exited" = 1 ] || { echo "BLOCKED: START did not produce guest CITY-to-MAP edge"; exit 1; }
  echo CITY_EXIT_SENT
  sleep 0.35
  for i in 1 2; do
   xdotool keydown --clearmodifiers Down
   sleep 0.20
   xdotool keyup Down
   sleep 0.42
  done
  xdotool key --clearmodifiers --delay 120 c
  echo ROOFTOP_MENU_SENT
  guest=""
  transition=""
  for i in $(seq 1 160); do
   guest="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-guest.csv -print -quit 2>/dev/null || true)"
   transition="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-transitions.csv -print -quit 2>/dev/null || true)"
   [ -n "$guest" ] && [ -n "$transition" ] && [ "$(wc -l < "$guest")" -gt 2 ] && [ "$(wc -l < "$transition")" -gt 1 ] && break
   sleep 0.10
  done
  [ -n "$guest" ] && [ -n "$transition" ] || { echo "BLOCKED: missing ROOFTOP guest state"; exit 1; }
  IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
  [ "$seq" = 1 ] && [ "$om" = 0 ] && [ "$nm" = 1 ] &&
   [ "$ns" = 2 ] && [ "$red" = 1 ] && [ "$weapon" = 1 ] &&
   [ "$after" = 0 ] && [ "$cells" = 0 ] ||
    { echo "BLOCKED: red unlock not preserved into actual ROOFTOP"; exit 1; }
  echo ROOFTOP_RED_UNLOCKED_GUEST frame=$frame
  xdotool keydown e; xdotool keydown d
  arrived=0
  for i in $(seq 1 650); do
   sleep 0.06
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 619000 ]; then
    echo "ROOFTOP_RED_APPROACH_GUEST frame=$frame x_milli=$x"; arrived=1; break
   fi
  done
  xdotool keyup d; xdotool keyup e
  [ "$arrived" = 1 ] || { echo "BLOCKED: failed red switch approach"; exit 1; }
  sleep 0.12; xdotool keydown z; sleep 0.18; xdotool keyup z
  echo ROOFTOP_RED_FIRE_SENT
  # A real 5.5px/frame shot stays inside the 19px overlap window
  # only ~3 simulation ticks. Sampling only tail -n1 every 50ms can
  # skip the authentic event. Search all untouched guest rows instead.
  intersect=0
  for i in $(seq 1 180); do
   sleep 0.05
   witness="$(python3 tools/rooftop_red_overlap_witness.py "$guest" || true)"
   if [ -n "$witness" ]; then
    read -r frame sx <<< "$witness"
    echo "ROOFTOP_RED_SWITCH_OVERLAP_GUEST frame=$frame x_milli=$sx"
    intersect=1; break
   fi
  done
  [ "$intersect" = 1 ] || { echo "BLOCKED: no real red projectile switch overlap"; exit 1; }
  sleep 0.95
  IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
  [ "$stage" = 2 ] && [ "$g0" = 0 ] && [ "$g1" = 0 ] &&
   [ "$tiles" = 0 ] || { echo "BLOCKED: bridge opened after red impact"; exit 1; }
  echo "ROOFTOP_RED_BRIDGE_CLOSED_GUEST frame=$frame"
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
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-red-acquire.csv -print > "$out/city-paths.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-transitions.csv -print > "$out/transition-paths.txt"
[ "$(wc -l < "$out/city-paths.txt")" = 1 ] && [ "$(wc -l < "$out/transition-paths.txt")" = 1 ] || { echo "BLOCKED: ambiguous CITY/red transitions" > "$out/result.txt"; exit 1; }
IFS= read -r city < "$out/city-paths.txt"
IFS= read -r transition < "$out/transition-paths.txt"
cp "$city" "$out/guest-city-red.csv"
cp "$transition" "$out/guest-transitions.csv"
for event in CITY_MENU_SENT CITY_RED_JUMP_APPROACH CITY_RED_JUMP_SENT CITY_RED_PICKUP_GUEST CITY_EXIT_GUEST CITY_EXIT_SENT ROOFTOP_MENU_SENT ROOFTOP_RED_UNLOCKED_GUEST ROOFTOP_RED_APPROACH_GUEST ROOFTOP_RED_FIRE_SENT ROOFTOP_RED_SWITCH_OVERLAP_GUEST ROOFTOP_RED_BRIDGE_CLOSED_GUEST; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
python3 tools/verify_rooftop_red_trace.py "$out/guest-city-red.csv" "$out/guest-rooftop.csv" "$out/guest-transitions.csv" "$out/rooftop-red-summary.json"
echo "PASS: real CITY pickup and red ROOFTOP guest shot kept bridge closed" > "$out/result.txt"
