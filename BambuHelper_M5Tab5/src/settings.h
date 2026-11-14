#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "bambu_state.h"

#if defined(BOARD_IS_WS_P4_7B)
#include "p4_settings.h"
extern P4UiSettings p4UiSettings;
inline P4UiSettings& p4UiState() { return p4UiSettings; }
void p4BootSettingsSnapshot(P4UiSettings& settings);
// The P4 storage task is the only post-boot caller. These functions update
// the same state that saveSettings()/loadSettings() serialize to NVS.
bool p4StoreSettings(P4UiSettings& settings);
bool p4ResetSettings(P4UiSettings& settings);
bool p4DeleteCloudToken();
#endif

// Gauge type identifiers for configurable slot layout
enum GaugeType : uint8_t {
  GAUGE_EMPTY       = 0,
  GAUGE_PROGRESS    = 1,
  GAUGE_NOZZLE      = 2,
  GAUGE_BED         = 3,
  GAUGE_PART_FAN    = 4,
  GAUGE_AUX_FAN     = 5,
  GAUGE_CHAMBER_FAN = 6,
  GAUGE_CHAMBER_TEMP= 7,
  GAUGE_HEATBREAK   = 8,
  GAUGE_CLOCK       = 9,
  GAUGE_AMS_HUM_1   = 10,  // AMS unit 1 humidity
  GAUGE_AMS_HUM_2   = 11,  // AMS unit 2 humidity
  GAUGE_AMS_HUM_3   = 12,  // AMS unit 3 humidity
  GAUGE_AMS_HUM_4   = 13,  // AMS unit 4 humidity
  GAUGE_LAYER       = 14,  // layer progress (current/total)
  GAUGE_AMS_TEMP_1  = 15,  // AMS unit 1 temperature
  GAUGE_AMS_TEMP_2  = 16,  // AMS unit 2 temperature
  GAUGE_AMS_TEMP_3  = 17,  // AMS unit 3 temperature
  GAUGE_AMS_TEMP_4  = 18,  // AMS unit 4 temperature
  GAUGE_AMS_FILAMENT_1 = 19,    // AMS unit 1 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_2 = 20,    // AMS unit 2 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_3 = 21,    // AMS unit 3 - all 4 trays + humidity
  GAUGE_AMS_FILAMENT_4 = 22,    // AMS unit 4 - all 4 trays + humidity
  GAUGE_AUX_FAN_RIGHT  = 23,    // X2D right aux fan (device.airduct.parts func=6)
  GAUGE_EXHAUST_FAN    = 24,    // X2D exhaust fan (device.airduct.parts func=2)
  GAUGE_AMS_BARS_1     = 25,    // AMS unit 1 - 4 vertical color bars (no humidity)
  GAUGE_AMS_BARS_2     = 26,    // AMS unit 2
  GAUGE_AMS_BARS_3     = 27,    // AMS unit 3
  GAUGE_AMS_BARS_4     = 28,    // AMS unit 4
  GAUGE_NOZZLE_RIGHT   = 29,    // dual-nozzle (H2D/H2C/X2D): right extruder (id 0), fixed side
  GAUGE_NOZZLE_LEFT    = 30,    // dual-nozzle: left extruder (id 1), fixed side
  GAUGE_POWER          = 31,    // Tasmota smart-plug live power draw (W / kW)
  GAUGE_CAMERA         = 32,    // P1/A1 LAN chamber-image thumbnail (PSRAM+touch boards)
  GAUGE_PLATE          = 33,    // plate preview of the current cloud job (HAS_CARD_THUMB)
  GAUGE_TYPE_COUNT  // sentinel - always last
};

// Standard 2x3 gauge grid - the 6 slots are used in every mode.
static const uint8_t GAUGE_SLOT_COUNT = 6;
// Extended grids add a 4th column (landscape) or 3rd row (portrait).
// Each extension stores its slots in its own PrinterConfig array so the gauge
// type for "col 4 row 1" and "row 3 col 1" can be set independently.
static const uint8_t LANDSCAPE_EXTRA_COUNT = 2;  // col 4 top, col 4 bot
static const uint8_t PORTRAIT_EXTRA_COUNT  = 3;  // row 3 left, mid, right
// The Ready and Print Complete screens show two gauges side by side, in their
// own slots so they can differ from the print-screen grid (e.g. chamber temp
// while a part cools). Shared by both screens - they never appear together.
static const uint8_t IDLE_SLOT_COUNT = 2;        // left, right
// Max slots displayed by any mode at once. Used to size per-frame slot caches
// in display_ui.cpp (prevSlotTypes, SlotGrid arrays).
static const uint8_t GAUGE_SLOT_MAX = GAUGE_SLOT_COUNT + PORTRAIT_EXTRA_COUNT;  // 9

// Persisted value types have no renderer or worker dependencies.
#include "settings_types.h"

