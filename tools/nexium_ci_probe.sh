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
expected="29250067b941f84b85a4fc2ec1357eeba33b31334b715c3b33545c38146eed76"

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
    timeout --signal=TERM --kill-after=5s 40s "$1" "$2" > "$3" 2>&1 &
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
      xdotool key --clearmodifiers --delay 120 c
      sleep 2
      xdotool keydown --clearmodifiers d
      sleep 1
      xdotool keyup d
      xdotool key --clearmodifiers --delay 120 x
      xdotool key --clearmodifiers --delay 120 z
      sleep 2
      xdotool key --clearmodifiers --delay 120 m
      echo "INPUT_PROBE_SENT: A, stick right, B, Y, Plus"
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

python3 - "$out" <<'PY'
import csv
import pathlib
import sys

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
    if 7 in events:
        result.write_text(f"PASS: guest NRO executed, first frame and real stage transition observed; events={events}\n")
    else:
        result.write_text(f"PASS: guest NRO executed through FIRST_FRAME; INPUT TRANSITION NOT CONFIRMED; events={events}\n")
except (ValueError, KeyError, OSError) as exc:
    result.write_text(f"INCONCLUSIVE: invalid guest trace: {exc}\n")
    sys.exit(1)
PY
