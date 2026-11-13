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

// Stage 2: committed, pre-generated assets. These replace the PlatformIO-only
// embed_files / extra_scripts mechanisms. web_assets_gz.h is included here for
// the stage-2 build check and will move to web_server.cpp in a later stage so
// that only ever one translation unit owns its static arrays.
#include "p4_sans_fonts.h"
#include "web_assets_gz.h"
#include "p4_printer_artwork.h"

// Stage 3: configuration, state and lookup-table layer. This is the first
// batch of actual BambuHelper definitions (not just board plumbing).
#include "config.h"
#include "bambu_state.h"
#include "settings_types.h"
#include "timezones.h"
#include "web_pages.h"
#include "error_tables/hms_table.h"
#include "fonts/inter_10.h"

// Compile-time guard for the board constants. This file also verifies that
// arduino-cli recursively compiles sources in the sketch src/ folder, and
// that a source file can include headers from its own directory.
static_assert(M5TAB5_SCREEN_W == 1280, "Tab5 panel width must be 1280");
static_assert(M5TAB5_SCREEN_H == 720, "Tab5 panel height must be 720");
static_assert(M5TAB5_TOUCH_SDA == 31, "Tab5 touch SDA must be GPIO31");
static_assert(GT911_SDA == 31 && GT911_SCL == 32 && GT911_INT == 23,
              "Tab5 GT911 pin map");
static_assert(BOARD_IS_WS_P4_7B == 1, "P4 runtime architecture gate must stay on");

// Asset layer guards: catch an empty or truncated generated blob at build time.
static_assert(p4SansData_size > 100000, "24px CJK font subset is missing");
static_assert(p4SansSmallData_size > 80000, "20px CJK font subset is missing");
static_assert(WEB_APP_CSS_GZ_LEN > 1000 && WEB_APP_JS_GZ_LEN > 1000,
              "gzipped web assets are missing");
static_assert(sizeof(p4_art_a1) > 1000, "printer artwork is missing");
static_assert(SCREEN_W == 1024 && SCREEN_H == 600, "stage-3 layout selector");
static_assert(sizeof(inter_10) > 100, "bitmap font table is missing");

const char* m5tab5BoardName() {
  return "BambuHelper/M5Tab5 stage 3";
}

// Referencing the generated blobs keeps them in the linked image. A bare
// static_assert would let --gc-sections discard everything, so this is what
// actually proves the asset layer fits the board package's 6.5 MB app slot.
// Each later stage replaces this with the real consumer (fonts.cpp / web_server).
uint32_t m5tab5AssetFingerprint() {
  return (uint32_t)p4SansData[0]
       ^ (uint32_t)p4SansSmallData[0]
       ^ (uint32_t)WEB_APP_CSS_GZ[0]
       ^ (uint32_t)p4_art_a1[0];
}
