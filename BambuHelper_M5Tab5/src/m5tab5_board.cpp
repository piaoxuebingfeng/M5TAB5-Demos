#include "board_config.h"

// =============================================================================
//  Build-time self check for the Arduino IDE port.
// =============================================================================
//
// This translation unit exists to fail the build loudly when the board macro
// matrix, the ported header layer or the pre-generated assets drift. It holds
// no runtime logic beyond the two getters used by the sketch banner.
//
// Stage 1: P4 runtime headers (hardware-independent layer). Including all of
// them here forces the compiler to parse the whole layer, so a missing core or
// library header shows up as a compile error instead of a link error later.
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
// embed_files / extra_scripts mechanisms.
//   p4_sans_fonts.h    <- assets/p4/*.vlw, full GB2312 face (tools/build_cjk_font.py)
//   p4_printer_artwork.h <- include/p4_printer_artwork.h (verbatim)
// web_assets_gz.h is deliberately NOT included here: it uses static storage,
// and web_server.cpp is now its single owner. Including it twice would put a
// second 26 KB copy in flash.
#include "p4_sans_fonts.h"
#include "p4_printer_artwork.h"

// Stage 3: configuration, state and lookup-table layer.
#include "config.h"
#include "bambu_state.h"
#include "settings_types.h"
#include "timezones.h"
#include "web_pages.h"
#include "error_tables/hms_table.h"
#include "fonts/inter_10.h"

// --- Board identity -------------------------------------------------------
static_assert(M5TAB5_SCREEN_W == 1280, "Tab5 panel width must be 1280");
static_assert(M5TAB5_SCREEN_H == 720, "Tab5 panel height must be 720");
static_assert(M5TAB5_TOUCH_SDA == 31, "Tab5 touch SDA must be GPIO31");
static_assert(GT911_SDA == 31 && GT911_SCL == 32 && GT911_INT == 23,
              "Tab5 GT911 pin map");
static_assert(BOARD_IS_WS_P4_7B == 1, "P4 runtime architecture gate must stay on");
static_assert(BOARD_IS_M5TAB5 == 1, "Tab5 board switch must be on");

// --- Asset integrity ------------------------------------------------------
// Full GB2312 face: 4.0 MB, so anything below 3.5 MB means the generator ran
// with the old source-charset subset (tofu boxes in print job titles).
static_assert(p4SansData_size > 3500000, "full GB2312 CJK face is missing");
static_assert(sizeof(p4_art_a1) > 1000, "printer artwork is missing");
static_assert(sizeof(inter_10) > 100, "bitmap font table is missing");

// The ported UI is still laid out for the original 1024x600 canvas; the
// 1280x720 relayout is a separate stage. Guarded so the mismatch cannot be
// forgotten silently.
static_assert(SCREEN_W == 1024 && SCREEN_H == 600, "stage-4 layout selector");

const char* m5tab5BoardName() {
  return "BambuHelper/M5Tab5 stage 6 (app layer live)";
}

// Referencing the generated blobs keeps them in the linked image: a bare
// static_assert would let --gc-sections drop them, so this proves the asset
// layer really fits the app slot declared in partitions.csv. Later stages
// replace this with the real consumers (fonts.cpp already reads p4SansData).
uint32_t m5tab5AssetFingerprint() {
  return (uint32_t)p4SansData[0]
       ^ (uint32_t)p4_art_a1[0];
}
