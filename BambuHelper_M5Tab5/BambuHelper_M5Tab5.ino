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
 * Stages 0-4 landed the scaffold, the board macro matrix, the pre-generated
 * assets and the full business source tree. Stage 5 (this file) brings the
 * hardware up for the first time: expander rails, panel, backlight and touch.
 * The dashboard logic itself still lives in reference/main.cpp and moves in a
 * later stage.
 */
#include <Arduino.h>
// Core built-in libraries are only added to the include search path when a
// sketch source references them explicitly. PlatformIO always had them on -I;
// under Arduino IDE we must name them here so headers that guard with
// __has_include(<Preferences.h>) resolve correctly in every .cpp.
#include <Preferences.h>
#include <esp32-hal-hosted.h>   // hostedSetPins(): C6 SDIO map
#include "src/board_config.h"
#include "src/m5tab5_board_io.h"
#include "src/settings.h"
#include "src/display_ui.h"
#include "src/button_touch_backend.h"

// Lives in src/m5tab5_board.cpp; proves sketch src/ sources are compiled+linked.
extern const char* m5tab5BoardName();
extern uint32_t m5tab5AssetFingerprint();

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("==============================================");
  Serial.println("*** BambuHelper / M5Stack Tab5 (stage 5) ***");
  Serial.println("==============================================");
  Serial.printf("build   : %s\n", m5tab5BoardName());
  Serial.printf("assets  : 0x%08x\n", m5tab5AssetFingerprint());
  Serial.printf("flash   : %u KB   psram: %u KB   panel: %dx%d\n",
                ESP.getFlashChipSize() / 1024,
                ESP.getPsramSize() / 1024,
                M5TAB5_SCREEN_W, M5TAB5_SCREEN_H);

  // 1. Expander rails and resets. Must precede the radio and the panel: the C6
  //    has no supply until WLAN_PWR_EN is set, and the DSI PHY stays mute while
  //    LCD_RST is asserted. initDisplay() calls this again; it is a no-op once
  //    the sequence succeeded.
  if (!m5tab5IoBegin()) {
    Serial.println("FATAL: PI4IOE5V6408 not responding - check the system I2C bus");
  }

  // 2. Tell ESP-Hosted which pins carry the C6 SDIO link, before any WiFi call.
  if (!hostedSetPins(M5TAB5_HOSTED_CLK, M5TAB5_HOSTED_CMD,
                     M5TAB5_HOSTED_D0, M5TAB5_HOSTED_D1,
                     M5TAB5_HOSTED_D2, M5TAB5_HOSTED_D3,
                     M5TAB5_HOSTED_RST)) {
    Serial.println("WARN: hostedSetPins() rejected the Tab5 SDIO map");
  }

  // 3. Settings, then the panel (the splash is drawn inside initDisplay()).
  loadSettings();
  recordBootSlotVersion();
  initDisplay();

  // 4. GT911. Its address was latched during the expander reset pulse, so this
  //    only has to probe and configure.
  touchInit();
  Serial.printf("touch   : ready=%d\n", (int)touchDiagnostics().ready);
}

void loop() {
  static uint32_t lastPrint = 0;
  TouchContact c = touchContact();
  if (c.down && c.valid && millis() - lastPrint > 120) {
    lastPrint = millis();
    Serial.printf("tap     : x=%u y=%u\n", c.x, c.y);
  }
  delay(20);
}
