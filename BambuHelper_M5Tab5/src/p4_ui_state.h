#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "p4_settings.h"
#include "button_touch_backend.h"
#include <cstdint>
#include <cstring>

struct P4UiStatus {
  uint8_t screen = 0, page = 0, amsPage = 0;
  uint32_t loopStackMin = 0;
  bool dashboard = false, nightBlackout = false, cameraImage = false;
  uint8_t slot = 0;
};

inline bool p4PrinterConfigured(const PrinterConfig& cfg, bool slotEnabled = true) {
  if (!slotEnabled) return false;
  return isCloudMode(cfg.mode) ? cfg.serial[0] && cfg.cloudUserId[0]
                               : cfg.serial[0] && cfg.ip[0];
}

#endif
