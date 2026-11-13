#include "board_config.h"

// Compile-time guard for the board constants. This file also verifies that
// arduino-cli recursively compiles sources placed in the sketch src/ folder,
// and that a source file can include headers from its own directory.
static_assert(M5TAB5_SCREEN_W == 1280, "Tab5 panel width must be 1280");
static_assert(M5TAB5_SCREEN_H == 720, "Tab5 panel height must be 720");
static_assert(M5TAB5_TOUCH_SDA == 31, "Tab5 touch SDA must be GPIO31");

const char* m5tab5BoardName() {
  return "BambuHelper/M5Tab5 stage 0";
}
