#include "board_config.h"

// Stage 1: ported P4 runtime headers (hardware-independent layer). Including
// every one here forces the compiler to parse the whole layer during the
// per-stage build check, catching missing core/library headers immediately.
#include "spsc_latest.h"
#include "p4_snapshot_diff.h"
#include "p4_transport_gate.h"
#include "p4_restart_gate.h"
#include "p4_mqtt_commands.h"
#include "p4_mqtt_lifecycle.h"
#include "p4_camera_schedule.h"
#include "p4_settings_merge.h"
#include "p4_storage_commit.h"
#include "p4_dma_buffer_layout.h"
#include "p4_checked_preferences.h"
#include "p4_json_allocator.h"
#include "p4_dashboard_layout.h"

// Compile-time guard for the board constants. This file also verifies that
// arduino-cli recursively compiles sources in the sketch src/ folder, and
// that a source file can include headers from its own directory.
static_assert(M5TAB5_SCREEN_W == 1280, "Tab5 panel width must be 1280");
static_assert(M5TAB5_SCREEN_H == 720, "Tab5 panel height must be 720");
static_assert(M5TAB5_TOUCH_SDA == 31, "Tab5 touch SDA must be GPIO31");
static_assert(GT911_SDA == 31 && GT911_SCL == 32 && GT911_INT == 23,
              "Tab5 GT911 pin map");
static_assert(BOARD_IS_WS_P4_7B == 1, "P4 runtime architecture gate must stay on");

const char* m5tab5BoardName() {
  return "BambuHelper/M5Tab5 stage 1";
}
