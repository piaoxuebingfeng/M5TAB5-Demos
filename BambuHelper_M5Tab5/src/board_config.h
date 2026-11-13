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
#define M5TAB5_TOUCH_SDA 31
#define M5TAB5_TOUCH_SCL 32
#define M5TAB5_TOUCH_INT 23
#define M5TAB5_BACKLIGHT_PIN 22

// --- Identity -------------------------------------------------------------
#define BOARD_IS_M5TAB5 1

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

// Note: no BOARD_HAS_ES8311_AUDIO here. Tab5 uses ES8388 + ES7210, so the
// Waveshare ES8311/I2S backend must not be compiled.

