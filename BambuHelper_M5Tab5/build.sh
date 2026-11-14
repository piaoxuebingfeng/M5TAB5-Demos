#!/usr/bin/env bash
# Reproducible CLI build for this Arduino IDE sketch.
#
# The Arduino IDE GUI uses the same FQBN (Tools > Board > M5Stack > M5Tab5),
# so anything that compiles here also compiles with Tools > Verify - but the
# IDE must also select Tools > Partition Scheme > Custom, because this sketch
# carries partitions.csv (a 15 MB app slot for the full GB2312 CJK face).
#
# Overrides:
#   ARDUINO_CLI=/path/to/arduino-cli   pick a specific CLI binary
#   FQBN=...                           pick a different board
#   JOBS=N                             parallel compile jobs (default: nproc)
#
# Note on CLI version: arduino-cli 0.35 (the one bundled with some toolchains)
# re-runs the include-detection preprocessor for *every* source file in every
# candidate library. With M5GFX that costs ~35 minutes before the first object
# file appears. arduino-cli 1.x does dependency discovery incrementally, so
# this script prefers 1.x when it is available. 0.35 also rejects --jobs, which
# is why the flag is feature-detected below.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="${FQBN:-m5stack:esp32:m5stack_tab5}"
JOBS="${JOBS:-$(nproc)}"
BOARD_OPTIONS="${BOARD_OPTIONS:-PartitionScheme=custom}"

pick_cli() {
  if [[ -n "${ARDUINO_CLI:-}" ]]; then echo "$ARDUINO_CLI"; return; fi
  local c
  for c in \
      "$HOME/.local/opt/arduino-cli/arduino-cli" \
      "/usr/local/bin/arduino-cli" \
      "/home/qiao/01-deepseek-harness/velxio/.tools/arduino-cli"; do
    [[ -x "$c" ]] && { echo "$c"; return; }
  done
  command -v arduino-cli
}

CLI="$(pick_cli)"
echo ">> $("$CLI" version | head -1)"
echo ">> fqbn=$FQBN board-options=$BOARD_OPTIONS jobs=$JOBS"

args=(
  compile
  --fqbn "$FQBN"
  --build-path "$SKETCH_DIR/build"
  --warnings default
)
# --board-options / --jobs only exist on arduino-cli 1.x. On 0.35 the custom
# partition scheme is picked up from the sketch-local partitions.csv anyway,
# and the size check falls back to the scheme's own upload.maximum_size.
if "$CLI" compile --help 2>&1 | grep -q -- "--board-options"; then
  args+=(--board-options "$BOARD_OPTIONS")
fi
if "$CLI" compile --help 2>&1 | grep -q -- "--jobs"; then
  args+=(--jobs "$JOBS")
fi
args+=("$SKETCH_DIR")

exec "$CLI" "${args[@]}"
