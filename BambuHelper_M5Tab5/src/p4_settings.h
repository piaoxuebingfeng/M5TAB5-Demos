#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "bambu_state.h"
#include "settings_types.h"
#include "p4_settings_merge.h"

struct P4UiSettings {
  uint32_t revision = 0;  // Advanced only by the storage owner.
  PrinterSlot printers[MAX_PRINTERS]{};
  RotationState rotState = { ROTATE_SMART, ROTATE_INTERVAL_MS, 0, 0, 0, false, 1 };
  NetworkSettings netSettings{};
  DisplaySettings dispSettings{};
  GaugeLabels gaugeLabels{};
  DisplayPowerSettings dpSettings{};
  ButtonType buttonType = BTN_DISABLED;
  uint8_t buttonPin = BUTTON_DEFAULT_PIN;
  BuzzerSettings buzzerSettings = { false, BUZZER_DEFAULT_PIN, 0, 0, false, false, 35 };
  LedSettings ledSettings{};
  TasmotaSettings tasmotaSettings[TASMOTA_PLUG_COUNT]{};
  char wifiSSID[33]{}, wifiPass[65]{}, tasmotaCurrency[8] = "\xE2\x82\xAC";
  float tasmotaTariffPerKwh = 0;
  uint8_t brightness = 200, activePrinterIndex = 0;
  bool dualPrinterUnsafe = false, quadPrinterBeta = false, wifiTxCapped = false;
};

inline P4UiSettings p4SettingsMerge(const P4UiSettings& current,
                                    const P4UiSettings& before,
                                    const P4UiSettings& after) {
  return p4MergeSettings(current, before, after, MAX_PRINTERS);
}
#endif
