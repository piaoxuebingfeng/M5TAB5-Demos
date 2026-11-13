#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "tasmota.h"

// P4 is a printer dashboard, not a smart-plug controller. Keep the original
// web settings readable for export/backups, but never start its network task
// or let its old power/gauge paths drive rendering and input.
inline void tasmotaInit() {}
inline uint8_t tasmotaPlugForPrinterSlot(uint8_t) { return 0xFF; }
inline uint8_t tasmotaPrinterSlotForPlug(uint8_t) { return 0; }
inline uint8_t tasmotaControlPlugForSlot(uint8_t) { return 0xFF; }
inline bool tasmotaPrinterOffForSlot(uint8_t) { return false; }
inline bool tasmotaPlugStartingForSlot(uint8_t, uint32_t) { return false; }
inline bool tasmotaIsActiveForSlot(uint8_t) { return false; }
inline bool tasmotaConfiguredForSlot(uint8_t) { return false; }
inline float tasmotaGetWattsForSlot(uint8_t) { return 0; }
inline uint8_t tasmotaDisplayModeForSlot(uint8_t) { return 0; }
inline float tasmotaGetPrintKwhUsedForSlot(uint8_t) { return -1; }
inline float tasmotaTariffForSlot(uint8_t) { return 0; }
inline const char* tasmotaCurrencySymbol() { return ""; }
inline bool tasmotaKwhChangedForSlot(uint8_t) { return false; }
inline void tasmotaMarkPrintStart(uint8_t) {}
inline void tasmotaMarkPrintEnd(uint8_t) {}
inline bool tasmotaSetPower(uint8_t, bool) { return false; }
inline void tasmotaGetStats(uint8_t, TasmotaPlugStatsView* out) {
  if (out) *out = TasmotaPlugStatsView{};
}
#endif
