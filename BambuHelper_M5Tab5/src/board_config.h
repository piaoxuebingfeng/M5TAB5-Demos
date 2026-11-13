#pragma once

// BambuHelper -> M5Stack Tab5 board layer (Arduino IDE build).
// Stage 0 intentionally carries only board identity. The full -D macro matrix
// from boards/ws_p4_lcd_7b.ini is ported here in stage 1.

#define M5TAB5_BUILD 1

// Official Tab5 hardware constants. First-batch unit (purchased 2025-06-30):
// ILI9881C display driver + GT911 touch @ 0x14.
#define M5TAB5_SCREEN_W 1280
#define M5TAB5_SCREEN_H 720
#define M5TAB5_TOUCH_SDA 31
#define M5TAB5_TOUCH_SCL 32
#define M5TAB5_TOUCH_INT 23
#define M5TAB5_BACKLIGHT_PIN 22
