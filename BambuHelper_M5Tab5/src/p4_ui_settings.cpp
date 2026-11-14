// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "p4_ui_state.h"
#if defined(BOARD_IS_WS_P4_7B)
#undef printers
#undef rotState
#include "p4_ui_settings.h"
#include "settings.h"
#include "display_ui.h"
#include "button.h"
#include "buzzer.h"
#include "led.h"
#include <cstring>
#include <esp_heap_caps.h>

namespace {
uint32_t appliedRevision = 0;
bool initialized = false;
P4UiSettings* beforeSettings = nullptr;
}

// Commit one complete configuration value to the renderer. Navigation timing
// and live telemetry are session state, not persisted settings.
bool p4UiApplySettings(const P4UiSettings& value) {
  if (initialized && value.revision <= appliedRevision) return true;
  if (!beforeSettings) {
    beforeSettings = static_cast<P4UiSettings*>(heap_caps_calloc(1, sizeof(P4UiSettings),
                                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!beforeSettings) return false;
  }
  P4UiSettings& ui = p4UiState();
  const bool wasInitialized = initialized;
  P4UiSettings& before = *beforeSettings;
  before = ui;
  RotationState navigation = value.rotState;
  navigation.displayIndex = before.rotState.displayIndex;
  navigation.lastRotateMs = before.rotState.lastRotateMs;
  navigation.displayHoldUntilMs = before.rotState.displayHoldUntilMs;
  navigation.splitIndexB = before.rotState.splitIndexB;

  BambuState telemetry[MAX_ACTIVE_PRINTERS];
  uint8_t changed = 0;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) {
    telemetry[i] = ui.printers[i].state;
    if (std::memcmp(&before.printers[i].config, &value.printers[i].config,
                    sizeof(PrinterConfig)) != 0) changed |= uint8_t(1U << i);
  }
  ui = value;
  ui.rotState = navigation;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) ui.printers[i].state = telemetry[i];

  // Publish readiness before reinitializing hardware: initButton() may consult
  // the generation and skip a destructive touchscreen re-probe for this revision.
  appliedRevision = value.revision;
  initialized = true;
  applyDisplaySettings();
  if (wasInitialized && before.brightness != value.brightness) setBacklight(value.brightness);
  if (wasInitialized) {
    // Saving an unrelated field must not reset the touch controller mid-gesture.
    if (before.buttonType != value.buttonType || before.buttonPin != value.buttonPin) initButton();
    if (memcmp(&before.buzzerSettings, &value.buzzerSettings, sizeof(BuzzerSettings)) != 0) initBuzzer();
  }
  p4SettingsPublish(value, changed);
  return true;
}
#endif
