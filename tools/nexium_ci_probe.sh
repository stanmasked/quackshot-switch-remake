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
export VK_ICD_FILENAMES="/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
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
set +e
xvfb-run -a -s "-screen 0 1280x720x24" \
    timeout --signal=TERM --kill-after=5s 75s "$emulator" "$nro" \
    > "$out/emulator-stdout.txt" 2>&1
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
    result.write_text(f"PASS: guest NRO executed through FIRST_FRAME; events={events}\n")
except (ValueError, KeyError, OSError) as exc:
    result.write_text(f"INCONCLUSIVE: invalid guest trace: {exc}\n")
    sys.exit(1)
PY
