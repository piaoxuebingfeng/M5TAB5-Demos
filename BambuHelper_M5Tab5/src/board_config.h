#pragma once

// Arduino IDE injects Arduino.h only into the .ino translation unit, not into
// standalone .cpp files. Every ported source includes this header first, so
// core types (String, Serial, HardwareSerial) are pulled in here once.
#include <Arduino.h>

// BambuHelper -> M5Stack Tab5 board layer (Arduino IDE build).
//
// Arduino IDE has no per-project -D injection, so this header is the direct
// replacement for the build_flags matrix in
// BambuHelper/boards/ws_p4_lcd_7b.ini:28-55. Keep it included first by every
// translation unit that needs board macros.

#define M5TAB5_BUILD 1

// Official Tab5 hardware constants. First-batch unit (purchased 2025-06-30):
// ILI9881C display driver + GT911 touch @ 0x14.
#define M5TAB5_SCREEN_W 1280
#define M5TAB5_SCREEN_H 720

// The panel itself is native portrait and the GT911 reports in that frame:
// M5GFX configures it with x_min=0/x_max=719/y_min=0/y_max=1279. Anything that
// touches raw touch coordinates must use these, not M5TAB5_SCREEN_W/H.
#define M5TAB5_PANEL_RAW_W 720
#define M5TAB5_PANEL_RAW_H 1280

#define M5TAB5_TOUCH_SDA 31
#define M5TAB5_TOUCH_SCL 32
#define M5TAB5_TOUCH_INT 23
#define M5TAB5_BACKLIGHT_PIN 22

// --- Identity -------------------------------------------------------------
// These three were -D string macros in boards/ws_p4_lcd_7b.ini. Quoted macros
// are error-prone in boards.local.txt, so they live here as plain #defines.
#define BOARD_IS_M5TAB5 1
#define BOARD_NAME "M5Stack_Tab5"
#define BOARD_PANEL "ILI9881C_1280x720_GT911"
#define BOARD_VARIANT "m5tab5"

// NOT a board pin switch - this is the ESP32-P4 runtime architecture gate
// (SPSC snapshot, worker ownership, PSRAM allocation policy, loop() fork).
// It must stay defined on Tab5; removing it drops the code back to the old
// small-screen single-loop path.
#define BOARD_IS_WS_P4_7B 1

// --- Core / memory --------------------------------------------------------
#ifndef BOARD_HAS_PSRAM
#define BOARD_HAS_PSRAM 1
#endif
#define MAX_ACTIVE_PRINTERS 2

// --- Display --------------------------------------------------------------
// Stage 1 deliberately keeps the original layout selector so the ported UI
// sources compile unchanged. The 1280x720 relayout is a later stage.
#define DISPLAY_1024x600 1
#define BACKLIGHT_PIN M5TAB5_BACKLIGHT_PIN

// --- Touch ----------------------------------------------------------------
#define USE_GT911 1
#define GT911_SDA M5TAB5_TOUCH_SDA
#define GT911_SCL M5TAB5_TOUCH_SCL
#define GT911_INT M5TAB5_TOUCH_INT

// Bring-up trace: logs the raw controller coordinates and the mapped canvas
// point on every press edge. Cheap (one line per tap), but it is diagnostic
// noise once the panel is trusted - comment it out then.
#define M5TAB5_TOUCH_TRACE 1

// --- I2C bus / board control ---------------------------------------------
// One system bus carries the two PI4IOE5V6408 expanders, the GT911, the
// BMI270 IMU, the RX8130CE RTC, the INA226 fuel gauge and both audio codecs.
#define M5TAB5_SYS_I2C_SDA 31
#define M5TAB5_SYS_I2C_SCL 32

#define M5TAB5_PI4IOE_1_ADDR 0x43   // resets, speaker, camera, RF path
#define M5TAB5_PI4IOE_2_ADDR 0x44   // C6 power, USB-A 5V, charger

// PI4IOE-1 bit map (see m5tab5_board_io.cpp for the source of these bits).
#define M5TAB5_IO1_SPK_EN  2
#define M5TAB5_IO1_LCD_RST 4
#define M5TAB5_IO1_TP_RST  5
#define M5TAB5_IO1_CAM_RST 6
// PI4IOE-2 bit map.
#define M5TAB5_IO2_WLAN_PWR_EN 0

#define M5TAB5_INA226_ADDR 0x41

// --- ESP32-C6 SDIO (ESP-Hosted) ------------------------------------------
// Official Tab5 map: D0=G11 D1=G10 D2=G9 D3=G8 CMD=G13 CLK=G12 RESET=G15.
// hostedSetPins() must run before the first WiFi call - the radio lives on the
// other side of this bus. (G14 = C6 IO2, not part of the SDIO bus.)
#define M5TAB5_HOSTED_CLK 12
#define M5TAB5_HOSTED_CMD 13
#define M5TAB5_HOSTED_D0  11
#define M5TAB5_HOSTED_D1  10
#define M5TAB5_HOSTED_D2  9
#define M5TAB5_HOSTED_D3  8
#define M5TAB5_HOSTED_RST 15

// Battery: NP-F550 7.4 V pack behind IP2326, monitored by an INA226 at 0x41.
// BambuHelper only has AXP2101 and ADC backends, so BOARD_HAS_BATTERY stays
// undefined until an INA226 backend exists - enabling it now would compile an
// empty battery.cpp and break the link.
// #define BOARD_HAS_BATTERY 1
// #define BOARD_HAS_BAT_INA226 1

// Note: no BOARD_HAS_ES8311_AUDIO here. Tab5 uses ES8388 + ES7210, so the
// Waveshare ES8311/I2S backend must not be compiled.

