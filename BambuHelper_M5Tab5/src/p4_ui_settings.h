#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include <cstdint>
struct P4UiSettings;
void p4SettingsPublish(const P4UiSettings& settings, uint8_t changedPrinters);
#endif
