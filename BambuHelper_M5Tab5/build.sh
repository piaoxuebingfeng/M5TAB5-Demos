#!/usr/bin/env bash
# Reproducible CLI build for this Arduino IDE sketch.
#
# The Arduino IDE GUI uses the same FQBN (Tools > Board > M5Stack > M5Tab5),
# so anything that compiles here also compiles with Tools > Verify.
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
# this script prefers 1.x when it is available.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FQBN="${FQBN:-m5stack:esp32:m5stack_tab5}"
JOBS="${JOBS:-$(nproc)}"

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
echo ">> fqbn=$FQBN jobs=$JOBS"

exec "$CLI" compile \
  --fqbn "$FQBN" \
  --build-path "$SKETCH_DIR/build" \
  --jobs "$JOBS" \
  --warnings default \
  "$SKETCH_DIR"
