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
 echo "source=real_virtual_controller_rooftop_yellow_rearm_guest"
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
 timeout --signal=TERM --kill-after=5s 145s "$1" "$2" > "$3" 2>&1 &
 emu_pid=$!
 (
  # BEGIN YELLOW RESET WINDOW AND GAME READY GATE
  # NeXium may create its X11 window after the guest is already running.
  # Never fail on one early lookup: poll a bounded number of times and
  # require the pinned emulator's real guest-present frame witness before
  # sending any game/controller input.
  window=""
  for startup_poll in $(seq 1 80); do
   window="$(xdotool search --onlyvisible --name NeXium 2>/dev/null | tail -n1 || true)"
   [ -n "$window" ] && break
   kill -0 "$emu_pid" 2>/dev/null || {
    echo "BLOCKED: emulator exited before visible window"; exit 1;
   }
   sleep 0.25
  done
  [ -n "$window" ] || {
   echo "BLOCKED: no emulator window after bounded startup poll"; exit 1;
  }
  echo NEXIUM_WINDOW_READY
  game_ready=0
  for boot_poll in $(seq 1 100); do
   bootlog="$(find "$XDG_CONFIG_HOME/NeXium/logs" -maxdepth 1 -type f -name "nexium-*.log" -print -quit 2>/dev/null || true)"
   if [ -n "$bootlog" ] && grep -qF "[guest-present] queued=300" "$bootlog"; then
    game_ready=1; break
   fi
   kill -0 "$emu_pid" 2>/dev/null || {
    echo "BLOCKED: emulator exited before NRO frames"; exit 1;
   }
   sleep 0.2
  done
  [ "$game_ready" = 1 ] || {
   echo "BLOCKED: no authenticated NRO guest frames"; exit 1;
  }
  echo NEXIUM_GUEST_FRAMES_READY
  xdotool windowfocus "$window" || {
   echo "BLOCKED: emulator window focus failed"; exit 1;
  }
  sleep 0.7
  # END YELLOW RESET WINDOW AND GAME READY GATE
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
  # Exit the FIRST roof only after a genuine, observed red impact.
  # Track the actual edge; the host never modifies mode or guest state.
  left=0
  for attempt in 1 2 3; do
   xdotool keydown --clearmodifiers m; sleep 0.18; xdotool keyup m
   for poll in $(seq 1 24); do
    sleep 0.07
    IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
    if [ "$seq" = 2 ] && [ "$om" = 1 ] && [ "$os" = 2 ] &&
       [ "$nm" = 0 ] && [ "$red" = 1 ] && [ "$weapon" = 1 ] &&
       [ "$after" = 0 ] && [ "$cells" = 0 ]; then
     echo "ROOFTOP_EXIT_GUEST frame=$frame"; left=1; break
    fi
   done
   [ "$left" = 1 ] && break
  done
  [ "$left" = 1 ] || { echo "BLOCKED: no real ROOFTOP to MAP transition"; exit 1; }
  sleep 0.30
  xdotool key --clearmodifiers --delay 140 c
  echo ROOFTOP_SECOND_ENTRY_SENT
  reentered=0
  for poll in $(seq 1 100); do
   sleep 0.08
   IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
   if [ "$seq" = 3 ] && [ "$om" = 0 ] && [ "$nm" = 1 ] &&
      [ "$ns" = 2 ] && [ "$red" = 1 ] && [ "$weapon" = 1 ] &&
      [ "$before" = 0 ] && [ "$after" = 0 ] && [ "$cells" = 0 ]; then
    reentry_frame="$frame"
    echo "ROOFTOP_REENTRY_RED_GUEST frame=$frame"; reentered=1; break
   fi
  done
  [ "$reentered" = 1 ] || { echo "BLOCKED: no authenticated second ROOFTOP entry"; exit 1; }
  xdotool keydown e; xdotool keydown d
  arrived=0
  for poll in $(seq 1 650); do
   sleep 0.06
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [ "$stage" = 2 ] && [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 619000 ] &&
      [ "$frame" -gt "$reentry_frame" ]; then
    echo "ROOFTOP_SECOND_APPROACH_GUEST frame=$frame x_milli=$x"; arrived=1; break
   fi
  done
  xdotool keyup d; xdotool keyup e
  [ "$arrived" = 1 ] || { echo "BLOCKED: no second ROOFTOP approach"; exit 1; }
  sleep 0.12; xdotool keydown z; sleep 0.18; xdotool keyup z
  echo ROOFTOP_SECOND_RED_FIRE_SENT
  intersect=0
  for poll in $(seq 1 180); do
   sleep 0.05
   witness="$(python3 tools/rooftop_red_overlap_witness.py "$guest" "$reentry_frame" || true)"
   if [ -n "$witness" ]; then
    read -r frame sx <<< "$witness"
    echo "ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST frame=$frame x_milli=$sx"
    intersect=1; break
   fi
  done
  [ "$intersect" = 1 ] || { echo "BLOCKED: no second genuine red switch overlap"; exit 1; }
  sleep 1.20
  IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
  [ "$stage" = 2 ] && [ "$g0" = 0 ] && [ "$g1" = 0 ] &&
   [ "$tiles" = 0 ] && [ "$frame" -gt "$((reentry_frame+65))" ] ||
     { echo "BLOCKED: second red shot opened bridge or insufficient observation"; exit 1; }
  echo "ROOFTOP_SECOND_RED_BRIDGE_CLOSED_GUEST frame=$frame"
  # Nintendo X physically maps to NeXium V; it is QS_CHANGE on Switch.
  # This press must be present in the independently saved NRO weapon log.
  weapon="$(find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-weapon.csv -print -quit 2>/dev/null || true)"
  [ -n "$weapon" ] || { echo "BLOCKED: no NRO weapon witness"; exit 1; }
  xdotool keydown --clearmodifiers v; sleep 0.18; xdotool keyup v
  echo ROOFTOP_NINTENDO_X_SENT
  changed=0
  for poll in $(seq 1 180); do
   sleep 0.05
   witness="$(python3 tools/rooftop_yellow_guest_witness.py "$guest" "$weapon" change "$reentry_frame" || true)"
   if [ -n "$witness" ]; then
    toggle_frame="$witness"
    echo "ROOFTOP_YELLOW_SELECTED_GUEST frame=$toggle_frame"; changed=1; break
   fi
  done
  [ "$changed" = 1 ] || { echo "BLOCKED: Nintendo X did not actually select YELLOW"; exit 1; }
  sleep 0.14
  xdotool keydown z; sleep 0.18; xdotool keyup z
  echo ROOFTOP_YELLOW_FIRE_SENT
  fired=0
  for poll in $(seq 1 180); do
   sleep 0.05
   witness="$(python3 tools/rooftop_yellow_guest_witness.py "$guest" "$weapon" fire "$toggle_frame" || true)"
   if [ -n "$witness" ]; then
    yellow_fire_frame="$witness"
    echo "ROOFTOP_YELLOW_FIRE_GUEST frame=$witness"; fired=1; break
   fi
  done
  [ "$fired" = 1 ] || { echo "BLOCKED: no authentic yellow FIRE edge"; exit 1; }
  opened=0
  for poll in $(seq 1 180); do
   sleep 0.05
   witness="$(python3 tools/rooftop_yellow_guest_witness.py "$guest" "$weapon" bridge "$yellow_fire_frame" || true)"
   if [ -n "$witness" ]; then
    echo "ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST frame=$witness"; opened=1; break
   fi
  done
  [ "$opened" = 1 ] || { echo "BLOCKED: no genuine YELLOW projectile bridge opening"; exit 1; }
  sleep 1.00
  IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
  [ "$stage" = 2 ] && [ "$g0" = 1 ] && [ "$g1" = 1 ] &&
   [ "$tiles" = 6 ] || { echo "BLOCKED: yellow bridge did not remain physically open"; exit 1; }
  echo "ROOFTOP_YELLOW_OPEN_STABLE_GUEST frame=$frame"
  # Once YELLOW opened all six physical bridge cells, exit by actual START.
  # Scene transition NRO CSV must report old bridge OPEN in MAP.
  left_open=0
  for attempt in 1 2 3; do
   xdotool keydown --clearmodifiers m; sleep 0.18; xdotool keyup m
   for poll in $(seq 1 25); do
    sleep 0.07
    IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
    if [ "$seq" = 4 ] && [ "$om" = 1 ] && [ "$os" = 2 ] && [ "$nm" = 0 ] &&
       [ "$before" = 1 ] && [ "$after" = 1 ] && [ "$cells" = 6 ] &&
       [ "$red" = 1 ] && [ "$weapon" = 0 ]; then
     echo "ROOFTOP_YELLOW_OPEN_EXIT_GUEST frame=$frame"; left_open=1; break
    fi
   done
   [ "$left_open" = 1 ] && break
  done
  [ "$left_open" = 1 ] || { echo "BLOCKED: no genuine exit from OPEN yellow bridge"; exit 1; }
  sleep 0.30
  xdotool key --clearmodifiers --delay 140 c
  echo ROOFTOP_YELLOW_RESET_ENTRY_SENT
  reset_entry=0
  for poll in $(seq 1 100); do
   sleep 0.08
   IFS=, read -r seq frame om os nm ns bits before after cells badge red weapon origin <<< "$(tail -n1 "$transition")"
   if [ "$seq" = 5 ] && [ "$om" = 0 ] && [ "$nm" = 1 ] && [ "$ns" = 2 ] &&
      [ "$before" = 1 ] && [ "$after" = 0 ] && [ "$cells" = 0 ] &&
      [ "$red" = 1 ] && [ "$weapon" = 0 ]; then
    yellow_reset_entry_frame="$frame"
    echo "ROOFTOP_YELLOW_RESET_ENTRY_GUEST frame=$frame"; reset_entry=1; break
   fi
  done
  [ "$reset_entry" = 1 ] || { echo "BLOCKED: YELLOW open bridge did not reset on third entry"; exit 1; }
  # No more guest inputs. Observe >=100 contiguous NRO frames with closed bridge.
  closed=0
  for poll in $(seq 1 135); do
   sleep 0.05
   IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
   if [[ "$frame" =~ ^[0-9]+$ ]] && [ "$frame" -ge "$((yellow_reset_entry_frame+130))" ] &&
      [ "$stage" = 2 ] && [ "$mode" = 1 ] &&
      [ "$g0" = 0 ] && [ "$g1" = 0 ] && [ "$tiles" = 0 ] &&
      [ "$sb" = 0 ] && [ "$sa" = 0 ]; then
    echo "ROOFTOP_YELLOW_RESET_CLOSED_GUEST frame=$frame"
    closed=1; break
   fi
  done
  [ "$closed" = 1 ] || { echo "BLOCKED: insufficient actual NRO closed bridge reset frames"; exit 1; }
  # Third visit: only real controller DASH+RIGHT, retained YELLOW and FIRE.
  # Do not inject coordinates, projectile positions or bridge state.
  xdotool keydown e; xdotool keydown d
  arrived=0
  for poll in $(seq 1 650); do
    sleep 0.06
    IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
    if [ "$stage" = 2 ] && [ "$mode" = 1 ] &&
       [[ "$x" =~ ^[0-9]+$ ]] && [ "$x" -ge 619000 ] &&
       [ "$tiles" = 0 ] && [ "$frame" -gt "$yellow_reset_entry_frame" ]; then
      echo "ROOFTOP_YELLOW_REARM_APPROACH_GUEST frame=$frame x_milli=$x"
      arrived=1; break
    fi
  done
  xdotool keyup d; xdotool keyup e
  [ "$arrived" = 1 ] || { echo "BLOCKED: third visit never reached the switch with closed bridge"; exit 1; }
  sleep 0.12; xdotool keydown z; sleep 0.18; xdotool keyup z
  echo ROOFTOP_YELLOW_REARM_FIRE_SENT
  fired=0
  for poll in $(seq 1 160); do
    sleep 0.05
    witness="$(python3 tools/rooftop_yellow_guest_witness.py "$guest" "$weapon" fire "$yellow_reset_entry_frame" || true)"
    if [ -n "$witness" ]; then
      third_yellow_fire_frame="$witness"
      echo "ROOFTOP_YELLOW_REARM_FIRE_LIVE_POLL frame=$witness"
      fired=1; break
    fi
  done
  # Host polling may miss a valid NRO event while ROOF and WEAPON are
  # concurrently flushed. Do not claim a pass or abort solely on this.
  # After the process exits, the complete paired guest CSV is
  # independently checked, and missing host witness frames are derived
  # from it before the full Stage160 verifier gates acceptance.
  if [ "$fired" != 1 ]; then
    echo "ROOFTOP_YELLOW_REARM_FIRE_LIVE_POLL_MISSED"
    third_yellow_fire_frame="$yellow_reset_entry_frame"
  fi
  opened_again=0
  for poll in $(seq 1 160); do
    sleep 0.05
    witness="$(python3 tools/rooftop_yellow_guest_witness.py "$guest" "$weapon" bridge "$third_yellow_fire_frame" || true)"
    if [ -n "$witness" ]; then
      echo "ROOFTOP_YELLOW_REARM_OPEN_LIVE_POLL frame=$witness"
      third_yellow_open_frame="$witness";opened_again=1;break
    fi
  done
  if [ "$opened_again" != 1 ]; then
    echo "ROOFTOP_YELLOW_REARM_OPEN_LIVE_POLL_MISSED"
    third_yellow_open_frame="$yellow_reset_entry_frame"
  fi
  stable=0
  for poll in $(seq 1 135); do
    sleep 0.05
    IFS=, read -r seq frame mode stage x y grounded input sx vx red sb sa g0 g1 tiles origin <<< "$(tail -n1 "$guest")"
    if [[ "$frame" =~ ^[0-9]+$ ]] && [ "$frame" -gt "$((third_yellow_open_frame+35))" ] &&
       [ "$mode" = 1 ] && [ "$stage" = 2 ] &&
       [ "$g0" = 1 ] && [ "$g1" = 1 ] && [ "$tiles" = 6 ]; then
      echo "ROOFTOP_YELLOW_REARM_STABLE_LIVE_POLL frame=$frame"
      stable=1;break
    fi
  done
  [ "$stable" = 1 ] || { echo "BLOCKED: reopened bridge not stable in genuine guest telemetry"; exit 1; }
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
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-weapon.csv -print > "$out/weapon-paths.txt"
[ "$(wc -l < "$out/weapon-paths.txt")" = 1 ] || { echo "BLOCKED: missing/ambiguous real weapon log" > "$out/result.txt"; exit 1; }
IFS= read -r weapon < "$out/weapon-paths.txt"
cp "$weapon" "$out/guest-weapon.csv"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-red-acquire.csv -print > "$out/city-paths.txt"
find "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" -type f -name quackshot-rooftop-transitions.csv -print > "$out/transition-paths.txt"
[ "$(wc -l < "$out/city-paths.txt")" = 1 ] && [ "$(wc -l < "$out/transition-paths.txt")" = 1 ] || { echo "BLOCKED: ambiguous CITY/red transitions" > "$out/result.txt"; exit 1; }
IFS= read -r city < "$out/city-paths.txt"
IFS= read -r transition < "$out/transition-paths.txt"
cp "$city" "$out/guest-city-red.csv"
cp "$transition" "$out/guest-transitions.csv"
# Reconcile every Stage160 event with the *finished*, strictly paired NRO
# files, not a transient unsynchronized host poll. Never assert host PASS
# from a mere controller command: if the true fire/open/terrain evidence
# is absent, this block fails before any event is appended.
reset_frame="$(sed -n 's/^ROOFTOP_YELLOW_RESET_ENTRY_GUEST frame=\([0-9][0-9]*\)$/\1/p' "$out/input-injection.txt")"
[[ "$reset_frame" =~ ^[0-9]+$ ]] || { echo "BLOCKED: reset frame missing" > "$out/result.txt"; exit 1; }
fire_frame="$(python3 tools/rooftop_yellow_guest_witness.py "$out/guest-rooftop.csv" "$out/guest-weapon.csv" fire "$reset_frame")" || { echo "BLOCKED: post-run third YELLOW FIRE not in paired NRO logs" > "$out/result.txt"; exit 1; }
open_frame="$(python3 tools/rooftop_yellow_guest_witness.py "$out/guest-rooftop.csv" "$out/guest-weapon.csv" bridge "$fire_frame")" || { echo "BLOCKED: post-run second bridge activation not in paired NRO logs" > "$out/result.txt"; exit 1; }
IFS=, read -r seq final_frame mode stage x y grounded bits sx vx red sb sa g0 g1 cells origin <<< "$(tail -n1 "$out/guest-rooftop.csv")"
[[ "$final_frame" =~ ^[0-9]+$ ]] &&
  [ "$final_frame" -ge "$((open_frame+36))" ] &&
  [ "$mode" = 1 ] && [ "$stage" = 2 ] &&
  [ "$g0" = 1 ] && [ "$g1" = 1 ] && [ "$cells" = 6 ] ||
    { echo "BLOCKED: no post-run 36-frame stable open bridge" > "$out/result.txt"; exit 1; }
for label in ROOFTOP_YELLOW_REARM_FIRE_GUEST ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST ROOFTOP_YELLOW_REARM_STABLE_GUEST; do
  case "$label" in
    ROOFTOP_YELLOW_REARM_FIRE_GUEST) trusted="$fire_frame";;
    ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST) trusted="$open_frame";;
    ROOFTOP_YELLOW_REARM_STABLE_GUEST) trusted="$final_frame";;
  esac
  # Keep live observations as separate diagnostic markers. The
  # canonical authenticated FIRE -> OPEN -> STABLE events are appended
  # only after all original NRO CSVs have been paired and validated.
  case "$label" in
    ROOFTOP_YELLOW_REARM_FIRE_GUEST) live_label=ROOFTOP_YELLOW_REARM_FIRE_LIVE_POLL;;
    ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST) live_label=ROOFTOP_YELLOW_REARM_OPEN_LIVE_POLL;;
    ROOFTOP_YELLOW_REARM_STABLE_GUEST) live_label=ROOFTOP_YELLOW_REARM_STABLE_LIVE_POLL;;
  esac
  observed="$(sed -n "s/^$live_label frame=\([0-9][0-9]*\)$/\1/p" "$out/input-injection.txt")"
  if [ -n "$observed" ]; then
    [[ "$observed" =~ ^[0-9]+$ ]] || { echo "BLOCKED: invalid live-poll witness" > "$out/result.txt"; exit 1; }
    if [ "$label" != ROOFTOP_YELLOW_REARM_STABLE_GUEST ] && [ "$observed" != "$trusted" ]; then
      echo "BLOCKED: live witness disagrees with final paired NRO state" > "$out/result.txt"; exit 1
    fi
    if [ "$label" = ROOFTOP_YELLOW_REARM_STABLE_GUEST ] &&
       { [ "$observed" -lt "$((open_frame+30))" ] || [ "$observed" -gt "$final_frame" ]; }; then
      echo "BLOCKED: invalid live stable marker" > "$out/result.txt"; exit 1
    fi
  fi
  printf '%s frame=%s\n' "$label" "$trusted" >> "$out/input-injection.txt"
