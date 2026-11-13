/*
 * BambuHelper for M5Stack Tab5 - Arduino IDE project.
 *
 * Board package : M5Stack official (m5stack:esp32), 3.3.7 or newer
 * Board         : M5Tab5  ->  m5stack:esp32:m5stack_tab5
 * Hardware      : ESP32-P4, 16 MB flash, 32 MB octal PSRAM, 1280x720 MIPI-DSI
 *
 * Build in Arduino IDE : Tools > Board > M5Stack > M5Tab5, then Verify.
 * Build from CLI       : ./build.sh
 *
 * Migration stage 0: scaffold only. Business sources arrive in later stages,
 * each one verified with a successful compile before it is committed.
 */
#include <Arduino.h>
// Core built-in libraries are only added to the include search path when a
// sketch source references them explicitly. PlatformIO always had them on -I;
// under Arduino IDE we must name them here so headers that guard with
// __has_include(<Preferences.h>) resolve correctly in every .cpp.
#include <Preferences.h>
#include "src/board_config.h"

// Lives in src/m5tab5_board.cpp; proves sketch src/ sources are compiled+linked.
extern const char* m5tab5BoardName();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(m5tab5BoardName());
  Serial.printf("flash=%u KB  psram=%u KB  screen=%dx%d\n",
                ESP.getFlashChipSize() / 1024,
                ESP.getPsramSize() / 1024,
                M5TAB5_SCREEN_W, M5TAB5_SCREEN_H);
}

void loop() { delay(1000); }
