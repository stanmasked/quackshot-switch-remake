#!/usr/bin/env bash
# Drive real virtual-controller UP+FIRE in CITY TEST and verify the numeric NRO trace.
# No writes to guest state, no ROM, only sampled guest-produced CSV.
set -euo pipefail
[ "$#" = 3 ] || { echo "Usage: $0 nro output profile" >&2; exit 2; }
nro="$(realpath "$1")"; out="$(realpath -m "$2")"; profile="$3"
case "$profile" in 1|2) ;; *) echo "invalid repeat" >&2; exit 2;; esac
mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
# Pinned to GitHub release asset digest verified on 2026-10-23; keep fail-closed.
expected=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642
{
 echo "profile=$profile"
 echo "source=real_virtual_controller_city_up_guest"
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
   guest="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-city-up-guest.csv -print -quit 2>/dev/null || true)"
   if [ -n "$guest" ] && [ "$(wc -l < "$guest")" -gt 10 ]; then
    IFS=, read -r seq frame attempt mode stage up fire grounded active age visible x y legacy origin <<< "$(tail -n1 "$guest")"
    if [ "$mode" = 1 ] && [ "$stage" = 0 ] && [ "$grounded" = 1 ] &&
       [ "$active" = 0 ] && [ "$legacy" = 0 ]; then
     echo "CITY_GROUNDED_GUEST frame=$frame attempt=$attempt";ready=1;break
    fi
   fi
   sleep 0.12
  done
  [ "$ready" = 1 ] || { echo "BLOCKED: CITY grounded guest unavailable"; exit 1; }
  # NeXium ArrowUp = Nintendo D-pad Up. Z = physical Y / SDL X / Fire.
  # Host sends keys only; it never mutates the game state or guest CSV.
  xdotool keydown --clearmodifiers Up
  sleep 0.15
  xdotool keydown --clearmodifiers z
  sleep 0.24
  xdotool keyup z
  sleep 0.08
  xdotool keyup Up
  echo CITY_UP_FIRE_SENT
  sleep 1.6
  [ "$(wc -l < "$guest")" -ge 56 ] || { echo "BLOCKED: incomplete guest trace"; exit 1; }
  echo CITY_UP_CAPTURE_COMPLETE
 ) > "$4" 2>&1 &
 input_pid=$!
 wait "$emu_pid"; emu_result=$?
 wait "$input_pid"; input_result=$?
 echo "emulator_exit=$emu_result input_exit=$input_result"
 [ "$input_result" = 0 ] &&
   { [ "$emu_result" = 124 ] || [ "$emu_result" = 137 ] || [ "$emu_result" = 0 ]; }
' bash "$emu" "$nro" "$out/emulator-stdout.log" "$out/input-injection.txt" > "$out/runner-status.log" 2>&1
rc=$?
set -e
echo "runner_exit=$rc" >> "$out/metadata.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f \
 -name quackshot-city-up-guest.csv -print > "$out/guest-paths.txt"
[ "$(wc -l < "$out/guest-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous guest file" > "$out/result.txt"; exit 1; }
IFS= read -r guest < "$out/guest-paths.txt"
cp "$guest" "$out/guest-city-up.csv"
for event in CITY_MENU_SENT CITY_GROUNDED_GUEST CITY_UP_FIRE_SENT CITY_UP_CAPTURE_COMPLETE; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
python3 tools/verify_city_up_guest.py "$out/guest-city-up.csv" "$out/city-up-summary.json"
echo "PASS: real virtual-controller UP+FIRE generated the bounded numeric NRO witness" > "$out/result.txt"