done
printf 'postrun_guest_fire=%s\npostrun_guest_open=%s\npostrun_stable=%s\n' "$fire_frame" "$open_frame" "$final_frame" >> "$out/metadata.txt"
for event in NEXIUM_WINDOW_READY NEXIUM_GUEST_FRAMES_READY CITY_MENU_SENT CITY_RED_JUMP_APPROACH CITY_RED_JUMP_SENT CITY_RED_PICKUP_GUEST CITY_EXIT_GUEST CITY_EXIT_SENT ROOFTOP_MENU_SENT ROOFTOP_RED_UNLOCKED_GUEST ROOFTOP_RED_APPROACH_GUEST ROOFTOP_RED_FIRE_SENT ROOFTOP_RED_SWITCH_OVERLAP_GUEST ROOFTOP_RED_BRIDGE_CLOSED_GUEST ROOFTOP_EXIT_GUEST ROOFTOP_SECOND_ENTRY_SENT ROOFTOP_REENTRY_RED_GUEST ROOFTOP_SECOND_APPROACH_GUEST ROOFTOP_SECOND_RED_FIRE_SENT ROOFTOP_SECOND_RED_SWITCH_OVERLAP_GUEST ROOFTOP_SECOND_RED_BRIDGE_CLOSED_GUEST ROOFTOP_NINTENDO_X_SENT ROOFTOP_YELLOW_SELECTED_GUEST ROOFTOP_YELLOW_FIRE_SENT ROOFTOP_YELLOW_FIRE_GUEST ROOFTOP_YELLOW_BRIDGE_OPEN_GUEST ROOFTOP_YELLOW_OPEN_STABLE_GUEST ROOFTOP_YELLOW_OPEN_EXIT_GUEST ROOFTOP_YELLOW_RESET_ENTRY_SENT ROOFTOP_YELLOW_RESET_ENTRY_GUEST ROOFTOP_YELLOW_RESET_CLOSED_GUEST ROOFTOP_YELLOW_REARM_APPROACH_GUEST ROOFTOP_YELLOW_REARM_FIRE_SENT ROOFTOP_YELLOW_REARM_FIRE_GUEST ROOFTOP_YELLOW_REARM_BRIDGE_OPEN_GUEST ROOFTOP_YELLOW_REARM_STABLE_GUEST; do
 grep -q "^$event" "$out/input-injection.txt" || { echo "BLOCKED: missing host input event" > "$out/result.txt"; exit 1; }
done
[ "$rc" = 0 ] || { echo "BLOCKED: virtual-controller session did not finish" > "$out/result.txt"; exit 1; }
python3 tools/verify_rooftop_yellow_rearm.py "$out/guest-city-red.csv" "$out/guest-rooftop.csv" "$out/guest-transitions.csv" "$out/guest-weapon.csv" "$out/input-injection.txt" "$out/rooftop-yellow-rearm-summary.json"
echo "PASS: real second YELLOW bridge opening after authentic reset and reentry" > "$out/result.txt"