inline const char* gaugeLabelOr(const char* override_, const char* def) {
  return (override_ && override_[0]) ? override_ : def;
}
void sanitizeGaugeLabel(const char* in, char* out, size_t outLen);
void utf8TrimPartial(char* s);

#if defined(BOARD_IS_WS_P4_7B)
#define CLR_TEXT      (p4UiState().dispSettings.textColor)
#define CLR_TEXT_DIM  (p4UiState().dispSettings.textDimColor)
#endif

#if !defined(BOARD_IS_WS_P4_7B)
extern char wifiSSID[33];
extern char wifiPass[65];
extern uint8_t brightness;
extern DisplaySettings dispSettings;

// The live neutral text colors (#163). Defined here rather than in config.h so
// there is exactly one definition and no per-file #undef dance: a drawing file
// already includes settings.h, and one that does not fails to compile instead
// of silently painting the factory literal. config.h keeps only
// CLR_TEXT_DEFAULT / CLR_TEXT_DIM_DEFAULT, which settings.cpp uses to seed
// these fields.
#define CLR_TEXT      (dispSettings.textColor)
#define CLR_TEXT_DIM  (dispSettings.textDimColor)

extern GaugeLabels gaugeLabels;
extern NetworkSettings netSettings;
extern DisplayPowerSettings dpSettings;
extern ButtonType buttonType;
extern uint8_t buttonPin;
extern BuzzerSettings buzzerSettings;
extern LedSettings ledSettings;
extern TasmotaSettings tasmotaSettings[TASMOTA_PLUG_COUNT];
extern char  tasmotaCurrency[8];
extern float tasmotaTariffPerKwh;
extern bool dualPrinterUnsafe;
extern bool quadPrinterBeta;
// C3 antenna workaround (issue #146): true = this board's radio only works at
// reduced TX power, so every WiFi connect must be capped at 8.5 dBm. Set by
// wifi_manager when a reduced-power STA attempt succeeds after full-power
// attempts failed. Only meaningful on BOARD_IS_C3 builds.
extern bool wifiTxCapped;
#endif

#if defined(BOARD_IS_WS_P4_7B) && !defined(P4_SETTINGS_NO_ALIAS)
#include "p4_legacy_aliases.h"
#endif

void loadSettings();
void saveSettings();

// Normalize a string into a valid mDNS/DNS label: lowercase, keep only
// [a-z0-9-], strip leading/trailing hyphens. Falls back to "bambuhelper" if
// nothing usable remains. Used by every path that writes netSettings.hostname.
void sanitizeHostname(const char* in, char* out, size_t outSize);
void savePrinterConfig(uint8_t index);
void resetPrinterConfig(PrinterConfig& cfg);  // Value only; never touches NVS.
void clearPrinterConfig(uint8_t index);  // reset slot to factory defaults + erase its NVS keys
void saveRotationSettings();
void saveButtonSettings();
void saveBuzzerSettings();
void saveLedSettings();
void saveBatteryIndicatorSetting();
void saveWifiTxCapped();
void resetSettings();

// Cloud token persistence (shared across printer slots). The token is stored
// as an NVS blob so it can span NVS pages; a fragmented partition that refuses
// a ~1 KB string write can still take it. Returns false when the write did not
// stick (storage genuinely full) - callers must surface that, never discard it.
bool saveCloudToken(const char* token);
bool loadCloudToken(char* buf, size_t bufLen);
bool clearCloudToken();          // also drops the stored email and password

// NVS entry usage of the whole partition, for diagnostics and error messages.
void getNvsUsage(uint16_t& used, uint16_t& freeEntries, uint16_t& total);

// Persist "which BambuHelper version sits in the currently running app slot"
// so the OTA rollback UI can name the other slot's firmware. Call once per
// boot, after loadSettings().
void recordBootSlotVersion();

// Read that note ("<version>|<sha8>") for an app slot by partition label.
// Returns false when no firmware ever recorded one there.
bool loadSlotVersionNote(const char* label, char* buf, size_t bufLen);

void saveCloudEmail(const char* email);
bool loadCloudEmail(char* buf, size_t bufLen);
void saveCloudPassword(const char* password);
bool loadCloudPassword(char* buf, size_t bufLen);
void clearCloudPassword();
#if defined(BOARD_IS_WS_P4_7B)
// Keep account metadata in the same verified NVS owner as printer settings.
// Password-style values never cross into a worker mailbox.
bool p4StoreCloudLogin(const char* email, const char* password, bool savePassword);
#endif

// RGB565 <-> HTML hex conversion
uint16_t htmlToRgb565(const char* hex);
void rgb565ToHtml(uint16_t color, char* buf);  // buf must be >= 8 chars

// Bambu RRGGBBAA hex to RGB565 (e.g. "9D432CFF" -> RGB565)
uint16_t bambuColorToRgb565(const char* rrggbbaa);

// Load default display settings
void defaultDisplaySettings(DisplaySettings& ds);

#endif // SETTINGS_H
