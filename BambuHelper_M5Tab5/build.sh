#!/usr/bin/env bash
# Reproducible CLI build for this Arduino IDE sketch.
# The Arduino IDE GUI uses the same FQBN (Tools > Board > M5Stack > M5Tab5).
# Override with ARDUINO_CLI=... or FQBN=... if your setup differs.
set -euo pipefail
CLI="${ARDUINO_CLI:-/home/qiao/01-deepseek-harness/velxio/.tools/arduino-cli}"
FQBN="${FQBN:-m5stack:esp32:m5stack_tab5}"
SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$CLI" compile --fqbn "$FQBN" --build-path "$SKETCH_DIR/build" "$SKETCH_DIR"
